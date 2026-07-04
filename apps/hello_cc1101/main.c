// hello_cc1101 — on-hardware smoke test for the CC1101 sub-GHz radio driver.
// Three phases over SEGGER RTT (fw rtt), no display, no external RF gear:
//   1) probe the chip over SPI1 (PARTNUM/VERSION);
//   2) sweep RSSI across the 433 MHz ISM band and report floor/peak;
//   3) same-pad TX->capture check: drive an OOK pulse train on GDO0 while the
//      capture PIO (pio2) + DMA records the same pad, then report the edge count.
// Phase 3 is a plumbing test (one pin can't RX its own TX over the air); it proves
// the SPI TX-config path, ook_tx timing, and the PIO2/DMA/drain chain end to end.
#include "fw2.h"
#include "platform/diag.h"
#include "platform/ioexp.h"
#include "pico/stdlib.h"

int main(void) {
    board_init();   // 250 MHz + vreg + clk_peri re-source; also ioexp_init + I2C1
    DIAG("\n=== hello_cc1101: sub-GHz radio smoke test ===\n");

    // --- Phase 1: probe ---
    ioexp_antenna(ANT_CC1101_433);          // route the 433 MHz antenna
    bool ok = cc1101_init();                // DIAGs PARTNUM/VERSION internally
    DIAG("cc1101: probe %s\n", ok ? "PASS" : "FAIL");
    if (!ok) {
        DIAG("cc1101: halting — no radio on SPI1 (check bus/power/antenna)\n");
        while (1) tight_loop_contents();
    }

    // --- Phase 2: RSSI sweep across the 433 MHz ISM band ---
    uint32_t start_hz, step_hz; uint16_t nbins;
    scan_preset(1, &start_hz, &step_hz, &nbins);   // idx 1 = 433 band, 128 bins
    cc1101_set_modulation(CC1101_MOD_ASK_OOK);
    cc1101_set_frequency(start_hz + step_hz * (nbins / 2));  // band center
    cc1101_calibrate();                            // one manual cal for the band
    scan_begin(start_hz, step_hz, nbins);
    // Drive the async sweep to completion of one full row (each bin needs
    // ST_SET_FREQ -> ST_ARM_RX -> ST_WAIT (400us settle) -> ST_READ, so several
    // scan_step() calls per bin; give generous slack). row[] is pre-zeroed so the
    // floor computation is safe even if the row never completes within the guard.
    int row[128] = {0};
    bool have_row = false;
    for (int guard = 0; guard < nbins * 16 && !have_row; guard++) {
        have_row = scan_take_row(row, nbins);
        scan_step();
        sleep_us(100);
    }
    scan_peak_t pk = scan_get_peak();   // peak accumulates per-bin, valid with or without a full row
    int floor_dbm = 0;
    for (int i = 0; i < nbins; i++) if (row[i] < floor_dbm) floor_dbm = row[i];
    DIAG("scan: row=%s floor=%d dBm  peak=%d dBm @ %u Hz\n",
         have_row ? "ok" : "partial", floor_dbm, pk.rssi_dbm, (unsigned)pk.freq_hz);

    // --- Phase 3: same-pad TX -> capture check ---
    gdo_capture_init();                     // claim pio2 SM + DMA
    gdo_capture_start();
    cc1101_tx_ook_start(433920000u);        // OOK TX regs + key carrier; 3-states chip GDO0
    // Synthesized OOK train: 24 pulses of 500 us each (level toggles per entry).
    static const uint32_t train[24] = {
        500,500,500,500,500,500,500,500,
        500,500,500,500,500,500,500,500,
        500,500,500,500,500,500,500,500,
    };
    ook_tx_send(train, 24, true);           // drives GDO0/GPIO32 as SIO output
    cc1101_tx_ook_stop();
    gdo_capture_attach_pin();               // re-route GDO0 to the capture PIO
    sleep_ms(2);
    static uint32_t drained[128];
    uint32_t got = gdo_capture_drain(drained, 128);
    DIAG("capture: sent=24 pulses, drained=%u edges (nonzero => PIO2/DMA path live)\n",
         (unsigned)got);
    DIAG("cc1101: smoke test complete\n");

    while (1) tight_loop_contents();
}
