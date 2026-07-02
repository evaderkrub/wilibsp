// src/audio/vu_meter.c — pure VU-meter math. No hardware, no I/O.
#include "audio/vu_meter.h"
#include <math.h>

int16_t vu_sample(uint32_t frame, int slot) {
    uint16_t u = (slot == 0) ? (uint16_t)(frame >> 16) : (uint16_t)(frame & 0xFFFF);
    return (int16_t)u;   // two's-complement reinterpret
}

uint16_t vu_peak(const uint32_t *frames, uint32_t n, int slot) {
    int32_t peak = 0;
    for (uint32_t i = 0; i < n; i++) {
        int32_t s = vu_sample(frames[i], slot);
        if (s < 0) s = -s;
        if (s > peak) peak = s;
    }
    if (peak > 32767) peak = 32767;   // -32768 -> 32767
    return (uint16_t)peak;
}

int vu_bar_px(uint16_t peak, int max_px) {
    if (peak == 0) return 0;
    float db = 20.0f * log10f((float)peak / 32767.0f);
    if (db <= (float)VU_DB_FLOOR) return 0;
    if (db >= 0.0f) return max_px;
    float frac = (db - (float)VU_DB_FLOOR) / (0.0f - (float)VU_DB_FLOOR);
    int px = (int)(frac * (float)max_px + 0.5f);
    if (px < 0) px = 0;
    if (px > max_px) px = max_px;
    return px;
}

static uint16_t be(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }

uint16_t vu_color_be(uint16_t peak) {
    if (peak < VU_YELLOW_PEAK) return be(0x07E0);   // green
    if (peak < VU_RED_PEAK)    return be(0xFFE0);   // yellow
    return be(0xF800);                              // red
}
