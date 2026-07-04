// src/radio/monitor_engine.h
#ifndef MONITOR_ENGINE_H
#define MONITOR_ENGINE_H
#include <stdint.h>
#include <stdbool.h>

// Edge-duration stream: each value is the number of 1 us ticks the CURRENT level
// was held before an edge; the level toggles per value. A value >= MON_IDLE_TICKS
// is a saturated idle gap (PIO overflow sentinel) and marks a burst boundary.
#define MON_WIDTH_BINS 8
#define MON_IDLE_TICKS 20000u   // >= 20 ms held == idle / burst boundary

typedef struct {
    uint32_t bins[MON_WIDTH_BINS];  // cumulative pulse-width histogram
    uint32_t edges;                 // pulses in the current/last burst
    uint32_t burst_ticks;           // active ticks in the current/last burst
    uint32_t max_gap_ticks;         // largest idle gap seen
    bool     level;                 // reconstructed level after the last edge
    bool     in_burst;              // inside a burst (not idle)
} monitor_stats_t;

int  monitor_width_bin(uint32_t ticks);            // 0..MON_WIDTH_BINS-1
void monitor_reset(monitor_stats_t *st, bool start_level);
void monitor_feed(monitor_stats_t *st, uint32_t ticks);
#endif
