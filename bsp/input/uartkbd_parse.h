/*
 * uartkbd_parse — pure frame parser for the FW2 UART keyboard.
 * Protocol: C:\~prj\Dropbox\FreeWilli\vibe\Wilikeyboard.md (23-byte frame,
 * sync 0xBD 0x1D, button bitmaps in bytes 2-5, additive 8-bit checksum of
 * bytes 0-21 in byte 22). No hardware includes — host-testable.
 */
#ifndef UARTKBD_PARSE_H
#define UARTKBD_PARSE_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    UARTKBD_BTN_GREY = 0,
    UARTKBD_BTN_YELLOW,
    UARTKBD_BTN_GREEN,
    UARTKBD_BTN_BLUE,
    UARTKBD_BTN_RED,
    UARTKBD_BTN_NAV_CENTER,
    UARTKBD_BTN_NAV_UP,
    UARTKBD_BTN_NAV_DOWN,
    UARTKBD_BTN_NAV_LEFT,
    UARTKBD_BTN_NAV_RIGHT,
    UARTKBD_BTN_HOME,
    UARTKBD_BTN_OK,
    UARTKBD_BTN_CANCEL,
    UARTKBD_BTN_PAGE,
    UARTKBD_BTN_COUNT
} uartkbd_btn_t;

typedef struct {
    uartkbd_btn_t btn;
    bool          pressed;   /* true = press edge, false = release edge */
} uartkbd_event_t;

/* Level flags (connection detects) — never emitted as events */
#define UARTKBD_FLAG_AUDIO   0x01u
#define UARTKBD_FLAG_HOTPLUG 0x02u
#define UARTKBD_FLAG_USB     0x04u

#define UARTKBD_FRAME_LEN  23
#define UARTKBD_EVENT_RING 8

typedef struct {
    uint8_t  state;                        /* 0 hunt 0xBD, 1 expect 0x1D, 2 collect */
    uint8_t  count;
    uint8_t  frame[UARTKBD_FRAME_LEN];
    uint16_t buttons;                      /* bit N = uartkbd_btn_t N, 1 = down */
    uint8_t  flags;                        /* UARTKBD_FLAG_* */
    uartkbd_event_t ring[UARTKBD_EVENT_RING];
    uint8_t  ring_head, ring_count;
    uint32_t frames;                       /* checksum-valid frames */
    uint32_t errors;                       /* sync misses + checksum failures */
} uartkbd_parser_t;

void     uartkbd_parse_init(uartkbd_parser_t *p);
void     uartkbd_parse_byte(uartkbd_parser_t *p, uint8_t b);
bool     uartkbd_parse_next_event(uartkbd_parser_t *p, uartkbd_event_t *ev);
uint16_t uartkbd_parse_buttons(const uartkbd_parser_t *p);
uint8_t  uartkbd_parse_flags(const uartkbd_parser_t *p);
uint32_t uartkbd_parse_frames(const uartkbd_parser_t *p);
uint32_t uartkbd_parse_errors(const uartkbd_parser_t *p);

#endif /* UARTKBD_PARSE_H */
