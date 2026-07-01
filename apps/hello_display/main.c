// hello_display — v1 on-hardware smoke test: display renders, touch responds,
// LEDs light. Diagnostics over SEGGER RTT (fw rtt).
#include "fw2.h"
#include "hardware/pio.h"
#include "platform/diag.h"

int main(void) {
    board_init();
    st7796_init();
    board_backlight_set(1);

    ws2812_init(pio1, 0, PIN_LED_DATA);
    ws2812_set_brightness(64);
    rgb_t green = { .r = 0, .g = 255, .b = 0 };
    ws2812_fill(green);
    ws2812_show();

    ft6336_init();

    st7796_fill_screen(0x0000);
    st7796_draw_text(8, 8, 2, 0xFFFF, 0x0000, "TOUCH THE SCREEN");
    DIAG("hello_display up: sys=%u kHz\n", BOARD_SYS_CLOCK_KHZ);

    uint16_t x, y;
    for (;;) {
        if (ft6336_poll(&x, &y)) {
            st7796_fill_rect(x - 4, y - 4, 8, 8, 0xE0FF /* red-ish BE */);
            DIAG("touch %u,%u\n", x, y);
        }
    }
}
