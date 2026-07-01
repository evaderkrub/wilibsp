# AGENTS.md — guide for AI agents and contributors

This file orients coding agents (Claude Code, Cursor, Copilot, etc.) and new
human contributors working in `wilibsp`. It is intentionally dense: the goal
is that you do **not** have to rediscover the hard-won facts this project was
built on. Read it before making changes.

## What this is

`wilibsp` is a board-support **monorepo** for the **FreeWili 2** (Raspberry Pi
**RP2350B**, 48 GPIO, 16 MB flash, 8 MB PSRAM). It provides:

- `bsp/` — the shared `freewili2_bsp` CMake **STATIC library**: platform
  bring-up, display, touch, and LED drivers, harvested and normalized from the
  owner's proven repos (primarily `subghz`).
- `apps/` — individual CMake executables that link `freewili2_bsp`
  (`template` — starter scaffold; `hello_display` — v1 on-hardware smoke
  test: display renders, touch responds, LEDs light).
- `tools/fw.py` (+ `tools/fw` / `tools/fw.cmd` launchers) — a cross-platform
  CLI that drives CMake + OpenOCD identically on Windows and Linux.
- `tests/` — a standalone host CTest tree for pure logic (no Pico SDK, no
  hardware).

The umbrella header is `bsp/fw2.h` — include this from an app to pull in the
board + drivers.

**Status:** Tasks 1-8 of the implementation plan are done and building.
**Task 9 (on-hardware smoke test) is PENDING** — nothing here has been
verified running on physical hardware yet. Where this doc or others describe
expected behavior (e.g. "white text, green LEDs, red touch dots"), treat it
as the design intent, not a confirmed-working result, until Task 9 lands.

## Command vocabulary

All commands run from the repo root and are identical on Windows and Linux
(the CLI is Python; `tools/fw` is the POSIX launcher, `tools/fw.cmd` the
Windows one — both just call `python tools/fw.py "$@"`).

| Command | What it does |
|---|---|
| `fw build [app]` | Configure + build `apps/<app>` for the RP2350B target via `cmake --build --preset target --target <app>` (default app: `hello_display`) |
| `fw flash [app]` | Program `build/apps/<app>/<app>.elf` over the cmsis-dap debug probe via OpenOCD (`tools/openocd/freewili2.cfg`) |
| `fw rtt` | Attach to the target and stream SEGGER RTT diagnostics (OpenOCD RTT server on port 9090) |
| `fw test` | Configure + build + run the standalone host CTest tree in `tests/` (MinGW GCC + Ninja on Windows; no Pico SDK, no hardware) |
| `fw new-app <name>` | Scaffold `apps/<name>` by copying `apps/template` and rewriting the CMake target name |

Add `--print` to `build`/`flash`/`rtt`/`test` to print the underlying
command(s) instead of running them (useful for an agent to inspect what would
run without touching hardware).

After `fw new-app <name>` you must add
`add_subdirectory(apps/<name>)` to the top-level `CMakeLists.txt` yourself —
the CLI only scaffolds the directory, it does not edit the top-level CMake.

## Invariants — do NOT relearn these the hard way

Treat these as facts; each cost real debugging time in the source repos this
BSP was harvested from. They are also recorded in `docs/hardware/facts.md`.

1. **RP2350B, not RP2350A.** `bsp/boards/freewili2.h` sets `PICO_RP2350A 0`
   (48 GPIO). The board is selected via `set(PICO_BOARD freewili2)` in the
   top-level `CMakeLists.txt` — **NEVER** pass `-DPICO_BOARD` on the cmake
   command line; it overrides the cached value and reverts to the wrong
   config.
2. **Clock/RAM invariant.** `board_init()` (`bsp/platform/board.c`) does:
   `vreg_set_voltage(VREG_VOLTAGE_1_25)` → `sleep_ms(10)` →
   `set_sys_clock_khz(250000, true)` → **re-source `clk_peri` from
   `clk_sys`** via
   `clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, f, f)`.
   Without that re-source the SPI peripheral has no clock and the LCD is
   dead. Every app binary is `pico_set_binary_type(<app> copy_to_ram)`: all
   code+data+bss live in 512 KB SRAM, so watch the RAM budget — large buffers
   (framebuffers, capture clips) belong in PSRAM (`PSRAM_BASE 0x11000000`,
   APS6404L, 8 MB, brought up by `bsp/platform/psram.c`).
3. **Diagnostics = SEGGER RTT only.** `DIAG(...)` (`bsp/platform/diag.h`) →
   `SEGGER_RTT_printf(0, ...)` on channel 0. There is no UART/USB stdio.
   `SEGGER_RTT_printf` supports `%d %u %x %s %c` and field widths — **no
   floats**. View with `fw rtt`.
4. **DMA_IRQ_0 is shared.** The ST7796 async flush registers with
   `irq_add_shared_handler(DMA_IRQ_0, ...)` and acts only on its own DMA
   channel's status. Any new DMA user on this line must do the same — never
   `irq_set_exclusive_handler(DMA_IRQ_0, ...)`.
5. **Shared SPI1 / GPIO8 dual-function.** `PIN_LCD_DC = 8` doubles as
   `PIN_CC1101_MISO`; `PIN_CC1101_CS = 40` is parked HIGH in `board_init()`
   before any LCD traffic. (The CC1101 radio driver itself is not yet
   harvested into this repo — see `docs/hardware/catalog.md` — but the pin
   sharing and parking are already live in `board.c`.)
