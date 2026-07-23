/*
 * uartkbd_parse — pure frame parser for the FW2 UART keyboard.
 * Protocol: C:\~prj\Dropbox\FreeWilli\vibe\Wilikeyboard.md (23-byte frame,
 * sync 0xBD 0x1D, button bitmaps in bytes 2-5, charger telemetry in bytes 10-21, additive 8-bit checksum of
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

/* Charger enum codes (fields carry the frame's raw code verbatim, so
 * undocumented codes pass through — compare, don't assume exhaustive). */
#define UARTKBD_CHG_NOT_CHARGING 0u   /* charge_status */
#define UARTKBD_CHG_PRECHARGE    1u
#define UARTKBD_CHG_FASTCHARGE   2u
#define UARTKBD_CHG_DONE         3u
#define UARTKBD_VBUS_NONE        0u   /* vbus_status */
#define UARTKBD_VBUS_USB_HOST    1u
#define UARTKBD_VBUS_ADAPTER     2u
#define UARTKBD_VBUS_OTG         7u
#define UARTKBD_FAULT_NORMAL     0u   /* fault */
#define UARTKBD_FAULT_INPUT      1u
#define UARTKBD_FAULT_THERMAL    2u
#define UARTKBD_FAULT_TIMER      3u
#define UARTKBD_RANK_NORMAL      0u   /* temp_rank */
#define UARTKBD_RANK_WARM        2u
#define UARTKBD_RANK_COOL        3u
#define UARTKBD_RANK_COLD        5u
#define UARTKBD_RANK_HOT         6u
#define UARTKBD_CC_NONE          0u   /* cc_tier */
#define UARTKBD_CC_500MA         1u
#define UARTKBD_CC_1A5           2u
#define UARTKBD_CC_3A            3u

/* Charger snapshot, scaled to engineering units on demand by
 * uartkbd_parse_charger() (frame time only captures raw bytes). */
typedef struct {
    uint16_t vbus_mv;              /* 2600 + code*100 */
    uint16_t vsys_mv;              /* 2304 + code*20  */
    uint16_t vbatt_mv;             /* 2304 + code*20  */
    uint16_t current_ma;           /* code*50         */
    uint16_t temp_tspct;           /* code*465/100 + 210, tenths of percent */
    uint8_t  charge_status;        /* UARTKBD_CHG_*   */
    uint8_t  vbus_status;          /* UARTKBD_VBUS_*  */
    uint8_t  fault;                /* UARTKBD_FAULT_* */
    uint8_t  temp_rank;            /* UARTKBD_RANK_*  */
    uint8_t  cc_tier;              /* UARTKBD_CC_*    */
    bool     vsys_regulation;
    bool     thermal_regulation;
    bool     vbus_attached;
    uint16_t cc1_mv;               /* code*8 */
    uint16_t cc2_mv;               /* code*8 */
} uartkbd_charger_t;

#define UARTKBD_FRAME_LEN  23
#define UARTKBD_EVENT_RING 8

typedef struct {
    uint8_t  state;                        /* 0 hunt 0xBD, 1 expect 0x1D, 2 collect */
    uint8_t  count;
    uint8_t  frame[UARTKBD_FRAME_LEN];
    uint16_t buttons;                      /* bit N = uartkbd_btn_t N, 1 = down */
    uint8_t  flags;                        /* UARTKBD_FLAG_* */
    uint8_t  charger_raw[12];              /* frame bytes 10-21, last valid */
    bool     charger_valid;                /* any valid frame seen yet */
    bool     primed;                       /* first valid frame only latches
                                             * the baseline (coprocessor boot
                                             * frames carry garbage bits);
                                             * edges start from the second
                                             * frame. */
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
bool     uartkbd_parse_charger(const uartkbd_parser_t *p, uartkbd_charger_t *out);
uint32_t uartkbd_parse_frames(const uartkbd_parser_t *p);
uint32_t uartkbd_parse_errors(const uartkbd_parser_t *p);

#endif /* UARTKBD_PARSE_H */
