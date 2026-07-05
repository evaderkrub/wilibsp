// tests/test_palette.c — host tests for the pure RSSI->RGB565 waterfall ramp.
// Stops: {0x0010, 0x001F, 0xFA00, 0xFC00, 0xFDE0} over [-100, -68] dBm, 8 dB/segment.
#include "gfx/palette.h"
#include "test_util.h"

int main(void) {
    // 1. Clamp below the floor and above the ceiling (both far and boundary).
    ASSERT_EQ(inferno_rgb565(-120), 0x0010);
    ASSERT_EQ(inferno_rgb565(-100), 0x0010);
    ASSERT_EQ(inferno_rgb565(-68), 0xFDE0);
    ASSERT_EQ(inferno_rgb565(0), 0xFDE0);

    // 2. Exact stop values at segment boundaries (t = 8*seg, f = 0).
    ASSERT_EQ(inferno_rgb565(-92), 0x001F);   // seg 1 start
    ASSERT_EQ(inferno_rgb565(-84), 0xFA00);   // seg 2 start
    ASSERT_EQ(inferno_rgb565(-76), 0xFC00);   // seg 3 start

    // 3. Mid-segment interpolation stays between its two stops per channel
    //    (seg 0: 0x0010 -> 0x001F, only blue changes: 16 -> 31, so at f=4
    //    blue = 16 + 15*4/8 = 23, red = green = 0).
    ASSERT_EQ(inferno_rgb565(-96), 0x0017);

    // 4. Red channel is monotonically non-decreasing across the whole ramp
    //    (blue->orange means red only ever rises).
    int prev_r = -1;
    for (int dbm = -100; dbm <= -68; dbm++) {
        int r = (inferno_rgb565(dbm) >> 11) & 0x1F;
        ASSERT_TRUE(r >= prev_r);
        prev_r = r;
    }

    TEST_RETURN();
}
