#include "afsk_mod.h"
#include <math.h>

static int16_t s_sin[256];
static int s_lut_ready;

static void lut_init(void) {
    if (s_lut_ready) return;
    for (int i = 0; i < 256; i++)
        s_sin[i] = (int16_t)lroundf((float)AFSK_AMPL *
                                    sinf((float)i * 6.28318531f / 256.0f));
    s_lut_ready = 1;
}

#define PH_INC(hz) (uint32_t)(((uint64_t)(hz) << 32) / AFSK_FS)
#define BIT_Q16    (uint32_t)(((uint64_t)AFSK_FS << 16) / AFSK_BAUD)

typedef struct { uint32_t phase, acc; int16_t *out; unsigned n; } mod_t;

// Render one tone segment lasting dur_q16 (Q16 sample count); the fractional
// remainder carries in m->acc so bit boundaries never drift.
static void emit_tone(mod_t *m, uint32_t hz, uint32_t dur_q16) {
    uint32_t inc = PH_INC(hz);
    int space = (hz == AFSK_SPACE_HZ);   // pre-emphasis: space plays quieter
    m->acc += dur_q16;
    while (m->acc >= (1u << 16)) {
        m->acc -= 1u << 16;
        int16_t v = s_sin[m->phase >> 24];
        m->out[m->n++] = space ? (int16_t)(((int32_t)v * AFSK_SPACE_NUM) >> 4) : v;
        m->phase += inc;
    }
}

unsigned afsk_mod_max_samples(unsigned nbytes) {
    return (AFSK_CARRIER_MS * AFSK_FS) / 1000u
         + (nbytes * 10u * AFSK_FS + AFSK_BAUD - 1u) / AFSK_BAUD
         + (4u * AFSK_FS + AFSK_BAUD - 1u) / AFSK_BAUD   // 4-bit tail
         + 8u;                                           // rounding slack
}

unsigned afsk_mod_render(const uint8_t *bytes, unsigned nbytes, int16_t *out) {
    lut_init();
    mod_t m = { 0, 0, out, 0 };
    emit_tone(&m, AFSK_MARK_HZ,
              (uint32_t)(((uint64_t)AFSK_CARRIER_MS * AFSK_FS << 16) / 1000u));
    for (unsigned i = 0; i < nbytes; i++) {
        emit_tone(&m, AFSK_SPACE_HZ, BIT_Q16);                    // start bit
        for (int b = 0; b < 8; b++)
            emit_tone(&m, ((bytes[i] >> b) & 1u) ? AFSK_MARK_HZ
                                                 : AFSK_SPACE_HZ, BIT_Q16);
        emit_tone(&m, AFSK_MARK_HZ, BIT_Q16);                     // stop bit
    }
    emit_tone(&m, AFSK_MARK_HZ, BIT_Q16 * 4u);  // tail: flush demod LPF/PLL
    return m.n;
}
