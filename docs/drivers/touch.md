# Touch driver (`bsp/input/`) — FT6336U

**What it does:** polls the FT6336U capacitive touch controller on I2C1
(SDA=GPIO26, SCL=GPIO27, same bus as the platform's sensors). There is no
INT pin wired on this board, so the driver is poll-only; its hardware reset
is shared with the display's (both released by the I/O expander during
`board_init()` → `ioexp_init()`, not toggled by this driver). Coordinates
come back already oriented to the 480x320 landscape panel — no extra
rotation/mapping needed by the caller. `bsp/input/ft6336_map.{c,h}` holds the
pure coordinate-mapping logic split out for host testing.

**How to use it:** call `ft6336_init()` once (after `board_i2c1_init()`,
which `board_init()` already calls), then poll each loop iteration:

```c
#include "fw2.h"
#include "platform/diag.h"

int main(void) {
    board_init();
    ...
    ft6336_init();

    uint16_t x, y;
    for (;;) {
        if (ft6336_poll(&x, &y)) {
            st7796_fill_rect(x - 4, y - 4, 8, 8, 0x00F8);
            DIAG("touch %u,%u\n", x, y);
        }
    }
}
```

**Key APIs** (`bsp/input/ft6336.h`): `ft6336_init()` — reads the chip ID to
confirm presence and sets `DEVICE_MODE=normal`, returns `true` on ACK.
`ft6336_poll(uint16_t *x, uint16_t *y)` — returns `true` on exactly one
finger down (writes screen coords, `x: 0..479`, `y: 0..319`); returns
`false` for no touch or multi-touch (2+ fingers).

**Dependencies:** I2C1 must already be up (`board_i2c1_init()`, called from
`board_init()`). No display dependency, though `hello_display` draws touch
feedback via `st7796_fill_rect`.
