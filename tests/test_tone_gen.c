#include "test_util.h"
#include "audio/tone_gen.h"

int main(void) {
    // 65 samples of a 1 kHz sine at 16 kHz (64 = 4 whole periods, +1 to check wrap).
    int16_t buf[65];
    float ph = 0.0f;
    tone_gen_fill(buf, 65, 1000.0f, 16000.0f, &ph);

    // Peak amplitude is ~28000 scale, minus the half-step sample-center offset.
    int pk = 0;
    for (int i = 0; i < 64; i++) { int a = buf[i] < 0 ? -buf[i] : buf[i]; if (a > pk) pk = a; }
    ASSERT_TRUE(pk > 27000 && pk <= 28000);

    // 4 periods over 64 samples => 8 zero crossings.
    int zc = 0;
    for (int i = 1; i < 64; i++) if ((buf[i-1] < 0) != (buf[i] < 0)) zc++;
    ASSERT_EQ(zc, 8);

    // Phase-continuous wrap: sample 64 ~= sample 0 within int16 rounding.
    int d = buf[64] - buf[0]; if (d < 0) d = -d;
    ASSERT_TRUE(d <= 2);

    // Near-zero DC over a whole number of periods.
    long sum = 0;
    for (int i = 0; i < 64; i++) sum += buf[i];
    ASSERT_TRUE(sum > -2000 && sum < 2000);

    TEST_RETURN();
}
