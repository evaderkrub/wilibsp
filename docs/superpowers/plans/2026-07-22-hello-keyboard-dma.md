# hello_keyboard DMA Framebuffer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Commit the hardware-verified styling batch, then rework `apps/hello_keyboard` to render into a PSRAM framebuffer flushed by the ST7796 driver's non-blocking DMA path, so input polling never stalls during redraws.

**Architecture:** App-only change. `main.c` gains `fb_fill_rect`/`fb_draw_text` helpers (same font5x7 glyph math as `st7796_draw_text`, but writing to a `uint16_t*` framebuffer at `PSRAM_BASE`), the draw functions switch to them, and the main loop kicks `st7796_flush_async(0,0,479,319,fb,NULL)` whenever the fb is dirty and no flush is in flight (coalescing). No BSP driver changes.

**Tech Stack:** C11, Pico SDK, wilibsp BSP (`st7796_flush_async`/`st7796_flush_busy`, `psram_init`, font5x7).

**Spec:** `docs/superpowers/specs/2026-07-22-hello-keyboard-dma-design.md`

## Global Constraints

- Framebuffer: `uint16_t *` at `PSRAM_BASE` (0x11000000), 480x320 RGB565 wire-order, 307,200 bytes; app halts with a DIAG if `psram_init()` returns < 307200.
- Flush signature (bsp/display/st7796.h): `void st7796_flush_async(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, const uint16_t *pixels, st7796_flush_done_cb done)` — `done` may be NULL (guarded in the IRQ handler). Never start a flush while `st7796_flush_busy()`.
- No blocking `st7796_draw_text`/`st7796_fill_rect`/`st7796_fill_screen` calls remain in the app's steady-state loop.
- DIAG() only; `%d %u %x %s %c` format specifiers only. Never pass `-DPICO_BOARD`. Build via `python tools/fw.py ...` from `C:\~prj\Dropbox\vibeProjects\wilibsp`.
- The working tree currently holds hardware-verified uncommitted changes (font5x7 0x20-0x7E extension, st7796 case-fold removal, main.c styling/touch). Task 1 commits them WITH doc touch-ups; do not lose or rewrite them.
- Conventional Commits.

---

### Task 1: Commit the verified styling batch + retire stale doc comments

**Files:**
- Modify: `bsp/display/st7796.h:34` (stale "Lowercase maps to uppercase" comment)
- Modify: `apps/hello_keyboard/main.c:1-4` (stale layout header comment)
- Modify: `docs/drivers/keyboard.md` (Display caveat paragraph)
- Commit (already modified in tree, verified on hardware — do not edit further): `bsp/display/font5x7.c`, `bsp/display/font5x7.h`, `bsp/display/st7796.c`, `apps/hello_keyboard/main.c`

**Interfaces:**
- Consumes: the working tree as-is.
- Produces: a clean committed baseline for Task 2.

- [ ] **Step 1: Fix the stale comment in `bsp/display/st7796.h`**

Replace (in the `st7796_draw_text` doc comment):

```c
// Draw ASCII text with the built-in 5x7 font at integer scale (1..4): each
// glyph cell is 6*scale x 8*scale, drawn as one block window per character
// (the proven write shape). Lowercase maps to uppercase; unknown chars are
// blanks. Text that would overrun the panel edge is clipped at whole chars.
```

with:

```c
// Draw ASCII text with the built-in 5x7 font at integer scale (1..4): each
// glyph cell is 6*scale x 8*scale, drawn as one block window per character
// (the proven write shape). Full printable ASCII 0x20-0x7E; unknown chars are
// blanks. Text that would overrun the panel edge is clipped at whole chars.
```

- [ ] **Step 2: Fix the stale header comment in `apps/hello_keyboard/main.c`**

Replace lines 1-4:

```c
// hello_keyboard — fw2kb two-press chord keyboard on the ST7796, driven by
// the FW2 UART keyboard (GREY/YELLOW/GREEN/BLUE/RED chords, PAGE cycles
// pages) with touch space/backspace (below/above the split line).
// Layout (480x320 landscape): text area y 0-271, button bar y 272-319.
```

with:

```c
// hello_keyboard — fw2kb two-press chord keyboard on the ST7796, driven by
// the FW2 UART keyboard (GREY/YELLOW/GREEN/BLUE/RED chords, PAGE cycles
// pages) with touch space/backspace (below/above the split line; the soft
// buttons are display-only). Layout: text area above a one-line button bar
// styled after the FW2 firmware soft menu (see BAR_* defines).
```

- [ ] **Step 3: Rewrite the Display caveat in `docs/drivers/keyboard.md`**

Replace the "Display caveat" paragraph (added earlier, describing uppercase
folding and missing glyphs) with:

```markdown
**Display note:** `font5x7`/`st7796_draw_text` cover full printable ASCII
0x20-0x7E (lowercase included) as of the fw2kb work — the earlier
uppercase-folding caveat no longer applies. The RTT `fw2kb char` log remains
the authoritative record of generated characters.
```

- [ ] **Step 4: Verify tests and build**

Run (from `C:\~prj\Dropbox\vibeProjects\wilibsp`):
`python tools/fw.py test` → Expected: `100% tests passed, 0 tests failed out of 30`
`python tools/fw.py build hello_keyboard` → Expected: clean build.

- [ ] **Step 5: Commit (everything pending — one verified batch)**

```bash
git add bsp/display/font5x7.c bsp/display/font5x7.h bsp/display/st7796.c bsp/display/st7796.h apps/hello_keyboard/main.c docs/drivers/keyboard.md
git commit -m "feat: lowercase font (full printable ASCII), firmware-matched soft-menu styling, touch-anywhere zones"
```

