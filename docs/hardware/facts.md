# Hardware facts — hard-won invariants

These cost real debugging time in the repos this BSP was harvested from
(primarily `subghz`). Treat them as ground truth; don't relearn them. They're
also summarized in `AGENTS.md`; this file is the fuller record.

## RP2350B, not RP2350A

`bsp/boards/freewili2.h` sets `PICO_RP2350A 0`, giving 48 GPIO (not the 30 of
an "A" variant). The board is selected in the **top-level `CMakeLists.txt`**
via `set(PICO_BOARD freewili2 CACHE STRING "Board type")` plus
`list(APPEND PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/bsp/boards")`.
**Never** pass `-DPICO_BOARD=...` on the cmake command line — a cache
variable set that way overrides the `CMakeLists.txt` value and reverts the
build to the wrong board config (wrong GPIO count, wrong flash size).

## Clock / RAM invariant

`board_init()` in `bsp/platform/board.c`:

1. `vreg_set_voltage(VREG_VOLTAGE_1_25)`
2. `sleep_ms(10)`
3. `set_sys_clock_khz(BOARD_SYS_CLOCK_KHZ /* 250000 */, true)`
4. Re-source `clk_peri` from `clk_sys`:
   `clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, f, f)`

Step 4 is not optional: without re-sourcing `clk_peri` after the sys-clock
change, the hardware SPI peripheral has no valid clock and the LCD shows
nothing. The 1.25 V vreg bump exists because 250 MHz at the default vreg was
marginal during heavy ST7796 bring-up in the source repo.

Every app is built `pico_set_binary_type(<app> copy_to_ram)` (see
`apps/template/CMakeLists.txt`, `apps/hello_display/CMakeLists.txt`): the
whole binary (code + data + bss) runs from the RP2350's 512 KB SRAM, not
flash XIP. This is what makes running at 250 MHz safe without flash-timing
concerns, but it means SRAM is the tight budget — large buffers (framebuffers,
capture clips, waterfalls) must go in PSRAM
(`PSRAM_BASE 0x11000000`, APS6404L, 8 MB, brought up by `psram_init()` in
`bsp/platform/psram.c`), not on the stack or in a static SRAM array.

## Diagnostics = SEGGER RTT only

`bsp/platform/diag.h`: `#define DIAG(...) SEGGER_RTT_printf(0, __VA_ARGS__)`.
There is **no UART or USB stdio** anywhere in this BSP — don't add `printf`.
`SEGGER_RTT_printf` supports `%d %u %x %s %c` and field widths, but **no
floating point** conversions. View RTT output live with `fw rtt`.

## DMA_IRQ_0 is shared

The ST7796 async flush (`bsp/display/st7796.c`) registers its DMA completion
handler with `irq_add_shared_handler(DMA_IRQ_0, ...)` and checks only its own
DMA channel's status flag before acting. Any future user of `DMA_IRQ_0` (a
radio capture, a second display path, etc.) must do the same — registering
with `irq_set_exclusive_handler(DMA_IRQ_0, ...)` would silently break the
display's IRQ.

## Shared SPI1 / GPIO8 dual-function

`PIN_LCD_DC = 8` (`bsp/platform/board.h`) doubles as `PIN_CC1101_MISO`. It is
an OUTPUT (DC) for the LCD and would need to be muxed to SPI1 RX (input)
around any CC1101 SPI access. `PIN_CC1101_CS = 40` is parked HIGH in
`board_init()` before any LCD traffic runs, so the (not-yet-harvested) CC1101
never drives lines shared with the LCD by accident. `bsp/platform/spi_bus.h`
already documents the intended arbitration API
(`spi_bus_acquire_cc1101()` / `spi_bus_release_cc1101()` /
`spi_bus_cc1101_cs()`) for whenever the radio driver is harvested.

## LED count = 16 (LED discrepancy record)

**`FwDisplayVibe.md`** (repo root, the original hardware description) says:

> There are 7 ws serial LEDs connected to GPIO 21.

**The verified board header and driver disagree and are authoritative:**
`bsp/leds/ws2812_driver.h` defines `WS2812_NUM_PIXELS 16` and
`FW2_LED_COUNT` as an alias for it — "single source of truth = 16, verified
board" per the header's own comment. `bsp/leds/ws2812_driver.c` drives 16
pixels on `pio1` via `PIN_LED_DATA` (GPIO 21, matching `FwDisplayVibe.md` on
the GPIO, just not the count).

**Resolution: 16 is authoritative.** Do not use the `FwDisplayVibe.md` count
of 7 anywhere in code, docs, or tests. If you ever see `7` in a new context
describing this board's LEDs, treat it as the same stale figure and correct
it to 16.

