# wilibsp — FreeWili2 Board Support Package

Board-support monorepo for the **FreeWili 2** (Raspberry Pi **RP2350B**, 48
GPIO, 16 MB flash, 8 MB PSRAM): a shared `freewili2_bsp` CMake static
library, a set of apps, and a cross-platform `fw` CLI to build/flash/test
them.

The board drives a 480x320 ST7796-class touch LCD, 16 on-board WS2812 RGB
LEDs, and (deferred to future work) a CC1101 sub-GHz radio, NFC, IR,
DVI/HSTX video-out, I2S audio, PDM mics, a 14-button coprocessor,
Pico-PIO-USB, and several I2C sensors. See
`docs/hardware/catalog.md` for what's driven today vs. what's still `TODO`.

**Agents:** read [`AGENTS.md`](./AGENTS.md) first — it's the dense
orientation doc (command table, hardware invariants, how to add a driver).
[`CLAUDE.md`](./CLAUDE.md) just points there.

## Quick start

Prerequisites: Pico SDK 2.x + ARM GCC toolchain (`~/.pico-sdk`), CMake +
Ninja, a cmsis-dap debug probe (e.g. Raspberry Pi Debug Probe) + OpenOCD for
flashing/RTT, Python 3 for the `fw` CLI. Works the same on Windows
(PowerShell) and Linux.

```bash
fw build            # configure + build apps/hello_display for the RP2350B target
fw flash            # program it over the debug probe (OpenOCD)
fw rtt              # stream live SEGGER RTT diagnostics
```

(`tools/fw` is the POSIX launcher, `tools/fw.cmd` the Windows one; both just
invoke `python tools/fw.py`. Run them from the repo root, or put `tools/` on
your `PATH`.)

No hardware handy? Run the host-only unit tests instead (no Pico SDK, no
debug probe):

```bash
fw test
```

Scaffold a new app from the template:

```bash
fw new-app my_app
# then add `add_subdirectory(apps/my_app)` to the top-level CMakeLists.txt
```

**Status:** BSP + `hello_display` smoke app build cleanly (`fw build`,
`fw test`). Actual on-hardware verification (Task 9 of the implementation
plan) has not run yet — see `docs/hardware/facts.md` for what's confirmed
by source vs. what's still pending a physical board.

## Repo map

```
wilibsp/
  CMakeLists.txt              top-level: PICO_BOARD, pico_sdk_init, add bsp + apps
  CMakePresets.json           the "target" configure/build preset
  bsp/                        shared freewili2_bsp STATIC library
    fw2.h                     umbrella include — pull this into an app
    boards/freewili2.h        SDK board header (RP2350B, 48 GPIO, 16 MB flash)
    platform/                 clocks/vreg, pin map (board.h), I/O expander, PSRAM,
                               SPI1 bus arbitration, RTT diag
    display/                  ST7796 480x320 LCD driver + 5x7 font
    input/                    FT6336U capacitive touch driver
    leds/                     WS2812 x16 driver + led_color/led_ui helpers
    gfx/                      color palette header (inferno_rgb565 — see facts.md)
    third_party/segger_rtt/   SEGGER RTT (vendored)
  apps/
    template/                 starter app — `fw new-app` copies this
    hello_display/            v1 on-hardware smoke test (display + touch + LEDs)
  tools/                      fw CLI (fw.py) + POSIX/Windows launchers + its own pytest
  tests/                      standalone host CTest tree (no Pico SDK, no hardware)
  docs/
    hardware/                 pinmap.md, facts.md, catalog.md
    drivers/                  platform.md, display.md, touch.md, leds.md
    superpowers/plans/        the full implementation plan / spec
  skills/
    freewili2-new-app/        Claude Code skill: scaffold a new app
    freewili2-add-driver/     Claude Code skill: harvest a new driver
  AGENTS.md                   dense agent orientation (read this first)
  CLAUDE.md                   thin pointer to AGENTS.md
  FwDisplayVibe.md            original hardware description (secondary source —
                               known to have at least one error; see facts.md)
```

## Further reading

- [`AGENTS.md`](./AGENTS.md) — command vocabulary, hardware invariants, how
  to add a driver, naming conventions.
- [`docs/hardware/pinmap.md`](./docs/hardware/pinmap.md) — full pin table.
- [`docs/hardware/facts.md`](./docs/hardware/facts.md) — hard-won invariants
  and the LED-count discrepancy record.
- [`docs/hardware/catalog.md`](./docs/hardware/catalog.md) — peripheral →
  driver status → harvest source.
- [`docs/superpowers/plans/2026-07-01-freewili2-bsp.md`](./docs/superpowers/plans/2026-07-01-freewili2-bsp.md)
  — the full implementation plan this repo was built from.
