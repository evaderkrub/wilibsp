// tests/test_dcblock.c — host tests for the one-pole DC-blocking high-pass.
#include "dsp/dcblock.h"
#include "test_util.h"
#include <stdlib.h>

#define N 512u

int main(void) {
    // 1. Pure DC offset decays to ~0 by the tail of the block.
    int16_t buf[N];
    for (unsigned i = 0; i < N; i++) buf[i] = 1000;
    dcblock_inplace(buf, N);
    ASSERT_EQ(buf[0], 0);                    // first output defined as 0
    ASSERT_TRUE(abs(buf[N - 1]) <= 2);       // DC fully blocked

    // 2. Alternating +/-1000 (high-frequency AC) passes with the analytic
    //    steady-state gain 2/(1+R) = 2/1.9 ~ x1.05: tail amplitude ~1052.
    for (unsigned i = 0; i < N; i++) buf[i] = (i & 1) ? -1000 : 1000;
    dcblock_inplace(buf, N);
    ASSERT_TRUE(abs(buf[N - 1]) >= 900);
    ASSERT_TRUE(abs(buf[N - 1]) <= 1200);
    ASSERT_TRUE((buf[N - 1] > 0) != (buf[N - 2] > 0));   // still alternating

    // 3. AC riding on DC: offset removed, AC survives.
    for (unsigned i = 0; i < N; i++) buf[i] = (int16_t)(5000 + ((i & 1) ? -1000 : 1000));
    dcblock_inplace(buf, N);
    ASSERT_TRUE(abs(buf[N - 1]) >= 900);
    ASSERT_TRUE(abs(buf[N - 1]) <= 1200);

    // 4. n=0 is a no-op (must not crash).
    dcblock_inplace(buf, 0);

    TEST_RETURN();
}
