# wilibsp — FreeWili2 Board Support Package

Board-support monorepo for the **FreeWili 2** (Raspberry Pi **RP2350B**, 48
GPIO, 16 MB flash, 8 MB PSRAM): a shared `freewili2_bsp` CMake static
library, a set of apps, and a cross-platform `fw` CLI to build/flash/test
them.

Driven today: the 480x320 ST7796-class touch LCD, FT6336U touch, 16 WS2812
RGB LEDs, full-duplex I2S audio (NAU88C10), the CC1101 sub-GHz radio, the
4-mic PDM array, four I2C sensors (OPT4001 light, SHT40 humidity/temp,
BMI323 IMU, BMM350 magnetometer), IR receive/decode/encode/transmit (with a
Flipper-`.ir` parser/writer), and a polled native-USB host MSC stack
(thumb drives, no TinyUSB) with FatFs. Still `TODO`: NFC, DVI/HSTX
video-out, the 14-button coprocessor, and Pico-PIO-USB. See
[`docs/hardware/catalog.md`](./docs/hardware/catalog.md) for the full
peripheral → driver → provenance table, and `docs/drivers/` for per-driver
usage docs. Each driver ships with an `apps/hello_*` on-hardware smoke
test; the pure-logic layers (DSP, palettes, IR protocol codecs, `.ir`
parsing, sensor compensation) are host-unit-tested with no hardware or
Pico SDK needed.

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

**Status:** every harvested driver group has passed its `hello_*` smoke
test on a physical board (most recently `hello_ir`'s TX→RX loopback and
`hello_usbdrive`'s thumb-drive mount, 2026-07-06). The host test tree is at
26 green binaries. `docs/hardware/facts.md` records the hard-won invariants
— shared SPI1 arbitration, shared DMA_IRQ_0 ownership, pio2 cohabitation
(radio GDO capture + IR, radio inits first), the power-gated rails on the
PCAL6524 I/O expander — and keeps claims scoped to what a bench session
actually demonstrated.

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
    gfx/                      color palettes (host-tested)
    audio/                    NAU88C10 I2S full-duplex + capture/tone/VU helpers
    radio/                    CC1101 sub-GHz: regs, GDO capture, OOK TX, engines
    pdm/ dsp/                 4-mic PDM array + integer CIC/DC-block filters
    sensors/                  OPT4001, SHT40, BMI323, BMM350(+compensation)
    ir/                       IR capture/TX (pio2) + protocol codecs + .ir files
    usbhost/                  polled native-USB host MSC (no TinyUSB) + FatFs glue
    third_party/segger_rtt/   SEGGER RTT (vendored)
    third_party/fatfs/        FatFs R0.15b (vendored)
  apps/
    template/                 starter app — `fw new-app` copies this
    hello_display/            display + touch + LEDs smoke test
    hello_audio/ hello_cc1101/ hello_mics/ hello_sensors/
    hello_ir/                 NEC TX->RX loopback + live decode
    hello_usbdrive/           thumb-drive mount + root listing
  tools/                      fw CLI (fw.py) + POSIX/Windows launchers + its own pytest
  tests/                      standalone host CTest tree (no Pico SDK, no hardware)
  docs/
    hardware/                 pinmap.md, facts.md, catalog.md
    drivers/                  per-driver usage docs (platform ... ir, usbhost)
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

## License

MIT — see [LICENSE](LICENSE). Vendored third-party components (SEGGER RTT,
FatFs) keep their own permissive licenses, noted in the LICENSE file and in
the vendored file headers. Harvested drivers carry the MIT/BSD-3-Clause
terms of their source repos where noted.
