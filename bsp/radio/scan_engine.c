#include "scan_engine.h"

uint32_t scan_freq_at(uint32_t start_hz, uint32_t step_hz, uint16_t n, uint16_t i) {
    (void)n;
    return start_hz + (uint32_t)step_hz * i;
}

scan_peak_t scan_track_peak(scan_peak_t cur, uint32_t freq_hz, int rssi_dbm) {
    if (!cur.valid || rssi_dbm > cur.rssi_dbm) {
        cur.valid = true; cur.freq_hz = freq_hz; cur.rssi_dbm = rssi_dbm;
    }
    return cur;
}

bool scan_preset(int idx, uint32_t *start_hz, uint32_t *step_hz, uint16_t *n) {
    static const uint32_t BANDS[4][2] = {
        { 314000000u, 316000000u },   // 0: 315 MHz ISM
        { 433000000u, 435000000u },   // 1: 433.92 MHz ISM
        { 867000000u, 869000000u },   // 2: 868 MHz ISM
        { 914000000u, 916000000u },   // 3: 915 MHz ISM
    };
    if (idx < 0 || idx > 3) return false;
    *n = 128;
    *start_hz = BANDS[idx][0];
    *step_hz = (BANDS[idx][1] - BANDS[idx][0]) / 128u;
    return true;
}

#ifndef HOST_TEST
#include "radio/cc1101.h"
#include "radio/cc1101_regs.h"
#include "pico/stdlib.h"

#define SCAN_MAX_BINS 128
// Fixed RX settle after each hop. Per-hop auto-cal is OFF (fast PLL relock), so the
// limiter is AGC/RSSI settling, not calibration or MARCSTATE. Wait a fixed window
// so RSSI is read settled (reading too early returns an inflated AGC transient).
#define RSSI_SETTLE_US 400

static uint32_t s_start, s_step; static uint16_t s_n, s_i;
static scan_peak_t s_peak; static uint32_t s_last_f; static int s_last_rssi; static bool s_have_last;
static int s_row[SCAN_MAX_BINS];        // RSSI of the current sweep, per bin
static int s_row_done[SCAN_MAX_BINS];   // last fully-completed sweep
static bool s_row_ready;
typedef enum { ST_SET_FREQ, ST_ARM_RX, ST_WAIT, ST_READ } st_t;
static st_t s_state; static absolute_time_t s_settle_at;

void scan_begin(uint32_t start_hz, uint32_t step_hz, uint16_t n) {
    if (n > SCAN_MAX_BINS) n = SCAN_MAX_BINS;
    s_start = start_hz; s_step = step_hz; s_n = n; s_i = 0;
    s_peak = (scan_peak_t){0,-200,false}; s_have_last = false; s_state = ST_SET_FREQ;
    s_row_ready = false;
    for (int k = 0; k < SCAN_MAX_BINS; k++) s_row[k] = s_row_done[k] = -200;
}

// Advance to the next bin; on wrap to index 0 a full sweep just completed, so
// snapshot the per-bin row. Fires whether the last bin was read OR skipped
// (out-of-band), so scan_take_row() can never stall on an out-of-band tail bin.
static void scan_advance(void) {
    bool wrapped = (s_i == s_n - 1);
    s_i = (uint16_t)((s_i + 1) % s_n);
    if (wrapped) {
        for (int k = 0; k < s_n; k++) s_row_done[k] = s_row[k];
        s_row_ready = true;
    }
}

void scan_step(void) {
    if (s_n == 0) return;
    uint32_t f = scan_freq_at(s_start, s_step, s_n, s_i);
    switch (s_state) {
        case ST_SET_FREQ:
            if (!cc1101_freq_in_band(f)) {           // skip out-of-band, one advance/call
                scan_advance();
                break;
            }
            cc1101_set_frequency(f);
            s_state = ST_ARM_RX;
            break;
        case ST_ARM_RX:
            cc1101_strobe_rx();
            s_settle_at = make_timeout_time_us(RSSI_SETTLE_US);
            s_state = ST_WAIT;
            break;
        case ST_WAIT:                                // fixed AGC/RSSI settle (no bus op)
            if (absolute_time_diff_us(get_absolute_time(), s_settle_at) <= 0)
                s_state = ST_READ;
            break;
        case ST_READ: {
            int rssi = cc1101_read_rssi_dbm();
            s_last_f = f; s_last_rssi = rssi; s_have_last = true;
            s_row[s_i] = rssi;                       // s_i < s_n <= SCAN_MAX_BINS (clamped in scan_begin)
            s_peak = scan_track_peak(s_peak, f, rssi);
            scan_advance();                          // advances + snapshots on sweep completion
            s_state = ST_SET_FREQ;
            break;
        }
    }
}

bool scan_take_row(int *out, uint16_t n) {
    if (!s_row_ready) return false;
    uint16_t m = (n < s_n) ? n : s_n;
    for (uint16_t k = 0; k < m; k++) out[k] = s_row_done[k];
    s_row_ready = false;
    return true;
}

scan_peak_t scan_get_peak(void) { return s_peak; }
bool scan_get_last(uint32_t *freq_hz, int *rssi_dbm) {
    if (!s_have_last) return false;
    *freq_hz = s_last_f; *rssi_dbm = s_last_rssi; return true;
}
#endif
