# CC1101 sub-GHz radio driver — design (Increment 3)

**Date:** 2026-07-04
**Status:** approved, ready for planning
**Harvest source:** `subghz` repo (`src/radio/*`, MIT, © 2026 Dave Robins)
**Increment:** 3 of the audio/RF/sensor driver harvest (I2S audio DONE → PDM → **CC1101** → I2C sensors)

## Goal

Harvest the proven CC1101 sub-GHz radio stack from the `subghz` repo into `wilibsp`
as the shared `freewili2_bsp` library gains its next driver, following the exact
spec → plan → subagent-build → whole-branch review → on-hardware verify cycle that
landed Increment 1 (I2S audio). All CC1101 dependencies are already staged in the
BSP (SPI1 arbiter, antenna mux, pins), so this is a near-verbatim harvest plus one
deliberate adaptation.

## Scope

**In scope — full radio stack (7 modules):**

| Module | Files | Role | Host-testable |
|---|---|---|---|
| Core driver | `cc1101.{c,h}` | SPI init/probe, set frequency, set modulation, RX strobe, RSSI read, monitor-RX (async transparent), OOK-TX start/stop | hardware |
| Register math | `cc1101_regs.{c,h}` | Hz→FREQ registers (26 MHz XTAL, integer), RSSI raw→dBm, ISM band validation | **pure** |
| GDO0 capture | `gdo_capture.{c,h}` + `gdo_capture.pio` | PIO+DMA edge-duration ring buffer (ENDLESS DMA, polled progress — **no IRQ handler**) | hardware |
| Monitor stats | `monitor_engine.{c,h}` | burst/edge/idle-gap statistics + pulse-width histogram over drained durations | **pure** |
| OOK transmit | `ook_tx.{c,h}` | bit-bang GDO0 from a duration timeline with drift-free absolute-deadline timing; `ook_tx_clamp_us()` is pure | split |
| Scan engine | `scan_engine.{c,h}` | async RSSI frequency-sweep state machine + 4 ISM band presets; `scan_freq_at()`/`scan_track_peak()`/`scan_preset()` are pure | split |
| Clip store | `capture_store.{c,h}` | single-clip FIFO over a caller-provided buffer (app places it in PSRAM), 64-bit tick overflow protection | pure-ish |

**Out of scope (YAGNI):**

- The subghz UI screens (`screen_analyzer`, `screen_monitor`, `screen_demo`) and LVGL — this increment is driver + RTT demo only, no display UI.
- Frequency hopping / FCC-compliance logic (not present in the source either).
- GDO2 (GPIO37): defined in `board.h`, left unused, exactly as in the source.
- Any FSK demodulation path beyond the register settings `cc1101_monitor_rx()` already applies (OOK/2FSK transparent modes only).

## Architecture

The radio is a self-contained `bsp/radio/` subsystem with a clean dependency
boundary: it depends only on already-staged platform primitives and never on the
display or UI.

```
apps/hello_cc1101 (RTT demo)
        │  calls
        ▼
bsp/radio/  ── cc1101 (core) ── cc1101_regs (pure math)
            ── gdo_capture (PIO2+DMA)  ── monitor_engine (pure stats)
            ── ook_tx  ── scan_engine  ── capture_store
        │  depends on (all already present)
        ▼
bsp/platform/  spi_bus (acquire/release/cs + GPIO8 mux) · ioexp (antenna) · board (pins/clocks) · diag (RTT)
```

- **SPI access** goes exclusively through the staged arbiter: every core-driver
  transaction is `spi_bus_acquire_cc1101()` → burst → `spi_bus_release_cc1101()`,
  with `spi_bus_cc1101_cs()` bracketing each register access. The arbiter spins on
  `st7796_flush_busy()` first, muxes GPIO8 (LCD_DC ↔ CC1101 MISO), and switches the
  bus to 5 MHz. Unchanged from source; unchanged here.
- **Antenna** is routed by `ioexp_antenna(ANT_CC1101_433)` (already defaults there in
  `ioexp_init()`), so the demo only re-asserts it explicitly.
