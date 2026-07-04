// tests/test_monitor_engine.c
#include "test_util.h"
#include "radio/monitor_engine.h"

int main(void) {
    // Width bins are monotonic log-ish buckets: shorter ticks -> lower bin.
    ASSERT_TRUE(monitor_width_bin(50)  < monitor_width_bin(500));
    ASSERT_TRUE(monitor_width_bin(500) < monitor_width_bin(5000));
    ASSERT_EQ(monitor_width_bin(0), 0);
    // Saturated/huge widths clamp to the top bin, never out of range.
    ASSERT_TRUE(monitor_width_bin(0xFFFFFFFFu) == MON_WIDTH_BINS - 1);

    monitor_stats_t st;
    monitor_reset(&st, false);              // start level low
    ASSERT_EQ((int)st.level, 0);
    ASSERT_EQ((int)st.in_burst, 0);

    // A burst of 4 pulses (~350us each), then a 30ms idle gap.
    monitor_feed(&st, 350); monitor_feed(&st, 350);
    monitor_feed(&st, 350); monitor_feed(&st, 350);
    ASSERT_EQ((int)st.in_burst, 1);
    ASSERT_EQ(st.edges, 4u);
    ASSERT_EQ(st.burst_ticks, 1400u);
    ASSERT_EQ((int)st.level, 0);            // toggled 4x from low -> back to low
    ASSERT_EQ(st.bins[3], 4u);              // four ~350-tick pulses land in bin 3

    monitor_feed(&st, 30000);               // >= MON_IDLE_TICKS -> idle
    ASSERT_EQ((int)st.in_burst, 0);
    ASSERT_EQ(st.max_gap_ticks, 30000u);

    // Next pulse after idle starts a fresh burst (edges/burst_ticks reset).
    monitor_feed(&st, 200);
    ASSERT_EQ((int)st.in_burst, 1);
    ASSERT_EQ(st.edges, 1u);
    ASSERT_EQ(st.burst_ticks, 200u);
    TEST_RETURN();
}
