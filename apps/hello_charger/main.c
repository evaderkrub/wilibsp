// hello_charger — live view of the FW2 keyboard-coprocessor status frame:
// charger telemetry (frame bytes 10-21) plus connection detects, button
// bitmap, and link stats. Read-only smoke screen for the uartkbd charger
// API; no touch, no fw2kb. Redraws only when a frame changes something.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fw2.h"
#include "platform/diag.h"
#include "pico/stdlib.h"
#include "display/font5x7.h"
#include "platform/psram.h"

/* RGB565, byte-swapped to wire order per st7796.h (same as hello_keyboard) */
#define COL_BLACK  0x0000
#define COL_WHITE  0xFFFF
#define COL_DIM    0xE739   /* 0x39E7 */
#define COL_HDR    0x06FF   /* #ffe331 */

#define TEXT_SCALE 2
#define LINE_H     (8 * TEXT_SCALE)

/* Off-screen framebuffer in PSRAM (AGENTS.md: large buffers live in PSRAM),
 * flushed whole-screen by DMA so the main loop never blocks on SPI. */
static uint16_t *const s_fb = (uint16_t *)PSRAM_BASE;

/* Everything shown on screen; memset before filling so struct padding is
 * zeroed and memcmp is a valid change detector. */
typedef struct {
    bool              valid;
    uartkbd_charger_t chg;
    uint16_t          buttons;
    uint8_t           flags;
    uint32_t          frames, errors;
} snap_t;

static snap_t s_last;
static bool   s_drawn;

static void fb_fill_rect(int x, int y, int w, int h, uint16_t color_be)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > ST7796_W) w = ST7796_W - x;
    if (y + h > ST7796_H) h = ST7796_H - y;
    for (int yy = y; yy < y + h; yy++) {
        uint16_t *row = s_fb + (size_t)yy * ST7796_W + x;
        for (int xx = 0; xx < w; xx++) row[xx] = color_be;
    }
}

static void fb_draw_text(int x, int y, int scale, uint16_t fg_be, uint16_t bg_be,
                         const char *s)
{
    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;
    const int w = 6 * scale, h = 8 * scale;
    for (; *s; s++, x += w) {
        if (x + w > ST7796_W || y + h > ST7796_H || x < 0 || y < 0) break;
        char c = *s;
        const uint8_t *cols = (c >= FONT5X7_FIRST && c <= FONT5X7_LAST)
                                  ? font5x7[c - FONT5X7_FIRST]
                                  : font5x7[0];
        for (int gy = 0; gy < h; gy++) {
            int grow = gy / scale;
            uint16_t *row = s_fb + (size_t)(y + gy) * ST7796_W + x;
            for (int gx = 0; gx < w; gx++) {
                int col = gx / scale;
                bool on = col < 5 && grow < 7 && ((cols[col] >> grow) & 1);
                row[gx] = on ? fg_be : bg_be;
            }
        }
    }
}

/* Enum code -> name, or "code N" for anything undocumented (the driver
 * passes raw codes through verbatim). NULL entries = gaps in the doc. */
static const char *enum_str(uint8_t v, const char *const *names, int n,
                            char buf[12])
{
    if (v < n && names[v]) return names[v];
    snprintf(buf, 12, "code %u", v);
    return buf;
}

static const char *const k_chg[]   = { "NotCharging", "PreCharge", "FastCharge",
                                       "Done" };
static const char *const k_vbus[]  = { "NoInput", "UsbHost", "Adapter", NULL,
                                       NULL, NULL, NULL, "OTG" };
static const char *const k_fault[] = { "Normal", "InputFault", "Thermal",
                                       "TimerExp" };
static const char *const k_rank[]  = { "Normal", NULL, "Warm", "Cool", NULL,
                                       "Cold", "Hot" };
static const char *const k_tier[]  = { "None", "500mA", "1.5A", "3A" };

