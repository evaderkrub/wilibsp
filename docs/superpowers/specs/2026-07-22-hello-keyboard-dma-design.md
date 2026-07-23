# hello_keyboard DMA framebuffer rendering (design)

Date: 2026-07-22
Status: approved

## Purpose

Eliminate the input blind spot in `apps/hello_keyboard`: blocking SPI redraws
(10-40 ms on a full text area) currently stall the main loop past the UART RX
FIFO's ~5 ms fill time, dropping keyboard frames (`uartkbd_errors()` climbing
during heavy typing). Rework rendering to draw into a PSRAM framebuffer and
flush with the ST7796 driver's existing non-blocking DMA path, so core 0
never stops polling input.

No BSP driver changes — `st7796_flush_async()` / `st7796_flush_busy()`
(shared DMA_IRQ_0, hardware-proven) are used as-is. App-only rework.

## Architecture

```
core 0 main loop (never blocks):
  uartkbd_task / handle_buttons / handle_touch / handle_fw2kb_events
  -> render changed regions into fb (memory writes, microseconds-fast)
  -> if fb dirty && !st7796_flush_busy(): st7796_flush_async(full screen, fb)
DMA (background): streams 307,200 bytes fb -> SPI1 -> panel (~25 ms)
```

- **Framebuffer:** `uint16_t *fb = (uint16_t *)PSRAM_BASE`, 480x320 RGB565
  wire-order (big-endian), 307,200 bytes in the 8 MB PSRAM (per AGENTS.md:
  large buffers live in PSRAM). App calls `psram_init()` after `board_init()`
  (the `orca_browser` pattern) and halts with a DIAG if it returns
  < 307,200.
- **Software rendering helpers (app-local):** `fb_fill_rect(x,y,w,h,color)`
  and `fb_draw_text(x,y,scale,fg,bg,str)` — same font5x7 glyph loop as
  `st7796_draw_text` (including the 0x20-0x7E range, no case fold) but
  writing into `fb` instead of SPI. `draw_text_area()`/`draw_bar()` switch
  from `st7796_*` to `fb_*` calls; layout, colors, and content logic are
  unchanged.
- **Flush policy (coalescing):** rendering sets `s_fb_dirty`. Each loop pass:
  if `s_fb_dirty && !st7796_flush_busy()`, clear the flag and
  `st7796_flush_async(0, 0, ST7796_W-1, ST7796_H-1, fb, NULL)`. Changes that
  land during an in-flight flush stay dirty and go out in the next flush —
  worst-case display latency is one flush (~25 ms), imperceptible for
  typing; input latency is unaffected.
- **Tearing / mutation during flush:** accepted for this demo. The DMA reads
  `fb` while later loop passes may mutate it; worst case is a partially
  updated glyph for one frame, corrected by the coalesced follow-up flush.
  (A double buffer in PSRAM would remove this; YAGNI for a text demo.)
- **Boot:** first frame renders into `fb` (both regions dirty at init) and
  flushes; `st7796_init()`/backlight sequence unchanged.

## Constraints

- DMA source is the PSRAM XIP window (0x11000000). DMA reads from the QMI
  memory-mapped window pace against SPI TX DREQ (SPI at <=100 MHz is the
  bottleneck, not PSRAM). If hardware disproves this (visible corruption or
  a wedged flush), fall back to a 512 KB-budget-checked SRAM framebuffer or
  per-region flushes — but test the PSRAM path first.
- `st7796_flush_async` registers on shared DMA_IRQ_0 (already done in
  `st7796_init()`); nothing new to wire.
- The blocking `st7796_draw_text`/`st7796_fill_rect` calls disappear from
  the app's steady-state loop entirely (no mixed blocking/async SPI use, so
  no bus-arbitration concerns).

## Error handling

- `psram_init()` too small/absent: DIAG and halt (demo requires it).
- Flush completion is IRQ-driven inside the driver; the app only ever polls
  `st7796_flush_busy()` — no callback state.

## Testing

- Host tests: unchanged (rendering helpers are thin; the pure logic —
  fw2kb, uartkbd_parse — is already covered). No new host tests.
- On-hardware acceptance: type fast with a near-full text area while
  watching `fw rtt`: `uartkbd errors` must stay flat (previously climbed
  during redraws); typed characters all appear; no visible tearing worse
  than transient single-glyph artifacts.

## Out of scope

- BSP driver changes, double buffering, partial-region flushes, core-1
  rendering (rejected in favor of the driver's existing DMA path).
