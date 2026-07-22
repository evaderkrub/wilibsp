#include "fw2.h"

int main(void) {
    board_init();
    st7796_init();
    board_backlight_set(1);
    st7796_fill_screen(0x0000);
    st7796_draw_text(8, 8, 2, 0xFFFF, 0x0000, "HELLO FREEWILI2");
    for (;;) { tight_loop_contents(); }
}