static void draw_screen(const snap_t *s)
{
    char ln[48], b1[12], b2[12];
    int y = 4;
    fb_fill_rect(0, 0, ST7796_W, ST7796_H, COL_BLACK);
    fb_draw_text(4, y, TEXT_SCALE, COL_HDR, COL_BLACK, "FW2 CHARGER / STATUS");
    y += LINE_H + 8;

    if (!s->valid) {
        fb_draw_text(4, y, TEXT_SCALE, COL_DIM, COL_BLACK,
                     "waiting for first frame...");
        y += LINE_H + 4;
    } else {
        const uartkbd_charger_t *c = &s->chg;
        snprintf(ln, sizeof ln, "VBUS  %5u mV   VSYS %4u mV",
                 c->vbus_mv, c->vsys_mv);
        fb_draw_text(4, y, TEXT_SCALE, COL_WHITE, COL_BLACK, ln);
        y += LINE_H + 4;
        snprintf(ln, sizeof ln, "VBATT %5u mV   CURR %4u mA",
                 c->vbatt_mv, c->current_ma);
        fb_draw_text(4, y, TEXT_SCALE, COL_WHITE, COL_BLACK, ln);
        y += LINE_H + 4;

        float tc = uartkbd_charger_temp_c(c->temp_tspct);
        if (tc > -100.0f) {
            int t10 = (int)(tc * 10.0f + (tc >= 0.0f ? 0.5f : -0.5f));
            snprintf(ln, sizeof ln, "TEMP  %u.%u%%  %s%d.%d C",
                     c->temp_tspct / 10, c->temp_tspct % 10,
                     t10 < 0 ? "-" : "", abs(t10) / 10, abs(t10) % 10);
        } else {
            snprintf(ln, sizeof ln, "TEMP  %u.%u%%  --- C",
                     c->temp_tspct / 10, c->temp_tspct % 10);
        }
        fb_draw_text(4, y, TEXT_SCALE, COL_WHITE, COL_BLACK, ln);
        y += LINE_H + 4;

        snprintf(ln, sizeof ln, "CHG   %-11s FAULT %s",
                 enum_str(s->chg.charge_status, k_chg, 4, b1),
                 enum_str(s->chg.fault, k_fault, 4, b2));
        fb_draw_text(4, y, TEXT_SCALE, COL_WHITE, COL_BLACK, ln);
        y += LINE_H + 4;
        snprintf(ln, sizeof ln, "VBUS  %-11s RANK  %s",
                 enum_str(s->chg.vbus_status, k_vbus, 8, b1),
                 enum_str(s->chg.temp_rank, k_rank, 7, b2));
        fb_draw_text(4, y, TEXT_SCALE, COL_WHITE, COL_BLACK, ln);
        y += LINE_H + 4;
        snprintf(ln, sizeof ln, "CC    %-5s %s%s%s",
                 enum_str(c->cc_tier, k_tier, 4, b1),
                 c->vbus_attached ? "ATT " : "",
                 c->vsys_regulation ? "VREG " : "",
                 c->thermal_regulation ? "TREG" : "");
        fb_draw_text(4, y, TEXT_SCALE, COL_WHITE, COL_BLACK, ln);
        y += LINE_H + 4;
        snprintf(ln, sizeof ln, "CC1   %4u mV    CC2  %4u mV",
                 c->cc1_mv, c->cc2_mv);
        fb_draw_text(4, y, TEXT_SCALE, COL_WHITE, COL_BLACK, ln);
        y += LINE_H + 8;
    }

    snprintf(ln, sizeof ln, "BTNS %04X  DET %s%s%s",
             s->buttons,
             (s->flags & UARTKBD_FLAG_AUDIO)   ? "AUD " : "",
             (s->flags & UARTKBD_FLAG_HOTPLUG) ? "HPD " : "",
             (s->flags & UARTKBD_FLAG_USB)     ? "USB" : "");
    fb_draw_text(4, y, TEXT_SCALE, COL_DIM, COL_BLACK, ln);
    y += LINE_H + 4;
    snprintf(ln, sizeof ln, "FRAMES %lu  ERRORS %lu",
             (unsigned long)s->frames, (unsigned long)s->errors);
    fb_draw_text(4, y, TEXT_SCALE, COL_DIM, COL_BLACK, ln);
}

int main(void)
{
    board_init();
    size_t psram_bytes = psram_init();
    if (psram_bytes < (size_t)ST7796_W * ST7796_H * 2) {
        DIAG("hello_charger: PSRAM absent/too small (%u bytes) - halting\n",
             (unsigned)psram_bytes);
        for (;;) tight_loop_contents();
    }
    st7796_init();
    board_backlight_set(1);
    uartkbd_init();
    DIAG("hello_charger up\n");

    uint64_t next_log = 0;
    while (true) {
        uartkbd_task();
        uartkbd_event_t ev;
        while (uartkbd_next_event(&ev))    /* drain so the ring never wraps */
            DIAG("uartkbd btn %d %s\n", (int)ev.btn, ev.pressed ? "down" : "up");

        snap_t s;
        memset(&s, 0, sizeof s);           /* zero padding for memcmp */
        s.valid   = uartkbd_charger(&s.chg);
        s.buttons = uartkbd_buttons();
        s.flags   = uartkbd_flags();
        s.frames  = uartkbd_frames();
        s.errors  = uartkbd_errors();

        /* If a change lands mid-flush we skip this pass; the snapshot still
         * differs from s_last next loop, so the redraw coalesces. */
        if ((!s_drawn || memcmp(&s, &s_last, sizeof s) != 0)
            && !st7796_flush_busy()) {
            s_last = s;
            s_drawn = true;
            draw_screen(&s);
            st7796_flush_async(0, 0, ST7796_W - 1, ST7796_H - 1, s_fb, NULL);
        }

        uint64_t now = time_us_64();
        if (now >= next_log) {
            DIAG("uartkbd frames=%u errors=%u flags=%x btns=%x\n",
                 (unsigned)s.frames, (unsigned)s.errors,
                 (unsigned)s.flags, (unsigned)s.buttons);
            next_log = now + 1000000;
        }
        sleep_ms(2);
    }
}