6. **LED count = 16.** `FW2_LED_COUNT` / `WS2812_NUM_PIXELS`
   (`bsp/leds/ws2812_driver.h`) = 16, on `pio1` via `PIN_LED_DATA` (GPIO 21).
   `FwDisplayVibe.md` (repo root, the original hardware description) says 7
   and is **WRONG** — the verified board header wins. `FwDisplayVibe.md` also
   disagrees with `board.h` on the CC1101 chip-select pin (says GPIO 23;
   `board.h`/`board.c` say GPIO 40 and actively drive it). See
   `docs/hardware/facts.md` for both discrepancy records.
7. **No LCD reset GPIO.** The panel relies on SWRESET only; RESX is
   hardware/ioexp-handled (`bsp/platform/ioexp.c` releases it as part of
   `ioexp_init()`). Don't look for a `PIN_LCD_RESET`-style define — there
   isn't one.
8. **Board selection is CMake-only.** `set(PICO_BOARD freewili2)` lives in
   the top-level `CMakeLists.txt`; `list(APPEND PICO_BOARD_HEADER_DIRS ...)`
   points at `bsp/boards`. Never override on the command line (repeats
   invariant 1 — it's the single most common way to break a fresh build).

## How to add a driver

The BSP grows by harvesting a proven driver from one of the owner's other
repos (see `docs/hardware/catalog.md` for which repo owns which peripheral),
not by writing one from scratch:

1. **Copy** the `.c`/`.h` (and any `.pio`) files verbatim into
   `bsp/<domain>/` (e.g. `bsp/radio/`, `bsp/nfc/`), matching the directory
   layout the source repo uses under its `src/` (`platform/`, `display/`,
   `input/`, `leds/`, ...) so its existing `#include "domain/x.h"`-style
   includes resolve unchanged against the `bsp/` include root.
2. **Wire it into `bsp/CMakeLists.txt`**: add the new `.c` files to the
   `add_library(freewili2_bsp STATIC ...)` source list, and add any new
   `target_link_libraries` (pico_sdk component) or
   `pico_generate_pio_header` calls it needs.
3. **Activate the include in `bsp/fw2.h`**: add
   `#include "domain/x.h" // (Task N)` to the umbrella header so apps get it
   for free via `#include "fw2.h"`.
4. **Update `docs/hardware/catalog.md`**: flip the peripheral's row from
   `TODO` to `DONE`.
5. If the driver has pure/host-testable logic, add a `tests/test_*.c` +
   `tests/CMakeLists.txt` entry so `fw test` covers it (see the
   `subghz`-repo pattern of splitting pure decision logic from hardware
   binding behind `#ifndef HOST_TEST`).

The `skills/freewili2-add-driver/SKILL.md` skill in this repo walks an agent
through exactly this procedure.

## Where things live

- **Pin map**: `docs/hardware/pinmap.md` (generated from and cross-checked
  against `bsp/platform/board.h`, the **authoritative** pin source).
- **Hardware facts / invariants**: `docs/hardware/facts.md`.
- **Peripheral status (done vs. TODO)**: `docs/hardware/catalog.md`.
- **Per-driver usage docs**: `docs/drivers/{platform,display,touch,leds}.md`.
- **Original hardware description**: `FwDisplayVibe.md` (repo root) — a
  secondary source, useful for the broader peripheral inventory (radio, NFC,
  IR, DVI, audio, mics, buttons, PIO-USB, sensors) not yet in `board.h`, but
  known to contain at least one error (LED count — see above). When it
  conflicts with `bsp/platform/board.h` or `bsp/leds/ws2812_driver.h`, the
  verified board header/driver code wins.
- **Full implementation plan / spec**:
  `docs/superpowers/plans/2026-07-01-freewili2-bsp.md`.

## Naming note

Harvested drivers keep their proven names from the source repos:
`st7796_*` (display), `ft6336_*` (touch), `ws2812_*` (LEDs), `board_*` /
`ioexp_*` / `psram_*` (platform). There was **no** `fw2_`-prefix rename — the
spec proposed one, but forcing it onto an already-consistent, harvested
codebase would be pure churn. `fw2.h` is the umbrella include; the `fw2_`
prefix convention (if ever used) applies only to new BSP-level convenience
code written from scratch in this repo, not to harvested drivers.

## Conventions

- **Conventional Commits**: `feat:`, `fix:`, `docs:`, `refactor:`, `test:`
  with optional scope; imperative subject.
- **Diagnostics via `DIAG()`** — never `printf`, never USB/UART stdio.
- `build/`, `build-tests/`, `*.uf2`, `*.elf`, `*.bin`, `__pycache__/`,
  `.venv/` are git-ignored — don't commit them.

## Gotchas for automated edits

- Don't add USB/UART `printf` stdio — use `DIAG()` (invariant 3).
- Never pass `-DPICO_BOARD` on a cmake command line (invariant 1/8).
- Register any new DMA_IRQ_0 user as a shared handler, guarded on its own
  channel (invariant 4).
- If you touch GPIO8, remember it is dual-purpose (LCD_DC / CC1101 MISO)
  (invariant 5).
- Trust `bsp/platform/board.h` over `FwDisplayVibe.md` for any pin or LED
  count discrepancy (invariant 6).
- `bsp/leds/led_ui.c` (`led_spectrum_map`) depends on `bsp/gfx/palette.h`;
  only the header is present in this repo today — `gfx/palette.c` is
  **not** (harvest it from `subghz/src/gfx/palette.c` if you wire up
  `led_ui_*`/`led_spectrum_map`). Current v1 apps (`template`,
  `hello_display`) don't use `led_ui`, so this doesn't bite yet.
