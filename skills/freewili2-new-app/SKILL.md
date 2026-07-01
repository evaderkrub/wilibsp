---
name: freewili2:new-app
description: Use when scaffolding a new FreeWili2 app in this repo (wilibsp) — creates apps/<name> from the template, wires it into the top-level CMake build, and gets it building against the fw2.h BSP.
---

# freewili2:new-app

Scaffold a new app under `apps/` that links the shared `freewili2_bsp`
library and builds for the RP2350B target via the `fw` CLI.

## Procedure

1. **Scaffold the directory.** From the repo root:

   ```
   fw new-app <name>
   ```

   This copies `apps/template/` to `apps/<name>/` and rewrites the CMake
   target name in `apps/<name>/CMakeLists.txt` from `template` to `<name>`
   (`tools/fw.py`'s `new_app()`). It errors if `apps/<name>` already exists —
   pick a name that doesn't collide.

2. **Add it to the top-level build.** `fw new-app` only creates the
   directory; it does **not** edit the top-level `CMakeLists.txt`. Open the
   repo-root `CMakeLists.txt` and add a line next to the existing app(s):

   ```cmake
   add_subdirectory(apps/<name>)
   ```

   (Currently only `add_subdirectory(apps/hello_display)` is listed there —
   add yours alongside it, don't replace it unless asked to.)

3. **Edit `apps/<name>/main.c` against `fw2.h`.** The template already
   `#include`s `fw2.h`, which pulls in the board + all four current BSP
   drivers (platform, display, touch, LEDs). Every app must call
   `board_init()` first, before touching any other driver. See
   `apps/hello_display/main.c` for the canonical shape:

   ```c
   #include "fw2.h"
   #include "platform/diag.h"

   int main(void) {
       board_init();
       st7796_init();
       board_backlight_set(1);
       // ... your app logic, e.g. ft6336_init()/ft6336_poll(),
       // ws2812_init()/ws2812_fill()/ws2812_show() ...
       for (;;) { /* main loop */ }
   }
   ```

   Use `DIAG(...)` (`platform/diag.h`) for any diagnostic output — there is
   no UART/USB stdio on this board (see `AGENTS.md` invariants). The binary
   is built `copy_to_ram` (already set in the template's CMakeLists.txt via
   `pico_set_binary_type(<name> copy_to_ram)`) — all code+data+bss run from
   SRAM, so keep large buffers out of the app and prefer PSRAM
   (`bsp/platform/psram.h`) if you need them.

4. **Build it.**

   ```
   fw build <name>
   ```

   This runs `cmake --build --preset target --target <name>`. If the
   top-level CMake wasn't reconfigured since you added the
   `add_subdirectory` line, CMake will pick it up automatically on the next
   build (the `target` preset's binary dir is `build/`).

5. Optionally flash + watch diagnostics once hardware is available:
   `fw flash <name>` then `fw rtt`. (As of this writing, on-hardware
   verification of this repo's BSP itself is still pending — see
   `docs/hardware/facts.md`.)

## Reference

- `AGENTS.md` — command vocabulary, invariants, naming conventions.
- `docs/drivers/{platform,display,touch,leds}.md` — per-driver usage
  snippets to crib from.
- `apps/template/README.md`, `apps/hello_display/README.md` — existing app
  README conventions (keep new app READMEs equally short: what it does, how
  to build/flash/observe it).
