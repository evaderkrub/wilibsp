#include "uartkbd_parse.h"
#include <string.h>
#include <math.h>

enum { ST_HUNT = 0, ST_SYNC2 = 1, ST_COLLECT = 2 };

static void push_event(uartkbd_parser_t *p, uartkbd_btn_t btn, bool pressed)
{
    if (p->ring_count == UARTKBD_EVENT_RING) {          /* drop oldest */
        p->ring_head = (uint8_t)((p->ring_head + 1) % UARTKBD_EVENT_RING);
        p->ring_count--;
    }
    uint8_t tail = (uint8_t)((p->ring_head + p->ring_count) % UARTKBD_EVENT_RING);
    p->ring[tail].btn = btn;
    p->ring[tail].pressed = pressed;
    p->ring_count++;
}

/* Bytes 2-5 -> 14-bit button state (bit = uartkbd_btn_t). Reserved bits
 * are never read. Mapping per Wilikeyboard.md. The wire is active-low
 * (hardware-verified 2026-07-23: idle lines read 1, a held button reads 0);
 * the returned bitmap uses bit set = pressed. */
static uint16_t decode_buttons(const uint8_t *f)
{
    uint16_t b = 0;
    if (!(f[2] & 0x01)) b |= 1u << UARTKBD_BTN_GREY;
    if (!(f[2] & 0x02)) b |= 1u << UARTKBD_BTN_YELLOW;
    if (!(f[2] & 0x04)) b |= 1u << UARTKBD_BTN_GREEN;
    if (!(f[2] & 0x08)) b |= 1u << UARTKBD_BTN_BLUE;
    if (!(f[2] & 0x10)) b |= 1u << UARTKBD_BTN_RED;
    if (!(f[2] & 0x20)) b |= 1u << UARTKBD_BTN_NAV_CENTER;
    if (!(f[3] & 0x01)) b |= 1u << UARTKBD_BTN_NAV_DOWN;
    if (!(f[3] & 0x08)) b |= 1u << UARTKBD_BTN_NAV_RIGHT;
    if (!(f[3] & 0x10)) b |= 1u << UARTKBD_BTN_NAV_UP;
    if (!(f[3] & 0x20)) b |= 1u << UARTKBD_BTN_NAV_LEFT;
    if (!(f[4] & 0x80)) b |= 1u << UARTKBD_BTN_HOME;
    if (!(f[5] & 0x01)) b |= 1u << UARTKBD_BTN_OK;
    if (!(f[5] & 0x02)) b |= 1u << UARTKBD_BTN_CANCEL;
    if (!(f[5] & 0x04)) b |= 1u << UARTKBD_BTN_PAGE;
    return b;
}

static uint8_t decode_flags(const uint8_t *f)
{
    uint8_t fl = 0;
    if (f[3] & 0x04) fl |= UARTKBD_FLAG_AUDIO;
    if (f[3] & 0x02) fl |= UARTKBD_FLAG_HOTPLUG;
    if (f[4] & 0x04) fl |= UARTKBD_FLAG_USB;
    return fl;
}

static void accept_frame(uartkbd_parser_t *p)
{
    uint8_t sum = 0;
    for (int i = 0; i < UARTKBD_FRAME_LEN - 1; i++)
        sum = (uint8_t)(sum + p->frame[i]);
    if (sum != p->frame[UARTKBD_FRAME_LEN - 1]) {
        p->errors++;
        return;
    }
    p->frames++;
    p->flags = decode_flags(p->frame);
    memcpy(p->charger_raw, &p->frame[10], sizeof p->charger_raw);
    p->charger_valid = true;
    uint16_t nb = decode_buttons(p->frame);
    if (!p->primed) {
        p->primed = true;
        p->buttons = nb;
        return;
    }
    uint16_t changed = (uint16_t)(nb ^ p->buttons);
    for (int i = 0; i < UARTKBD_BTN_COUNT; i++)
        if (changed & (1u << i))
            push_event(p, (uartkbd_btn_t)i, (nb >> i) & 1u);
    p->buttons = nb;
}

