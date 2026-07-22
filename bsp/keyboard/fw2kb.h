/*
 * fw2kb — FreeWili 2 two-press chord keyboard, GUI-agnostic core.
 * C port of rpFw2KeyBoard (freewili-firmware/freewilimain/rmpLib).
 * Poll-based: feed inputs (press/touch/hid), then pop events and query labels.
 */
#ifndef FW2KB_H
#define FW2KB_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FW2KB_BTN_GRAY = 0,
    FW2KB_BTN_YELLOW,
    FW2KB_BTN_GREEN,
    FW2KB_BTN_BLUE,
    FW2KB_BTN_RED,
    FW2KB_BTN_CTRL_UP,     /* reserved: history recall lives outside the keyboard */
    FW2KB_BTN_CTRL_DOWN,   /* cycle page, or cancel a half-entered chord */
    FW2KB_BTN_AI           /* alias of CTRL_DOWN (firmware clickedControlAI) */
} fw2kb_btn;

typedef enum {
    FW2KB_MODE_ALL = 0,
    FW2KB_MODE_LOWER,
    FW2KB_MODE_UPPER,
    FW2KB_MODE_NUMBERS,
    FW2KB_MODE_HEX
} fw2kb_mode;

typedef enum {
    FW2KB_KEY_NONE = 0,
    FW2KB_KEY_CHAR,
    FW2KB_KEY_UP, FW2KB_KEY_DOWN, FW2KB_KEY_LEFT, FW2KB_KEY_RIGHT,
    FW2KB_KEY_HOME, FW2KB_KEY_END, FW2KB_KEY_PAGEUP, FW2KB_KEY_PAGEDOWN,
    FW2KB_KEY_BACKSPACE, FW2KB_KEY_DEL, FW2KB_KEY_ENTER, FW2KB_KEY_TAB,
    FW2KB_KEY_SAVE,   /* Ctrl+S */
    FW2KB_KEY_EXIT    /* Ctrl+X / Ctrl+C */
} fw2kb_key;

typedef struct {
    fw2kb_key key;
    char ch;          /* valid when key == FW2KB_KEY_CHAR */
} fw2kb_event;

#define FW2KB_EVENT_RING 8

typedef struct {
    fw2kb_mode mode;
    int page;              /* current page table index */
    bool group_state;      /* true = labels show 5 groups; false = mid-chord */
    int sub;               /* group chosen by the first chord press */
    int touch_threshold;   /* y > threshold => space, else backspace */
    fw2kb_event ring[FW2KB_EVENT_RING];
    int ring_head;
    int ring_count;
    char scratch[5][2];    /* single-char labels while mid-chord */
} fw2kb_t;

void fw2kb_init(fw2kb_t *kb);                       /* zero + reset */
void fw2kb_reset(fw2kb_t *kb);                      /* mode ALL, lower page, group state */
void fw2kb_set_mode(fw2kb_t *kb, fw2kb_mode mode);

void fw2kb_press(fw2kb_t *kb, fw2kb_btn btn);
void fw2kb_touch(fw2kb_t *kb, int x, int y);
void fw2kb_set_touch_threshold(fw2kb_t *kb, int y);

/* HID usage + modifier byte (LCtrl 0x01, LShift 0x02, RCtrl 0x10, RShift 0x20).
 * Queues an event and returns true, or returns false for unmapped usages. */
bool fw2kb_hid(fw2kb_t *kb, uint8_t usage, uint8_t modifiers);

bool fw2kb_next_event(fw2kb_t *kb, fw2kb_event *out);
void fw2kb_get_labels(const fw2kb_t *kb, const char *labels[5]);
bool fw2kb_in_chord(const fw2kb_t *kb);

#ifdef __cplusplus
}
#endif

#endif /* FW2KB_H */
