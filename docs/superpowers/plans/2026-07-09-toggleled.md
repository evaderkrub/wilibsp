# toggleled Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A wilibsp app for the FreeWili 2 display CPU that toggles main-CPU GPIO 25 every 500 ms via the OneWili C API over the FwGUI display link.

**Architecture:** Copy the generated `onewili_fwgui` static library into `libs/onewili/` (new `libs/` tier for reusable non-BSP libraries), then scaffold `apps/toggleled` from the app template; `main.c` brings the board up, opens the FwGUI link (UART0, 8 Mbaud, HW flow control) with an RTT-diagnosed retry loop, and toggles main-CPU GPIO 25 in a 500 ms loop.

**Tech Stack:** Pico SDK (RP2350B), CMake, `fw` CLI (`python tools/fw.py`), SEGGER RTT diagnostics.

**Spec:** `docs/superpowers/specs/2026-07-09-toggleled-design.md`

## Global Constraints

- Repo root for all commands: `C:\~prj\Dropbox\vibeProjects\wilibsp` (branch `toggleled`).
- Every app binary uses `pico_set_binary_type(<app> copy_to_ram)` — firmware runs from 512 KB SRAM.
- NEVER pass `-DPICO_BOARD` on any cmake command line (board is set by the top-level CMakeLists cache).
- `board_init()` MUST run before `ow_open_fwgui()` — `uart_init` derives the baud divisor from `clk_peri` at call time, and `board_init()` re-sources `clk_peri` at 250 MHz.
- Diagnostics are RTT only: `DIAG(...)` from `bsp/platform/diag.h` (printf subset: `%d %u %x %s %c`, NO floats).
- No automated host tests for this feature (generated lib + I/O glue — per spec). Verification is compile-clean builds; on-hardware check is user-performed.
- The copied library must stay diffable against its generator output — no source edits beyond the one source-of-truth comment specified in Task 1.

---

### Task 1: Import the OneWili library as `libs/onewili`

**Files:**
- Create: `libs/onewili/` (copy of `C:\~prj\Dropbox\FreeWilli\master\freewili-firmware\freewilimain\testprojects\onewili\wilibsp` — `CMakeLists.txt`, `README.md`, `src/` 5 files, `include/` 6 headers, `examples/blink.c`)
- Modify: `CMakeLists.txt` (top-level, line 14 area — after `add_subdirectory(bsp)`)

**Interfaces:**
- Consumes: nothing (first task).
- Produces: CMake STATIC library target `onewili_fwgui` with PUBLIC include dir `libs/onewili/include`. Key API used by Task 2: `ow_status ow_open_fwgui(ow_device* dev);` (from `onewili_fwgui.h`), `ow_status ow_io_gpio_set_io_toggle(ow_device* dev, int32_t pin);` and enum value `OW_OK` (from `onewili.h`).

- [ ] **Step 1: Copy the library into the tree**

Run (Git Bash):
```bash
cd /c/~prj/Dropbox/vibeProjects/wilibsp
mkdir -p libs
cp -r "/c/~prj/Dropbox/FreeWilli/master/freewili-firmware/freewilimain/testprojects/onewili/wilibsp" libs/onewili
find libs/onewili -type f | sort
```
Expected: 14 files — `CMakeLists.txt`, `README.md`, `examples/blink.c`, `include/onewili.h`, `include/onewili_binary.h`, `include/onewili_binary_framing.h`, `include/onewili_enums.h`, `include/onewili_events.h`, `include/onewili_fwgui.h`, `src/binary_framing.c`, `src/binary_transport.c`, `src/onewili.c`, `src/onewili_events.c`, `src/onewili_fwgui.c`.

- [ ] **Step 2: Add the source-of-truth comment**

In `libs/onewili/CMakeLists.txt`, the file currently starts:

```cmake
# OneWili C API for WiliBSP (FreeWili 2 display CPU, RP2350B).
# Drop this directory into your wilibsp tree and add_subdirectory() it, or
```

Insert one line after the first line so it reads:

```cmake
# OneWili C API for WiliBSP (FreeWili 2 display CPU, RP2350B).
# COPY — source of truth: freewili-firmware/freewilimain/testprojects/onewili/wilibsp (menutool-generated; re-copy on regeneration, do not edit here).
# Drop this directory into your wilibsp tree and add_subdirectory() it, or
```

No other edits to any copied file.

- [ ] **Step 3: Register the library in the top-level CMakeLists**

In the top-level `CMakeLists.txt`, after the line `add_subdirectory(bsp)`, add:

```cmake
# Reusable non-BSP libraries.
add_subdirectory(libs/onewili)
```

- [ ] **Step 4: Verify the library configures and compiles**

Run (from repo root):
```powershell
python tools/fw.py build hello_display
cmake --build --preset target --target onewili_fwgui
```
Expected: the first command configures + builds `hello_display` cleanly, proving the added `add_subdirectory` doesn't break the existing tree; the second finishes with `libonewili_fwgui.a` built and no warnings from `libs/onewili` sources. (If the `target` preset needs configuring first, the `fw build` invocation does that.)

