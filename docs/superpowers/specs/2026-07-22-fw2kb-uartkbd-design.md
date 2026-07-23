# fw2kb + UART keyboard on FreeWili 2 hardware (design)

Date: 2026-07-22
Status: approved

## Purpose

Get the fw2kb two-press chord keyboard working on FreeWili 2 hardware:
harvest the finished `fw2kb` library from the `wilikeyboard` repo into this
BSP, add a driver for the FW2 **UART keyboard** (the button processor that
streams button-state frames into the display CPU), and ship a demo app that
types on the ST7796 display with touch space/backspace.

No USB involvement. Input comes from the real device buttons over UART.

## Sources of truth

- **fw2kb library**: `C:\~prj\Dropbox\vibeProjects\wilikeyboard\src\`
  (fw2kb.h, fw2kb.c, fw2kb_hidmap.c) — reviewed, tested, frozen. Harvested
  verbatim, zero changes.
- **UART keyboard protocol**: `C:\~prj\Dropbox\FreeWilli\vibe\Wilikeyboard.md`.
  This SUPERSEDES the FW2 firmware's `rpPICComm` protocol — the firmware
  source is stale on message bytes; the keyboard firmware is being updated
  now. Where Wilikeyboard.md is silent, assumptions are flagged below.

## Protocol (from Wilikeyboard.md)

UART on display-CPU GPIO38 (TX) / GPIO39 (RX), **62500 baud, 8N1**.
Frames arrive **unsolicited** (confirmed by owner) — the driver is RX-only;
GPIO38 is claimed for UART but never driven with data.

23-byte fixed frame:

| Offset | Content |
|--------|---------|
| 0 | sync `0xBD` |
| 1 | sync `0x1D` |
| 2 | b0 GREY, b1 YELLOW, b2 GREEN, b3 BLUE, b4 RED, b5 NAV_CENTER (b6-7 reserved) |
| 3 | b0 NAV_DOWN, b1 HOTPLUG_DET, b2 AUDIO_DET, b3 NAV_RIGHT, b4 NAV_UP, b5 NAV_LEFT (b6-7 reserved) |
| 4 | b2 USB_DET_PIC, b7 HOME_PB (rest reserved) |
| 5 | b0 OK_PB, b1 CANCEL_PB, b2 PAGE_PB (rest reserved) |
| 6-21 | reserved |
| 22 | 8-bit checksum over bytes 0-21 |

**Flagged assumptions** (keyboard firmware is in flux; the parser is one
small pure file so corrections are cheap):

1. Checksum = additive 8-bit sum: `byte22 == (sum of bytes 0..21) & 0xFF`.
2. Button bits are 1 = pressed, 0 = released.
3. AUDIO_DET / HOTPLUG_DET / USB_DET_PIC are level flags (connection
   status), not momentary buttons — exposed as flags, never as events.

## Architecture

```
apps/hello_keyboard      draws UI, owns text buffer, routes input
bsp/keyboard/fw2kb*      chord engine (harvested, frozen)
bsp/input/uartkbd.{c,h}  UART1 binding: init, FIFO drain      (hardware)
bsp/input/uartkbd_parse.{c,h}  frame parser + edge detection  (pure, host-testable)
```

### uartkbd_parse (pure logic, no hardware includes)

Byte-at-a-time state machine: hunt `0xBD` → expect `0x1D` (else re-hunt,
re-considering the current byte as a possible `0xBD`) → collect through
byte 22 → verify checksum → latch. A failed checksum discards the frame and
returns to hunting. Reserved bits are masked off before use.

On each valid frame: compare the 14-button state against the previous
frame's, and push one press/release event per changed button into a small
fixed ring (8 entries, oldest dropped — same policy as fw2kb). Detect flags
update silently.

Amendment (2026-07-22, hardware finding): the first valid frame primes the
baseline without emitting events — the keyboard coprocessor's boot frames
carry garbage button bits that otherwise produce phantom presses (observed:
a phantom PAGE press cycling the keyboard off its boot page).

Button enum (order fixed, used in the event and the raw-state bitmask):
GREY, YELLOW, GREEN, BLUE, RED, NAV_CENTER, NAV_UP, NAV_DOWN, NAV_LEFT,
NAV_RIGHT, HOME, OK, CANCEL, PAGE.

API:

```c
void uartkbd_parse_init(uartkbd_parser_t *p);
void uartkbd_parse_byte(uartkbd_parser_t *p, uint8_t b);
bool uartkbd_parse_next_event(uartkbd_parser_t *p, uartkbd_event_t *ev);
     /* ev = { uartkbd_btn_t btn; bool pressed; } */
