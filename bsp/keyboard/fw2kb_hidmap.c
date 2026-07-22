/*
 * US HID keymap + translation, ported from rpFw2KeyBoard.cpp
 * (keymap[] + hidToEditKey). Indices 0-127 only: the firmware table's
 * F-key/lock tail (indices 128+) is never consulted by the translator.
 */
#include "fw2kb.h"

void fw2kb_push_event(fw2kb_t *kb, fw2kb_key key, char ch);   /* fw2kb.c */

#define HID_MOD_CTRL   0x01
#define HID_MOD_SHIFT  0x02
#define HID_MOD_RCTRL  0x10
#define HID_MOD_RSHIFT 0x20

typedef struct { unsigned char usage; unsigned char modifier; } keymap_entry;

#define S HID_MOD_SHIFT
static const keymap_entry k_keymap[128] = {
    /* 0x00-0x07 NUL..BEL */ {0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
    /* 0x08 BS  */ {0x2a,0},
    /* 0x09 TAB */ {0x2b,0},
    /* 0x0A LF  */ {0x28,0},
    /* 0x0B-0x1F */ {0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
                    {0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
                    {0,0},
    /* ' ' */ {0x2c,0}, /* ! */ {0x1e,S}, /* " */ {0x34,S}, /* # */ {0x20,S},
    /* $ */ {0x21,S}, /* % */ {0x22,S}, /* & */ {0x24,S}, /* ' */ {0x34,0},
    /* ( */ {0x26,S}, /* ) */ {0x27,S}, /* * */ {0x25,S}, /* + */ {0x2e,S},
    /* , */ {0x36,0}, /* - */ {0x2d,0}, /* . */ {0x37,0}, /* / */ {0x38,0},
    /* 0 */ {0x27,0}, /* 1 */ {0x1e,0}, /* 2 */ {0x1f,0}, /* 3 */ {0x20,0},
    /* 4 */ {0x21,0}, /* 5 */ {0x22,0}, /* 6 */ {0x23,0}, /* 7 */ {0x24,0},
    /* 8 */ {0x25,0}, /* 9 */ {0x26,0},
    /* : */ {0x33,S}, /* ; */ {0x33,0}, /* < */ {0x36,S}, /* = */ {0x2e,0},
    /* > */ {0x37,S}, /* ? */ {0x38,S}, /* @ */ {0x1f,S},
    /* A-Z */
    {0x04,S},{0x05,S},{0x06,S},{0x07,S},{0x08,S},{0x09,S},{0x0a,S},{0x0b,S},
    {0x0c,S},{0x0d,S},{0x0e,S},{0x0f,S},{0x10,S},{0x11,S},{0x12,S},{0x13,S},
    {0x14,S},{0x15,S},{0x16,S},{0x17,S},{0x18,S},{0x19,S},{0x1a,S},{0x1b,S},
    {0x1c,S},{0x1d,S},
    /* [ */ {0x2f,0}, /* \ */ {0x31,0}, /* ] */ {0x30,0}, /* ^ */ {0x23,S},
    /* _ */ {0x2d,S}, /* ` */ {0x35,0},
    /* a-z */
    {0x04,0},{0x05,0},{0x06,0},{0x07,0},{0x08,0},{0x09,0},{0x0a,0},{0x0b,0},
    {0x0c,0},{0x0d,0},{0x0e,0},{0x0f,0},{0x10,0},{0x11,0},{0x12,0},{0x13,0},
    {0x14,0},{0x15,0},{0x16,0},{0x17,0},{0x18,0},{0x19,0},{0x1a,0},{0x1b,0},
    {0x1c,0},{0x1d,0},
    /* { */ {0x2f,S}, /* | */ {0x31,S}, /* } */ {0x30,S}, /* ~ */ {0x35,S},
    /* DEL */ {0,0},
};
#undef S

bool fw2kb_hid(fw2kb_t *kb, uint8_t usage, uint8_t modifiers)
{
    const bool shift = (modifiers & (HID_MOD_SHIFT | HID_MOD_RSHIFT)) != 0;
    const bool ctrl  = (modifiers & (HID_MOD_CTRL  | HID_MOD_RCTRL))  != 0;

    if (ctrl) {                                   /* Ctrl chords first */
        if (usage == 0x16) { fw2kb_push_event(kb, FW2KB_KEY_SAVE, 0); return true; } /* s */
        if (usage == 0x1B) { fw2kb_push_event(kb, FW2KB_KEY_EXIT, 0); return true; } /* x */
        if (usage == 0x06) { fw2kb_push_event(kb, FW2KB_KEY_EXIT, 0); return true; } /* c */
        return false;
    }
    switch (usage) {                              /* non-printable keys */
        case 0x28: fw2kb_push_event(kb, FW2KB_KEY_ENTER, 0);     return true;
        case 0x2A: fw2kb_push_event(kb, FW2KB_KEY_BACKSPACE, 0); return true;
        case 0x2B: fw2kb_push_event(kb, FW2KB_KEY_TAB, 0);       return true;
        case 0x4A: fw2kb_push_event(kb, FW2KB_KEY_HOME, 0);      return true;
        case 0x4B: fw2kb_push_event(kb, FW2KB_KEY_PAGEUP, 0);    return true;
        case 0x4C: fw2kb_push_event(kb, FW2KB_KEY_DEL, 0);       return true;
        case 0x4D: fw2kb_push_event(kb, FW2KB_KEY_END, 0);       return true;
        case 0x4E: fw2kb_push_event(kb, FW2KB_KEY_PAGEDOWN, 0);  return true;
        case 0x4F: fw2kb_push_event(kb, FW2KB_KEY_RIGHT, 0);     return true;
        case 0x50: fw2kb_push_event(kb, FW2KB_KEY_LEFT, 0);      return true;
        case 0x51: fw2kb_push_event(kb, FW2KB_KEY_DOWN, 0);      return true;
        case 0x52: fw2kb_push_event(kb, FW2KB_KEY_UP, 0);        return true;
        default: break;
    }
    for (int i = 32; i <= 126; i++) {             /* printable: reverse lookup */
        if (k_keymap[i].usage == usage) {
            bool needs_shift = (k_keymap[i].modifier & HID_MOD_SHIFT) != 0;
            if (needs_shift == shift) {
                fw2kb_push_event(kb, FW2KB_KEY_CHAR, (char)i);
                return true;
            }
        }
    }
    return false;
}
