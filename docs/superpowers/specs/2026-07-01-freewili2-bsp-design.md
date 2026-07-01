# Free Wili2 Board Support Repo (`wilibsp`) — Design

Date: 2026-07-01
Status: Approved (design), pending spec review

## 1. Purpose & core workflow

`wilibsp` is a single repository you point an AI session (or a human) at that
contains **everything needed to build a Free Wili2 app fast and cheaply**: proven
drivers, a scaffolding path, and a dense agent-orientation layer so the agent does
not rediscover hard-won hardware facts.

Target workflow:

> "Here is my idea + this repo" → agent reads `AGENTS.md` → runs `fw new-app` →
> wires up BSP drivers → `fw build && fw flash` → working app on the board.

Success criteria for v1:

- An agent (or human) can scaffold a new app and get the flagship drivers
  (display, touch, LEDs) working with minimal time and tokens.
- The repo builds and flashes to real hardware via the attached debug probe.
- Pure-logic modules run as host tests without hardware.

## 2. Hardware summary

Free Wili2 = Raspberry Pi **RP2350B** (48 GPIO, `PICO_RP2350A=0`), 12 MHz main
oscillator, 16 MB QSPI/XIP flash, 8 MB serial SRAM (APS6404L, CS on GPIO 47).
Toolchain: **Pico SDK 2.x (C/C++) + ARM GCC**, CMake, Ninja, OpenOCD. A Raspberry
Pi Debug Probe is attached at `cmsis-dap usb interface 0`.

Full peripheral inventory lives in `docs/hardware/pinmap.md`; the authoritative pin
definitions live in `bsp/boards/freewili2.h`. Key v1 peripherals:

- **Display:** ST7796, 480×320, SPI (DMA required). `LCD_DC=8`, `LCD_CS=9`,
  `LCD_SCLK=10`, `LCD_MOSI=11`, backlight `GPIO 25`.
- **Touch:** FT6336U on I2C1 (SDA 26 / SCL 27), no INT pin (polled), reset shared
  with display.
- **LEDs:** WS2812 serial LEDs on GPIO 21, signal inverted by a driver chip.
  **Count: 16** (see discrepancy note below).
- **Shared SPI:** the LCD SPI bus is shared with the CC1101 radio (CS 23, MISO on
  the LCD_DC pin GPIO 8) — GPIO 8 is dual-function.

### Hardware discrepancy note

`FwDisplayVibe.md` (the original hardware description) states **7** WS serial LEDs
on GPIO 21. The board owner states there are **16**. v1 treats **16** as
authoritative via a single configurable constant (`FW2_LED_COUNT`) in
`bsp/boards/freewili2.h`, and records the discrepancy in `docs/hardware/facts.md`
for reconciliation against real hardware. General rule: **where `FwDisplayVibe.md`
and the verified board header disagree, the verified header wins.**

## 3. Repo architecture (monorepo)

A shared BSP library plus an `apps/` directory of individual CMake targets.

```
wilibsp/
  bsp/                     # reusable freewili2 BSP library (CMake STATIC/INTERFACE lib)
    boards/freewili2.h     # SINGLE SOURCE OF TRUTH for pins & board constants
    platform/              # board.c (init/clocks), ioexp, psram, spi_bus, i2c_bus, diag (RTT)
    display/               # st7796 + DMA
    input/                 # ft6336 touch
    leds/                  # ws2812 (16), led_color, led_ui
    fw2.h                  # umbrella public header
  apps/
    template/              # copy-me starter app
    hello_display/         # v1 smoke-test app (display + touch + LEDs)
  tools/
    fw / fw.py             # cross-platform CLI: build|flash|rtt|test|new-app
    CMakePresets.json      # configure/build presets (humans can drive raw cmake)
    openocd/               # openocd cfg for the cmsis-dap probe
  docs/
    hardware/pinmap.md     # machine-readable pin table + peripheral catalog
    hardware/facts.md      # "hard-won invariants"
    drivers/<name>.md      # per-driver usage docs
  skills/                  # Claude Code skills (freewili2:new-app, freewili2:add-driver)
  AGENTS.md  CLAUDE.md     # agent entry points (CLAUDE.md -> thin pointer to AGENTS.md)
  README.md
```

Dependency direction: `app → bsp drivers → platform bus (spi/i2c/ioexp) → hardware`.
The BSP compiles for target (RP2350B); pure-logic modules also compile on host under
`#ifndef HOST_TEST`.

Source material: all driver code is harvested and normalized from the owner's
existing Free Wili2 repos (they own all of them). The richest source is `subghz`,
which already has a well-factored BSP layout (`platform/`, `display/st7796`,
`input/ft6336`, `leds/ws2812`) and a mature `AGENTS.md`. `wilidispval` provides
additional display/LED material.

## 4. BSP library & driver conventions

- **Naming:** public API prefixed `fw2_` (e.g. `fw2_display_init()`,
  `fw2_led_set(i, rgb)`). Each driver = one header + one clear purpose + documented
  dependencies. A reader should understand what a driver does without reading its
  internals; internals can change without breaking consumers.