uint16_t uartkbd_parse_buttons(const uartkbd_parser_t *p);  /* bit = btn enum */
uint8_t  uartkbd_parse_flags(const uartkbd_parser_t *p);    /* AUDIO/HOTPLUG/USB bits */
uint32_t uartkbd_parse_frames(const uartkbd_parser_t *p);   /* valid-frame counter */
uint32_t uartkbd_parse_errors(const uartkbd_parser_t *p);   /* checksum/sync failures */
```

### uartkbd (hardware binding)

```c
void uartkbd_init(void);   /* UART1 @ 62500 8N1, GPIO38/39, UART-AUX pin function */
void uartkbd_task(void);   /* drain RX FIFO through the parser; call every loop */
/* thin pass-throughs to the parser: */
bool     uartkbd_next_event(uartkbd_event_t *ev);
uint16_t uartkbd_buttons(void);
uint8_t  uartkbd_flags(void);
uint32_t uartkbd_frames(void);
uint32_t uartkbd_errors(void);
```

GPIO38/39 take the RP2350 **UART-AUX** function (on these pins the plain
UART function is CTS/RTS; UART-AUX routes TX/RX). GPIO38/39 are currently
unused in `bsp/platform/board.h` — no conflicts. UART1 is free (UART0 at
8 Mbaud is the OneWili/FwGUI link). Polled, no IRQs — matches the BSP's
convention (same as usbhost/IR).

## Harvest (per the BSP add-a-driver workflow)

1. Copy `fw2kb.h`, `fw2kb.c`, `fw2kb_hidmap.c` → `bsp/keyboard/` verbatim.
   (The HID map is unused this phase but ships as part of the frozen
   library — a future USB or PC-bridge input source feeds it.)
2. Add all new `.c` files to `add_library(freewili2_bsp STATIC ...)`.
3. `bsp/fw2.h`: add `#include "keyboard/fw2kb.h"` and
   `#include "input/uartkbd.h"`.
4. Copy `tests/test_fw2kb.c`, `tests/test_fw2kb_hid.c`, `tests/test_util.h`
   from wilikeyboard into `tests/`, register in `tests/CMakeLists.txt`.
5. `docs/hardware/catalog.md`: keyboard row → DONE.
   New doc: `docs/drivers/keyboard.md` covering fw2kb + uartkbd + demo.

## Demo app: `apps/hello_keyboard`

480x320 landscape (ST7796_W x ST7796_H).

Layout:
- Bottom bar, y 272-319: five 96x48 soft buttons colored gray, yellow,
  green, blue, red, each showing its live label from `fw2kb_get_labels()`
  (group strings, or single characters mid-chord) via `st7796_draw_text`.
- Text area, y 0-271: typed text, wrapped, scale-2 font. A dim divider at
  y 136 with hint text: above = backspace zone, below = space zone.

Input routing each main-loop iteration:
- `uartkbd_task()`; then for each `uartkbd_next_event`:
  - GREY/YELLOW/GREEN/BLUE/RED pressed → `fw2kb_press(FW2KB_BTN_GRAY..RED)`
  - PAGE pressed → `fw2kb_press(FW2KB_BTN_AI)` (page cycle / chord cancel)
  - NAV_*/OK/CANCEL/HOME → `DIAG()` log only (not fw2kb's concern)
  - releases ignored (fw2kb acts on presses)
- `ft6336_poll(&x,&y)`: y >= 272 → ignored (button bar is display-only;
  chords come from the physical buttons). Else `fw2kb_touch(x, y)` with
  `fw2kb_set_touch_threshold(136)`: y > 136 → space, else backspace.
  One event per touch-down (gate on release so holding doesn't repeat).
- Drain `fw2kb_next_event`: CHAR appends to the line buffer, BACKSPACE
  deletes, ENTER newline, TAB four spaces; SAVE/EXIT/nav → `DIAG()` only.
- Redraw text area and button bar only when their content changed
  (labels change on every chord press / page cycle).

Diagnostics: `DIAG()` per button event and a once-per-second link line
(frames / errors counters) so `fw rtt` shows the keyboard stream health.

## Error handling

- Parser: bad sync or checksum → count in `errors`, resync; never stalls.
- No frames arriving (keyboard firmware absent/old): demo still runs —
  touch space/backspace works, screen shows labels; the RTT link line shows
  frames=0, which is the diagnostic.
- fw2kb is total (proven in phase 1); no failure modes added here.

## Testing

Host CTest (`fw test`):
- `test_fw2kb.c`, `test_fw2kb_hid.c` — copied unchanged from wilikeyboard.
- `test_uartkbd_parse.c` — new:
  - valid frame → buttons latched, frames counter increments
  - press + release edges emitted per changed button; multiple changes in
    one frame → multiple events
  - checksum failure → frame dropped, errors increments, state unchanged
  - garbage stream + resync mid-stream (0xBD inside payload of a corrupt
    frame must not derail the hunt indefinitely)
  - reserved bits set in bytes 2-5 → masked, no phantom buttons
  - detect flags (AUDIO/HOTPLUG/USB) update but emit no events
  - event ring overflow drops oldest

On-hardware smoke (the real acceptance test):
`fw build hello_keyboard` → `fw flash hello_keyboard` → press physical
buttons: chord types characters, PAGE cycles pages, touch does
space/backspace, `fw rtt` shows button events and a healthy frame counter.

## Out of scope

- USB keyboard input (dropped by owner decision — UART keyboard only)
- TX side of the keyboard UART (frames are unsolicited; no commands sent)
- Nav-button cursor movement / text editing beyond append+backspace
- LVGL renderer, line-editor widget
- Keyboard firmware itself (being updated separately; parser assumptions
  above are the interface contract to re-verify when it lands)