- **Capture data flow:** CC1101 GDO0 (async sliced data) → PIO2 SM measures low/high
  durations at 2 MHz (1 µs/tick) → DMA ENDLESS ring (4096 words) → app polls
  `gdo_capture_drain()` → optionally `monitor_engine` stats and/or `capture_store`.
- **TX data flow:** `cc1101_tx_ook_start()` puts the modem in async OOK and 3-states
  the chip's GDO0 driver → `ook_tx_send()` drives GPIO32 as an SIO output from a
  duration array → `cc1101_tx_ook_stop()` + `gdo_capture_attach_pin()` restore RX.

## The one adaptation: PIO2

The source `gdo_capture.c` uses `pio0` and calls `pio_set_gpio_base(pio0, 16)` to
reach GPIO32. In `wilibsp`, **pio0 is owned by the audio I2S driver** (GPIO 4–7) and
**pio1 by the WS2812 LED driver** (GPIO21). Setting the GPIO base to 16 on a shared
PIO block would move that block's whole window to GPIO 16–47 and break audio's access
to GPIO 4–7.

**Adaptation:** change `gdo_capture.c`'s `static PIO s_pio = pio0;` to `pio2` — the
RP2350's third, currently-unused PIO block. `pio_set_gpio_base(s_pio, 16)` stays.
This makes radio capture independent of both audio and LEDs. It is the *only*
functional change to harvested code; every other file copies byte-for-byte.

The BSP must compile with `PICO_PIO_USE_GPIO_BASE=1` (required for any GPIO≥32 PIO
access). This is added as a `PUBLIC` compile definition on `freewili2_bsp` so it
propagates to every app that links it.

## Build integration

1. **Copy** the 14 files (7 `.c`, 7 `.h`) + `gdo_capture.pio` into `bsp/radio/`,
   preserving the `radio/` include paths.
2. **`bsp/CMakeLists.txt`:**
   - Add the 7 `.c` files to the `add_library(freewili2_bsp STATIC ...)` source list.
   - Add `pico_generate_pio_header(freewili2_bsp ${CMAKE_CURRENT_SOURCE_DIR}/radio/gdo_capture.pio)`.
   - Add `target_compile_definitions(freewili2_bsp PUBLIC PICO_PIO_USE_GPIO_BASE=1)`.
   - `hardware_pio`, `hardware_dma`, `hardware_spi` are already linked — no new
     pico-sdk components needed.
3. **`bsp/fw2.h`:** add the radio includes, tagged `// (Task 3-radio)`:
   `cc1101.h`, `gdo_capture.h`, `scan_engine.h`, `ook_tx.h`, `monitor_engine.h`,
   `capture_store.h`.

## Host tests

Ported from the subghz `tests/` tree, using the wilibsp pattern (a
`tests/test_*.c` that directly `#include`s the module `.c` from `../bsp`, plus an
`add_test`). Takes `fw test` from 3 tests to **8**:

| Test | Module | `HOST_TEST` guard | Notes |
|---|---|---|---|
| `test_cc1101_regs` | `cc1101_regs.c` | none (pure) | 433.92 MHz freq calc, RSSI two's-complement, all 5 modulation enums, band boundaries |
| `test_monitor_engine` | `monitor_engine.c` | none (pure) | edge/burst/gap statistics, width histogram |
| `test_scan_engine` | `scan_engine.c` | **yes** | compiled with `HOST_TEST` to exclude the hardware sweep loop; tests `scan_freq_at`/`scan_track_peak`/`scan_preset` |
| `test_ook_tx` | `ook_tx.c` | **yes** | compiled with `HOST_TEST` to exclude `ook_tx_send`; tests `ook_tx_clamp_us` |
| `test_capture_store` | `capture_store.c` | none | single-clip FIFO, tick-overflow handling |

## Demo app: `apps/hello_cc1101`

RTT-only (no display, no LVGL). A single linear self-verify sequence — chosen because
it exercises SPI, PIO/DMA capture, and OOK TX timing with **zero external RF gear**:

