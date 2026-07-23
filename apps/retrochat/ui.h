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

// Compose bar: replaces the canned grid while typing on the chord keyboard.
void ui_compose_show(const char *draft, const char *labels[5]);
void ui_compose_hide(void);

#endif
