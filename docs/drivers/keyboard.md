# Keyboard (`bsp/keyboard/`, `bsp/input/uartkbd*`) — fw2kb chord engine + FW2 UART keyboard

**What it does:** two-press chord text entry on the 5 colored FW2 buttons
(gray, yellow, green, blue, red): first press picks a group of 5 characters,
second press picks the character — every a-y letter is exactly two presses.
PAGE cycles pages (upper / lower / numbers / symbols / hex; mid-chord it
cancels the chord). Touch is space (below the app's split line) or
backspace (above). Button state arrives from the FW2 UART keyboard
(button coprocessor) as unsolicited 23-byte frames.

**Pieces:**
- `keyboard/fw2kb.{h,c}`, `keyboard/fw2kb_hidmap.c` — the chord engine,
  harvested verbatim from the owner's `wilikeyboard` repo (poll-based,
  no malloc, no callbacks; host-tested by `tests/test_fw2kb*.c`). The HID
  map translates USB HID usage+modifier pairs — unused on-device today,
  available for a future USB/PC-bridge input source.
- `input/uartkbd_parse.{h,c}` — pure frame parser + press/release edge
  detection (host-tested by `tests/test_uartkbd_parse.c`). Protocol per
  `FreeWilli/vibe/Wilikeyboard.md`: sync 0xBD 0x1D, buttons in bytes 2-5,
  additive 8-bit checksum of bytes 0-21 in byte 22.
- `input/uartkbd.{h,c}` — UART1 binding @ 62500 8N1, GPIO38 (TX, claimed,
  never driven) / GPIO39 (RX), `GPIO_FUNC_UART_AUX` (the plain UART
  function on these pins is CTS/RTS). RX-only, polled.
  RX bytes are drained by a dedicated DMA channel (endless mode, no IRQ)
  into a 1 KB SRAM ring; `uartkbd_task()` reads behind the DMA write
  pointer, so keyboard input survives CPU stalls up to ~164 ms (e.g. long
  blocking renders) without losing frames.

**How to use:**

    #include "fw2.h"

    fw2kb_t kb;
    uartkbd_init();
    fw2kb_init(&kb);
    fw2kb_set_touch_threshold(&kb, 136);

    for (;;) {
        uartkbd_task();                         /* drain UART each loop */
        uartkbd_event_t bev;
        while (uartkbd_next_event(&bev)) {
            if (!bev.pressed) continue;
            if (bev.btn <= UARTKBD_BTN_RED) fw2kb_press(&kb, (fw2kb_btn)bev.btn);
            else if (bev.btn == UARTKBD_BTN_PAGE) fw2kb_press(&kb, FW2KB_BTN_AI);
        }
        fw2kb_event kev;
        while (fw2kb_next_event(&kb, &kev)) { /* CHAR/BACKSPACE/... */ }
        const char *labels[5];
        fw2kb_get_labels(&kb, labels);          /* draw on the soft buttons */
    }

`apps/hello_keyboard` is the worked example (soft-button bar + text area +
touch zones). Link health: `uartkbd_frames()` / `uartkbd_errors()` — a
healthy link shows frames climbing and errors static.

**Display note:** `font5x7`/`st7796_draw_text` cover full printable ASCII
0x20-0x7E (lowercase included) as of the fw2kb work — the earlier
uppercase-folding caveat no longer applies. The RTT `fw2kb char` log remains
the authoritative record of generated characters.

**Protocol assumptions to re-verify when the keyboard firmware lands**
(the PIC-side firmware was being updated when this driver was written):
checksum = additive 8-bit sum of bytes 0-21; button bits 1 = pressed;
AUDIO/HOTPLUG/USB detect bits are level flags. All three live in
`uartkbd_parse.c` only.

**Dependencies:** none beyond `pico_stdlib` (`hardware_uart` comes with it).
UART1 must stay free for this driver (UART0 is the OneWili/FwGUI link).
