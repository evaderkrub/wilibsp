# LED driver (`bsp/leds/`) — WS2812 x16

**What it does:** drives the board's **16** WS2812 RGB LEDs (single data
line, `PIN_LED_DATA` = GPIO 21) via a `pio1` state machine
(`bsp/leds/ws2812.pio`, header-generated into the build tree by
`pico_generate_pio_header` in `bsp/CMakeLists.txt`). Output is inverted at
the driver-chip level, handled inside `ws2812_driver.c`.
`bsp/leds/led_color.{c,h}` provides pure `rgb_t` pack/scale helpers.
**16 is the verified count** — see the discrepancy record in
`docs/hardware/facts.md` (`FwDisplayVibe.md` says 7 and is wrong).

`bsp/leds/led_ui.{c,h}` layers a higher-level "signal indicator" API
(breathing fade, spectrum-to-LED mapping) on top, but its
`led_spectrum_map()` depends on `bsp/gfx/palette.h`'s `inferno_rgb565()`,
whose `.c` implementation has **not** been harvested into this repo yet
(only the header exists). Don't call `led_ui_spectrum()` /
`led_spectrum_map()` until `bsp/gfx/palette.c` is added and wired into
`bsp/CMakeLists.txt` — the current v1 apps avoid it entirely and use the
lower-level `ws2812_*` API directly.

**How to use it (low-level `ws2812_*` API, as used by `hello_display`):**

```c
#include "fw2.h"
#include "hardware/pio.h"

int main(void) {
    board_init();
    ...
    ws2812_init(pio1, 0, PIN_LED_DATA);
    ws2812_set_brightness(64);
    rgb_t green = { .r = 0, .g = 255, .b = 0 };
    ws2812_fill(green);
    ws2812_show();
}
```

**Key APIs** (`bsp/leds/ws2812_driver.h`): `ws2812_init(PIO pio, uint sm,
uint gpio)`, `ws2812_set_pixel(uint i, rgb_t c)`, `ws2812_fill(rgb_t c)`,
`ws2812_clear(void)`, `ws2812_set_brightness(uint8_t level)` /
`ws2812_get_brightness(void)`, `ws2812_show(void)`. `WS2812_NUM_PIXELS` /
`FW2_LED_COUNT` = 16.

**Dependencies:** Pico SDK `hardware_pio`; no I2C/board dependency beyond
`board_init()` having run (for clocks). `pio1` is dedicated to the LEDs in
this BSP — don't reuse it for another PIO program without checking state
machine availability.
