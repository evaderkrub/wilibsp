// src/radio/capture_store.h — one PSRAM clip of GDO0 edge durations + metadata.
// Pure: the core operates on a caller-supplied buffer (PSRAM on target, a plain
// array in host tests). No Pico SDK dependency.
#ifndef CAPTURE_STORE_H
#define CAPTURE_STORE_H
#include <stdint.h>
#include <stdbool.h>
#include "radio/cc1101_regs.h"   // cc1101_mod_t

// Bind the store to a duration buffer + capacity (in durations). Clears the clip.
void     capture_store_init(uint32_t *buf, uint32_t capacity);
// Start a new clip: reset length + total ticks, record clip metadata.
// `antenna` is the opaque ANT_* value routed at record time (for TX replay).
void     capture_store_begin(uint32_t freq, cc1101_mod_t mod, bool start_level, int antenna);
// Append up to n durations; returns how many were stored (< n once it fills).
uint32_t capture_store_append(const uint32_t *durs, uint32_t n);
bool     capture_store_full(void);
uint32_t capture_store_len(void);          // durations in the current clip
const uint32_t *capture_store_data(void);  // pointer to the duration array

uint32_t     capture_store_freq(void);
cc1101_mod_t capture_store_mod(void);
bool         capture_store_start_level(void);
int          capture_store_antenna(void);       // ANT_* routed at record time
uint64_t     capture_store_total_ticks(void);   // sum of stored durations (us)
#endif
