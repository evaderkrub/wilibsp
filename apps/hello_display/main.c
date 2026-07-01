// hello_display — v1 on-hardware smoke test: display renders, touch responds,
// LEDs light. Diagnostics over SEGGER RTT (fw rtt).
#include "fw2.h"
#include "hardware/pio.h"
#include "platform/diag.h"
#include "pico/stdlib.h"   // absolute_time_t / get_absolute_time / make_timeout_time_ms

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

    // WS2812 note: the FIRST data frame after the PIO SM starts does not reliably
    // latch all pixels (known quirk on this board — only pixel 0 lights). Real apps
    // refresh the strip continuously; this smoke app re-shows the green framebuffer
    // periodically so all 16 LEDs stay lit. See docs/drivers/leds.md.
    uint16_t x, y;
    absolute_time_t next_led = get_absolute_time();
    for (;;) {
        if (ft6336_poll(&x, &y)) {
            st7796_fill_rect(x - 4, y - 4, 8, 8, 0x00F8 /* red: RGB565 0xF800 byte-swapped to wire order */);
            DIAG("touch %u,%u\n", x, y);
        }
        if (absolute_time_diff_us(get_absolute_time(), next_led) <= 0) {
            ws2812_show();                        // re-latch green (framebuffer persists)
            next_led = make_timeout_time_ms(250);
        }
    }
}