- [ ] **Step 5: Commit**

```bash
cd /c/~prj/Dropbox/vibeProjects/wilibsp
git add libs/onewili CMakeLists.txt
git commit -m "feat(libs): import generated onewili_fwgui library (OneWili C API over FwGUI link)"
```

---

### Task 2: Create the `toggleled` app

**Files:**
- Create: `apps/toggleled/CMakeLists.txt`, `apps/toggleled/main.c`, `apps/toggleled/README.md` (scaffolded by `fw new-app toggleled`, then edited)
- Modify: top-level `CMakeLists.txt` (apps block, after `add_subdirectory(apps/hello_dvi)`)

**Interfaces:**
- Consumes: `onewili_fwgui` CMake target from Task 1 — `ow_status ow_open_fwgui(ow_device* dev);`, `ow_status ow_io_gpio_set_io_toggle(ow_device* dev, int32_t pin);`, `OW_OK`; `freewili2_bsp` — `void board_init(void);` (via `fw2.h`), `DIAG(...)` (via `platform/diag.h`).
- Produces: `toggleled` executable target (`.uf2`/`.elf` under `build/apps/toggleled/`). Nothing downstream consumes it.

- [ ] **Step 1: Scaffold the app**

Run (from repo root):
```powershell
python tools/fw.py new-app toggleled
```
Expected: `apps/toggleled/` created with `CMakeLists.txt`, `main.c`, `README.md`; every `template` token rewritten to `toggleled`.

- [ ] **Step 2: Register the app in the top-level CMakeLists**

In the top-level `CMakeLists.txt` apps block, after `add_subdirectory(apps/hello_dvi)`, add:

```cmake
add_subdirectory(apps/toggleled)
```

- [ ] **Step 3: Link the OneWili library**

Replace the contents of `apps/toggleled/CMakeLists.txt` with:

```cmake
add_executable(toggleled main.c)
target_link_libraries(toggleled freewili2_bsp onewili_fwgui)
pico_set_binary_type(toggleled copy_to_ram)   # required: firmware runs from SRAM
pico_add_extra_outputs(toggleled)             # .uf2 / .bin / .map
```

- [ ] **Step 4: Write main.c**

Replace the contents of `apps/toggleled/main.c` with:

```c
/* toggleled — blink MAIN-CPU GPIO 25 from the display CPU over the FwGUI
 * link (UART0 @ 8 Mbaud, HW flow control on GPIO 0-3). The main CPU must
 * run the stock FreeWili 2 firmware, which carries the OneWili bridge. */
#include "fw2.h"
#include "platform/diag.h"
#include "pico/stdlib.h"
#include "onewili.h"
#include "onewili_fwgui.h"

int main(void) {
    board_init();   /* must precede ow_open_fwgui: uart_init reads clk_peri */

    ow_device dev;
    while (ow_open_fwgui(&dev) != OW_OK) {
        DIAG("toggleled: FwGUI link open failed (is the main CPU running stock fw?), retry in 1 s\n");
        sleep_ms(1000);
    }
    DIAG("toggleled: link up, toggling main-CPU GPIO 25 every 500 ms\n");

    for (;;) {
        ow_status s = ow_io_gpio_set_io_toggle(&dev, 25);
        if (s != OW_OK)
            DIAG("toggleled: toggle failed, status %d\n", (int)s);
        sleep_ms(500);
    }
}
```

Note: on command failure we log and keep looping (no re-open) — the handshake already happened and the link recovers via flow control (per spec's error-handling section).

- [ ] **Step 5: Write the app README**

Replace the contents of `apps/toggleled/README.md` with:

```markdown
# toggleled

Toggles **main-CPU GPIO 25** every 500 ms from the display CPU, using the
OneWili C API (`libs/onewili`) over the FwGUI display link (UART0,
8 Mbaud, hardware flow control on GPIO 0-3).

Requires the main CPU to run the stock FreeWili 2 firmware (it carries the
OneWili display bridge). If the link won't open, the app retries every
second and reports over RTT (`fw rtt`).

Build/flash: `fw build toggleled` / `fw flash toggleled`.
```

- [ ] **Step 6: Build**

Run (from repo root):
```powershell
python tools/fw.py build toggleled
```
Expected: configure + build succeed; `build/apps/toggleled/toggleled.uf2` and `.elf` exist; no compiler warnings from `apps/toggleled/main.c`.

- [ ] **Step 7: Commit**

```bash
cd /c/~prj/Dropbox/vibeProjects/wilibsp
git add apps/toggleled CMakeLists.txt
git commit -m "feat(apps): add toggleled - blink main-CPU GPIO 25 over the FwGUI link"
```

---

## On-hardware verification (user-performed, after Task 2)

Not a plan task — needs the physical board. `fw flash toggleled` with the main CPU on stock firmware; main-CPU GPIO 25 toggles at 1 Hz. `fw rtt` shows the link-up line, or the retry diagnostic if the bridge is absent.