## CC1101 chip-select GPIO discrepancy

**`FwDisplayVibe.md` says:**

> The LCD SPI bus is shared with CC1101 radio. Its chip select is on GPIO 23.

**`bsp/platform/board.h` and `bsp/platform/board.c` disagree and are
authoritative:** `#define PIN_CC1101_CS 40`, and `board_init()` actively
drives GPIO 40 (not 23) HIGH at boot to park the CC1101 off the shared SPI1
bus before any LCD traffic. GDO0 (GPIO32) and GDO2 (GPIO37) match between the
two sources, so this is specifically a chip-select-pin discrepancy, not a
wholesale renumbering.

**Resolution: GPIO 40 is authoritative** for `PIN_CC1101_CS`. When the CC1101
driver is eventually harvested (see `docs/hardware/catalog.md`), use
`board.h`'s `PIN_CC1101_CS` macro rather than hard-coding either number.

## No LCD reset GPIO

The ST7796-class panel has no dedicated reset GPIO in this design. The
comment at the top of `bsp/platform/board.h` states it plainly:

> There is NO LCD reset GPIO on this board — the panel RESX is
> hardware-handled, so the driver relies on SWRESET only.

`bsp/platform/ioexp.c`'s `ioexp_init()` releases the LCD's hardware reset
(`SCREEN_NRST`) via the PCAL6524 I/O expander as part of board bring-up; the
`st7796_init()` driver itself only ever issues a software reset (SWRESET)
command over SPI. Don't add a `PIN_LCD_RESET`/`gpio_init` reset sequence —
there's no pin for it, and `FwDisplayVibe.md`'s claim that "reset is shared
with the display [touch controller]" refers to this same hardware-only
reset line, not a GPIO the firmware toggles.

## gfx/palette carry (complete)

`bsp/leds/led_ui.c`'s `led_spectrum_map()` depends on `bsp/gfx/palette.h`
(`inferno_rgb565()`). Both the header and the implementation
(`bsp/gfx/palette.c`, harvested verbatim from `subghz/src/gfx/palette.c`) are
now present and compiled into `freewili2_bsp` via `bsp/CMakeLists.txt`'s
source list. `led_ui.c` / `led_spectrum_map()` link cleanly against the real
`inferno_rgb565` implementation — no further harvesting needed.

## WS2812 first-frame latch (refresh the LEDs)

The **first** `ws2812_show()` transmission after the `pio1` state machine starts
does not reliably latch all 16 LEDs — in practice only pixel 0 (the data-in end
of the chain) lights and the other 15 stay dark. A **second** `ws2812_show()`
(any subsequent frame) latches the full strip. Verified on hardware 2026-07-01.

This is **not** a driver defect: `bsp/leds/ws2812_driver.c` and `ws2812.pio` are
functionally identical to the proven `sensorview` and `wilidispval`
implementations (confirmed by diff — only whitespace/comment/include differ). It
is a WS2812 timing/startup quirk on this board.

**How to handle it:** refresh the LED state periodically instead of calling
`ws2812_show()` exactly once. Real apps redraw every frame and never notice.
`apps/hello_display/main.c` re-issues `ws2812_show()` every ~250 ms from its main
loop; the symptom was originally seen because an earlier version showed the LEDs
once during setup and then sat in a touch-only loop that never refreshed them.

## Host tests are a standalone CMake project

`tests/` is configured and built as its **own** CMake project (no Pico SDK,
no cross-compiler) via `fw test` → `tools/fw.py`'s `test_command()`, which
runs `cmake -S tests -B build-tests`, `cmake --build build-tests`, then
`ctest --test-dir build-tests`. There is **no `host-test` CMake preset** in
`CMakePresets.json` — an earlier draft of the plan proposed one, but it was
removed once the standalone-project approach was chosen. Don't tell an agent
to run `cmake --preset host-test`; it doesn't exist. The only preset defined
today is `target` (for on-device builds).

## Audio: lock LRCK to MCLK/256 (DAC sample-slip tick)

The NAU88C10 runs **MCLK-direct** (reg 0x06 = 0), so its DAC/ADC sample rate is set
by MCLK, not by LRCK. MCLK is generated by an **integer** PWM divide of `clk_sys`, and
250 MHz does **not** divide to an exact 4.096 MHz: `250e6 / 61 = 4.0984 MHz`, so the
codec actually runs `fs = 4.0984e6 / 256 = 16009 Hz`. If the I2S frame rate (LRCK) is
set to the nominal 16000 Hz, the DAC consumes ~9 samples/s faster than data arrives and
**slips one sample ~9 times/second → an audible ~9 Hz periodic tick** (a blown/loud
speaker masks it; a clean headphone/line-out exposes it). Verified on 2026-07-04 as
±9.2 Hz sidebands on the 1 kHz carrier (−17 dB), gone (−44 dB) after the fix.

