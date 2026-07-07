# DVI Video + OSD Harvest — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Harvest the movie player's plain 640×480p60 DVI-over-HSTX video output (GPIO 12–19) plus its software OSD into `freewili2_bsp`, with a `hello_dvi` demo app, stripping out the HDMI-audio-island path.

**Architecture:** Copy the proven plain-DVI scanout driver and pure OSD rasterizer from `../movieplayer/src/display/` into `bsp/display/`, dropping the compiled-in HDMI-audio path (islands, `pico_hdmi`, `hdmi_audio_ring`, the SRAM arena). The driver turns an SRAM framebuffer into a zero-IRQ DMA scanout; the app writes native-endian RGB565 pixels into a strided video region and HSTX serialises them. The board clock stays project-selectable (default 250 MHz, audio-optimal); the DVI demo opts into 252 MHz via a new `board_init_clk(khz)` so `clk_hstx = clk_sys/2 = 126 MHz` yields an exact 25.2 MHz pixel clock, without disturbing the other apps or the verified audio path.

**Tech Stack:** C11, Raspberry Pi Pico SDK (RP2350B), RP2350 HSTX peripheral, CMake + Ninja, host CTest tree for pure logic.

## Global Constraints

- **Target: RP2350B**, board selected by `set(PICO_BOARD freewili2)` in the top-level `CMakeLists.txt`. **NEVER** pass `-DPICO_BOARD` on a cmake command line.
- **All app binaries are `copy_to_ram`** — code+data+bss live in 512 KB SRAM. Watch the RAM budget: the DVI framebuffer is a ~327 KB static array.
- **Diagnostics via `DIAG(...)`** (`bsp/platform/diag.h` → SEGGER RTT channel 0). No UART/USB stdio. `SEGGER_RTT_printf` supports `%d %u %x %s %c` and field widths — **no floats**.
- **DMA_IRQ_0 is shared** — the plain-DVI scanout is deliberately **zero-IRQ** (installs no handler), so it does not touch this line. Keep it that way.
- **Pixel format is native little-endian RGB565.** The app writes `v` (no byte-swap) into the video region.
- **DVI pins: GPIO 12–19** (CLK 12/13, D0 14/15, D1 16/17, D2 18/19). `_P` = odd gpio (non-inverted), `_N` = even gpio (inverted).
- **Board clock is project-selectable; default stays 250 MHz.** `board_init()` runs the 250 MHz default (audio-optimal; invariant 2 default unchanged). An app opts into another even-MHz clock via `board_init_clk(khz)` (added in Task 1); the DVI demo uses 252 MHz for an exact 25.2 MHz pixel clock. The DVI driver reads `clk_sys` at runtime, so it works at whatever the app chose (25.2 MHz at 252, 25.0 MHz at the 250 default).
- **Harvest convention:** copied `.c`/`.h` keep their proven names and `#include "display/…"`-style includes, which resolve against the `bsp/` include root (`target_include_directories(freewili2_bsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})`).
- **Commits:** Conventional Commits (`feat:`, `docs:`, …). Git may warn `LF will be replaced by CRLF` — that is expected on this Windows checkout, not an error.

## File Structure

**New files:**
- `bsp/display/hstx_modes.h` — shared HSTX 640×480p60 mode constants (trimmed: DVI only).
- `bsp/display/hstx_dvi.h` — plain-DVI driver public surface.
- `bsp/display/hstx_dvi.c` — plain-DVI scanout driver (stripped of the HDMI-audio path).
- `bsp/display/dvi_osd.h` / `bsp/display/dvi_osd.c` — pure software OSD rasterizer (verbatim copy).
- `tests/test_dvi_osd.c` — host tests for the pure OSD geometry.
- `apps/hello_dvi/main.c` / `CMakeLists.txt` / `README.md` — on-hardware demo.
- `docs/drivers/dvi.md` — per-driver usage doc.

**Modified files:**
- `bsp/platform/board.h` — add `board_init_clk(uint32_t)` decl; keep `BOARD_SYS_CLOCK_KHZ` at 250000 (default).
- `bsp/platform/board.c` — refactor `board_init` into a wrapper over new `board_init_clk(khz)`.
- `AGENTS.md` — invariant 2 note: 250 MHz default, overridable via `board_init_clk`.
- `docs/hardware/facts.md` — note the override + the DVI-at-252 audio-pitch interaction (numbers unchanged).
- `docs/hardware/catalog.md` — DVI row TODO → DONE.
- `docs/hardware/pinmap.md` — add DVI pins.
- `bsp/CMakeLists.txt` — add `display/hstx_dvi.c` and `display/dvi_osd.c` to the STATIC lib.
- `bsp/fw2.h` — add the two new includes.
- `tests/CMakeLists.txt` — add the `dvi_osd` host test.
- `CMakeLists.txt` (top level) — `add_subdirectory(apps/hello_dvi)`.

**Existing files reused unchanged:** `bsp/display/font5x7.{c,h}` (byte-identical to the source; the OSD links against it).

---

### Task 1: Make the board system clock project-selectable (default 250 MHz)

The DVI pixel clock is `clk_sys / 2 / 5`, so an exact 25.2 MHz (640×480p60) needs `clk_sys = 252 MHz`. But **250 MHz is audio-optimal** and must stay the default: the NAU88C10 codec MCLK is an integer divide `clk_sys/61`, which lands at `250e6/61 = 4.0984 MHz` (≈ nominal 16 kHz fs) only at 250 MHz; at 252 MHz the audio sample rate shifts ~0.8% (a slight pitch change — no tick artifact, since LRCK stays locked to MCLK/256). `board.c` is compiled **once** into the shared `freewili2_bsp` static lib, so the clock cannot be a per-app compile `-D`. Instead add a **runtime** override: `board_init_clk(khz)` does the full bring-up at a caller-chosen clock, and `board_init()` becomes a thin wrapper at the 250 MHz default. Existing apps and the verified audio path are untouched; the DVI demo (Task 4) opts into 252 MHz. Invariant 2's default is unchanged.

**Files:**
- Modify: `bsp/platform/board.h:67-72` (add `board_init_clk` decl; keep `BOARD_SYS_CLOCK_KHZ` at 250000)
- Modify: `bsp/platform/board.c:12-19` (refactor `board_init` → `board_init_clk(khz)`)
- Modify: `AGENTS.md` (invariant 2 note)
- Modify: `docs/hardware/facts.md` (note the override + DVI/audio interaction)

