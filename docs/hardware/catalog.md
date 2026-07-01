# Peripheral driver catalog

Status of every peripheral known on the FreeWili2 board (per
`bsp/platform/board.h` and `FwDisplayVibe.md`), and — for `TODO` rows — which
owner repo to harvest the driver from. See `AGENTS.md` § "How to add a
driver" and `skills/freewili2-add-driver/SKILL.md` for the mechanical
procedure once you've picked a row.

## DONE

| Peripheral | Driver location | Notes |
|---|---|---|
| Platform core (clocks/vreg, I/O expander, PSRAM, SPI bus arbitration, RTT diag) | `bsp/platform/{board,ioexp,psram,spi_bus}.{c,h}`, `bsp/platform/diag.h` | Harvested from `subghz`/`usbcamfw`. Board bring-up (`board_init()`), PCAL6524 I/O expander, APS6404L PSRAM, shared-SPI1 arbitration primitives, SEGGER RTT diagnostics. |
| Display — ST7796 (480x320 panel, ST7789-class controller) | `bsp/display/{st7796,font5x7}.{c,h}` | SPI1, blocking + async DMA flush, 5x7 bitmap font. |
| Touch — FT6336U capacitive touch | `bsp/input/{ft6336,ft6336_map}.{c,h}` | Polled over I2C1, no INT pin wired, coordinates pre-oriented to the 480x320 panel. |
| LEDs — WS2812 x16 | `bsp/leds/{led_color,ws2812_driver}.{c,h}`, `bsp/leds/ws2812.pio` | `pio1`, GPIO 21, inverted output. `FW2_LED_COUNT` = 16 (see facts.md discrepancy record). `led_ui.{c,h}` is present but its `led_spectrum_map` dependency (`gfx/palette.c`) is not yet harvested — see below. |

These four are exactly what `bsp/CMakeLists.txt` compiles into
`freewili2_bsp` today (plus `third_party/segger_rtt`) and exactly what
`bsp/fw2.h` includes.

## TODO (future add-driver increments)

| Peripheral | GPIOs / bus (source: `FwDisplayVibe.md` unless noted) | Harvest from |
|---|---|---|
| Sub-GHz radio — CC1101 | Shares SPI1 with the LCD; CS=GPIO40, MISO=GPIO8 (shared with `PIN_LCD_DC`), GDO0=GPIO32, GDO2=GPIO37 (all already `#define`d in `bsp/platform/board.h`) | `subghz` (`src/radio/cc1101*.c/.h`, `src/platform/spi_bus.c` arbitration is already ported here) |
| NFC — ST25R3916B | I2C1 (SDA=26/SCL=27) | `subghz`/`sensorview` (check which owns an NFC driver; not confirmed at catalog time) |
| IR TX/RX | TX=GPIO20, RX=GPIO24 | `sensorview` or a new harvest — owner repo not yet confirmed |
| DVI / HSTX | DVI_CLK_N/P=12/13, DVI_D0_N/P=14/15, DVI_D1_N/P=16/17, DVI_D2_N/P=18/19 | Owner repo not yet confirmed (HSTX peripheral, likely a fresh Pico SDK HSTX example port) |
| I2S audio codec — NAU88C10YG | SPK_DOUT=4, SPK_DIN=5, SPK_LRCK=6, SPK_BCLK=7, MCLK=22; I2C addr 0x1A (26 decimal, per `FwDisplayVibe.md`'s "I2C address is 26") | `usbcamfw` / `wili8c` |
| PDM microphones (4-mic linear array, 19 mm spacing) | MIC_CLK=28 (shared), MIC_SIG_1=29 (pair 1 L/R), MIC_SIG_2=30 (pair 2 L/R); MIC_PWR via the I/O expander | `usbcamfw` / `wili8c` |
| 14-button serial coprocessor | TX=GPIO38, RX=GPIO39 (UART) | Owner repo not yet confirmed — buttons: Up, Down, Left, Right, Center, Home, OK, Cancel, Page, Grey, Yellow, Green, Blue, Red |
| Pico-PIO-USB (USB host via PIO) | D+=GPIO42, D-=GPIO43; 1.5K D+ pullup enabled via the I/O expander | `usbcamfw` / `wili8c` |
| Ambient light — OPT4001 | I2C, addr 0x45 (ADDR strapped high) | `sensorview` |
| Humidity — SHT40-AD1B-R3 | I2C, addr 0x44 | `sensorview` |
| IMU — BMI323 | I2C | `sensorview` |
| Magnetometer — BMM350 | I2C | `sensorview` |

## Partial / in-repo but not wired up

| Item | Status |
|---|---|
| `bsp/gfx/palette.h` (`inferno_rgb565`) | Header present, `.c` **not** harvested. Needed only if/when `bsp/leds/led_ui.c`'s `led_spectrum_map()` gets used by an app. Harvest `subghz/src/gfx/palette.c` into `bsp/gfx/palette.c` and add it to `bsp/CMakeLists.txt` first. See `docs/hardware/facts.md`. |

## Confirming this catalog

Exactly four peripherals are marked `DONE` above: **platform, display
(ST7796), touch (FT6336), LEDs (WS2812 x16)**. This matches the source list
compiled by `bsp/CMakeLists.txt` (`platform/*.c`, `display/*.c`,
`input/*.c`, `leds/*.c`, `third_party/segger_rtt/*.c`) and the includes
activated in `bsp/fw2.h`. If you add a new `DONE` row, the corresponding
source files must already be in `bsp/CMakeLists.txt`'s `add_library(...)`
list and the header must be `#include`d from `bsp/fw2.h` — otherwise it
isn't actually done yet.
