// ui.h — RetroChat touch UI: status bar, scrolling chat, canned-button grid.
#ifndef RC_UI_H
#define RC_UI_H
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    UI_NONE = -1,
    UI_CANNED_0 = 0,          // ..UI_CANNED_7 = 7 map to proto_canned[i]
    UI_RESEND = 8,
    UI_SELFTEST_TOGGLE = 9,   // long-press (>=1 s) on the status bar
} ui_action_t;

void ui_init(uint8_t self_id);
void ui_add_message(uint8_t sender, const char *text, bool own);
void ui_set_status(bool txing, bool selftest);
void ui_set_stats(unsigned crc_err, int peak);
ui_action_t ui_poll(void);
// Clear ui_poll's press-tracking state (call when compose mode takes over
// touch, so a stale release doesn't fire a canned send on return to the grid).
void ui_poll_reset(void);

// Chord-keyboard label bar: always visible at the bottom of the screen.
void ui_kb_bar(const char *labels[5]);
// Compose: replaces the canned grid with the draft line while typing (the
// label bar below stays; refresh it via ui_kb_bar after hide).
void ui_compose_show(const char *draft, const char *labels[5]);
void ui_compose_hide(void);

#endif