**Interfaces:**
- Produces:
  - `void board_init_clk(uint32_t sys_clock_khz);` — full board bring-up (vreg 1.25 V, `set_sys_clock_khz`, clk_peri re-source, `spi_bus_init`, park CC1101 CS, backlight off, I2C1, `ioexp_init`) at the given clock.
  - `void board_init(void);` — unchanged signature; now `= board_init_clk(BOARD_SYS_CLOCK_KHZ)`.
  - `BOARD_SYS_CLOCK_KHZ == 250000` (default; still consumed by `hello_display`'s DIAG).
- Consumed by Task 4 (`hello_dvi` calls `board_init_clk(252000)`).

- [ ] **Step 1: Add the board_init_clk declaration**

In `bsp/platform/board.h`, replace the clock define + `board_init` doc comment block (lines 67–72):

```c
// --- Clocks: overclock to 250 MHz @ vreg 1.25 V, run from RAM (copy_to_ram). ---
#define BOARD_SYS_CLOCK_KHZ 250000

// Bring up clocks (250 MHz + vreg 1.25V + clk_peri re-source), park CC1101 CS, backlight off,
// and initialises I2C1 @ 400 kHz on GPIO 26/27.
void board_init(void);
```
with:
```c
// --- Clocks: default overclock to 250 MHz @ vreg 1.25 V, run from RAM
// (copy_to_ram). 250 MHz is the default because it is audio-optimal: the NAU88C10
// MCLK is an integer divide clk_sys/61 = 4.0984 MHz (~16 kHz fs) at 250. Apps that
// need a different even-MHz clock pass it to board_init_clk() (e.g. DVI uses 252
// MHz for an exact 25.2 MHz pixel clock, trading ~0.8% audio pitch). ---
#define BOARD_SYS_CLOCK_KHZ 250000

// Full board bring-up at the DEFAULT clock (BOARD_SYS_CLOCK_KHZ = 250 MHz): vreg
// 1.25 V, clk_peri re-source, SPI1 bus, park CC1101 CS, backlight off, I2C1
// @ 400 kHz, ioexp_init.
void board_init(void);

// Same bring-up at a caller-chosen system clock (kHz; use an even MHz so downstream
// /2 dividers like DVI's clk_hstx are exact). board_init() == board_init_clk(BOARD_SYS_CLOCK_KHZ).
void board_init_clk(uint32_t sys_clock_khz);
```

- [ ] **Step 2: Refactor board.c to split out board_init_clk**

In `bsp/platform/board.c`, replace the function signature + the clock comment/set (lines 12–19):

```c
void board_init(void) {
    // Raise the core voltage before overclocking. The earlier 252 MHz fault was
    // marginal Vcore at 1.15 V during the heavy st7796 bring-up; the firmware runs
    // from RAM (copy_to_ram) so flash XIP timing doesn't cap clk_sys. 1.25 V gives
    // solid headroom for the overclock below.
    vreg_set_voltage(VREG_VOLTAGE_1_25);
    sleep_ms(10);
    set_sys_clock_khz(BOARD_SYS_CLOCK_KHZ, true);
```
with:
```c
void board_init(void) { board_init_clk(BOARD_SYS_CLOCK_KHZ); }

void board_init_clk(uint32_t sys_clock_khz) {
    // Raise the core voltage before overclocking. The earlier 252 MHz fault was
    // marginal Vcore at 1.15 V during the heavy st7796 bring-up; the firmware runs
    // from RAM (copy_to_ram) so flash XIP timing doesn't cap clk_sys. 1.25 V gives
    // solid headroom for the overclock (250 MHz default; DVI apps pass 252).
    vreg_set_voltage(VREG_VOLTAGE_1_25);
    sleep_ms(10);
    set_sys_clock_khz(sys_clock_khz, true);
```

Everything below that line (the `clk_peri` re-source, `spi_bus_init()`, CC1101 CS park, backlight, `board_i2c1_init()`, `ioexp_init()`) is unchanged — it now lives inside `board_init_clk`. Do not duplicate it into `board_init`; `board_init` is only the one-line wrapper.

- [ ] **Step 3: Update AGENTS.md invariant 2**

In `AGENTS.md`, invariant 2 documents `set_sys_clock_khz(250000, true)`. Keep the `250000` (it remains the default) and append to that invariant:

```
   250 MHz is the DEFAULT (audio-optimal: NAU88C10 MCLK = clk_sys/61 = 4.0984 MHz
   ~ 16 kHz fs). An app may bring the board up at another even-MHz clock via
   board_init_clk(khz) — the DVI demo uses 252 MHz for an exact 25.2 MHz pixel
   clock, which shifts audio pitch ~0.8% (only relevant if that app also plays
   audio). board_init() == board_init_clk(250000). See docs/drivers/dvi.md.
```

- [ ] **Step 4: Note the interaction in docs/hardware/facts.md**

`facts.md` already records the audio MCLK fact (`250e6 / 61 = 4.0984 MHz`). Do NOT change those numbers — 250 MHz stays the default and audio-verified operating point. Add one sentence next to that fact:

```
250 MHz is the board DEFAULT partly for this reason — it divides near-exactly to
the codec MCLK. Apps needing a different clock call board_init_clk(khz); the DVI
demo runs 252 MHz for an exact 25.2 MHz pixel clock, which moves the codec MCLK to
252e6/61 = 4.131 MHz (~0.8% audio pitch shift) — only relevant if that app also
plays audio.
```

- [ ] **Step 5: Verify existing builds and host tests still pass**

Run: `python tools/fw.py test`
Expected: all existing host tests PASS (no host test depends on the clock).

Run: `python tools/fw.py build hello_display`
Expected: configures and builds cleanly. `board_init()` is unchanged for it (still 250 MHz via the wrapper), and `BOARD_SYS_CLOCK_KHZ` (250000) still resolves for its DIAG.

- [ ] **Step 6: Commit**

```bash
git add bsp/platform/board.h bsp/platform/board.c AGENTS.md docs/hardware/facts.md
git commit -m "feat(platform): add board_init_clk() for project-selectable clock

board_init() keeps the 250 MHz default (audio-optimal: NAU88C10 MCLK =
clk_sys/61 ~ 16 kHz). board_init_clk(khz) brings the board up at a
caller-chosen even-MHz clock; DVI apps opt into 252 MHz for an exact
25.2 MHz pixel clock without disturbing audio or the other apps.
Invariant 2 default unchanged.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: Harvest the OSD rasterizer + host tests

The OSD (`dvi_osd.{c,h}`) is pure, host-testable, and depends only on `font5x7` (already present in `bsp/display/`). Copy it verbatim and characterize the risk-bearing geometry (letterbox strip height + rect clipping) with host tests.

**Files:**
- Create: `bsp/display/dvi_osd.h` (verbatim copy)
- Create: `bsp/display/dvi_osd.c` (verbatim copy)
- Create: `tests/test_dvi_osd.c`
- Modify: `bsp/CMakeLists.txt` (add `display/dvi_osd.c`)
- Modify: `tests/CMakeLists.txt` (add the `dvi_osd` test)

**Interfaces:**
- Consumes: `bsp/display/font5x7.h` (`extern const uint8_t font5x7[...][5]`), already in the tree.
- Produces (from `dvi_osd.h`):
  - `#define DVI_OSD_OVERLAY_H 18`
  - `int dvi_osd_strip_h(int region_h, int movie_h, bool *overlay);`
  - `void dvi_osd_fill_rect(uint16_t *base, int stride, int region_w, int region_h, int x, int y, int w, int h, uint16_t color);`
  - `void dvi_osd_text(uint16_t *base, int stride, int region_w, int region_h, int x, int y, int scale, uint16_t fg, uint16_t bg, const char *s);`
  - `void dvi_osd_text_msg(uint16_t *base, int stride, int region_w, int region_h, int movie_h, const char *msg);`
  - `void dvi_osd_progress(uint16_t *base, int stride, int region_w, int region_h, int movie_h, uint32_t cur, uint32_t total, const char *text);`
  - `void dvi_osd_clear_strip(uint16_t *base, int stride, int region_w, int region_h, int movie_h);`

- [ ] **Step 1: Write the failing host test**

Create `tests/test_dvi_osd.c`:

```c
// tests/test_dvi_osd.c — host tests for the pure DVI OSD geometry (dvi_osd.c has
// no Pico SDK dependency). Covers the letterbox strip-height decision and the
// rect clipping the text/progress helpers are built on.
#include "display/dvi_osd.h"
#include "test_util.h"

int main(void) {
    // --- dvi_osd_strip_h: real bottom margin when the bar is >= 8px ---
    bool overlay = true;
    ASSERT_EQ(dvi_osd_strip_h(320, 288, &overlay), 16);   // (320-288)/2 = 16
    ASSERT_TRUE(overlay == false);

    // Thin bar (< 8px) -> overlay fallback, DVI_OSD_OVERLAY_H, overlay=true.
    overlay = false;
    ASSERT_EQ(dvi_osd_strip_h(272, 270, &overlay), DVI_OSD_OVERLAY_H);  // bar=1
    ASSERT_TRUE(overlay == true);

    // Movie fills the region -> bar=0 -> overlay fallback.
    overlay = false;
    ASSERT_EQ(dvi_osd_strip_h(320, 320, &overlay), DVI_OSD_OVERLAY_H);
    ASSERT_TRUE(overlay == true);

    // NULL overlay pointer is allowed; large margin returned verbatim.
    ASSERT_EQ(dvi_osd_strip_h(480, 320, NULL), 80);       // (480-320)/2 = 80

    // --- dvi_osd_fill_rect: clipping. Region is 16 wide x 20 tall, stride 16. ---
    uint16_t fb[16 * 20];
    for (int i = 0; i < 16 * 20; i++) fb[i] = 0;

    // Top-left overhang: rect (-2,-3) 5x5 clips to cols 0..2, rows 0..1.
    dvi_osd_fill_rect(fb, 16, 16, 20, -2, -3, 5, 5, 0xABCD);
    ASSERT_EQ(fb[0 * 16 + 0], 0xABCD);   // inside
    ASSERT_EQ(fb[1 * 16 + 2], 0xABCD);   // inside, far corner of the clipped rect
    ASSERT_EQ(fb[2 * 16 + 0], 0x0000);   // row 2 not filled (h clipped to 2)
    ASSERT_EQ(fb[0 * 16 + 3], 0x0000);   // col 3 not filled (w clipped to 3)

    // Bottom-right overhang: rect (14,18) 5x5 clips to cols 14..15, rows 18..19.
    dvi_osd_fill_rect(fb, 16, 16, 20, 14, 18, 5, 5, 0x1234);
    ASSERT_EQ(fb[18 * 16 + 14], 0x1234);
    ASSERT_EQ(fb[19 * 16 + 15], 0x1234);

    // Fully out of bounds: nothing written, no crash.
    uint16_t before = fb[0];
    dvi_osd_fill_rect(fb, 16, 16, 20, 20, 0, 4, 4, 0xFFFF);
    ASSERT_EQ(fb[0], before);

    TEST_RETURN();
}
```

- [ ] **Step 2: Add the test to the CMake tree**

Append to `tests/CMakeLists.txt`:

```cmake
add_executable(test_dvi_osd
    test_dvi_osd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp/display/dvi_osd.c)
target_include_directories(test_dvi_osd PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp
    ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME dvi_osd COMMAND test_dvi_osd)
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `python tools/fw.py test`
Expected: FAIL at CMake configure/build — `bsp/display/dvi_osd.c` and `bsp/display/dvi_osd.h` do not exist yet (missing source / missing header).

- [ ] **Step 4: Copy the OSD files verbatim**

```bash
cp "../movieplayer/src/display/dvi_osd.h" bsp/display/dvi_osd.h
cp "../movieplayer/src/display/dvi_osd.c" bsp/display/dvi_osd.c
```

(These are exact copies — no edits. `dvi_osd.c` includes `"display/dvi_osd.h"` and `"display/font5x7.h"`, both of which resolve against `bsp/`.)

- [ ] **Step 5: Run the test to verify it passes**

Run: `python tools/fw.py test`
Expected: PASS — the `dvi_osd` test runs green alongside the existing suite.

- [ ] **Step 6: Add dvi_osd.c to the BSP library**

In `bsp/CMakeLists.txt`, in the `add_library(freewili2_bsp STATIC ...)` source list, add after `display/font5x7.c`:

```cmake
    display/dvi_osd.c
```

- [ ] **Step 7: Commit**

```bash
git add bsp/display/dvi_osd.h bsp/display/dvi_osd.c tests/test_dvi_osd.c tests/CMakeLists.txt bsp/CMakeLists.txt
git commit -m "feat(display): harvest DVI OSD rasterizer + host tests

Pure software OSD (dvi_osd) copied verbatim from ../movieplayer; reuses
the existing bsp/display/font5x7. Host tests cover the letterbox
strip-height decision and rect clipping.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: Harvest the plain-DVI scanout driver (HDMI path stripped)

Copy `hstx_modes.h`, `hstx_dvi.h`, and `hstx_dvi.c` into `bsp/display/`, stripping the HDMI-audio-island mode. The result is behaviourally identical to the movie player's plain-DVI path. It compiles into the BSP STATIC lib; building any app compiles it.

**Files:**
- Create: `bsp/display/hstx_modes.h`
- Create: `bsp/display/hstx_dvi.h`
- Create: `bsp/display/hstx_dvi.c`
- Modify: `bsp/CMakeLists.txt` (add `display/hstx_dvi.c`)
- Modify: `bsp/fw2.h` (add includes)

**Interfaces:**
- Consumes: `clk_sys` at runtime (whatever clock the app selected via `board_init`/`board_init_clk` in Task 1 — the driver reads it with `clock_get_hz(clk_sys)`, no compile-time clock constant); `hstx_modes.h` macros (this task).
- Produces (from `hstx_dvi.h`):
  - `#define HSTX_DVI_W 640`, `HSTX_DVI_H 480`, `HSTX_VID_W_MAX 480`, `HSTX_VID_H_MAX 320`
  - `void hstx_dvi_init(int vid_w, int vid_h);`
  - `void hstx_dvi_set_geometry(int vid_w, int vid_h);`
  - `uint16_t *hstx_dvi_video_base(void);`
  - `uint16_t *hstx_dvi_region_base(void);`
  - `int hstx_dvi_video_stride(void);`  (uint16 elements per row)
  - `int hstx_dvi_video_w(void);`
  - `int hstx_dvi_video_h(void);`
  - `int hstx_dvi_region_h(void);`
  - `void hstx_dvi_enable(bool on);`

- [ ] **Step 1: Create `bsp/display/hstx_modes.h` (trimmed to DVI-only)**

Write `bsp/display/hstx_modes.h` with exactly this content (the HDMI-island preamble/guard/data-island macros from the source are dropped; the scanout builder does not use them):

```c
// bsp/display/hstx_modes.h — shared HSTX 640x480p60 mode constants.
//
// Harvested from ../movieplayer/src/display/hstx_modes.h, trimmed to the plain-DVI
// scanout constants (the HDMI data-island preamble/guard/width macros were dropped
// with the island path). No values changed.
#ifndef HSTX_MODES_H
#define HSTX_MODES_H

// ----------------------------------------------------------------------------
// DVI/HDMI control symbols (10-bit TMDS control characters, one per lane).
#define TMDS_CTRL_00 0x354u
#define TMDS_CTRL_01 0x0abu
#define TMDS_CTRL_10 0x154u
#define TMDS_CTRL_11 0x2abu

// Per-line sync words (lane0 = ctrl, lanes1&2 = blanking). V/H = vsync/hsync active.
#define SYNC_V0_H0 (TMDS_CTRL_00 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V0_H1 (TMDS_CTRL_01 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H0 (TMDS_CTRL_10 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H1 (TMDS_CTRL_11 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))

// ----------------------------------------------------------------------------
// 640x480p60 VESA timing.
#define MODE_H_FRONT_PORCH   16
#define MODE_H_SYNC_WIDTH    96
#define MODE_H_BACK_PORCH    48
#define MODE_H_ACTIVE_PIXELS 640
#define MODE_V_FRONT_PORCH   10
#define MODE_V_SYNC_WIDTH    2
#define MODE_V_BACK_PORCH    33
#define MODE_V_ACTIVE_LINES  480

// ----------------------------------------------------------------------------
// HSTX command-word opcodes (top nibble of a command dword).
#define HSTX_CMD_RAW         (0x0u << 12)
#define HSTX_CMD_RAW_REPEAT  (0x1u << 12)
#define HSTX_CMD_TMDS        (0x2u << 12)
#define HSTX_CMD_TMDS_REPEAT (0x3u << 12)

#endif // HSTX_MODES_H
```

- [ ] **Step 2: Create `bsp/display/hstx_dvi.h` (stripped)**

Write `bsp/display/hstx_dvi.h` with exactly this content (no `hdmi_audio_ring.h` include; no HDMI-audio API; `HSTX_VID_H_MAX` raised to 320 now that the player's SRAM-arena cap is gone):

```c
// bsp/display/hstx_dvi.h — 640x480p60 DVI over the RP2350 HSTX block (GPIO 12-19).
// Harvested from ../movieplayer (plain-DVI path only; the HDMI-audio-island mode was
// stripped). Architecture (proven by the movie player's Phase-0 spike):
//   - zero-IRQ baked-command DMA (a SCREEN channel streams a command buffer to the
//     HSTX FIFO; a LOOPER channel restarts it) — no interrupts,
//   - framebuffer in SRAM (PSRAM scanout underruns the FIFO and wedges flash),
//   - the <=480x320 video region is stored; the surrounding 640x480 border is
//     GENERATED by the command list (HSTX_CMD_TMDS_REPEAT), not stored,
//   - clk_hstx = clk_sys/2, read at runtime from clk_sys. An exact 25.2 MHz pixel
//     (640x480p60) needs clk_sys = 252 MHz (even); at the 250 MHz board default the
//     pixel is 25.0 MHz (0.7% low). Apps select the clock via board_init_clk().
#ifndef HSTX_DVI_H_
#define HSTX_DVI_H_
#include <stdint.h>
#include <stdbool.h>

// The DVI frame is fixed 640x480p60. The centered video region may be up to
// 480x320; the rest is HSTX-generated border (not stored).
#define HSTX_DVI_W 640
#define HSTX_DVI_H 480
#define HSTX_VID_W_MAX 480
// Max stored DVI video height. 480x320 = the full panel size. The ~327 KB SRAM
// command buffer (see hstx_dvi.c) leaves ample room in a copy_to_ram demo; lower
// this #define if the DVI driver is ever linked alongside the USB/FatFs + LCD-strip
// stack in one binary. Taller frames are center-cropped on DVI.
#define HSTX_VID_H_MAX 320

// Configure clk_hstx (= clk_sys/2), the HSTX block and GPIO 12-19, build the
// scanout command buffer for a vid_w x vid_h video centered in 640x480, and start
// the zero-IRQ baked-command DMA. clk_sys must already be at its final (even MHz)
// value. The video region is cleared to black; it stays valid for program life.
void hstx_dvi_init(int vid_w, int vid_h);

// Rebuild the centered video size (call at content start). vid_w is rounded down to
// even; both dims are clamped to HSTX_VID_*_MAX. Does NOT touch the scanout DMA.
void hstx_dvi_set_geometry(int vid_w, int vid_h);

// The video region as a strided native-endian RGB565 framebuffer: row y of the
// video lives at hstx_dvi_video_base() + y*hstx_dvi_video_stride(), and is
// hstx_dvi_video_w() pixels wide. (Pixels are interleaved with command words,
// hence the stride.) The writer stores pixels native little-endian (NO byte-swap).
uint16_t *hstx_dvi_video_base(void);
// Pixel(0,0) of the FIXED region (the movie is centered within it; the OSD draws
// in the margin below the movie). Same stride as hstx_dvi_video_stride().
uint16_t *hstx_dvi_region_base(void);
int hstx_dvi_video_stride(void);   // uint16 elements between consecutive rows
int hstx_dvi_video_w(void);
int hstx_dvi_video_h(void);
// Height of the FIXED region in rows (HSTX_VID_H_MAX). OSD row math MUST use this.
int hstx_dvi_region_h(void);

// Blank/unblank the three DVI DATA lanes (the CLK lane keeps running so the
// monitor stays locked). Instant; leaves the scanout DMA and framebuffer intact.
void hstx_dvi_enable(bool on);

#endif // HSTX_DVI_H_
```

- [ ] **Step 3: Create `bsp/display/hstx_dvi.c` (stripped)**

Write `bsp/display/hstx_dvi.c` with exactly this content (the full HDMI-audio path — islands, refresh IRQ, arena, `pico_hdmi`, `hdmi_audio_ring`, `bus_ctrl` — is removed; `framebuf` is sized for plain DVI only):

```c
// bsp/display/hstx_dvi.c — 640x480p60 DVI scanout over the RP2350 HSTX block.
//
// Harvested from ../movieplayer/src/display/hstx_dvi.c (stage6-hstx-dvi) with the
// HDMI-audio-island path stripped out — this is the plain-DVI scanout only. Proven
// facts baked in (see the movie player's memory freewili-hstx-dvi):
//
//   - ZERO-IRQ baked-command DMA: one SCREEN channel streams a command buffer
//     (sync RAW words + TMDS_REPEAT borders + a TMDS run of video pixels, per
//     scanline) to the HSTX FIFO; a LOOPER channel rewrites SCREEN.read_addr and
//     chains back. No interrupts. Channels are claimed dynamically.
//   - Framebuffer in SRAM. PSRAM scanout underruns the FIFO (no signal) and
//     monopolises the QMI (wedges flash). The <=480x320 video region is stored;
//     the 640x480 border is GENERATED by HSTX_CMD_TMDS_REPEAT (one word -> N
//     pixels, ~zero bandwidth), not stored.
//   - FreeWili 2 differential polarity (the colour fix): each pair's _P pin (the
//     ODD gpio 13/15/17/19 -> HSTX odd bit) is NON-inverted; the _N pin (even
//     gpio) is INVERTED.
//   - RGB565 encode == MichaelBell/dvhstx MODE_RGB565: expand_tmds L2 nbits4 rot8
//     (Red) / L1 nbits5 rot3 (Green) / L0 nbits4 rot29 (Blue); expand_shift
//     2/16/1/0. Pixels are native little-endian uint16 (NO byte-swap).
//   - clk_hstx = clk_sys/2 (read at runtime); CSR CLKDIV 5 -> pixel = clk_hstx/5.
//     clk_sys = 252 MHz -> clk_hstx 126 MHz -> 25.2 MHz pixel = standard 640x480p60
//     (DVI apps opt into 252 via board_init_clk; the board default is 250 -> 25.0).

#include "display/hstx_dvi.h"
#include "display/hstx_modes.h"

#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/structs/hstx_ctrl.h"
#include "hardware/structs/hstx_fifo.h"

#define BORDER_RGB565 0x0000u   // black border around the video region

// ---------------------------------------------------------------------------
// Command-buffer sizing (plain DVI). Per video line: 6 sync dwords + 2 (left
// border) + 1 (TMDS cmd) + vid_w/2 pixel dwords + 2 (right border) = 11 + vid_w/2.
// Border-only active lines are 8 dwords. Plus ~200 dwords of vblank + margin.
// At 480x320: 200 + 320*(11+240) + 160*8 = ~81800 dwords ~= 327 KB SRAM.
#define HSTX_PLAIN_DWORDS \
    (200 + HSTX_VID_H_MAX * (11 + HSTX_VID_W_MAX / 2) + \
     (MODE_V_ACTIVE_LINES - HSTX_VID_H_MAX) * 8)

// SRAM command buffer (commands + video pixels interleaved) and scanout state.
static uint32_t framebuf[HSTX_PLAIN_DWORDS];
static uint32_t *g_loop_src = framebuf;   // LOOPER copies this into SCREEN.read_addr

static unsigned g_fill;                    // command-buffer length in dwords
static uint16_t *g_region_base;            // pixel(0,0) of the FIXED region
static uint16_t *g_video_base;             // top-left of the centered movie in it
static int g_video_stride;                 // uint16 elements per video row
static int g_region_h = HSTX_VID_H_MAX;    // stored region height
static int g_movie_w, g_movie_h;           // current movie size (<= the region)

static int ch_screen = -1, ch_looper = -1;

// One blanking band (front porch / sync / back porch worth of identical lines),
// merging each line's back-porch+active+next-front-porch into one RAW_REPEAT.
static void emit_vblank_band(unsigned *fillp, unsigned von, unsigned lines) {
    unsigned fill = *fillp;
    framebuf[fill++] = HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH;
    framebuf[fill++] = von ? SYNC_V0_H1 : SYNC_V1_H1;
    for (unsigned y = 0; y < lines; y++) {
        framebuf[fill++] = HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH;
        framebuf[fill++] = von ? SYNC_V0_H0 : SYNC_V1_H0;
        framebuf[fill++] = HSTX_CMD_RAW_REPEAT |
            (MODE_H_BACK_PORCH + MODE_H_ACTIVE_PIXELS + MODE_H_FRONT_PORCH);
        framebuf[fill++] = von ? SYNC_V0_H1 : SYNC_V1_H1;
    }
    framebuf[fill - 2] = HSTX_CMD_RAW_REPEAT | (MODE_H_BACK_PORCH + MODE_H_ACTIVE_PIXELS);
    *fillp = fill;
}

// Build the whole-frame command buffer ONCE for the FIXED HSTX_VID_W_MAX x
// HSTX_VID_H_MAX video region centered in 640x480, clearing it to black. The
// movie is recentered within the region by hstx_dvi_set_geometry without touching
// the DMA.
static void hstx_build(int vid_w, int vid_h) {
    if (vid_w > HSTX_VID_W_MAX) vid_w = HSTX_VID_W_MAX;
    if (vid_h > HSTX_VID_H_MAX) vid_h = HSTX_VID_H_MAX;
    if (vid_w < 2) vid_w = 2;
    if (vid_h < 1) vid_h = 1;
    vid_w &= ~1;                           // even: two RGB565 px per 32-bit word
    g_region_h = vid_h;                    // stored region height

    const int hborder_l = (MODE_H_ACTIVE_PIXELS - vid_w) / 2;
    const int hborder_r = MODE_H_ACTIVE_PIXELS - vid_w - hborder_l;
    const int vtop = (MODE_V_ACTIVE_LINES - vid_h) / 2;
    const uint32_t border2 = ((uint32_t)BORDER_RGB565 << 16) | BORDER_RGB565;

    unsigned fill = 0;
    emit_vblank_band(&fill, 0, MODE_V_FRONT_PORCH);
    emit_vblank_band(&fill, 1, MODE_V_SYNC_WIDTH);
    emit_vblank_band(&fill, 0, MODE_V_BACK_PORCH);

    g_region_base = NULL;
    for (int y = 0; y < MODE_V_ACTIVE_LINES; y++) {
        framebuf[fill++] = HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH; framebuf[fill++] = SYNC_V1_H1;
        framebuf[fill++] = HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH;  framebuf[fill++] = SYNC_V1_H0;
        framebuf[fill++] = HSTX_CMD_RAW_REPEAT | MODE_H_BACK_PORCH;  framebuf[fill++] = SYNC_V1_H1;
        if (y < vtop || y >= vtop + vid_h) {
            framebuf[fill++] = HSTX_CMD_TMDS_REPEAT | MODE_H_ACTIVE_PIXELS;
            framebuf[fill++] = border2;
        } else {
            framebuf[fill++] = HSTX_CMD_TMDS_REPEAT | hborder_l; framebuf[fill++] = border2;
            framebuf[fill++] = HSTX_CMD_TMDS | vid_w;
            uint16_t *px = (uint16_t *)&framebuf[fill];
            if (!g_region_base) g_region_base = px;
            for (int x = 0; x < vid_w; x++) px[x] = 0x0000;   // video starts black
            fill += (unsigned)vid_w / 2;
            framebuf[fill++] = HSTX_CMD_TMDS_REPEAT | hborder_r; framebuf[fill++] = border2;
        }
    }
    g_fill = fill;
    // Per video line: 11 + vid_w/2 dwords. Stride in uint16 elements:
    g_video_stride = (11 + vid_w / 2) * 2;
}

// One-time HSTX peripheral + pin configuration (encoder, polarity, pads).
static void hstx_setup_peripheral(void) {
    hstx_ctrl_hw->csr = 0;
    hstx_ctrl_hw->csr =
        HSTX_CTRL_CSR_EXPAND_EN_BITS |
        5u << HSTX_CTRL_CSR_CLKDIV_LSB |
        5u << HSTX_CTRL_CSR_N_SHIFTS_LSB |
        2u << HSTX_CTRL_CSR_SHIFT_LSB |
        HSTX_CTRL_CSR_EN_BITS;

    // FreeWili pinout/polarity: clock pair GP12/13 on HSTX bit 0/1 with the ODD
    // gpio (13, _P) NON-inverted and the EVEN gpio (12, _N) INVERTED; data lanes
    // on bit 2/4/6 likewise (odd=_P non-inv, even=_N inv). Matches dvhstx.
    hstx_ctrl_hw->bit[1] = HSTX_CTRL_BIT0_CLK_BITS;                           // GP13 CLK_P
    hstx_ctrl_hw->bit[0] = HSTX_CTRL_BIT0_CLK_BITS | HSTX_CTRL_BIT0_INV_BITS; // GP12 CLK_N
    for (unsigned lane = 0; lane < 3; ++lane) {
        static const int lane_to_output_bit[3] = {2, 4, 6};   // even (_N) gpio bit
        int bit = lane_to_output_bit[lane];
        uint32_t sel = (lane * 10) << HSTX_CTRL_BIT0_SEL_P_LSB |
                       (lane * 10 + 1) << HSTX_CTRL_BIT0_SEL_N_LSB;
        hstx_ctrl_hw->bit[bit + 1] = sel;                            // odd (_P): non-inv
        hstx_ctrl_hw->bit[bit    ] = sel | HSTX_CTRL_BIT0_INV_BITS;  // even (_N): inv
    }

    hstx_ctrl_hw->expand_tmds =
        4  << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB | 8  << HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB |
        5  << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB | 3  << HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB |
        4  << HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB | 29 << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB;
    hstx_ctrl_hw->expand_shift =
        2  << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB |
        16 << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB |
        1  << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB |
        0  << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB;

    for (int g = 12; g <= 19; ++g) gpio_set_function(g, 0);   // GPIO_FUNC_HSTX == 0
}

// (Re)configure and start the SCREEN+LOOPER scanout DMA for the current buffer.
// Zero-IRQ: no DMA_IRQ_0 handler is installed.
static void hstx_dma_start(void) {
    if (ch_screen < 0) ch_screen = dma_claim_unused_channel(true);
    if (ch_looper < 0) ch_looper = dma_claim_unused_channel(true);

    dma_channel_config c;
    c = dma_channel_get_default_config(ch_screen);
    channel_config_set_chain_to(&c, ch_looper);
    channel_config_set_dreq(&c, DREQ_HSTX);
    dma_channel_configure(ch_screen, &c, &hstx_fifo_hw->fifo, framebuf, g_fill, false);

    c = dma_channel_get_default_config(ch_looper);
    channel_config_set_chain_to(&c, ch_screen);
    channel_config_set_dreq(&c, DREQ_HSTX);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, false);
    dma_channel_configure(ch_looper, &c, &dma_hw->ch[ch_screen].read_addr,
                          &g_loop_src, 1, false);

    dma_channel_start(ch_screen);
}

// ----------------------------------------------------------------------------
// Public API

void hstx_dvi_init(int vid_w, int vid_h) {
    uint32_t fsys = clock_get_hz(clk_sys);
    // clk_hstx = clk_sys / 2 (integer divide; clk_sys must be even MHz). Keeps
    // clk_sys high while clk_hstx -> 25.2 MHz DVI pixel clock.
    clock_configure(clk_hstx, 0, CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLK_SYS,
                    fsys, fsys / 2);

    hstx_setup_peripheral();
    hstx_build(HSTX_VID_W_MAX, HSTX_VID_H_MAX);   // fixed region, built ONCE
    hstx_dvi_set_geometry(vid_w, vid_h);          // center the initial movie size
    hstx_dma_start();                             // scanout runs forever on framebuf
}

// Recenter the movie within the FIXED region: clear the region to black (so the
// letterbox margins are black) and point g_video_base at the centered top-left.
// Deliberately does NOT touch the scanout DMA.
void hstx_dvi_set_geometry(int vid_w, int vid_h) {
    if (vid_w > HSTX_VID_W_MAX) vid_w = HSTX_VID_W_MAX;
    if (vid_h > g_region_h) vid_h = g_region_h;
    if (vid_w < 2) vid_w = 2;
    if (vid_h < 1) vid_h = 1;
    vid_w &= ~1;
    g_movie_w = vid_w;
    g_movie_h = vid_h;

    for (int yy = 0; yy < g_region_h; yy++) {
        uint16_t *row = g_region_base + (size_t)yy * g_video_stride;
        for (int xx = 0; xx < HSTX_VID_W_MAX; xx++) row[xx] = 0x0000;
    }
    int off_x = (HSTX_VID_W_MAX - vid_w) / 2;
    int off_y = (g_region_h - vid_h) / 2;
    g_video_base = g_region_base + (size_t)off_y * g_video_stride + off_x;
}

uint16_t *hstx_dvi_video_base(void)  { return g_video_base; }
uint16_t *hstx_dvi_region_base(void) { return g_region_base; }
int       hstx_dvi_video_stride(void) { return g_video_stride; }
int       hstx_dvi_video_w(void)      { return g_movie_w; }
int       hstx_dvi_video_h(void)      { return g_movie_h; }
int       hstx_dvi_region_h(void)     { return g_region_h; }

void hstx_dvi_enable(bool on) {
    // Park the three DATA lanes (GP14-19) low when off; leave the CLK pair
    // (GP12/13) on HSTX so the monitor keeps its pixel-clock lock. Instant, and
    // it never touches the scanout DMA or the framebuffer.
    for (int g = 14; g <= 19; g++) {
        if (on) {
            gpio_set_function(g, 0);   // back to HSTX
        } else {
            gpio_init(g);              // -> SIO
            gpio_set_dir(g, GPIO_OUT);
            gpio_put(g, 0);
        }
    }
}
```

- [ ] **Step 4: Add hstx_dvi.c to the BSP library**

In `bsp/CMakeLists.txt`, in the `add_library(freewili2_bsp STATIC ...)` source list, add (next to `display/dvi_osd.c` from Task 2):

```cmake
    display/hstx_dvi.c
```

- [ ] **Step 5: Add the includes to the umbrella header**

In `bsp/fw2.h`, after the existing `#include "display/st7796.h"` line, add:

```c
#include "display/hstx_dvi.h"   // (harvested: 640x480p60 DVI over HSTX, GPIO 12-19)
#include "display/dvi_osd.h"    // (harvested: software OSD for the DVI video region)
```

- [ ] **Step 6: Build an app to compile the new driver into the lib**

Run: `python tools/fw.py build hello_display`
Expected: configures and builds cleanly. `bsp/display/hstx_dvi.c` compiles as part of `freewili2_bsp` with no errors or warnings (HSTX registers resolve from the header-only `hardware/structs/hstx_*.h`; `clk_hstx` / `CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLK_SYS` / `DREQ_HSTX` from the already-linked `hardware_clocks` / `hardware_dma`). If the linker reports an undefined HSTX symbol, add `hardware_hstx` to `target_link_libraries(freewili2_bsp PUBLIC ...)` in `bsp/CMakeLists.txt` and rebuild — but none is expected.

- [ ] **Step 7: Run host tests (regression check)**

Run: `python tools/fw.py test`
Expected: all host tests PASS (the `dvi_osd` test from Task 2 and all prior tests). The new driver is target-only and not in the host tree.

- [ ] **Step 8: Commit**

```bash
git add bsp/display/hstx_modes.h bsp/display/hstx_dvi.h bsp/display/hstx_dvi.c bsp/CMakeLists.txt bsp/fw2.h
git commit -m "feat(display): harvest plain 640x480p60 DVI-over-HSTX driver

hstx_dvi scanout (GPIO 12-19) copied from ../movieplayer with the
HDMI-audio-island path stripped: zero-IRQ baked-command DMA, SRAM
framebuffer, HSTX-generated border. clk_hstx = clk_sys/2 = 126 MHz ->
25.2 MHz pixel. Stored region raised to the full 480x320 panel.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: `hello_dvi` demo app

An on-hardware smoke test following the `hello_display` / `hello_usbdrive` pattern: paint a colour-bar test pattern + box outline into the DVI video region, draw an OSD title and an animating progress bar in the bottom margin, and stream it out the DVI connector.

**Files:**
- Create: `apps/hello_dvi/main.c`
- Create: `apps/hello_dvi/CMakeLists.txt`
- Create: `apps/hello_dvi/README.md`
- Modify: `CMakeLists.txt` (top level) — `add_subdirectory(apps/hello_dvi)`

**Interfaces:**
- Consumes: `board_init()` (Task 1 clock), the `hstx_dvi_*` API and OSD API (Tasks 2–3), `DIAG` (`bsp/platform/diag.h`).

- [ ] **Step 1: Write the demo main**

Create `apps/hello_dvi/main.c`:

```c
// hello_dvi — on-hardware smoke test for the harvested plain-DVI driver
// (bsp/display/hstx_dvi) + the DVI OSD. RTT-only. Paints an 8-bar colour test
// pattern + a 1px box outline into the 480x288 video region (centered in the
// 640x480 DVI frame), draws an OSD title + an animating progress bar in the
// bottom letterbox margin, and streams it out the DVI connector (GPIO 12-19).
//
// Pass criteria on a DVI monitor: locks to 640x480, 8 vertical colour bars
// (white, yellow, cyan, green, magenta, red, blue, black) inside a white box,
// black borders around, and a progress bar sweeping left->right along the
// bottom. RTT prints "hello_dvi: DVI up, clk_hstx=126000 kHz".
#include "fw2.h"
#include "platform/diag.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"

#define VID_W 480
#define VID_H 288

// Eight classic colour-bar RGB565 values (native little-endian, as stored).
static const uint16_t BARS[8] = {
    0xFFFF, // white
    0xFFE0, // yellow
    0x07FF, // cyan
    0x07E0, // green
    0xF81F, // magenta
    0xF800, // red
    0x001F, // blue
    0x0000, // black
};

// Paint 8 vertical colour bars across the video region, then a 1px white box
// outline around the whole region, into the strided native-endian framebuffer.
static void paint_test_pattern(void) {
    uint16_t *base = hstx_dvi_video_base();
    int stride = hstx_dvi_video_stride();
    int w = hstx_dvi_video_w();
    int h = hstx_dvi_video_h();
    for (int y = 0; y < h; y++) {
        uint16_t *row = base + (size_t)y * stride;
        for (int x = 0; x < w; x++) row[x] = BARS[(x * 8) / w];
    }
    // 1px white box outline.
    for (int x = 0; x < w; x++) {
        base[x] = 0xFFFF;
        base[(size_t)(h - 1) * stride + x] = 0xFFFF;
    }
    for (int y = 0; y < h; y++) {
        base[(size_t)y * stride] = 0xFFFF;
        base[(size_t)y * stride + (w - 1)] = 0xFFFF;
    }
}

int main(void) {
    board_init_clk(252000);             // 252 MHz -> exact 25.2 MHz DVI pixel clock
                                        // (the board default via board_init() is 250)
    hstx_dvi_init(VID_W, VID_H);        // start the scanout (480x288 in 640x480)
    DIAG("hello_dvi: DVI up, clk_hstx=%u kHz\n",
         (unsigned)(clock_get_hz(clk_hstx) / 1000u));

    paint_test_pattern();

    // OSD title, centered in the bottom letterbox margin below the movie.
    uint16_t *rbase = hstx_dvi_region_base();
    int rstride = hstx_dvi_video_stride();
    int region_h = hstx_dvi_region_h();
    dvi_osd_text_msg(rbase, rstride, VID_W, region_h, VID_H, "FREEWILI 2 DVI");
    sleep_ms(1500);

    // Animate a progress bar across the bottom margin so motion proves the
    // scanout is live and reading the framebuffer continuously.
    uint32_t t = 0;
    for (;;) {
        dvi_osd_progress(rbase, rstride, VID_W, region_h, VID_H,
                         t % 101u, 100u, "DVI");
        t++;
        sleep_ms(50);
    }
}
```

- [ ] **Step 2: Write the app CMake**

Create `apps/hello_dvi/CMakeLists.txt`:

```cmake
add_executable(hello_dvi main.c)
target_link_libraries(hello_dvi freewili2_bsp)
pico_set_binary_type(hello_dvi copy_to_ram)
pico_add_extra_outputs(hello_dvi)
```

- [ ] **Step 3: Register the app in the top-level CMake**

In the top-level `CMakeLists.txt`, after `add_subdirectory(apps/hello_usbdrive)`, add:

```cmake
add_subdirectory(apps/hello_dvi)
```

- [ ] **Step 4: Write the app README**

Create `apps/hello_dvi/README.md`:

```markdown
# hello_dvi

On-hardware smoke test for the harvested plain-DVI driver (`bsp/display/hstx_dvi`)
and the DVI OSD (`bsp/display/dvi_osd`). RTT-only diagnostics.

Paints an 8-bar colour test pattern + a 1px white box outline into the 480×288
video region (centered in the 640×480 DVI frame) and draws an OSD title plus an
animating progress bar in the bottom letterbox margin, streamed out the DVI
connector on GPIO 12–19.

## Run

    fw build hello_dvi
    fw flash hello_dvi
    fw rtt        # expect: "hello_dvi: DVI up, clk_hstx=126000 kHz"

## Pass criteria (bench monitor over DVI)

- Locks to 640×480.
- Eight vertical colour bars (white, yellow, cyan, green, magenta, red, blue,
  black), left→right, inside a white box outline, with black borders around.
- Colours correct (validates the native-endian pixel path and the FreeWili
  differential polarity).
- A progress bar sweeps left→right along the bottom margin (proves the scanout
  is continuously reading the framebuffer).

No monitor attached = nothing to observe (no HPD/EDID); RTT still prints the
"DVI up" line and the board does not crash.
```

- [ ] **Step 5: Build the app**

Run: `python tools/fw.py build hello_dvi`
Expected: builds cleanly; produces `build/apps/hello_dvi/hello_dvi.uf2` (and `.elf`).

- [ ] **Step 6: Commit**

```bash
git add apps/hello_dvi/ CMakeLists.txt
git commit -m "feat(apps): hello_dvi DVI colour-bar + OSD smoke demo

Paints an 8-bar test pattern + box outline into the 480x288 DVI video
region and an animating OSD progress bar in the bottom margin, streamed
out GPIO 12-19. RTT prints the clk_hstx rate.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

- [ ] **Step 7: On-hardware verification (human)**

Flash and observe on a DVI monitor:

```bash
python tools/fw.py flash hello_dvi
python tools/fw.py rtt
```

Confirm against the README pass criteria: 640×480 lock, correct colour bars in a white box, black borders, sweeping progress bar, and the RTT `DVI up, clk_hstx=126000 kHz` line. Record the result in the commit trailer or a findings note. (No automated hardware gate — this is a visual check.)

---

### Task 5: Documentation — catalog, pinmap, driver doc

Record the DVI peripheral as DONE and document its usage, per the AGENTS.md harvest procedure.

**Files:**
- Modify: `docs/hardware/catalog.md` (DVI row TODO → DONE)
- Modify: `docs/hardware/pinmap.md` (add DVI pins)
- Create: `docs/drivers/dvi.md`

**Interfaces:** none (docs only).

- [ ] **Step 1: Flip the catalog row to DONE**

In `docs/hardware/catalog.md`, the DVI/HSTX row currently reads:

```
| DVI / HSTX | DVI_CLK_N/P=12/13, DVI_D0_N/P=14/15, DVI_D1_N/P=16/17, DVI_D2_N/P=18/19 | Owner repo not yet confirmed (HSTX peripheral, likely a fresh Pico SDK HSTX example port) |
```

Change the third column to record it as DONE, harvested from the movie player:

```
| DVI / HSTX | DVI_CLK_N/P=12/13, DVI_D0_N/P=14/15, DVI_D1_N/P=16/17, DVI_D2_N/P=18/19 | **DONE** — plain 640x480p60 DVI (`bsp/display/hstx_dvi`) harvested from ../movieplayer; HDMI-audio-island mode not harvested. See docs/drivers/dvi.md |
```

- [ ] **Step 2: Add the DVI pins to the pinmap**

In `docs/hardware/pinmap.md`, add rows for the DVI/HSTX pins (GPIO 12–19) in the same table format the file already uses, noting `_P` = odd gpio (non-inverted), `_N` = even gpio (inverted): DVI_CLK 12/13, DVI_D0 14/15, DVI_D1 16/17, DVI_D2 18/19. Match the existing column layout of the file.

- [ ] **Step 3: Write the driver usage doc**

Create `docs/drivers/dvi.md`:

```markdown
# DVI video output (`bsp/display/hstx_dvi`) + OSD (`bsp/display/dvi_osd`)

Plain 640×480p60 DVI over the RP2350 HSTX block (GPIO 12–19), harvested from the
FreeWili 2 movie player. Turns an SRAM framebuffer into a DVI signal with a
zero-IRQ baked-command DMA scanout. The HDMI-audio-island mode of the source was
**not** harvested (DVI carries video only here).

## Clock

`hstx_dvi_init` sets `clk_hstx = clk_sys / 2` (read from `clk_sys` at runtime), and
the HSTX CSR divides by 5, so the pixel clock is `clk_sys / 10`. The board **default
is 250 MHz** (audio-optimal; see invariant 2), which gives a 25.0 MHz pixel clock —
0.7% below the 640×480p60 standard 25.175 MHz, but within most monitors' tolerance.
For an **exact 25.2 MHz** clock, bring the board up at 252 MHz with
`board_init_clk(252000)` instead of `board_init()` (`clk_hstx = 126 MHz`). `clk_sys`
must be at its final even-MHz value before `hstx_dvi_init`. Note: 252 MHz shifts the
NAU88C10 audio pitch ~0.8% (`clk_sys/61` MCLK), so only opt in where DVI timing
matters more than audio, or in DVI-only apps like `hello_dvi`.

## Model

- The DVI frame is a fixed 640×480. A centered **video region** of up to
  `HSTX_VID_W_MAX`×`HSTX_VID_H_MAX` (480×320) is stored in SRAM; the surrounding
  border is generated by the HSTX command list (`HSTX_CMD_TMDS_REPEAT`), not
  stored.
- The framebuffer lives in **SRAM** (~327 KB). PSRAM scanout underruns the HSTX
  FIFO and wedges flash — do not move it. `HSTX_VID_H_MAX` is a compile-time
  `#define`; lower it if you link the DVI driver alongside the USB/FatFs + LCD
  stack in one binary.
- Scanout is **zero-IRQ** (a SCREEN + LOOPER DMA pair), so it does not touch
  `DMA_IRQ_0`. Two DMA channels are claimed dynamically at init.
- Pixels are **native little-endian RGB565** — write `v` directly, no byte-swap.

## API

```c
void hstx_dvi_init(int vid_w, int vid_h);        // configure + start scanout
void hstx_dvi_set_geometry(int vid_w, int vid_h);// recenter/resize (no DMA touch)
uint16_t *hstx_dvi_video_base(void);             // top-left of the centered movie
uint16_t *hstx_dvi_region_base(void);            // pixel(0,0) of the fixed region
int  hstx_dvi_video_stride(void);                // uint16 elements between rows
int  hstx_dvi_video_w(void);
int  hstx_dvi_video_h(void);
int  hstx_dvi_region_h(void);                    // fixed region height
void hstx_dvi_enable(bool on);                   // blank/unblank the 3 data lanes
```

Write row `y` of the video at `hstx_dvi_video_base() + y*hstx_dvi_video_stride()`,
`hstx_dvi_video_w()` pixels wide. The stride exceeds the width because pixels are
interleaved with HSTX command words — always index by the stride.

## OSD

`dvi_osd_*` draws status text and a progress bar into the region framebuffer
(origin = `hstx_dvi_region_base()`, using `hstx_dvi_video_stride()`), in the
bottom letterbox margin below the movie. It reuses `bsp/display/font5x7`. It only
writes `x ∈ [0, region_w)` so it never touches the interleaved command words. Pure
and host-tested (`tests/test_dvi_osd.c`).

## Example

See `apps/hello_dvi` — colour-bar test pattern + box outline + animated OSD.
```

- [ ] **Step 4: Commit**

```bash
git add docs/hardware/catalog.md docs/hardware/pinmap.md docs/drivers/dvi.md
git commit -m "docs: DVI driver catalog/pinmap/usage for the HSTX harvest

Flip the DVI/HSTX catalog row to DONE, add GPIO 12-19 to the pinmap, and
document hstx_dvi + dvi_osd usage and the 252->126->25.2 MHz clock chain.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Self-Review

**1. Spec coverage** (design §§ checked against tasks):
- Scope: plain DVI + OSD, HDMI path dropped → Tasks 2 (OSD), 3 (driver strip). ✓
- Project-selectable clock via `board_init_clk` (default 250 MHz preserved, audio-optimal) + invariant-2/facts notes → Task 1. ✓
- DVI demo opts into 252 MHz for exact pixel clock → Task 4 (`board_init_clk(252000)`). ✓
- Stored region 480×320 → Task 3 (`HSTX_VID_H_MAX 320`). ✓
- Files harvested into `bsp/display/`, font reused → Tasks 2–3, `font5x7` unchanged. ✓
- Driver public surface (init/set_geometry/accessors/enable) → Task 3 header. ✓
- The strip (removed includes/functions/arena) → Task 3 Step 3. ✓
- `hstx_modes.h` trim → Task 3 Step 1. ✓
- Demo app `hello_dvi` (bars + box + OSD + DIAG) → Task 4. ✓
- Wiring (CMake lib, fw2.h, top-level add_subdirectory) → Tasks 2/3/4. ✓
- Docs (catalog/pinmap/driver doc) → Task 5. ✓
- Host test for OSD geometry → Task 2. ✓

**2. Placeholder scan:** No TBD/TODO/"handle edge cases"/"similar to Task N". All code steps show complete content; verbatim copies use exact `cp` commands. ✓

**3. Type consistency:** `hstx_dvi_*` signatures identical across the header (Task 3 Step 2), the `.c` (Step 3), the OSD-independent accessors, and the `hello_dvi` calls (Task 4). OSD signatures in the test (Task 2 Step 1) match `dvi_osd.h`. `hstx_dvi_video_stride()` is the single stride source used for both video and region (region_base shares the video stride) — the demo passes it as `rstride` consistently. `HSTX_VID_H_MAX`/`HSTX_VID_W_MAX` used identically in the header and the `HSTX_PLAIN_DWORDS` sizing. ✓

One consistency note enforced in the code: the demo draws the OSD with `hstx_dvi_video_stride()` (not a separate region stride) because the region and video share one stride — matches the driver's contract that `region_base` and `video_base` are the same buffer at the same stride.