250 MHz is the board DEFAULT partly for this reason — it divides near-exactly to
the codec MCLK. Apps needing a different clock call board_init_clk(khz); the DVI
demo runs 252 MHz for an exact 25.2 MHz pixel clock, which moves the codec MCLK to
252e6/61 = 4.131 MHz (~0.8% audio pitch shift) — only relevant if that app also
plays audio.

**Rule:** derive the I2S PIO clkdiv from the **same integer MCLK divider**, not from
the nominal sample rate, so `LRCK == MCLK/256` exactly and data-in == data-out. In
`bsp/audio/audio_i2s_duplex.c`: `float div = (float)(8u * ticks) / 3.0f;` (where
`ticks` is the MCLK PWM wrap). Net pitch is +0.06 % (16009 vs 16000 Hz), inaudible.
Do **not** "simplify" this back to `clk_sys / (96*fs)` — that reintroduces the tick.

## Hardware verification status

The `hello_display` on-hardware smoke test **passed on 2026-07-01** (RP2350 rev 3,
programmed + verified over the cmsis-dap probe). Confirmed live on the board:

- **Display (ST7796 + DMA):** panel lights, backlight on, "TOUCH THE SCREEN"
  text renders.
- **Touch (FT6336U):** chip detected over I2C (`ft6336: init ok id=0x64` via RTT);
  taps register and draw dots at the correct coordinates.
- **LEDs (WS2812 ×16):** all 16 light green (after applying the periodic-refresh
  fix — see "WS2812 first-frame latch" above).
- **Platform:** boots at 250 MHz (`hello_display up: sys=250000 kHz` via RTT),
  ioexp init OK, RTT diagnostics working (control block found at `0x200045d0`).

The **I2S full-duplex audio** driver (`hello_audio`) **passed on 2026-07-04** (RP2350
rev 3): codec bring-up OK (`rev=0x01A`, `pm2=0x015`), speaker↔jack routing switches at
the register level, RX capture never starves (shared `DMA_IRQ_0` coexists with the
ST7796 flush), and — after the MCLK/LRCK-lock fix (see "Audio: lock LRCK to MCLK/256"
above) — the 1 kHz tone plays clean on both the onboard speaker and the 3.5 mm jack
(±9.2 Hz slip sidebands −17 dB → −44 dB). Full record:
`docs/superpowers/findings/2026-07-04-i2s-audio-e2e.md`.

Peripherals still marked TODO in `docs/hardware/catalog.md` (NFC, IR, DVI,
buttons, PIO-USB) remain unverified — their driver harvest is future work.
PDM mics are now DONE (see "PDM microphones" below). The four I2C sensors
(OPT4001, SHT40, BMI323, BMM350) are also now DONE (see "I2C sensors"
below) — hardware-verified 2026-07-04 via `apps/hello_sensors` (4/4 chip-ids,
gravity vector PASS; human stimulus tests pending — see
`docs/superpowers/findings/2026-07-04-i2c-sensors-e2e.md`).

## Radio: GDO0 capture runs on PIO2, not PIO0

`bsp/radio/gdo_capture.c` sets `s_pio = pio2` and calls `pio_set_gpio_base(pio2, 16)`
so it can reach GDO0 = GPIO32 (the PIO GPIO-base window must move to 16..47).
`pio_set_gpio_base` shifts the base for the WHOLE PIO block, so this cannot run on
`pio0` (audio I2S, GPIO 4–7) or `pio1` (WS2812 LEDs, GPIO 21) without breaking their
low-GPIO access. The BSP is built with
`PICO_PIO_USE_GPIO_BASE=1` (a PUBLIC compile def on `freewili2_bsp`) — required for any
GPIO≥32 PIO access. The subghz source used `pio0`; the `pio0`→`pio2` change is the only
functional edit made during the harvest. GDO2 (GPIO37) remains unused. The capture DMA
is ENDLESS and polled (`write_addr`), so it registers no `DMA_IRQ_0` handler.

### pio2 cohabitation: radio + IR (harvest, 2026-07-06)