1. **Probe.** `board_init()` → `ioexp_antenna(ANT_CC1101_433)` → `cc1101_init()`.
   `DIAG` PARTNUM/VERSION and a clear PASS/FAIL on the presence check
   (`part==0x00 && ver∉{0x00,0xFF}`). Halt-with-message on FAIL.
2. **RSSI sweep.** Drive `scan_engine` across the 433 band; `DIAG` the noise floor and
   peak (frequency + dBm, all integers → RTT-safe, no floats). Verifies SPI + RSSI
   with nothing connected.
3. **Same-pad TX→capture check.** `gdo_capture_init()` (on pio2) + `gdo_capture_start()`,
   then `cc1101_tx_ook_start(433_920_000)` (exercises the OOK-TX register path over SPI
   and keys the carrier) + `ook_tx_send()` of a synthesized OOK pulse train that drives
   GDO0/GPIO32 as an SIO output, then `cc1101_tx_ook_stop()` +
   `gdo_capture_attach_pin()` and `gdo_capture_drain()`. The capture PIO reads the same
   GDO0 pad the transmit code is toggling (the pad input synchronizer is live regardless
   of which function drives the output), so this deterministically verifies the
   PIO2/DMA/drain path **and** the `ook_tx` timing without any RF link. `DIAG` the
   captured edge count and assert it is on the order of the number of pulses sent.

   **This is a plumbing test, not an RF-link test.** GDO0 is a single pin — it is either
   the chip's RX-data output or the MCU's TX-data input, never both at once — so the demo
   does **not** claim the radio receives its own transmission over the air. Verifying an
   actual RX demod path needs an external transmitter (a follow-on bench test, out of
   scope here).

Register in the top-level `CMakeLists.txt` via `add_subdirectory(apps/hello_cc1101)`;
build with `pico_set_binary_type(hello_cc1101 copy_to_ram)`.

## Error handling & invariants

- **Diagnostics** exclusively via `DIAG()` → RTT (no UART/USB stdio, no floats). All
  demo output is integers (dBm, counts, hex reg values).
- **No new DMA IRQ handler:** `gdo_capture` uses an ENDLESS DMA channel and reads
  progress by polling `write_addr` — it registers no handler, so the
  "DMA_IRQ_0 is a shared handler line" invariant is respected trivially.
- **SPI arbiter** and **antenna mux** are already staged and used unchanged.
- **Clock assumptions hold:** the PIO clkdiv (125 @ 250 MHz → 2 MHz, 1 µs/tick) and
  `busy_wait_*` OOK timing both assume the 250 MHz sysclk that `board.c` already sets.
  No source-repo clock config is adopted.
- `cc1101_init()` returns `bool`; the demo branches on it (mirrors `ft6336_init()`,
  `ioexp_init()` conventions).

## Documentation updates

- `docs/hardware/catalog.md`: flip the **Sub-GHz radio — CC1101** row from TODO to
  DONE (driver location `bsp/radio/*`, demo `apps/hello_cc1101`).
- `docs/hardware/facts.md`: record the **PIO2 + GPIO-base-16** decision — radio
  capture cannot share pio0 with audio; pio1 is LEDs; pio2 is the radio's. Note
  `PICO_PIO_USE_GPIO_BASE=1` is required, and GDO2/GPIO37 is intentionally unused.
- `docs/drivers/radio.md`: new per-driver usage doc (init/probe, sweep, monitor,
  OOK TX/capture API, antenna selection).

## Success criteria

- `fw test` passes 8/8 (5 new radio tests + existing 3).
- `fw build hello_cc1101` compiles clean; `bsp/CMakeLists.txt` and `fw2.h` include
  the radio sources.
- Whole-branch review approved (opus final pass).
- On-hardware smoke test: `fw flash hello_cc1101` + `fw rtt` shows PARTNUM=0x00 with a
  plausible VERSION, a sane RSSI floor/peak from the sweep, and a captured edge count
  from the same-pad TX→capture check on the order of the pulses sent.
- Merge to `master` off a `feat/cc1101-radio` branch; findings recorded under
  `docs/superpowers/findings/`.
