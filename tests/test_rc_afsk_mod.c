// Host test: AFSK modulator — duration, tone frequency by zero-crossing count,
// phase continuity (no sample-to-sample jump can exceed the max slope of the
// higher tone plus the pre-emphasis amplitude step at a tone boundary:
// 2*pi*2200/16000 * 19500 ~ 16846, + step (3/16)*24000 = 4500 -> < 22000;
// a real discontinuity/click would jump ~2*amplitude ~ 48000).
#include "afsk_mod.h"
#include "test_util.h"
#include <stdlib.h>

static unsigned zero_crossings(const int16_t *s, unsigned n) {
    unsigned zc = 0;
    for (unsigned i = 1; i < n; i++)
        if ((s[i - 1] < 0) != (s[i] < 0)) zc++;
    return zc;
}

int main(void) {
    // Carrier only (nbytes = 0): 150 ms of 1200 Hz + 4 bit tail.
    unsigned cap = afsk_mod_max_samples(0);
    int16_t *buf = malloc(cap * sizeof(int16_t));
    unsigned n = afsk_mod_render(NULL, 0, buf);
    ASSERT_TRUE(n <= cap);
    // 150 ms carrier = 2400 samples; tail = 4 bits ~ 213 at 300 baud. Allow +/- 4.
    ASSERT_TRUE(n >= 2609 && n <= 2617);
    // 1200 Hz over the first 2400 samples -> 2 crossings/cycle * 1200 * 0.15 = 360.
    unsigned zc = zero_crossings(buf, 2400);
    ASSERT_TRUE(zc >= 358 && zc <= 362);
    free(buf);

    // One byte 0x00: start(0) + 8 zero bits + stop(1) -> 9 space bits + 1 mark bit.
    // Space section: 9 bits * 53.33 smp = 480 samples of 2200 Hz after the carrier.
    cap = afsk_mod_max_samples(1);
    buf = malloc(cap * sizeof(int16_t));
    uint8_t zero = 0x00;
    n = afsk_mod_render(&zero, 1, buf);
    ASSERT_TRUE(n <= cap);
    // 2200 Hz across samples 2400..2880 -> 2*2200*(480/16000) = 132 crossings.
    zc = zero_crossings(buf + 2400, 480);
    ASSERT_TRUE(zc >= 127 && zc <= 137);

    // Phase continuity across the whole render.
    int max_jump = 0;
    for (unsigned i = 1; i < n; i++) {
        int d = (int)buf[i] - (int)buf[i - 1];
        if (d < 0) d = -d;
        if (d > max_jump) max_jump = d;
    }
    ASSERT_TRUE(max_jump < 22000);
    free(buf);

    // Duration formula: 10 bits/byte at 300 baud.
    cap = afsk_mod_max_samples(214);
    buf = malloc(cap * sizeof(int16_t));
    uint8_t big[214];
    for (int i = 0; i < 214; i++) big[i] = (uint8_t)i;
    n = afsk_mod_render(big, 214, buf);
    // 2400 carrier + 2140 bits * 53.333 + 4-bit tail = 2400 + 114133 + 213 = 116746 (+/-8).
    ASSERT_TRUE(n >= 116738 && n <= 116754);
    ASSERT_TRUE(n <= cap);
    free(buf);

    // Bit order: 0x0F LSB-first -> data bits 0..3 = 1 (mark 1200 Hz), bits
    // 4..7 = 0 (space 2200 Hz). After the 2400-sample carrier and the ~53.3
    // sample start bit, data bits 0..3 span samples ~2453..2667 and bits 4..7
    // span ~2667..2880. Window A (2480..2607) sits inside the mark section,
    // window B (2700..2827) inside the space section. Mark at 1200 Hz gives
    // ~19 zero crossings per 128-sample window; space at 2200 Hz gives ~35.
    // MSB-first encoding would swap the windows and fail both.
    cap = afsk_mod_max_samples(1);
    buf = malloc(cap * sizeof(int16_t));
    uint8_t nibbles = 0x0F;
    n = afsk_mod_render(&nibbles, 1, buf);
    ASSERT_TRUE(n <= cap);
    {
        unsigned zc_a = zero_crossings(buf + 2480, 128);
        unsigned zc_b = zero_crossings(buf + 2700, 128);
        ASSERT_TRUE(zc_a >= 16 && zc_a <= 22);   // mark (1200 Hz) section
        ASSERT_TRUE(zc_b >= 31 && zc_b <= 39);   // space (2200 Hz) section
        ASSERT_TRUE(zc_a < zc_b);
    }
    free(buf);

    TEST_RETURN();
}
