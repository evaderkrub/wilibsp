# FreeWili2 pin map

**Authoritative source: `bsp/platform/board.h`.** The table below transcribes
its `#define`s verbatim (line-referenced). Where a peripheral isn't in
`board.h` yet (no driver in this repo), its GPIOs come from the secondary
source `FwDisplayVibe.md` (repo root) and are marked accordingly — verify
against `board.h` again once that peripheral gets a driver, since
`FwDisplayVibe.md` is known to contain at least one error (see
`docs/hardware/facts.md`).

## Verified (`bsp/platform/board.h`) — has a driver in `bsp/`

| Signal | GPIO | Peripheral | Notes |
|---|---|---|---|
| `PIN_LCD_DC` | 8 | SPI1 (LCD data/command) | Also `PIN_CC1101_MISO` — dual function, see facts.md |
| `PIN_LCD_CS` | 9 | SPI1 (LCD chip-select) | Active low |
| `PIN_LCD_SCLK` | 10 | SPI1 (LCD SCK) | |
| `PIN_LCD_MOSI` | 11 | SPI1 (LCD TX) | |
| `PIN_LCD_TE` | 33 | LCD tearing-effect output | Unused by the driver |
| `PIN_LCD_BL` | 25 | LCD backlight | Plain on/off GPIO (no PWM dimming yet) |
| `PIN_CC1101_CS` | 40 | CC1101 chip-select | Active low; parked HIGH in `board_init()` before any LCD traffic. **`FwDisplayVibe.md` says GPIO 23 for this signal — that's a second discrepancy; `board.h`/`board.c` (which actively drives GPIO 40 HIGH at boot) is authoritative. See facts.md.** |
| `PIN_CC1101_MISO` | 8 | CC1101 MISO | == `PIN_LCD_DC`; an OUTPUT (DC) for the LCD, must mux to SPI1 RX (input) around CC1101 access |
| `PIN_CC1101_GDO0` | 32 | CC1101 live data / sync | PIO2-sampled edge capture (`gdo_capture`) |
| `PIN_CC1101_GDO2` | 37 | CC1101 GDO2 | Unused |
| `PIN_LED_DATA` | 21 | WS2812 data (16 pixels) | Driven by a `pio1` state machine |
| `PIN_I2C1_SDA` | 26 | I2C1 SDA | Touch (FT6336) + sensors, 400 kHz |
| `PIN_I2C1_SCL` | 27 | I2C1 SCL | Touch (FT6336) + sensors, 400 kHz |
| `PIN_PSRAM_CS` | 47 | APS6404L PSRAM chip-select | GPIO47 = XIP_CS1n (function 9), NOT on SPI1 |
| `PIN_AUDIO_DIN`  | 4  | I2S ADC data (SPK_DOUT, codec -> MCU) | PIO0 in; mic capture |
| `PIN_AUDIO_DATA` | 5  | I2S DAC data (SPK_DIN, MCU -> codec)  | PIO0 out; playback |
| `PIN_AUDIO_LRCK` | 6  | I2S word clock (SPK_LRCK)             | PIO sideset bit 0 |
| `PIN_AUDIO_BCLK` | 7  | I2S bit clock (SPK_BCLK)              | PIO sideset bit 1 |
| `PIN_AUDIO_MCLK` | 22 | Codec master clock (SPK_MCLK)         | 256*fs square wave (PWM) |
| `PIN_MIC_CLK` | 28 | PDM mic array (shared clock out) | `pio1` side-set, 1.024 MHz (NOT 3.072 — see facts.md) |
| `PIN_MIC_SIG1` | 29 | PDM data line 1 | Mic A (clk-high) + Mic B (clk-low); consecutive with SIG2 for `in pins, 2` |
| `PIN_MIC_SIG2` | 30 | PDM data line 2 | Mic C (clk-high) + Mic D (clk-low) |

SPI1 baud rates (shared bus, from `board.h`): LCD 100 MHz
(`LCD_SPI_BAUD`, divider-limited by `clk_peri`), CC1101 5 MHz
(`CC1101_SPI_BAUD`, CC1101 SPI ceiling ~6.5 MHz).

Touch (FT6336U) and the on-board LEDs (WS2812x16) sit on the buses above —
FT6336U on I2C1 (no dedicated GPIO of its own beyond SDA/SCL), WS2812 on
`PIN_LED_DATA`.

## Broader board inventory (`FwDisplayVibe.md`) — not yet in `board.h`

These peripherals exist on the board but have no driver in this repo yet
(see `docs/hardware/catalog.md` for harvest plans). GPIOs are as documented
in `FwDisplayVibe.md`; **cross-check against `bsp/platform/board.h` again
once each one gets a driver** — do not assume `FwDisplayVibe.md` is exact
(it undercounts the WS2812 LEDs; see facts.md).

| Signal | GPIO | Peripheral | Driver status |
|---|---|---|---|
| IR TX | 20 | IR transmitter | TODO |
| IR RX | 24 | IR receiver | TODO |
| DVI_CLK_N | 12 | HSTX DVI | TODO |
| DVI_CLK_P | 13 | HSTX DVI | TODO |
| DVI_D0_N | 14 | HSTX DVI | TODO |
| DVI_D0_P | 15 | HSTX DVI | TODO |
| DVI_D1_N | 16 | HSTX DVI | TODO |
| DVI_D1_P | 17 | HSTX DVI | TODO |
| DVI_D2_N | 18 | HSTX DVI | TODO |
| DVI_D2_P | 19 | HSTX DVI | TODO |
| Haptic motor | 46 | Haptic driver | TODO |
| Buttons TX | 38 | 14-button serial coprocessor (UART out) | TODO |
| Buttons RX | 39 | 14-button serial coprocessor (UART in) | TODO |
| PIO-USB D+ | 42 | Pico-PIO-USB host port | TODO |
| PIO-USB D- | 43 | Pico-PIO-USB host port | TODO |

Peripherals on I2C1 (SDA=26 / SCL=27, same bus as touch) with no dedicated
GPIOs of their own, per `FwDisplayVibe.md`:

| Peripheral | I2C address | Driver status |
|---|---|---|
| ST25R3916B NFC | (I2C, address not in `FwDisplayVibe.md`) | TODO |
| OPT4001 ambient light | 0x45 (ADDR strapped high) | DONE (`bsp/sensors/opt4001.c`) |
| SHT40-AD1B-R3 humidity | 0x44 | DONE (`bsp/sensors/sht40.c`) |
| BMI323 IMU | 0x68 | DONE (`bsp/sensors/bmi323.c`) |
| BMM350 magnetometer | 0x14 | DONE (`bsp/sensors/bmm350.c`) |

Note: `FwDisplayVibe.md` also lists a USB host hub (CH334F) on the default
USB port, exposing ports HP1/HP2 with power switched by the IO expander —
this is board-level context, not a GPIO the BSP drives directly today.

## Cross-reference

- `bsp/platform/ioexp.h` documents the PCAL6524 I/O expander (I2C1, addr
  0x23) that selects the CC1101/sub-GHz antenna path (`ANT_LORA`,
  `ANT_CC1101_433`, `ANT_CC1101_315_415`, `ANT_CC1101_915`) and releases the
  LCD's hardware reset — relevant context for the radio and display rows
  above.
- `bsp/platform/psram.h` documents `PSRAM_BASE 0x11000000` (the memory-mapped
  window, not a GPIO).
