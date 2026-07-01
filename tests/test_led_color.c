#include "test_util.h"
#include "leds/led_color.h"

int main(void) {
    rgb_t c = { 0x11, 0x22, 0x33 };                 // r=0x11 g=0x22 b=0x33
    ASSERT_EQ(led_color_pack_grb(c), 0x221133u);    // GRB order: g,r,b
    ASSERT_EQ(led_color_scale(255, 255), 255);
    ASSERT_EQ(led_color_scale(255, 0),   0);
    ASSERT_EQ(led_color_scale(255, 32),  32);       // 255*32/255 = 32
    ASSERT_EQ(led_color_scale(0, 128),   0);
    ASSERT_EQ(led_color_scale(200, 128), 100);      // (200*128+127)/255 = 100
    TEST_RETURN();
}