(`build.log` / `test.log` scratch files in the repo root are untracked
leftovers — do not add them; delete them.)

---

### Task 2: DMA framebuffer rendering in hello_keyboard

**Files:**
- Modify: `apps/hello_keyboard/main.c`

**Interfaces:**
- Consumes: `st7796_flush_async` / `st7796_flush_busy` (signatures in Global Constraints), `psram_init()` (`platform/psram.h`, returns detected bytes), `PSRAM_BASE`, `font5x7[]` + `FONT5X7_FIRST/LAST` (`display/font5x7.h`).
- Produces: the final demo app; nothing downstream.

- [ ] **Step 1: Add includes, framebuffer state, and fb helpers**

After the existing includes (`string.h`, `fw2.h`, `platform/diag.h`, `pico/stdlib.h`) add:

```c
#include "display/font5x7.h"
#include "platform/psram.h"
```

After the `s_bar_cache` declaration add:

```c
/* Off-screen framebuffer in PSRAM (AGENTS.md: large buffers live in PSRAM).
 * Rendered by fb_* helpers; flushed whole-screen by DMA (st7796_flush_async)
 * so the main loop never blocks on SPI. Wire-order RGB565, row-major 480x320. */
static uint16_t *const s_fb = (uint16_t *)PSRAM_BASE;
static bool s_fb_dirty = false;
```

Before `draw_bar()` add the two helpers (same clipping/glyph semantics as the
st7796 blocking versions):

```c
static void fb_fill_rect(int x, int y, int w, int h, uint16_t color_be)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > ST7796_W) w = ST7796_W - x;
    if (y + h > ST7796_H) h = ST7796_H - y;
    for (int yy = y; yy < y + h; yy++) {
        uint16_t *row = s_fb + (size_t)yy * ST7796_W + x;
        for (int xx = 0; xx < w; xx++) row[xx] = color_be;
    }
}

static void fb_draw_text(int x, int y, int scale, uint16_t fg_be, uint16_t bg_be,
                         const char *s)
{
    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;
    const int w = 6 * scale, h = 8 * scale;
    for (; *s; s++, x += w) {
        if (x + w > ST7796_W || y + h > ST7796_H || x < 0 || y < 0) break;
        char c = *s;
        const uint8_t *cols = (c >= FONT5X7_FIRST && c <= FONT5X7_LAST)
                                  ? font5x7[c - FONT5X7_FIRST]
                                  : font5x7[0];
        for (int gy = 0; gy < h; gy++) {
            int grow = gy / scale;                /* 0..7; row 7 = line gap */
            uint16_t *row = s_fb + (size_t)(y + gy) * ST7796_W + x;
            for (int gx = 0; gx < w; gx++) {
                int col = gx / scale;             /* 0..5; col 5 = char gap */
                bool on = col < 5 && grow < 7 && ((cols[col] >> grow) & 1);
                row[gx] = on ? fg_be : bg_be;
            }
        }
    }
}
```

- [ ] **Step 2: Switch the draw functions to the fb helpers**

In `draw_bar()` and `draw_text_area()`, replace every `st7796_fill_rect(` with
`fb_fill_rect(` and every `st7796_draw_text(` with `fb_draw_text(` (no other
changes — layout, colors, cache logic stay identical).

- [ ] **Step 3: PSRAM gate in main(), coalesced flush in the loop**

In `main()`, after `board_init();` insert:

```c
    size_t psram_bytes = psram_init();
    if (psram_bytes < (size_t)ST7796_W * ST7796_H * 2) {
        DIAG("hello_keyboard: PSRAM absent/too small (%u bytes) - halting\n",
             (unsigned)psram_bytes);
        for (;;) tight_loop_contents();
    }
```

Replace the loop's draw/flush section:

```c
        if (bar_changed()) s_bar_dirty = true;
        if (s_text_dirty) { draw_text_area(); s_text_dirty = false; }
        if (s_bar_dirty)  { draw_bar();       s_bar_dirty  = false; }
```

with:

```c
        if (bar_changed()) s_bar_dirty = true;
        if (s_text_dirty) { draw_text_area(); s_text_dirty = false; s_fb_dirty = true; }
        if (s_bar_dirty)  { draw_bar();       s_bar_dirty  = false; s_fb_dirty = true; }
        if (s_fb_dirty && !st7796_flush_busy()) {
            s_fb_dirty = false;   /* changes landing mid-flush re-dirty and coalesce */
            st7796_flush_async(0, 0, ST7796_W - 1, ST7796_H - 1, s_fb, NULL);
        }
```

- [ ] **Step 4: Build and regression-test**

Run: `python tools/fw.py build hello_keyboard` → Expected: clean, no warnings from main.c.
Run: `python tools/fw.py test` → Expected: `100% tests passed, 0 tests failed out of 30`.

Grep check: `grep -n "st7796_draw_text\|st7796_fill_rect\|st7796_fill_screen" apps/hello_keyboard/main.c` → Expected: no matches (all rendering goes through fb_*).

- [ ] **Step 5: Commit**

```bash
git add apps/hello_keyboard/main.c
git commit -m "feat: render hello_keyboard via PSRAM framebuffer + DMA async flush"
```

- [ ] **Step 6: On-hardware acceptance (owner-run, board attached)**

```
python tools/fw.py flash hello_keyboard
python tools/fw.py rtt
```

1. UI appears as before (bar, labels, text area, split line) — first flush works from PSRAM.
2. Type fast with a substantially filled text area: `uartkbd errors=N` stays flat (previously climbed during redraws).
3. All typed characters appear; transient single-glyph artifacts during a flush are acceptable per spec; anything worse (persistent corruption, wedged display, flush never completing) triggers the spec's fallback investigation (SRAM framebuffer / per-region flushes).
