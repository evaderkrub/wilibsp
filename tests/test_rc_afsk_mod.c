// Host test: AFSK modulator — duration, tone frequency by zero-crossing count,
// phase continuity (no sample-to-sample jump can exceed the max slope of the
// higher tone: 2*pi*2200/16000 * 12000 amplitude ~ 10367; assert < 11000).
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
    // 150 ms carrier = 2400 samples; tail = 4 bits ~ 53. Allow +/- 4.
    ASSERT_TRUE(n >= 2449 && n <= 2457);
    // 1200 Hz over the first 2400 samples -> 2 crossings/cycle * 1200 * 0.15 = 360.
    unsigned zc = zero_crossings(buf, 2400);
    ASSERT_TRUE(zc >= 358 && zc <= 362);
    free(buf);

    // One byte 0x00: start(0) + 8 zero bits + stop(1) -> 9 space bits + 1 mark bit.
    // Space section: 9 bits * 13.33 smp = 120 samples of 2200 Hz after the carrier.
    cap = afsk_mod_max_samples(1);
    buf = malloc(cap * sizeof(int16_t));
    uint8_t zero = 0x00;
    n = afsk_mod_render(&zero, 1, buf);
    ASSERT_TRUE(n <= cap);
    // 2200 Hz across samples 2400..2520 -> 2*2200*(120/16000) = 33 crossings.
    zc = zero_crossings(buf + 2400, 120);
    ASSERT_TRUE(zc >= 31 && zc <= 35);

    // Phase continuity across the whole render.
    int max_jump = 0;
    for (unsigned i = 1; i < n; i++) {
        int d = (int)buf[i] - (int)buf[i - 1];
        if (d < 0) d = -d;
        if (d > max_jump) max_jump = d;
    }
    ASSERT_TRUE(max_jump < 11000);
    free(buf);

    // Duration formula: 10 bits/byte at 1200 baud.
    cap = afsk_mod_max_samples(214);
    buf = malloc(cap * sizeof(int16_t));
    uint8_t big[214];
    for (int i = 0; i < 214; i++) big[i] = (uint8_t)i;
    n = afsk_mod_render(big, 214, buf);
    // 2400 carrier + 2140 bits * 13.333 + 4-bit tail = 2400 + 28533 + 53 = 30986 (+/-8).
    ASSERT_TRUE(n >= 30978 && n <= 30994);
    ASSERT_TRUE(n <= cap);
    free(buf);
    TEST_RETURN();
}
