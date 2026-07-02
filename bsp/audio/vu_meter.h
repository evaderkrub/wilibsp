// src/audio/vu_meter.h — pure VU-meter math (no hardware deps; host-testable).
#ifndef VU_METER_H
#define VU_METER_H
#include <stdint.h>

// dB floor of the bar scale; peaks at/below this map to length 0.
#define VU_DB_FLOOR   (-48)
// Peak thresholds for the bar color (linear 0..32767).
#define VU_YELLOW_PEAK 8192
#define VU_RED_PEAK    24576

// Extract a signed 16-bit sample from one 32-bit I2S frame.
// slot 0 = left = high 16 bits; slot 1 = right = low 16 bits.
int16_t vu_sample(uint32_t frame, int slot);

// Max abs(sample) over n frames for `slot`, clamped to 0..32767.
uint16_t vu_peak(const uint32_t *frames, uint32_t n, int slot);

// Map peak (0..32767) to a bar length 0..max_px on a dB scale (floor VU_DB_FLOOR).
int vu_bar_px(uint16_t peak, int max_px);

// Big-endian RGB565 bar color for a peak: green/yellow/red by threshold.
uint16_t vu_color_be(uint16_t peak);

#endif // VU_METER_H