`pio2` is no longer radio-exclusive: the harvested `bsp/ir/` driver also runs
on it — `ir_capture_init()` claims SM0 (`ir_capture.pio`), `ir_tx_init()`
claims SM1 (`ir_tx.pio`). Across all three programs that can live on pio2 —
`gdo_capture` (11 instructions), `ir_capture` (11 instructions), `ir_tx` (9
instructions; confirmed via the generated `build/bsp/ir_tx.pio.h`'s
`.length = 9`, not the 8 an earlier estimate assumed) — pio2's 32-instruction
memory holds **31 of 32 slots**, 1 free.

**Init-order hazard:** `gdo_capture_init()` calls `pio_set_gpio_base(pio2, 16)`
to reach GDO0 = GPIO32, but does not check that call's return value. The Pico
SDK's `pio_set_gpio_base()` fails once any program already occupies pio2's
instruction memory — the base can only be (re)set before the PIO block holds
program data. So if an app calls `ir_capture_init()`/`ir_tx_init()` (which
load `ir_capture`/`ir_tx` into pio2) **before** `gdo_capture_init()`, the
later `pio_set_gpio_base(pio2, 16)` call silently fails, the gpio_base stays
at 0, and `gdo_capture` silently ends up capturing the wrong pin instead of
GDO0 = GPIO32 — no crash, no diagnostic, just wrong data. Radio-first
ordering (`gdo_capture_init()` before `ir_capture_init()`/`ir_tx_init()`)
avoids this because IR's pins (GPIO20/24) remain reachable from the
gpio_base=16 window the radio path establishes. **Any app that combines
radio and IR must call `gdo_capture_init()` first.** No current app in this
repo combines the two (see `docs/drivers/ir.md` § Dependencies).

## PDM microphones (increment 2, 2026-07-04)

- **PDM clock is 1.024 MHz, not the datasheet-typical 3.072 MHz.** The FW2
  MEMS mics did not output data at 3.072 MHz on this board (measured in the
  `microphonearray` repo; matches the known-working movieplayer mic). 1.024 MHz
  × CIC decimate 64 → 16 kHz PCM. `PDM_CLK_HZ` lives in `bsp/platform/board.h`.
- **RP2350 pad-isolation trap:** input pads power up with the input buffer
  disabled AND the ISO latch (PADS bit 8) engaged — the PIO reads stuck-0 and
  the PDM stream decimates to pure DC. `pdm_capture.pio`'s init explicitly
  enables the input buffers and clears ISO on GPIO 29/30.
- **Mic power is PCAL6524 P1 bit 7 (`MIC_PWR`, active-high)** — off at
  power-on and after `ioexp_init()`; `pdm_capture_init()` drives it on via
  `ioexp_mic_pwr(true)` and waits 50 ms.
- **Physical left-to-right mic order is D, B, A, C** (bench-measured phase
  ramp in the `microphonearray` repo), not A, B, C, D. Channel indices MIC_A..D
  are line/phase order: SIG1-high, SIG1-low, SIG2-high, SIG2-low.
- **Overrun semantics:** the capture DMA free-runs into a 32 KiB ring
  (~64 ms of slack). A consumer stalled longer than that silently loses/tears
  the stalled span — there is no overrun flag. Single consumer only (shared
  CIC state).

## I2C sensors (increment 4, 2026-07-04)

- **Guard convention:** `bsp/sensors/*` splits pure logic from hardware with
  `#ifdef PICO_BUILD` — the Pico SDK defines `PICO_BUILD=1` for every target
  build (sdk 2.2.0 `src/rp2350/pico_platform/CMakeLists.txt:11`) and the host
  CTest tree does not, so the guards work with zero configuration. This
  coexists with the repo's older `#ifndef HOST_TEST` convention (ook_tx,
  scan_engine): HOST_TEST requires the test target to define it; PICO_BUILD
  requires nothing. Both are valid; new harvests should keep whichever the
  source repo uses.
- **BMI323 and BMM350 I2C reads return 2 leading dummy bytes** (confirmed on
  hardware in the source repo). The drivers account for it; anyone writing
  raw I2C to these parts must too.
- **OPT4001 lux factor is package-dependent** (437.5e-6 in the driver, the
  datasheet value for the common package). Absolute lux should be calibrated
  against a known light level; relative changes are trustworthy regardless.
- **BMM350 init takes ~130 ms of mandatory settles** (soft reset 24 ms, OTP
  download, magnetic reset 14+18 ms, PMU steps, normal mode 40 ms). Do not
  "optimize" the sleeps — the sequence is ported from the hardware-validated
  fwcom protocol notes.
- All four sensors are blocking polled I2C1 (400 kHz) — no DMA/IRQ/PIO. The
  SHT40 high-precision measure blocks ~10 ms per read.