void uartkbd_parse_init(uartkbd_parser_t *p)
{
    memset(p, 0, sizeof *p);
}

void uartkbd_parse_byte(uartkbd_parser_t *p, uint8_t b)
{
    switch (p->state) {
    case ST_HUNT:
        if (b == 0xBD) { p->frame[0] = b; p->count = 1; p->state = ST_SYNC2; }
        break;
    case ST_SYNC2:
        if (b == 0x1D) {
            p->frame[1] = b; p->count = 2; p->state = ST_COLLECT;
        } else {
            p->errors++;
            /* the miss byte may itself be a new sync start */
            if (b == 0xBD) { p->frame[0] = b; p->count = 1; }
            else p->state = ST_HUNT;
        }
        break;
    case ST_COLLECT:
        p->frame[p->count++] = b;
        if (p->count == UARTKBD_FRAME_LEN) {
            p->state = ST_HUNT;
            accept_frame(p);
        }
        break;
    }
}

bool uartkbd_parse_next_event(uartkbd_parser_t *p, uartkbd_event_t *ev)
{
    if (p->ring_count == 0) return false;
    *ev = p->ring[p->ring_head];
    p->ring_head = (uint8_t)((p->ring_head + 1) % UARTKBD_EVENT_RING);
    p->ring_count--;
    return true;
}

uint16_t uartkbd_parse_buttons(const uartkbd_parser_t *p) { return p->buttons; }
uint8_t  uartkbd_parse_flags(const uartkbd_parser_t *p)   { return p->flags; }
uint32_t uartkbd_parse_frames(const uartkbd_parser_t *p)  { return p->frames; }
uint32_t uartkbd_parse_errors(const uartkbd_parser_t *p)  { return p->errors; }

/* Scaling per Wilikeyboard.md bytes 10-21 — applied only here, on demand. */
bool uartkbd_parse_charger(const uartkbd_parser_t *p, uartkbd_charger_t *out)
{
    if (!p->charger_valid) return false;
    const uint8_t *r = p->charger_raw;
    out->vbus_mv    = (uint16_t)(2600u + r[0] * 100u);
    out->vsys_mv    = (uint16_t)(2304u + r[1] * 20u);
    out->vbatt_mv   = (uint16_t)(2304u + r[2] * 20u);
    out->current_ma = (uint16_t)(r[3] * 50u);
    out->temp_tspct = (uint16_t)(r[4] * 465u / 100u + 210u);
    out->charge_status = r[5];
    out->vbus_status   = r[6];
    out->fault         = r[7];
    out->temp_rank     = r[8];
    out->cc_tier            = (uint8_t)((r[9] >> 3) & 0x03u);
    out->vsys_regulation    = (r[9] & 0x04u) != 0;
    out->thermal_regulation = (r[9] & 0x02u) != 0;
    out->vbus_attached      = (r[9] & 0x01u) != 0;
    out->cc1_mv = (uint16_t)(r[10] * 8u);
    out->cc2_mv = (uint16_t)(r[11] * 8u);
    return true;
}

float uartkbd_charger_temp_c(uint16_t tspct)
{
    float x = (float)tspct / 1000.0f;
    if (x <= 0.0f || x >= 1.0f) return -273.15f;
    float r_low = 5240.0f * x / (1.0f - x);        /* divider's lower leg */
    float inv = 1.0f / r_low - 1.0f / 30000.0f;    /* peel off 30k parallel */
    if (inv <= 0.0f) return -273.15f;              /* NTC absent / open */
    float r_ntc = 1.0f / inv;
    float t_k = 1.0f / (1.0f / 298.15f + logf(r_ntc / 10000.0f) / 3435.0f);
    return t_k - 273.15f;
}
