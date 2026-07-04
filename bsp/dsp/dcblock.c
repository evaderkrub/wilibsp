// src/dsp/dcblock.c — one-pole DC-blocking high-pass (see dcblock.h).
#include "dsp/dcblock.h"

void dcblock_inplace(int16_t* buf, unsigned n) {
    if (n == 0) return;
    float prev_x = (float)buf[0];
    float prev_y = 0.0f;
    buf[0] = 0;  // first output is defined as 0 (no prior sample)
    for (unsigned i = 1; i < n; i++) {
        float x = (float)buf[i];
        float y = x - prev_x + DCBLOCK_R * prev_y;
        prev_x = x;
        prev_y = y;
        if (y > 32767.0f) y = 32767.0f;
        else if (y < -32768.0f) y = -32768.0f;
        buf[i] = (int16_t)y;
    }
}
