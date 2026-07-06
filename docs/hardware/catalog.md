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
| LEDs — WS2812 x16 | `bsp/leds/{led_color,ws2812_driver}.{c,h}`, `bsp/leds/ws2812.pio` | `pio1`, GPIO 21, inverted output. `FW2_LED_COUNT` = 16 (see facts.md discrepancy record). `led_ui.{c,h}` and its `led_spectrum_map` dependency (`bsp/gfx/palette.c`, host-tested) are harvested and wired. |
| Audio — I2S full-duplex (NAU88C10 codec) | `bsp/audio/{audio_i2s_duplex,codec_nau88c10,audio_capture,tone_gen,vu_meter}.{c,h}`, `bsp/audio/i2s_duplex.pio` | PIO0 SM0 clocks the codec (slave, MCLK-direct); TX zero-CPU ring DMA, RX ping-pong DMA on SHARED DMA_IRQ_0. Playback (speaker/headphone) + mic capture (PCM blocks). Harvested from evaderkrub/freewili2-fullduplex-audio (MIT). Demo: `apps/hello_audio`. |
| Sub-GHz radio — CC1101 | `bsp/radio/{cc1101,cc1101_regs,gdo_capture,monitor_engine,ook_tx,scan_engine,capture_store}.{c,h}`, `bsp/radio/gdo_capture.pio` | SPI1 (shared with LCD via `spi_bus` arbiter, 5 MHz); GDO0 capture on **pio2** + ENDLESS DMA (polled, no IRQ); OOK TX bit-bangs GDO0. Harvested from `subghz` (MIT). Demo: `apps/hello_cc1101`. |
| PDM microphones — 4-mic array | `bsp/pdm/pdm_capture.{c,h}`, `bsp/pdm/pdm_capture.pio`, `bsp/dsp/{cic,dcblock}.{c,h}` | `pio1` (shared with LEDs), MIC_CLK=28 / SIG1=29 / SIG2=30, 1.024 MHz PDM → 16 kHz int16 PCM ×4 via integer CIC; free-running ring DMA, **no IRQ**. Mic power via `ioexp_mic_pwr()` (P1_7), driven by `pdm_capture_init()`. Harvested from local `microphonearray` (supersedes the earlier `usbcamfw`/`wili8c` pointer). Demo: `apps/hello_mics`. |
| Ambient light — OPT4001 | `bsp/sensors/opt4001.{c,h}` | I2C1 addr 0x45 (ADDR high), continuous 100 ms conversions; lux = (mantissa<<exp) × 437.5e-6 (package-dependent — calibrate against a known light level). Harvested verbatim from `sensorview`. Demo: `apps/hello_sensors`. |
| Humidity/temp — SHT40-AD1B | `bsp/sensors/sht40.{c,h}` | I2C1 addr 0x44, CRC-8-checked high-precision measure (~10 ms blocking). Harvested verbatim from `sensorview`. Demo: `apps/hello_sensors`. |
| IMU — BMI323 | `bsp/sensors/bmi323.{c,h}` | I2C1 addr 0x68, chip-id 0x43, ±4 g / ±500 dps @ 100 Hz; 16-bit LE regs, reads carry 2 leading dummy bytes. Harvested verbatim from `sensorview`. Demo: `apps/hello_sensors`. |
| Magnetometer — BMM350 | `bsp/sensors/{bmm350,bmm350_comp}.{c,h}` | I2C1 addr 0x14, chip-id 0x33; full OTP download + Bosch compensation → µT (+ sqrtf magnitude); reads carry 2 leading dummy bytes. Init ≈ 130 ms of settles. Harvested verbatim from `sensorview`. Demo: `apps/hello_sensors`. |
| IR TX/RX | `bsp/ir/{ir_capture,ir_tx,ir_decode,ir_encode,ir_protocols,ir_frame,ir_tx_pack,ir_file,db_sort,ir_resolve}.{c,h}`, `bsp/ir/{ir_capture,ir_tx}.pio` | TX=GPIO20 (`PIN_IR_TX`, pio2 SM1 + 1 DMA), RX=GPIO24 (`PIN_IR_RX`, pio2 SM0 + 1 DMA), both polled/no-IRQ. Gated by `ioexp_ir_pwr()` (PCAL6524 P2_0, off at power-on). Harvested from `WiliIR` (which also vendored `usbmsc` for USB and proved the whole stack on hardware 2026-07-05/06). Demo: `apps/hello_ir`. (pio2 shared with radio GDO capture; radio-first init order) |
| USB host MSC (thumb drive) | `bsp/usbhost/{usb_core,usb_hcd,usb_hub,usb_msc,usb_parse,msc_disk,usb_store}.{c,h}` + `bsp/third_party/fatfs/*` | Native RP2350 USB controller in host mode (mutually exclusive with TinyUSB device mode), CH334F hub, single hub tier only, polled/no-IRQ. Gated by `ioexp_usb_pwr()` (PCAL6524 P0_0 = HP1 + P1_4 = HP2, off at power-on). Harvested from `WiliIR`, origin the owner's `usbmsc` driver (vendored verbatim into WiliIR, then carried here unmodified). Demo: `apps/hello_usbdrive`. |

