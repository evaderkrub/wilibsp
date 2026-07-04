#ifndef SCAN_ENGINE_H
#define SCAN_ENGINE_H
#include <stdint.h>
#include <stdbool.h>

typedef struct { uint32_t freq_hz; int rssi_dbm; bool valid; } scan_peak_t;

uint32_t   scan_freq_at(uint32_t start_hz, uint32_t step_hz, uint16_t n, uint16_t i);
scan_peak_t scan_track_peak(scan_peak_t cur, uint32_t freq_hz, int rssi_dbm);

void       scan_begin(uint32_t start_hz, uint32_t step_hz, uint16_t n);
void       scan_step(void);
scan_peak_t scan_get_peak(void);
bool       scan_get_last(uint32_t *freq_hz, int *rssi_dbm);
bool       scan_preset(int idx, uint32_t *start_hz, uint32_t *step_hz, uint16_t *n);
bool       scan_take_row(int *out, uint16_t n);
#endif
