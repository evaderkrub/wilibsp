# Radio (CC1101 sub-GHz transceiver)

Sub-GHz ISM-band radio (300–928 MHz, CC1101) sharing SPI1 with the LCD.
Supports RSSI scanning, raw OOK/ASK monitor RX via a PIO2 edge-capture
pipeline, and OOK TX by bit-banging GDO0 with a previously captured (or
synthesized) duration timeline. Harvested from `subghz` (MIT).

**Note:** this is the CC1101 sub-GHz radio, not the on-board LoRa radio —
`ioexp_antenna()` selects between the two front-ends (see "Antenna
selection" below).

## Pins

`PIN_CC1101_CS` 40 (active low, parked HIGH by `board_init()`),
`PIN_CC1101_MISO` 8 (== `PIN_LCD_DC`, an LCD *output* muxed to SPI1 RX
*input* around CC1101 access by the `spi_bus` arbiter), SPI1 SCK = 10 /
MOSI = 11 (shared with the LCD, `CC1101_SPI_BAUD` = 5 MHz vs. the LCD's
100 MHz), `PIN_CC1101_GDO0` 32 (live data / sync, PIO2-sampled edge
capture), `PIN_CC1101_GDO2` 37 (unused). All `#define`d in
`bsp/platform/board.h`.

## Bring-up order

```c
board_init();                      // 250 MHz; also parks CC1101 CS HIGH, brings up ioexp
ioexp_antenna(ANT_CC1101_433);      // route a CC1101 antenna before talking to the chip
bool ok = cc1101_init();            // probes PARTNUM/VERSION over SPI1; DIAGs them internally
if (!ok) { /* no radio on the bus — halt or retry */ }
```

`cc1101_init()` acquires the shared SPI1 bus via `spi_bus_acquire_cc1101()`
internally, reads `PARTNUM`/`VERSION` off the chip, and returns `true` only
if those registers hold plausible (non-`0x00`/non-`0xFF`) values — that
presence check is the whole of "probe" semantics; it does not put the radio
into any particular RX/TX state beyond IDLE.

## Frequency / modulation

```c
cc1101_set_frequency(433920000u);          // programs FREQ2/FREQ1/FREQ0
cc1101_set_modulation(CC1101_MOD_ASK_OOK);  // CC1101_MOD_{2FSK,GFSK,ASK_OOK,4FSK,MSK}
cc1101_calibrate();                         // manual PLL cal at the current freq (MCSM0 auto-cal is off; call once per band, not per hop)
cc1101_strobe_rx();                         // SRX: enter RX
int dbm = cc1101_read_rssi_dbm();           // one-shot RSSI read, converted to dBm
```

## Async RSSI sweep (`scan_engine`)

The sweep is a small state machine driven a step at a time from the main
loop so nothing blocks — each bin needs several `scan_step()` calls
(set-freq -> arm RX -> settle -> read RSSI):

```c
uint32_t start_hz, step_hz; uint16_t nbins;
scan_preset(1, &start_hz, &step_hz, &nbins);   // preset index -> band params (e.g. idx 1 = 433 MHz ISM, 128 bins)
scan_begin(start_hz, step_hz, nbins);
int row[128];
bool have_row;
do {
    have_row = scan_take_row(row, nbins);   // copies the completed row, if any
    scan_step();                             // advance the state machine one tick
    sleep_us(100);
} while (!have_row);
scan_peak_t pk = scan_get_peak();   // {freq_hz, rssi_dbm, valid} — accumulates across bins as they complete
```

`scan_get_last()` returns the most recently sampled `(freq_hz, rssi_dbm)`
pair without waiting for a full row. `scan_freq_at()` and
`scan_track_peak()` are the pure helpers underlying the state machine
(host-tested).

## Monitor RX (raw OOK/ASK demod via `gdo_capture`)

`cc1101_monitor_rx()` puts the CC1101 into async-transparent RX so GDO0
carries the demodulated data edges directly (no SPI polling needed per
bit); `gdo_capture` (PIO2 + an ENDLESS DMA ring, see facts.md) timestamps
each edge as a duration in the current level, polled — no `DMA_IRQ_0`
usage:

```c
gdo_capture_init();                       // claim PIO2 SM + DMA (once)
gdo_capture_start();                      // arm PIO + DMA
cc1101_monitor_rx(433920000u, CC1101_MOD_ASK_OOK);  // GDO0 = live demodulated data
...
uint32_t durs[128];
uint32_t n = gdo_capture_drain(durs, 128); // copy new edge durations (~1 us ticks), non-blocking
// feed durs[] into monitor_engine (monitor_reset/monitor_feed) for pulse-width
// histograms and burst/idle detection (MON_IDLE_TICKS = 20 ms marks a burst boundary)
cc1101_monitor_stop();                    // SIDLE + restore IOCFG0
gdo_capture_stop();                       // halt PIO + DMA
```

## OOK TX (bit-bang GDO0 + `gdo_capture` drain)

`cc1101_tx_ook_start()` puts the CC1101 into async-transparent OOK TX and
keys the carrier, leaving GDO0 under MCU (SIO) control; `ook_tx_send()`
blocks, toggling GDO0 per duration using accumulated absolute deadlines so
a long burst can't drift:

```c
cc1101_tx_ook_start(433920000u);            // key the carrier; GDO0 becomes an SIO output
ook_tx_send(durs, n, /*start_level=*/true);  // durations clamped via ook_tx_clamp_us (max 100 ms/edge)
cc1101_tx_ook_stop();                        // SIDLE + restore RX regs
gdo_capture_attach_pin();                    // re-route GDO0 back to the PIO (undoes the SIO takeover)
gdo_capture_start();                         // resume capture if monitoring again
```

`apps/hello_cc1101/main.c` demonstrates this end to end as a **same-pad
plumbing test**: it drives an OOK pulse train out GDO0 with `ook_tx_send()`
then re-attaches `gdo_capture` on the same pin and drains it, so a nonzero
edge count confirms the SPI TX-config path, `ook_tx` timing, and the
PIO2/DMA capture chain all work — it is *not* an over-the-air RX
demonstration (one pin cannot receive its own transmission through the
air); a real RX test needs a second radio or a real signal source.

## Capture clips (`capture_store`)

A pure (no Pico SDK dependency) ring-free clip store that binds to a
caller-supplied duration buffer — a PSRAM allocation on target, a plain
array in host tests — so a full monitor or replay capture can be held
without touching SRAM:

```c
capture_store_init(psram_buf, capacity);              // bind buffer + capacity (durations, not bytes)
capture_store_begin(freq_hz, mod, start_level, ANT_CC1101_433);  // reset + record clip metadata
uint32_t stored = capture_store_append(durs, n);      // returns < n once the clip fills; capture_store_full() to check
// capture_store_len() / capture_store_data() / capture_store_freq() / _mod() /
// _start_level() / _antenna() / _total_ticks() read back the clip for replay or display
```

## Antenna selection

`ioexp_antenna()` (`bsp/platform/ioexp.h`) drives the PCAL6524 I/O expander
pins that route the front-end: `ANT_LORA` (0) selects the on-board LoRa
radio path; `ANT_CC1101_433` (1), `ANT_CC1101_315_415` (2), and
`ANT_CC1101_915` (3) all keep the CC1101 on SPI1 while switching between
its three antennas. Call it after `board_init()` (which brings up I2C1 +
the expander) and before any CC1101 SPI traffic.

## Constraints & gotchas

- **GDO0 capture runs on `pio2`**, not `pio0`/`pio1` (both already
  claimed by audio/LEDs) — see `docs/hardware/facts.md`, "Radio: GDO0
  capture runs on PIO2, not PIO0". Requires `PICO_PIO_USE_GPIO_BASE=1`
  (already a PUBLIC compile def on `freewili2_bsp`) so the PIO's GPIO
  window can reach GPIO32.
- **SPI1 is shared with the LCD**: use `spi_bus_acquire_cc1101()` /
  `spi_bus_release_cc1101()` / `spi_bus_cc1101_cs()` (called internally by
  the driver) rather than driving SPI1 or `PIN_CC1101_CS` directly, and
  never assume MISO (GPIO8) is in SPI RX mode without going through the
  arbiter — it's an LCD DC output the rest of the time.
- **No floats over RTT**: all diagnostics in this driver use `DIAG(...)`
  (SEGGER RTT), which supports only `%d %u %x %s %c` — RSSI, frequencies,
  and durations are logged as integers (dBm as `int`, Hz/ticks as
  `uint32_t`), never as floating point.

See `apps/hello_cc1101/main.c` for a complete worked example (probe, RSSI
sweep, and the same-pad TX/capture plumbing check).