- **Error handling:** functions return an `fw2_err_t` enum; diagnostics via a
  `FW2_LOG()` macro over **SEGGER RTT** (no USB/UART stdio).
- **Board init invariant** (adopted from subghz; non-negotiable): `board_init()`
  raises vreg → `set_sys_clock_khz(250000, true)` → **re-source `clk_peri` from
  `clk_sys`** (without this the SPI peripheral has no clock and the LCD is dead) →
  binary is `copy_to_ram`, `PICO_RP2350A=0`. Never override `PICO_BOARD` on the
  cmake line; `CMakeLists.txt` sets `PICO_BOARD=freewili2`.
- **Data flow specifics:** display writes via **DMA**; touch is **polled** over I2C
  (no INT pin); LEDs via **PIO** with an **inverted** signal.
- **RAM budget:** all code+data+bss live in 512 KB SRAM (`copy_to_ram`); large
  buffers (e.g. graphics) belong in PSRAM.

## 5. Agent enablement layer (portable docs + skills)

- **`AGENTS.md`** (root): dense orientation — what the board is, the invariants, the
  `fw` command vocabulary, how to add a driver, where the pin map lives. Modeled on
  subghz's `AGENTS.md`.
- **`CLAUDE.md`**: thin pointer to `AGENTS.md` so Claude Code and other harnesses
  converge on one source.
- **`docs/hardware/`**: `pinmap.md` (a table an agent can grep), a peripheral
  catalog marking which drivers exist vs TODO, and `facts.md` (invariants +
  discrepancies).
- **`skills/`**: Claude Code skills — `freewili2:new-app` (scaffold an app from the
  template) and `freewili2:add-driver` (harvest a driver from one of the owner's
  repos into the BSP shape). Portable docs work in any harness; skills add leverage
  for Claude Code specifically.

## 6. Tooling — the `fw` CLI (cross-platform)

One Python entry point, identical on Windows + Linux, thin over CMake/OpenOCD
(Python is already a Pico SDK dependency, so no new runtime):

- `fw build [app]` — configure + build via CMakePresets (default app = `hello_display`).
- `fw flash [app]` — OpenOCD SWD program + verify + reset over the cmsis-dap probe.
- `fw rtt [-s N]` — live SEGGER RTT diagnostics.
- `fw test` — host unit tests (CMake + Ninja + CTest, no hardware).
- `fw new-app <name>` — copy `apps/template` → `apps/<name>` with renames.

`CMakePresets.json` underpins everything so humans can bypass `fw` and run raw
`cmake`/`ctest`. Linux and Windows use the same command vocabulary.

## 7. Testing strategy

- **Host tests (no hardware):** pure-logic modules compiled with `HOST_TEST`, run
  via CTest through `fw test`. Logic is factored so the decision-making core is pure
  and host-tested, with hardware binding behind `#ifndef HOST_TEST` (subghz pattern).
- **Hardware smoke test:** the `hello_display` app is the on-board acceptance check —
  display renders, touch responds, LEDs light — built, flashed, and verified over the
  debug probe as part of v1.

## 8. v1 scope

**In scope:**

- Repo skeleton (monorepo `bsp/` + `apps/`).
- `fw` CLI + `CMakePresets.json` + OpenOCD config.
- `AGENTS.md`, `CLAUDE.md`, `docs/hardware/` (pinmap, catalog, facts), per-driver docs.
- One scaffolding skill (`freewili2:new-app`) and the `add-driver` skill.
- Drivers: **platform core** (board init, ioexp, psram, spi_bus, i2c_bus, diag),
  **display** (ST7796 + DMA), **touch** (FT6336U), **LEDs** (16× WS2812).
- Apps: `template` + `hello_display`.
- Host tests + on-hardware smoke test.

**Deferred** (documented as TODO in the peripheral catalog; each a later
`add-driver` increment): radio/CC1101, NFC (ST25R3916B), IR TX/RX, DVI/HSTX,
I2S audio (NAU88C10), PDM mics, 14-button serial coprocessor, Pico-PIO-USB, and the
I2C sensors (OPT4001 light, SHT40 humidity, BMI323 IMU, BMM350 magnetometer).

## 9. Open items / risks

- **Hardware fact verification:** `FwDisplayVibe.md` has at least one known error
  (LED count). Pin/peripheral facts should be validated against the verified board
  header and, where possible, real hardware; the smoke test surfaces gross errors.
- **LVGL:** subghz vendors LVGL as its graphics layer. v1 keeps the BSP at the
  driver level (ST7796 + a thin framebuffer/gfx helper) and leaves LVGL as an
  optional integration in an app, not a BSP dependency — revisit if apps need it.
- **PSRAM/RAM budget:** graphics buffers must live in PSRAM given the 512 KB SRAM
  `copy_to_ram` constraint; the display driver API should make this explicit.
