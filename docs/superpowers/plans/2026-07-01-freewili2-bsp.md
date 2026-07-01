# Free Wili2 Board Support Repo (`wilibsp`) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a Free Wili2 board-support monorepo that lets a human or AI agent scaffold and build a working RP2350B app fast, with proven platform + display + touch + LED drivers, a cross-platform `fw` CLI, and a dense agent-orientation layer.

**Architecture:** A shared `freewili2_bsp` CMake library (drivers harvested and normalized from the owner's existing repos — primarily `subghz`) plus an `apps/` directory of individual CMake targets. A Python `fw` CLI drives CMake + OpenOCD identically on Windows and Linux. Pure-logic modules compile on host under `HOST_TEST` for CTest; hardware bring-up is verified by an on-board smoke-test app over the attached debug probe.

**Tech Stack:** C11 + Pico SDK 2.x (RP2350B), CMake ≥3.13 + Ninja, ARM GCC, OpenOCD (cmsis-dap probe), SEGGER RTT diagnostics, Python 3 (fw CLI), pytest (fw CLI tests).

## Global Constraints

Every task's requirements implicitly include these. Values are copied verbatim from the proven source (`subghz`) and the spec.

- **RP2350B, not A.** `bsp/boards/freewili2.h` sets `PICO_RP2350A 0` (48 GPIO). Board is selected via `set(PICO_BOARD freewili2)` in CMake — **NEVER** pass `-DPICO_BOARD` on the command line; it reverts to the wrong config.
- **Clock/RAM invariant.** `board_init()` does: `vreg_set_voltage(VREG_VOLTAGE_1_25)` → `sleep_ms(10)` → `set_sys_clock_khz(250000, true)` → **re-source `clk_peri` from `clk_sys`** via `clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, f, f)`. Without the re-source the SPI peripheral has no clock and the LCD is dead. Binaries are `pico_set_binary_type(... copy_to_ram)`; all code+data+bss live in 512 KB SRAM — large buffers go in PSRAM (`PSRAM_BASE 0x11000000`).
- **Diagnostics = SEGGER RTT only** (channel 0, `DIAG(...)` macro). No UART/USB stdio. `SEGGER_RTT_printf` supports `%d %u %x %s %c` — **no floats**.
- **DMA_IRQ_0 is shared.** The ST7796 flush uses `irq_add_shared_handler(DMA_IRQ_0, ...)` and guards on its own channel. Never use `irq_set_exclusive_handler` on `DMA_IRQ_0`.
- **Shared SPI1 / GPIO8 dual-function.** `PIN_LCD_DC = 8` doubles as `PIN_CC1101_MISO`; `PIN_CC1101_CS = 40` is parked HIGH in `board_init`. (Radio itself is deferred, but the parking + pin facts stay.)
- **LED count = 16.** `FW2_LED_COUNT`/`WS2812_NUM_PIXELS` = 16. `FwDisplayVibe.md` says 7 and is WRONG — the verified board header wins; the discrepancy is recorded in `docs/hardware/facts.md`.
- **Cross-platform.** All tooling works on Windows (PowerShell) and Linux. Host tests need no hardware or Pico SDK.
- **Naming deviation from spec (deliberate).** The spec proposed an `fw2_` prefix. Because we are harvesting a large, already-consistent codebase (`st7796_*`, `ft6336_*`, `ws2812_*`, `board_*`, `ioexp_*`, `psram_*`), forcing a rename is churn that fights the "minimal tokens / harvest proven code" goal. We KEEP the proven driver names and expose one umbrella header `bsp/fw2.h` that includes them. The `fw2_` convention applies only to new BSP-level convenience code. This is documented in `AGENTS.md`.
- **Source of truth for harvest:** `C:/~prj/Dropbox/vibeProjects/subghz` (owner's repo). Directory layout under `bsp/` mirrors `subghz/src/` (`platform/`, `display/`, `input/`, `leds/`, `boards/`) so the existing `#include "platform/board.h"`-style includes need no edits — the bsp include root is `bsp/`.

---

## File Structure

```
wilibsp/
  CMakeLists.txt                 # top-level: board, pico_sdk_init, add bsp + apps
  CMakePresets.json              # configure/build presets (target + host-test)
  pico_sdk_import.cmake          # copied from subghz
  .gitignore
  README.md
  AGENTS.md                      # agent entry point
  CLAUDE.md                      # thin pointer to AGENTS.md
  bsp/
    CMakeLists.txt               # defines freewili2_bsp INTERFACE/STATIC lib
    fw2.h                        # umbrella public header
    boards/freewili2.h           # SDK board-detection header (PICO_RP2350A=0)
    platform/  board.c/.h ioexp.c/.h psram.c/.h psram_layout.h spi_bus.c/.h diag.h
    display/   st7796.c/.h font5x7.c/.h
    input/     ft6336.c/.h ft6336_map.c/.h
    leds/      led_color.c/.h ws2812_driver.c/.h ws2812.pio led_ui.c/.h
    third_party/segger_rtt/  SEGGER_RTT.c/.h SEGGER_RTT_Conf*.h SEGGER_RTT_printf.c
  apps/
    template/     CMakeLists.txt main.c README.md
    hello_display/CMakeLists.txt main.c README.md
  tools/
    fw                           # POSIX launcher (python3 fw.py "$@")
    fw.cmd                       # Windows launcher (python fw.py %*)
    fw.py                        # the CLI
    tests/test_fw.py             # pytest for the CLI
    openocd/freewili2.cfg        # cmsis-dap + rp2350 target cfg
  docs/
    hardware/pinmap.md facts.md catalog.md
    drivers/display.md touch.md leds.md platform.md
  skills/
    freewili2-new-app/SKILL.md
    freewili2-add-driver/SKILL.md
  tests/
    CMakeLists.txt               # host CTest tree (HOST_TEST)
    test_led_color.c
```

---

## Task 1: Repo skeleton, git init, and CMake spine (configures cleanly)

**Files:**
- Create: `.gitignore`, `README.md`, `pico_sdk_import.cmake`, `CMakeLists.txt`, `CMakePresets.json`, `bsp/boards/freewili2.h`, `bsp/CMakeLists.txt`, `bsp/fw2.h`
- Reference (copy from): `C:/~prj/Dropbox/vibeProjects/subghz/pico_sdk_import.cmake`, `.../src/boards/freewili2.h`

**Interfaces:**
- Consumes: nothing (first task).
- Produces: CMake target `freewili2_bsp` (empty INTERFACE lib for now, include root `bsp/`); top-level `PICO_BOARD=freewili2`; presets `target` and `host-test`.

- [ ] **Step 1: Initialize git and ignore build output**

Run:
```bash
cd "C:/~prj/Dropbox/vibeProjects/wilibsp"
git init
```
Create `.gitignore`:
```gitignore
build/
build-*/
__pycache__/
*.uf2
*.elf
*.bin
.venv/
```

- [ ] **Step 2: Copy the SDK import shim and board header verbatim**

```bash
cp "C:/~prj/Dropbox/vibeProjects/subghz/pico_sdk_import.cmake" ./pico_sdk_import.cmake
mkdir -p bsp/boards
cp "C:/~prj/Dropbox/vibeProjects/subghz/src/boards/freewili2.h" bsp/boards/freewili2.h
```

- [ ] **Step 3: Write the top-level `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.13)

# RP2350B board header (PICO_RP2350A=0 -> 48 GPIO, 16MB flash).
# Do NOT pass -DPICO_BOARD on the command line — that overrides this.
set(PICO_BOARD freewili2 CACHE STRING "Board type")
list(APPEND PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/bsp/boards")

include(pico_sdk_import.cmake)
project(wilibsp C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_subdirectory(bsp)

# Each app opts in; add new apps here.
add_subdirectory(apps/hello_display)
```
(Note: `apps/hello_display` is created in Task 8; until then, comment that line out or it will error. Leave it commented with a `# TODO(Task 8)` marker.)

- [ ] **Step 4: Write `bsp/CMakeLists.txt` as an INTERFACE lib (drivers added in later tasks)**

```cmake
# freewili2_bsp — shared board-support library.
# Starts as INTERFACE (headers/include-root only); driver .c files and their
# pico_sdk deps are added by Tasks 3-6 as the sources land.
add_library(freewili2_bsp INTERFACE)

target_include_directories(freewili2_bsp INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/segger_rtt)
```

- [ ] **Step 5: Write `bsp/fw2.h` umbrella header (grows as drivers land)**

```c
// bsp/fw2.h — umbrella include for the FreeWili2 BSP public API.
// Include this from an app to pull in the board + drivers.
// Naming: harvested drivers keep their proven names (st7796_*, ft6336_*,
// ws2812_*, board_*). See AGENTS.md.
#ifndef FW2_H
#define FW2_H

#include "platform/board.h"   // (Task 3)
// #include "display/st7796.h" // (Task 4)
// #include "input/ft6336.h"   // (Task 5)
// #include "leds/ws2812_driver.h" // (Task 6)

#endif // FW2_H
```
Keep the not-yet-created includes commented; uncomment each as its task lands.

- [ ] **Step 6: Write `CMakePresets.json`**

```json
{
  "version": 3,
  "cmakeMinimumRequired": { "major": 3, "minor": 21, "patch": 0 },
  "configurePresets": [
    {
      "name": "target",
      "displayName": "FreeWili2 target (RP2350B)",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "RelWithDebInfo" }
    },
    {
      "name": "host-test",
      "displayName": "Host unit tests",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build-tests",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug", "HOST_TEST": "ON" }
    }
  ],
  "buildPresets": [
    { "name": "target", "configurePreset": "target" },
    { "name": "host-test", "configurePreset": "host-test" }
  ]
}
```

- [ ] **Step 7: Verify configure succeeds**

Run:
```bash
cmake --preset target
```
Expected: configuration completes, `build/` created, `freewili2_bsp` target defined, no errors. (No app is built yet — the `add_subdirectory(apps/hello_display)` line stays commented until Task 8.)

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "chore: repo skeleton, board header, CMake spine, presets"
```

---

## Task 2: The `fw` CLI (TDD in Python)

The CLI is the one piece of genuinely new logic, so it gets real unit tests. `new-app` is pure file I/O (fully testable). `build/flash/rtt/test` are thin subprocess wrappers verified via a `--print` dry-run that emits the command instead of running it.

**Files:**
- Create: `tools/fw.py`, `tools/fw` (POSIX launcher), `tools/fw.cmd` (Windows launcher), `tools/tests/test_fw.py`

**Interfaces:**
- Consumes: `CMakePresets.json` preset names `target` / `host-test` (Task 1).
- Produces: CLI commands `fw build [app]`, `fw flash [app]`, `fw rtt`, `fw test`, `fw new-app <name>`. `build_command(app)`, `flash_command(app)`, `test_command()`, `new_app(name, repo_root)` are importable functions returning `list[str]` (commands) or the created path (`new_app`).

- [ ] **Step 1: Write the failing test for `new_app`**

Create `tools/tests/test_fw.py`:
```python
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import fw

def test_new_app_copies_template(tmp_path):
    root = tmp_path
    (root / "apps" / "template").mkdir(parents=True)
    (root / "apps" / "template" / "CMakeLists.txt").write_text(
        'add_executable(template main.c)\n')
    (root / "apps" / "template" / "main.c").write_text("// template\n")

    dest = fw.new_app("blinky", repo_root=root)

    assert dest == root / "apps" / "blinky"
    assert (dest / "main.c").exists()
    # the app target must be renamed from 'template' to the new name
    assert "add_executable(blinky" in (dest / "CMakeLists.txt").read_text()

def test_new_app_rejects_existing(tmp_path):
    root = tmp_path
    (root / "apps" / "blinky").mkdir(parents=True)
    try:
        fw.new_app("blinky", repo_root=root)
        assert False, "expected FileExistsError"
    except FileExistsError:
        pass

def test_build_command_uses_target_preset():
    cmd = fw.build_command("hello_display")
    assert cmd[:3] == ["cmake", "--build", "--preset"]
    assert "target" in cmd

def test_test_command_configures_and_runs_ctest():
    cmds = fw.test_command()
    assert cmds[0][:2] == ["cmake", "--preset"]
    assert cmds[-1][0] == "ctest"
```

- [ ] **Step 2: Run the tests to verify they fail**

Run:
```bash
cd "C:/~prj/Dropbox/vibeProjects/wilibsp"
python -m pytest tools/tests/test_fw.py -v
```
Expected: FAIL — `ModuleNotFoundError: No module named 'fw'` (fw.py not created yet).

- [ ] **Step 3: Implement `tools/fw.py`**

```python
#!/usr/bin/env python3
"""fw — FreeWili2 BSP task runner (cross-platform).

Commands:
  fw build [app]     configure+build an app for the RP2350B target (default hello_display)
  fw flash [app]     program the app over the cmsis-dap debug probe via OpenOCD
  fw rtt             stream SEGGER RTT diagnostics
  fw test            build+run host unit tests (CTest, no hardware)
  fw new-app <name>  scaffold apps/<name> from apps/template
Add --print to any build/flash/test command to print the command(s) instead of running.
"""
import argparse, pathlib, shutil, subprocess, sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_APP = "hello_display"

def build_command(app):
    return ["cmake", "--build", "--preset", "target", "--target", app]

def flash_command(app):
    elf = f"build/apps/{app}/{app}.elf"
    cfg = str(REPO_ROOT / "tools" / "openocd" / "freewili2.cfg")
    return ["openocd", "-f", cfg,
            "-c", f"program {elf} verify reset exit"]

def rtt_command():
    cfg = str(REPO_ROOT / "tools" / "openocd" / "freewili2.cfg")
    return ["openocd", "-f", cfg,
            "-c", "rtt setup 0x20000000 0x40000 \"SEGGER RTT\"",
            "-c", "init", "-c", "rtt start", "-c", "rtt server start 9090 0"]

def test_command():
    return [
        ["cmake", "--preset", "host-test"],
        ["cmake", "--build", "--preset", "host-test"],
        ["ctest", "--test-dir", "build-tests", "--output-on-failure"],
    ]

def new_app(name, repo_root=REPO_ROOT):
    src = pathlib.Path(repo_root) / "apps" / "template"
    dest = pathlib.Path(repo_root) / "apps" / name
    if dest.exists():
        raise FileExistsError(dest)
    shutil.copytree(src, dest)
    cml = dest / "CMakeLists.txt"
    cml.write_text(cml.read_text().replace("template", name))
    return dest

def _run(cmds, do_print):
    if isinstance(cmds[0], str):
        cmds = [cmds]
    for c in cmds:
        if do_print:
            print(" ".join(c))
        else:
            subprocess.run(c, cwd=REPO_ROOT, check=True)

def main(argv=None):
    p = argparse.ArgumentParser(prog="fw")
    sub = p.add_subparsers(dest="cmd", required=True)
    for name in ("build", "flash"):
        sp = sub.add_parser(name); sp.add_argument("app", nargs="?", default=DEFAULT_APP)
        sp.add_argument("--print", dest="show", action="store_true")
    sp = sub.add_parser("rtt"); sp.add_argument("--print", dest="show", action="store_true")
    sp = sub.add_parser("test"); sp.add_argument("--print", dest="show", action="store_true")
    sp = sub.add_parser("new-app"); sp.add_argument("name")

    a = p.parse_args(argv)
    if a.cmd == "build":   _run(build_command(a.app), a.show)
    elif a.cmd == "flash": _run(flash_command(a.app), a.show)
    elif a.cmd == "rtt":   _run(rtt_command(), a.show)
    elif a.cmd == "test":  _run(test_command(), a.show)
    elif a.cmd == "new-app":
        print("created", new_app(a.name))
    return 0

if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Run the tests to verify they pass**

Run:
```bash
python -m pytest tools/tests/test_fw.py -v
```
Expected: 4 passed.

- [ ] **Step 5: Add the OS launchers**

`tools/fw` (POSIX, make executable with `chmod +x`):
```sh
#!/bin/sh
exec python3 "$(dirname "$0")/fw.py" "$@"
```
`tools/fw.cmd` (Windows):
```bat
@echo off
python "%~dp0fw.py" %*
```

- [ ] **Step 6: Smoke-test the dry-run path end to end**

Run:
```bash
python tools/fw.py build --print
python tools/fw.py test --print
```
Expected: prints `cmake --build --preset target --target hello_display` and the three host-test commands. No hardware touched.

- [ ] **Step 7: Commit**

```bash
git add tools/
git commit -m "feat: cross-platform fw CLI (build/flash/rtt/test/new-app) with tests"
```

---

## Task 3: Platform core driver (board, ioexp, psram, spi_bus, diag + SEGGER RTT)

Harvest the interdependent platform layer as one cohesive unit — nothing builds without it. These are verbatim copies; the only edits are CMake wiring. On-hardware verification happens in Task 9.

**Files:**
- Create (copy verbatim from `subghz/src/platform/` and `subghz/third_party/`):
  `bsp/platform/{board.c,board.h,ioexp.c,ioexp.h,psram.c,psram.h,psram_layout.h,spi_bus.c,spi_bus.h,diag.h}`,
  `bsp/third_party/segger_rtt/{SEGGER_RTT.c,SEGGER_RTT.h,SEGGER_RTT_Conf.h,SEGGER_RTT_ConfDefaults.h,SEGGER_RTT_printf.c}`
- Modify: `bsp/CMakeLists.txt`, `bsp/fw2.h`

**Interfaces:**
- Consumes: `freewili2_bsp` include root (Task 1).
- Produces (public API, unchanged from source): `void board_init(void)`, `void board_backlight_set(uint8_t)`, `void board_i2c1_init(void)`; `bool ioexp_init(void)`, `void ioexp_antenna(uint8_t)`; `size_t psram_init(void)`, `int psram_selftest(size_t)`; `void spi_bus_acquire_cc1101(void)` etc.; `DIAG(...)` macro. Pin macros (`PIN_LCD_*`, `PIN_I2C1_*`, `PIN_LED_DATA`, `BOARD_SYS_CLOCK_KHZ`, ...) from `platform/board.h`.

- [ ] **Step 1: Copy platform sources and the SEGGER RTT vendor tree verbatim**

```bash
cd "C:/~prj/Dropbox/vibeProjects/wilibsp"
S="C:/~prj/Dropbox/vibeProjects/subghz"
mkdir -p bsp/platform bsp/third_party/segger_rtt
cp "$S/src/platform/"{board.c,board.h,ioexp.c,ioexp.h,psram.c,psram.h,psram_layout.h,spi_bus.c,spi_bus.h,diag.h} bsp/platform/
cp "$S/third_party/segger_rtt/"* bsp/third_party/segger_rtt/
```

- [ ] **Step 2: Convert `freewili2_bsp` to a STATIC lib and add platform sources**

Replace `bsp/CMakeLists.txt` with:
```cmake
# freewili2_bsp — shared board-support library (STATIC once it has sources).
add_library(freewili2_bsp STATIC
    platform/board.c
    platform/ioexp.c
    platform/psram.c
    platform/spi_bus.c
    third_party/segger_rtt/SEGGER_RTT.c
    third_party/segger_rtt/SEGGER_RTT_printf.c)

target_include_directories(freewili2_bsp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/segger_rtt)

target_link_libraries(freewili2_bsp PUBLIC
    pico_stdlib
    hardware_i2c
    hardware_spi
    hardware_dma
    hardware_pio
    hardware_clocks
    hardware_vreg)
```

- [ ] **Step 3: Uncomment the platform include in `bsp/fw2.h`**

Ensure `#include "platform/board.h"` is active (it already is from Task 1 Step 5).

- [ ] **Step 4: Verify the BSP library compiles for target**

Run:
```bash
cmake --preset target
cmake --build build --target freewili2_bsp
```
Expected: `freewili2_bsp` archive builds with no errors. (Warnings from vendor RTT are acceptable.)

- [ ] **Step 5: Commit**

```bash
git add bsp/
git commit -m "feat(bsp): platform core — board init/clocks, ioexp, psram, spi_bus, RTT diag"
```

---

## Task 4: Display driver (ST7796 + DMA + font)

**Files:**
- Create (copy verbatim from `subghz/src/display/`): `bsp/display/{st7796.c,st7796.h,font5x7.c,font5x7.h}`
- Modify: `bsp/CMakeLists.txt`, `bsp/fw2.h`

**Interfaces:**
- Consumes: platform core (Task 3) — uses `board.h` pins, DMA_IRQ_0 shared-handler model.
- Produces: `void st7796_init(void)`, `void st7796_fill_screen(uint16_t be)`, `void st7796_fill_rect(int,int,int,int,uint16_t be)`, `void st7796_blit_rect(...)`, `void st7796_draw_text(int,int,int,uint16_t,uint16_t,const char*)`, async: `void st7796_flush_async(...)`, `bool st7796_flush_busy(void)`; `#define ST7796_W 480`, `ST7796_H 320`.

- [ ] **Step 1: Copy display sources verbatim**

```bash
S="C:/~prj/Dropbox/vibeProjects/subghz"
mkdir -p bsp/display
cp "$S/src/display/"{st7796.c,st7796.h,font5x7.c,font5x7.h} bsp/display/
```

- [ ] **Step 2: Add display sources to `bsp/CMakeLists.txt`**

Add to the `add_library(freewili2_bsp STATIC ...)` source list:
```cmake
    display/st7796.c
    display/font5x7.c
```

- [ ] **Step 3: Activate the display include in `bsp/fw2.h`**

Change `// #include "display/st7796.h"` → `#include "display/st7796.h"`.

- [ ] **Step 4: Verify the library still builds for target**

Run:
```bash
cmake --build build --target freewili2_bsp
```
Expected: builds clean; `st7796.o` and `font5x7.o` produced.

- [ ] **Step 5: Commit**

```bash
git add bsp/
git commit -m "feat(bsp): ST7796 480x320 display driver (DMA async flush + 5x7 font)"
```

---

## Task 5: Touch driver (FT6336U)

**Files:**
- Create (copy verbatim from `subghz/src/input/`): `bsp/input/{ft6336.c,ft6336.h,ft6336_map.c,ft6336_map.h}`
- Modify: `bsp/CMakeLists.txt`, `bsp/fw2.h`

**Interfaces:**
- Consumes: platform core I2C1 (`board_i2c1_init`, Task 3).
- Produces: `bool ft6336_init(void)`, `bool ft6336_poll(uint16_t* x, uint16_t* y)` (coords oriented to 480×320). `ft6336_map.*` holds pure host-testable coordinate mapping.

- [ ] **Step 1: Copy touch sources verbatim**

```bash
S="C:/~prj/Dropbox/vibeProjects/subghz"
mkdir -p bsp/input
cp "$S/src/input/"{ft6336.c,ft6336.h,ft6336_map.c,ft6336_map.h} bsp/input/
```

- [ ] **Step 2: Add to `bsp/CMakeLists.txt` source list**

```cmake
    input/ft6336.c
    input/ft6336_map.c
```

- [ ] **Step 3: Activate the include in `bsp/fw2.h`**

Change `// #include "input/ft6336.h"` → `#include "input/ft6336.h"`.

- [ ] **Step 4: Verify build for target**

Run:
```bash
cmake --build build --target freewili2_bsp
```
Expected: builds clean.

- [ ] **Step 5: Commit**

```bash
git add bsp/
git commit -m "feat(bsp): FT6336U capacitive touch driver (polled I2C, 480x320 oriented)"
```

---

## Task 6: LED driver (16× WS2812 + PIO)

**Files:**
- Create (copy verbatim from `subghz/src/leds/`): `bsp/leds/{led_color.c,led_color.h,ws2812_driver.c,ws2812_driver.h,ws2812.pio,led_ui.c,led_ui.h}`
- Modify: `bsp/CMakeLists.txt`, `bsp/fw2.h`

**Interfaces:**
- Consumes: platform core; `hardware_pio`, `pico_generate_pio_header`.
- Produces: `void ws2812_init(PIO,uint,uint)`, `void ws2812_set_pixel(uint,rgb_t)`, `void ws2812_fill(rgb_t)`, `void ws2812_clear(void)`, `void ws2812_set_brightness(uint8_t)`, `void ws2812_show(void)`, `#define WS2812_NUM_PIXELS 16`; `rgb_t{r,g,b}`, `uint32_t led_color_pack_grb(rgb_t)`, `uint8_t led_color_scale(uint8_t,uint8_t)`; pure `led_fade_step`, `led_spectrum_map`.

- [ ] **Step 1: Copy LED sources verbatim**

```bash
S="C:/~prj/Dropbox/vibeProjects/subghz"
mkdir -p bsp/leds
cp "$S/src/leds/"{led_color.c,led_color.h,ws2812_driver.c,ws2812_driver.h,ws2812.pio,led_ui.c,led_ui.h} bsp/leds/
```

- [ ] **Step 2: Wire PIO header generation + sources in `bsp/CMakeLists.txt`**

Add to the source list:
```cmake
    leds/led_color.c
    leds/ws2812_driver.c
    leds/led_ui.c
```
And after the `target_link_libraries(...)` block add:
```cmake
# Generate the WS2812 PIO header into the build tree for ws2812_driver.c.
pico_generate_pio_header(freewili2_bsp
    ${CMAKE_CURRENT_SOURCE_DIR}/leds/ws2812.pio)
```

- [ ] **Step 3: Confirm `FW2_LED_COUNT` alias exists (guard the 16-LED constant)**

In `bsp/leds/ws2812_driver.h`, immediately after `#define WS2812_NUM_PIXELS 16`, add:
```c
// BSP-wide alias for the LED count (single source of truth = 16, verified board).
#define FW2_LED_COUNT WS2812_NUM_PIXELS
```

- [ ] **Step 4: Activate the include in `bsp/fw2.h`**

Change `// #include "leds/ws2812_driver.h"` → `#include "leds/ws2812_driver.h"`.

- [ ] **Step 5: Verify build for target**

Run:
```bash
cmake --preset target   # re-run configure so pico_generate_pio_header registers
cmake --build build --target freewili2_bsp
```
Expected: `ws2812.pio.h` generated, library builds clean.

- [ ] **Step 6: Commit**

```bash
git add bsp/
git commit -m "feat(bsp): WS2812 16-LED driver (PIO, inverted) + led_color/led_ui"
```

---

## Task 7: Host test harness (CTest, no hardware)

Prove the cross-platform host-test path works by porting one existing pure-logic test. This validates `fw test` end to end.

**Files:**
- Create: `tests/CMakeLists.txt`, `tests/test_led_color.c` (ported from `subghz/tests/test_led_color.c`)
- Reference: `subghz/tests/{test_led_color.c,test_util.h,CMakeLists.txt}`

**Interfaces:**
- Consumes: `bsp/leds/led_color.c` (Task 6) compiled on host.
- Produces: CTest target `test_led_color`; `fw test` returns green.

- [ ] **Step 1: Write the host test CMake tree**

`tests/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.13)
project(wilibsp_tests C)
enable_testing()
set(CMAKE_C_STANDARD 11)

# Pure modules compile identically on host under HOST_TEST.
add_compile_definitions(HOST_TEST=1)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../bsp)

add_executable(test_led_color
    test_led_color.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp/leds/led_color.c)
add_test(NAME test_led_color COMMAND test_led_color)
```

- [ ] **Step 2: Make the top-level build include tests under the host-test preset**

Append to the top-level `CMakeLists.txt`, guarded so it only adds under the host-test preset (which sets `HOST_TEST=ON`):
```cmake
if(HOST_TEST)
    enable_testing()
    add_subdirectory(tests)
endif()
```
Note: under the `host-test` preset we do NOT call `pico_sdk_init()` paths for these targets — the test tree is self-contained C. If the top-level `project()/pico_sdk_init()` cannot run without the SDK on a bare Linux box, split the host-test tree to configure `tests/` directly (`cmake -S tests -B build-tests`). Update `fw.test_command()` to `cmake -S tests -B build-tests` / `cmake --build build-tests` / `ctest --test-dir build-tests` and adjust the Task 2 test accordingly if this split is needed.

- [ ] **Step 3: Port the test file**

Copy `subghz/tests/test_led_color.c` to `tests/test_led_color.c`. If it includes `test_util.h`, either copy that too or inline the assert macro. It must exercise `led_color_pack_grb` (expect `(g<<16)|(r<<8)|b`) and `led_color_scale` (rounded scaling). Keep it dependency-free (no SDK includes).

- [ ] **Step 4: Run the host tests via the CLI**

Run:
```bash
python tools/fw.py test
```
Expected: configures `build-tests/`, builds, `ctest` reports `100% tests passed, 0 failed` for `test_led_color`.

- [ ] **Step 5: Commit**

```bash
git add tests/ CMakeLists.txt tools/
git commit -m "test: host CTest harness + led_color unit test via fw test"
```

---

## Task 8: App template + `hello_display` smoke app (builds a UF2)

**Files:**
- Create: `apps/template/{CMakeLists.txt,main.c,README.md}`, `apps/hello_display/{CMakeLists.txt,main.c,README.md}`
- Modify: top-level `CMakeLists.txt` (uncomment `add_subdirectory(apps/hello_display)`)

**Interfaces:**
- Consumes: `freewili2_bsp` + `fw2.h` full API (Tasks 3-6).
- Produces: `build/apps/hello_display/hello_display.uf2` and `.elf`; `apps/template` consumable by `fw new-app`.

- [ ] **Step 1: Write the template app CMake (target literally named `template`)**

`apps/template/CMakeLists.txt`:
```cmake
# App target name MUST be the single token 'template' — `fw new-app <name>`
# rewrites every 'template' occurrence to the new app name.
add_executable(template main.c)
target_link_libraries(template freewili2_bsp)
pico_set_binary_type(template copy_to_ram)   # required: firmware runs from SRAM
pico_add_extra_outputs(template)             # .uf2 / .bin / .map
```

`apps/template/main.c`:
```c
#include "fw2.h"

int main(void) {
    board_init();
    st7796_init();
    board_backlight_set(1);
    st7796_fill_screen(0x0000);
    st7796_draw_text(8, 8, 2, 0xFFFF, 0x0000, "HELLO FREEWILI2");
    for (;;) { tight_loop_contents(); }
}
```
`apps/template/README.md`: one paragraph — "Starter app. Copy with `fw new-app <name>`. Links `freewili2_bsp`; include `fw2.h`."

- [ ] **Step 2: Create `hello_display` as the smoke test (display + touch + LEDs)**

`apps/hello_display/CMakeLists.txt`:
```cmake
add_executable(hello_display main.c)
target_link_libraries(hello_display freewili2_bsp)
pico_set_binary_type(hello_display copy_to_ram)
pico_add_extra_outputs(hello_display)
```

`apps/hello_display/main.c`:
```c
// hello_display — v1 on-hardware smoke test: display renders, touch responds,
// LEDs light. Diagnostics over SEGGER RTT (fw rtt).
#include "fw2.h"
#include "hardware/pio.h"
#include "platform/diag.h"

int main(void) {
    board_init();
    st7796_init();
    board_backlight_set(1);

    ws2812_init(pio1, 0, PIN_LED_DATA);
    ws2812_set_brightness(64);
    rgb_t green = { .r = 0, .g = 255, .b = 0 };
    ws2812_fill(green);
    ws2812_show();

    ft6336_init();

    st7796_fill_screen(0x0000);
    st7796_draw_text(8, 8, 2, 0xFFFF, 0x0000, "TOUCH THE SCREEN");
    DIAG("hello_display up: sys=%u kHz\n", BOARD_SYS_CLOCK_KHZ);

    uint16_t x, y;
    for (;;) {
        if (ft6336_poll(&x, &y)) {
            st7796_fill_rect(x - 4, y - 4, 8, 8, 0xE0FF /* red-ish BE */);
            DIAG("touch %u,%u\n", x, y);
        }
    }
}
```

`apps/hello_display/README.md`: "v1 smoke test. `fw build hello_display && fw flash hello_display`, then `fw rtt`. Expect white text, green LEDs, red dots under touches."

- [ ] **Step 3: Enable the app in the top-level build**

Uncomment/confirm in top-level `CMakeLists.txt`:
```cmake
add_subdirectory(apps/hello_display)
```
(The `template` app is intentionally NOT added to the top-level build — it is a scaffolding source, and its bare `template` target name would collide after `new-app`.)

- [ ] **Step 4: Build the app for target**

Run:
```bash
python tools/fw.py build hello_display
```
Expected: `build/apps/hello_display/hello_display.uf2` and `.elf` produced, no errors.

- [ ] **Step 5: Verify `fw new-app` works against the real template**

Run:
```bash
python tools/fw.py new-app scratch_demo
ls apps/scratch_demo   # CMakeLists.txt (target renamed to scratch_demo) + main.c + README.md
rm -rf apps/scratch_demo
```
Expected: files present, `add_executable(scratch_demo` in the copied CMake. (Removed after checking — it's just a scaffolding smoke test.)

- [ ] **Step 6: Commit**

```bash
git add apps/ CMakeLists.txt
git commit -m "feat(apps): template + hello_display smoke app (builds copy_to_ram UF2)"
```

---

## Task 9: On-hardware smoke verification

The acceptance gate for all driver tasks. Requires the board + cmsis-dap probe attached.

**Files:**
- Create: `tools/openocd/freewili2.cfg`
- Reference: `subghz/tools/*.ps1` for the exact OpenOCD invocation/target names.

**Interfaces:**
- Consumes: `hello_display.elf` (Task 8), `fw flash` / `fw rtt` (Task 2).
- Produces: verified working board; confirmation the clock/DMA/I2C/PIO bring-up is correct on real silicon.

- [ ] **Step 1: Write the OpenOCD config for the probe + target**

`tools/openocd/freewili2.cfg` (adapt from subghz's flash script — confirm interface/target names it uses):
```tcl
source [find interface/cmsis-dap.cfg]
source [find target/rp2350.cfg]
adapter speed 5000
```
If subghz's scripts pass extra `-c` init for RP2350B (e.g. `set USE_CORE 0`), mirror them here.

- [ ] **Step 2: Flash the smoke app**

Run:
```bash
python tools/fw.py flash hello_display
```
Expected: OpenOCD connects to `cmsis-dap`, programs + verifies + resets. Board reboots into the app.

- [ ] **Step 3: Observe RTT diagnostics**

Run:
```bash
python tools/fw.py rtt
```
Expected: `hello_display up: sys=250000 kHz` appears; touching the screen prints `touch X,Y` lines.

- [ ] **Step 4: Visually verify the hardware**

Confirm on the board: backlight on, white "TOUCH THE SCREEN" text rendered (display + DMA OK), 16 LEDs green (WS2812 PIO + count OK), red dots follow finger touches (FT6336 I2C + orientation OK).

- [ ] **Step 5: Commit the OpenOCD config**

```bash
git add tools/openocd/
git commit -m "chore: OpenOCD cmsis-dap config; verified hello_display on hardware"
```

---

## Task 10: Agent enablement layer (docs + skills)

Turns the working repo into something an agent can drive with minimal tokens. Written last so every path, command, and API it references is already real and verified.

**Files:**
- Create: `AGENTS.md`, `CLAUDE.md`, `README.md` (flesh out the stub), `docs/hardware/{pinmap.md,facts.md,catalog.md}`, `docs/drivers/{platform.md,display.md,touch.md,leds.md}`, `skills/freewili2-new-app/SKILL.md`, `skills/freewili2-add-driver/SKILL.md`
- Reference: `subghz/AGENTS.md` (structure + hard-won facts), `bsp/platform/board.h` (pin table source).

**Interfaces:**
- Consumes: the entire repo (final task).
- Produces: no code; the human/agent entry points.

- [ ] **Step 1: Write `AGENTS.md`**

Dense orientation modeled on `subghz/AGENTS.md`. Required sections:
  - **What this is** — FreeWili2 (RP2350B) BSP monorepo; `bsp/` shared lib + `apps/`.
  - **Command vocabulary** — `fw build|flash|rtt|test|new-app` (one table, both OSes identical).
  - **Invariants** — copy the Global Constraints from this plan verbatim (clock re-source, copy_to_ram, RTT-only, DMA_IRQ_0 shared, shared SPI/GPIO8, 16 LEDs, PICO_BOARD in CMake only).
  - **How to add a driver** — copy from an owner repo into `bsp/<domain>/`, add to `bsp/CMakeLists.txt`, activate in `fw2.h`, mirror include layout so `#include "domain/x.h"` resolves; update `docs/hardware/catalog.md`.
  - **Where things live** — pin map (`docs/hardware/pinmap.md` + authoritative `bsp/platform/board.h`), driver docs, catalog of done-vs-TODO.
  - **Naming note** — harvested drivers keep proven names; `fw2.h` is the umbrella.

- [ ] **Step 2: Write `CLAUDE.md` as a thin pointer**

```markdown
# CLAUDE.md
This repo's agent guidance lives in [AGENTS.md](./AGENTS.md). Read it first.
```

- [ ] **Step 3: Write `docs/hardware/pinmap.md`**

A grid/table an agent can grep, generated from `bsp/platform/board.h` + `FwDisplayVibe.md`: each row = signal, GPIO, peripheral, notes. Include the full board peripheral inventory (display, touch, LEDs, radio, NFC, IR, DVI, audio, mics, buttons, PIO-USB, sensors) with GPIOs, marking which have drivers.

- [ ] **Step 4: Write `docs/hardware/facts.md`**

The hard-won invariants (from Global Constraints) + the **LED discrepancy record**: "FwDisplayVibe.md says 7 WS LEDs; verified board header (`bsp/platform/board.h`, `bsp/leds/ws2812_driver.h`) says 16. 16 is authoritative." + "No LCD reset GPIO — SWRESET only; RESX is hardware/ioexp-handled."

- [ ] **Step 5: Write `docs/hardware/catalog.md` (peripheral status)**

Table: peripheral → driver status (`DONE` platform/display/touch/leds; `TODO` radio/CC1101, NFC/ST25R3916B, IR, DVI/HSTX, I2S/NAU88C10, PDM mics, buttons coprocessor, Pico-PIO-USB, OPT4001, SHT40, BMI323, BMM350) → source repo to harvest from (e.g. subghz for radio, sensorview for sensors, usbcamfw/wili8c for USB/audio).

- [ ] **Step 6: Write per-driver usage docs**

`docs/drivers/{platform,display,touch,leds}.md`: for each, a 10-20 line "what it does / how to use it / dependencies" with a minimal code snippet lifted from `hello_display`.

- [ ] **Step 7: Write the two Claude Code skills**

`skills/freewili2-new-app/SKILL.md` — frontmatter `name: freewili2:new-app`, `description: Use when scaffolding a new FreeWili2 app in this repo`. Body: run `fw new-app <name>`, add `add_subdirectory(apps/<name>)` to top-level CMake, edit `main.c` against `fw2.h`, `fw build <name>`.

`skills/freewili2-add-driver/SKILL.md` — frontmatter `name: freewili2:add-driver`. Body: the "How to add a driver" procedure from AGENTS.md, plus "find the source in the owner repo listed in `docs/hardware/catalog.md`, copy into `bsp/<domain>/`, wire CMake + fw2.h, update catalog."

- [ ] **Step 8: Flesh out `README.md`**

Human-facing: what the board is, quick start (`fw build && fw flash && fw rtt`), repo map, link to AGENTS.md and the spec.

- [ ] **Step 9: Verify docs are internally consistent**

Grep the docs for the command names and pin values; confirm they match `tools/fw.py` and `bsp/platform/board.h`. Confirm `catalog.md` marks exactly platform/display/touch/leds as DONE.

- [ ] **Step 10: Commit**

```bash
git add AGENTS.md CLAUDE.md README.md docs/ skills/
git commit -m "docs: agent enablement layer — AGENTS.md, hardware docs, driver docs, skills"
```

---

## Self-Review

**Spec coverage** (each spec section → task):
- §3 repo architecture → Task 1 (spine), Tasks 3-6 (bsp dirs), Task 8 (apps).
- §4 BSP conventions/invariants → Global Constraints + Task 3 (board init) + Task 10 §facts.
- §5 agent layer (AGENTS/CLAUDE/docs/skills) → Task 10.
- §6 `fw` CLI cross-platform → Task 2.
- §7 testing (host + smoke) → Task 7 (host CTest), Tasks 8-9 (smoke).
- §8 v1 driver set (platform, display, touch, 16 LEDs) → Tasks 3, 4, 5, 6.
- §8 deferred peripherals documented → Task 10 §catalog.
- §9 LED discrepancy / LVGL optional / PSRAM budget → Global Constraints, facts.md (Task 10), template/apps avoid LVGL (Task 8).

**Placeholder scan:** No "TBD/TODO" left as work-avoidance. The two explicit `TODO(...)` markers are deliberate build-ordering guards (Task 1 Step 3 comment, resolved in Task 8) and are called out. Task 9 Step 1 and Task 7 Step 2 contain conditional "confirm/adjust" notes tied to facts only knowable at the machine (OpenOCD target names; whether the SDK top-level project can configure on a bare host) — these are genuine verify-on-execution branches with the fallback spelled out, not hand-waving.

**Type/name consistency:** Driver signatures in Interfaces blocks are copied verbatim from the harvested headers (`st7796_*`, `ft6336_*`, `ws2812_*`, `board_*`, `ioexp_*`, `psram_*`). CLI functions (`build_command`, `flash_command`, `test_command`, `new_app`) match between Task 2 tests, implementation, and Task 7/8 usage. `freewili2_bsp` target name, `fw2.h` umbrella, and `FW2_LED_COUNT` are used consistently across Tasks 1-8.
