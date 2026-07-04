// src/radio/monitor_engine.c
#include "radio/monitor_engine.h"

// Log-ish width buckets by tick (us) upper bounds; last bucket catches the rest.
static const uint32_t BIN_MAX[MON_WIDTH_BINS] = {
    64, 128, 256, 512, 1024, 2048, 4096, 0xFFFFFFFFu
};

int monitor_width_bin(uint32_t ticks) {
    for (int i = 0; i < MON_WIDTH_BINS; i++)
        if (ticks <= BIN_MAX[i]) return i;
    return MON_WIDTH_BINS - 1;
}

void monitor_reset(monitor_stats_t *st, bool start_level) {
    for (int i = 0; i < MON_WIDTH_BINS; i++) st->bins[i] = 0;
    st->edges = 0; st->burst_ticks = 0; st->max_gap_ticks = 0;
    st->level = start_level; st->in_burst = false;
}

void monitor_feed(monitor_stats_t *st, uint32_t ticks) {
    if (ticks >= MON_IDLE_TICKS) {              // idle gap: close the burst
        if (ticks > st->max_gap_ticks) st->max_gap_ticks = ticks;
        st->in_burst = false;
        st->level = !st->level;
        return;
    }
    if (!st->in_burst) {                        // first pulse after idle: new burst
        st->in_burst = true;
        st->edges = 0; st->burst_ticks = 0;
    }
    st->bins[monitor_width_bin(ticks)]++;
    st->edges++;
    st->burst_ticks += ticks;
    st->level = !st->level;
}
