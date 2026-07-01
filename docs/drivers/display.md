# Display driver (`bsp/display/`) — ST7796

**What it does:** drives the 480x320 ST7789-class panel over SPI1
(SCLK=GPIO10, MOSI=GPIO11, CS=GPIO9, DC=GPIO8). Offers blocking fill/blit/text
calls for bring-up and simple UIs, plus a non-blocking DMA flush
(`st7796_flush_async`) driven off a shared `DMA_IRQ_0` handler for anything
that needs an async pipeline (e.g. an LVGL port). There is no LCD reset GPIO
on this board — init relies on SWRESET only (see `docs/hardware/facts.md`).
`bsp/display/font5x7.{c,h}` supplies the built-in bitmap font used by
`st7796_draw_text`.

**How to use it:** call `st7796_init()` after `board_init()`, then draw:

```c
#include "fw2.h"

int main(void) {
    board_init();
    st7796_init();
    board_backlight_set(1);

    st7796_fill_screen(0x0000);                        // black, RGB565 big-endian
    st7796_draw_text(8, 8, 2, 0xFFFF, 0x0000,
                      "TOUCH THE SCREEN");              // white on black, scale 2
    st7796_fill_rect(x - 4, y - 4, 8, 8, 0x00F8);        // small red dot
}
```

**Key APIs** (`bsp/display/st7796.h`): `st7796_init()`,
`st7796_set_window(x0,y0,x1,y1)`, `st7796_fill_screen(color_be)`,
`st7796_fill_rect(x,y,w,h,color_be)`,
`st7796_blit_rect(x0,y0,x1,y1,pixels_be)` (blocking),
`st7796_flush_async(x0,y0,x1,y1,pixels,done_cb)` +
`st7796_flush_busy()` (non-blocking DMA path). Colors are RGB565, **already
byte-swapped to wire (big-endian) order** by the caller. `ST7796_W`/`ST7796_H`
= 480/320.

**Dependencies:** `board_init()` must run first (clocks + backlight GPIO).
Uses Pico SDK `hardware_spi` and `hardware_dma` (registers a shared handler
on `DMA_IRQ_0` — never register exclusively on that line; see
`docs/hardware/facts.md`).
