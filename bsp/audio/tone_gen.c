// src/audio/tone_gen.c — vendored from microphonearray/src/codec/tone_gen.c.
// Pure sine generator. The first sample is taken at a half-step phase offset so
// the sample grid is centered on the sine: one full period then shows both of its
// zero crossings inside the window rather than pinning sample 0 exactly to zero
// (which hides the rising crossing at the period boundary). *phase is advanced by
// n steps so successive calls stay phase-continuous.
#include "audio/tone_gen.h"
#include <math.h>
void tone_gen_fill(int16_t* buf, unsigned n, float hz, float fs, float* phase) {
    const float two_pi = 2.0f * (float)M_PI;
    float step = two_pi * hz / fs;
    float p = *phase - 0.5f * step;   // sample-center offset
    if (p < 0.0f) p += two_pi;
    for (unsigned i = 0; i < n; i++) {
        buf[i] = (int16_t)(28000.0f * sinf(p));
        p += step; if (p >= two_pi) p -= two_pi;
    }
    // Keep caller phase continuous: advance by exactly n steps from input phase.
    float out = *phase + step * (float)n;
    out = fmodf(out, two_pi); if (out < 0.0f) out += two_pi;
    *phase = out;
}
