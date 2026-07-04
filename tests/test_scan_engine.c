#include "test_util.h"
#include "radio/scan_engine.h"

int main(void) {
    // frequency stepping
    ASSERT_EQ(scan_freq_at(433000000u, 100000u, 10, 0), 433000000u);
    ASSERT_EQ(scan_freq_at(433000000u, 100000u, 10, 5), 433500000u);

    // peak tracking keeps the strongest (least-negative) RSSI
    scan_peak_t p = { 0, -200, false };
    p = scan_track_peak(p, 433000000u, -80);
    ASSERT_EQ((int)p.valid, 1);
    ASSERT_EQ(p.freq_hz, 433000000u);
    ASSERT_EQ(p.rssi_dbm, -80);
    p = scan_track_peak(p, 434000000u, -50);   // stronger -> replaces
    ASSERT_EQ(p.freq_hz, 434000000u);
    ASSERT_EQ(p.rssi_dbm, -50);
    p = scan_track_peak(p, 435000000u, -90);   // weaker -> ignored
    ASSERT_EQ(p.freq_hz, 434000000u);

    // band presets (pure): idx 1 = 433 band, 128 bins over 433.0..435.0 MHz
    uint32_t ps_start = 0, ps_step = 0; uint16_t ps_n = 0;
    ASSERT_EQ((int)scan_preset(1, &ps_start, &ps_step, &ps_n), 1);
    ASSERT_EQ(ps_start, 433000000u);
    ASSERT_EQ(ps_n, 128);
    ASSERT_EQ(ps_step, (435000000u - 433000000u) / 128u);   // 15625
    ASSERT_EQ((int)scan_preset(0, &ps_start, &ps_step, &ps_n), 1);
    ASSERT_EQ(ps_start, 314000000u);
    ASSERT_EQ((int)scan_preset(9, &ps_start, &ps_step, &ps_n), 0);  // out of range
    TEST_RETURN();
}
