#include "test_util.h"
#include "audio/vu_meter.h"

int main(void) {
    // slot 0 = left = high 16 bits; slot 1 = right = low 16 bits.
    uint32_t f = 0x1234ABCDu;
    ASSERT_EQ((int16_t)vu_sample(f, 0), (int16_t)0x1234);
    ASSERT_EQ((int16_t)vu_sample(f, 1), (int16_t)0xABCD);

    // Right-slot values: 0x0064=+100, 0x0FA0=+4000, 0xF060=-4000, 0x0000=0 -> peak 4000.
    uint32_t frames[4] = { 0x00000064u, 0x00000FA0u, 0x0000F060u, 0x00000000u };
    ASSERT_EQ(vu_peak(frames, 4, 1), 4000);

    // Silence -> 0.
    uint32_t z[2] = { 0, 0 };
    ASSERT_EQ(vu_peak(z, 2, 1), 0);

    // Bar length: 0 -> 0, full-scale -> max_px.
    ASSERT_EQ(vu_bar_px(0, 100), 0);
    ASSERT_EQ(vu_bar_px(32767, 100), 100);

    // Color thresholds (big-endian RGB565): green < 8192 <= yellow < 24576 <= red.
    ASSERT_EQ(vu_color_be(100),   (uint16_t)((0x07E0u >> 8) | (0x07E0u << 8)));
    ASSERT_EQ(vu_color_be(10000), (uint16_t)((0xFFE0u >> 8) | (0xFFE0u << 8)));
    ASSERT_EQ(vu_color_be(30000), (uint16_t)((0xF800u >> 8) | (0xF800u << 8)));

    TEST_RETURN();
}
