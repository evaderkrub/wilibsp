// tests/test_cic.c — host tests for the 3rd-order CIC decimator (pure integer).
#include "dsp/cic.h"
#include "test_util.h"
#include <stdlib.h>

int main(void) {
    // 1. Exactly one output per CIC_DECIMATE input bits.
    cic_t c;
    cic_init(&c);
    int16_t pcm = 0;
    int outputs = 0;
    for (int i = 0; i < 64 * 10; i++)
        if (cic_push_bit(&c, 1, &pcm)) outputs++;
    ASSERT_EQ(outputs, 10);

    // 2. Constant-1 input plateaus at +16384 (2^18 >> 4, the documented
    //    headroom scaling) once the comb pipeline fills (~3 outputs).
    ASSERT_EQ(pcm, 16384);

    // 3. Constant-0 input mirrors to -16384.
    cic_init(&c);
    for (int i = 0; i < 64 * 10; i++) cic_push_bit(&c, 0, &pcm);
    ASSERT_EQ(pcm, -16384);

    // 4. Alternating 1/0 (Nyquist-rate PDM) decimates to ~0 (deep stopband).
    cic_init(&c);
    for (int i = 0; i < 64 * 10; i++) cic_push_bit(&c, i & 1, &pcm);
    ASSERT_TRUE(abs(pcm) < 100);

    TEST_RETURN();
}