These thirteen are exactly what `bsp/CMakeLists.txt` compiles into
`freewili2_bsp` today (plus `third_party/segger_rtt` and `third_party/fatfs`)
and exactly what `bsp/fw2.h` includes. (`platform/*.c`, `display/*.c`,
`input/*.c`, `leds/*.c`, `audio/*.c`, `radio/*.c`, `pdm/*.c`, `dsp/*.c`,
`sensors/*.c`, `ir/*.c`, `usbhost/*.c`, plus `i2s_duplex.pio.h`,
`gdo_capture.pio.h`, `ir_capture.pio.h`, and `ir_tx.pio.h` generation)

## TODO (future add-driver increments)

| Peripheral | GPIOs / bus (source: `FwDisplayVibe.md` unless noted) | Harvest from |
|---|---|---|
| NFC — ST25R3916B | I2C1 (SDA=26/SCL=27) | `subghz`/`sensorview` (check which owns an NFC driver; not confirmed at catalog time) |
| DVI / HSTX | DVI_CLK_N/P=12/13, DVI_D0_N/P=14/15, DVI_D1_N/P=16/17, DVI_D2_N/P=18/19 | Owner repo not yet confirmed (HSTX peripheral, likely a fresh Pico SDK HSTX example port) |
| 14-button serial coprocessor | TX=GPIO38, RX=GPIO39 (UART) | Owner repo not yet confirmed — buttons: Up, Down, Left, Right, Center, Home, OK, Cancel, Page, Grey, Yellow, Green, Blue, Red |
| Pico-PIO-USB (USB host via PIO) | D+=GPIO42, D-=GPIO43; 1.5K D+ pullup enabled via the I/O expander | `usbcamfw` / `wili8c` |

## Partial / in-repo but not wired up

(none — the `gfx/palette.c` carry is complete: harvested verbatim from
`subghz/src/gfx/palette.c`, wired into `bsp/CMakeLists.txt`, host-tested in
`tests/test_palette.c`. See `docs/hardware/facts.md` "gfx/palette carry".)

## Confirming this catalog

Exactly thirteen peripherals are marked `DONE` above: **platform, display
(ST7796), touch (FT6336), LEDs (WS2812 x16), audio (I2S full-duplex), radio
(CC1101), PDM microphones, ambient light (OPT4001), humidity/temp (SHT40),
IMU (BMI323), magnetometer (BMM350), IR TX/RX, USB host MSC**. This matches
the source list compiled by `bsp/CMakeLists.txt` (`platform/*.c`,
`display/*.c`, `input/*.c`, `leds/*.c`, `audio/*.c`, `radio/*.c`, `pdm/*.c`,
`dsp/*.c`, `sensors/*.c`, `ir/*.c`, `usbhost/*.c`, `third_party/segger_rtt/*.c`,
`third_party/fatfs/*.c`) and the includes activated in `bsp/fw2.h`. If
you add a new `DONE` row, the corresponding source files must already be in
`bsp/CMakeLists.txt`'s
`add_library(...)` list and the header must be `#include`d from `bsp/fw2.h`
— otherwise it isn't actually done yet.
