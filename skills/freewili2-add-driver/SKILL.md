---
name: freewili2:add-driver
description: Use when adding a new peripheral driver to the FreeWili2 BSP (wilibsp) — harvesting proven code from an owner repo into bsp/<domain>/ and wiring it into the CMake build and fw2.h umbrella header.
---

# freewili2:add-driver

Add a new peripheral driver to `bsp/` by harvesting proven source from one of
the owner's other repos, rather than writing one from scratch. This mirrors
how the current BSP (platform, display, touch, LEDs) was built.

## Procedure

1. **Pick the peripheral and find its source repo.** Check
   `docs/hardware/catalog.md` — every `TODO` row lists which owner repo to
   harvest from (e.g. `subghz` for the CC1101 radio, `sensorview` for
   ambient-light/humidity/IMU/magnetometer sensors, `usbcamfw`/`wili8c` for
   USB/audio). If the catalog says "owner repo not yet confirmed," ask
   before guessing — don't invent a source.

2. **Copy the driver files verbatim** into `bsp/<domain>/` (e.g.
   `bsp/radio/`, `bsp/nfc/`), matching the directory name the source repo
   uses under its own `src/` tree (`platform/`, `display/`, `input/`,
   `leds/`, `radio/`, `gfx/`, ...). This keeps the driver's own
   `#include "domain/x.h"`-style includes resolving unchanged, since the BSP
   include root is `bsp/` itself (see `bsp/CMakeLists.txt`'s
   `target_include_directories(freewili2_bsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ...)`).
   Copy any `.pio` files alongside their `.c`/`.h`.

3. **Wire it into `bsp/CMakeLists.txt`:**
   - Add every new `.c` file to the `add_library(freewili2_bsp STATIC ...)`
     source list.
   - Add any new Pico SDK component the driver needs to
     `target_link_libraries(freewili2_bsp PUBLIC ...)` (e.g. `hardware_adc`,
     `hardware_uart` — check the source repo's own CMakeLists for the exact
     component names it links).
   - If the driver has a `.pio` program, add a
     `pico_generate_pio_header(freewili2_bsp ${CMAKE_CURRENT_SOURCE_DIR}/<domain>/<name>.pio)`
     call (see the existing WS2812 example in `bsp/CMakeLists.txt`).

4. **Activate the include in `bsp/fw2.h`:** add
   `#include "domain/x.h" // (harvested: <peripheral>)` to the umbrella
   header, so any app that already does `#include "fw2.h"` gets the new
   driver for free.

5. **Keep the proven names.** Do not rename functions/types to an `fw2_`
   prefix — the existing drivers keep their source-repo names (`st7796_*`,
   `ft6336_*`, `ws2812_*`, `board_*`, `ioexp_*`, `psram_*`) and a new
   harvested driver should follow the same rule (see `AGENTS.md` § Naming
   note).

6. **Update `docs/hardware/catalog.md`:** move the peripheral's row from the
   `TODO` table to the `DONE` table, with its `bsp/<domain>/` location.

7. **Update `docs/hardware/pinmap.md`** if the peripheral's GPIOs were only
   in the `FwDisplayVibe.md`-sourced "broader inventory" table before — once
   a driver exists and its pins are confirmed (ideally added as `#define`s
   in `bsp/platform/board.h`, matching the existing `PIN_*` convention),
   move/update its row into the verified table.

8. **Add a driver doc** at `docs/drivers/<domain>.md` following the existing
   pattern (`docs/drivers/{platform,display,touch,leds}.md`): what it does,
   how to use it (a short snippet), dependencies.

9. **If the driver has pure/host-testable logic**, split it out from the
   hardware binding (the `subghz` pattern: guard hardware calls behind
   `#ifndef HOST_TEST`) and add a `tests/test_*.c` + a `tests/CMakeLists.txt`
   entry so `fw test` covers it. Pure modules that touch no Pico SDK header
   compile identically on host and target.

10. **Build it:** `fw build <app>` for whichever app now exercises the new
    driver (or add a minimal exercise to `apps/hello_display` /a new app via
    `freewili2:new-app` first).

## Known incomplete harvest (fix opportunistically)

`bsp/gfx/palette.h` exists but `bsp/gfx/palette.c` was never harvested (see
`docs/hardware/facts.md`). If your driver work touches `bsp/leds/led_ui.c`'s
`led_spectrum_map()`, harvest `subghz/src/gfx/palette.c` into
`bsp/gfx/palette.c` and add it to `bsp/CMakeLists.txt` as part of the same
change.

## Reference

- `AGENTS.md` § "How to add a driver" — the condensed version of this
  procedure.
- `docs/hardware/catalog.md` — peripheral → status → source-repo table.
- `docs/hardware/pinmap.md`, `docs/hardware/facts.md` — pin map and
  invariants to check your new driver against (shared SPI1/GPIO8, shared
  DMA_IRQ_0, RTT-only diagnostics, copy_to_ram/SRAM budget).
