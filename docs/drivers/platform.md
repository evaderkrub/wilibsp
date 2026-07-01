# Platform driver (`bsp/platform/`)

**What it does:** brings the board to a known-good state before any other
driver runs — vreg + clocks (250 MHz, `clk_peri` re-sourced), the CC1101 CS
parked HIGH, the backlight GPIO initialized off, I2C1 up, and the PCAL6524
I/O expander configured (LCD reset release, antenna select, SPI1 buffer
directions). Also provides PSRAM bring-up and the shared-SPI1 arbitration
primitives. Diagnostics go out over SEGGER RTT (`DIAG()`).

**How to use it:** call `board_init()` once at the top of `main()`, before
any display/touch/LED calls. It's the very first thing every app does:

```c
#include "fw2.h"
#include "platform/diag.h"

int main(void) {
    board_init();               // vreg, 250 MHz clocks + clk_peri re-source,
                                 // CC1101 CS parked, backlight off, I2C1 up,
                                 // I/O expander (LCD reset release, antenna)
    st7796_init();
    board_backlight_set(1);      // turn the backlight on once the panel is ready
    ...
    DIAG("hello_display up: sys=%u kHz\n", BOARD_SYS_CLOCK_KHZ);
}
```

**Key APIs** (`bsp/platform/board.h`): `board_init()`,
`board_backlight_set(uint8_t level)`, `board_i2c1_init()` (called by
`board_init()`, exposed in case a driver needs to re-init I2C1 standalone).
`bsp/platform/diag.h`: `DIAG(...)` → RTT channel 0 (`fw rtt` to view; no
floats). `bsp/platform/psram.h`: `psram_init()` / `psram_selftest()` for the
8 MB APS6404L. `bsp/platform/ioexp.h`: `ioexp_init()` / `ioexp_antenna()`
(PCAL6524, I2C1 addr 0x23). `bsp/platform/spi_bus.h`:
`spi_bus_acquire_cc1101()` / `_release()` / `_cs()` — arbitration for the
shared SPI1 bus, ready for when the CC1101 driver is harvested.

**Dependencies:** none within the BSP (this is the foundation layer). Pulls
in Pico SDK `hardware_clocks`, `hardware_vreg`, `hardware_i2c`,
`hardware_gpio` and the vendored SEGGER RTT (`bsp/third_party/segger_rtt/`).
