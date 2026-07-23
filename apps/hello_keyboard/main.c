// hello_keyboard — fw2kb two-press chord keyboard on the ST7796, driven by
// the FW2 UART keyboard (GREY/YELLOW/GREEN/BLUE/RED chords, PAGE cycles
// pages) with touch space/backspace (below/above the split line; the soft
// buttons are display-only). Layout: text area above a one-line button bar
// styled after the FW2 firmware soft menu (see BAR_* defines).
#include <string.h>
#include "fw2.h"
#include "platform/diag.h"
#include "pico/stdlib.h"
#include "display/font5x7.h"
#include "platform/psram.h"

/* RGB565 colors, byte-swapped to wire (big-endian) order per st7796.h.
 * Button colors match the FW2 firmware soft menu exactly
 * (rpPanelManager::drawMenu). */
#define COL_BLACK  0x0000
#define COL_WHITE  0xFFFF
#define COL_GRAY   0x9AD6   /* #d6d2d6 */
#define COL_YELLOW 0x06FF   /* #ffe331 */
#define COL_GREEN  0x0012   /* #104100 */
#define COL_BLUE   0xF800   /* #001cc5 */
#define COL_RED    0x0780   /* #84003a */
#define COL_DIM    0xE739   /* 0x39E7 */

#define TEXT_SCALE 2
#define CHAR_W     (6 * TEXT_SCALE)
#define LINE_H     (8 * TEXT_SCALE)

/* Soft-button bar: one text line + 6 px tall to keep the app's screen space;
 * box geometry matches the FW2 firmware soft menu (rpPanelManager::drawMenu):
 * width (W-12)/5 = 93, 3 px spacing, no overlap. */
#define BAR_H       (LINE_H + 6)
#define BAR_Y       (ST7796_H - BAR_H)
#define BTN_W       ((ST7796_W - 12) / 5)   /* 93 */
#define BTN_PITCH   (BTN_W + 3)             /* 96 */
#define TOUCH_SPLIT (BAR_Y / 2)  /* fw2kb threshold: y > split = space, else backspace */

#define TEXT_COLS  (ST7796_W / CHAR_W)
#define TEXT_ROWS  (BAR_Y / LINE_H)
#define TEXT_MAX   1024

static fw2kb_t s_kb;
static char    s_text[TEXT_MAX];
static int     s_len;
static bool    s_text_dirty = true;
static bool    s_bar_dirty  = true;
static char    s_bar_cache[5][6];  /* 5 = fw2kb group width (labels are at most 5 chars) */

/* Off-screen framebuffer in PSRAM (AGENTS.md: large buffers live in PSRAM).
 * Rendered by fb_* helpers; flushed whole-screen by DMA (st7796_flush_async)
 * so the main loop never blocks on SPI. Wire-order RGB565, row-major 480x320. */
static uint16_t *const s_fb = (uint16_t *)PSRAM_BASE;
static bool s_fb_dirty = false;

static const uint16_t k_btn_cols[5] =
    { COL_GRAY, COL_YELLOW, COL_GREEN, COL_BLUE, COL_RED };

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
            int grow = gy / scale;                /* 0..7; row 7 = line gap */
            uint16_t *row = s_fb + (size_t)(y + gy) * ST7796_W + x;
            for (int gx = 0; gx < w; gx++) {
                int col = gx / scale;             /* 0..5; col 5 = char gap */
                bool on = col < 5 && grow < 7 && ((cols[col] >> grow) & 1);
                row[gx] = on ? fg_be : bg_be;
            }
        }
    }
}

static void draw_bar(void)
{
    const char *labels[5];
    fw2kb_get_labels(&s_kb, labels);
    fb_fill_rect(0, BAR_Y, ST7796_W, BAR_H, COL_BLACK);  /* clear the 3 px gaps */
    for (int i = 0; i < 5; i++) {
        int x = i * BTN_PITCH;
        fb_fill_rect(x, BAR_Y, BTN_W, BAR_H, k_btn_cols[i]);
        int len = (int)strlen(labels[i]);
        if (len > 5) len = 5;
        int tx = x + (BTN_W - len * CHAR_W) / 2;
        fb_draw_text(tx, BAR_Y + (BAR_H - LINE_H) / 2, TEXT_SCALE,
                     COL_WHITE, k_btn_cols[i], labels[i]);
        strncpy(s_bar_cache[i], labels[i], 5);
        s_bar_cache[i][5] = 0;
    }
}

static bool bar_changed(void)
{
    const char *labels[5];
    fw2kb_get_labels(&s_kb, labels);
    for (int i = 0; i < 5; i++)
        if (strncmp(labels[i], s_bar_cache[i], 5) != 0) return true;
    return false;
}

static void draw_text_area(void)
{
    fb_fill_rect(0, 0, ST7796_W, BAR_Y, COL_BLACK);
    fb_fill_rect(0, TOUCH_SPLIT, ST7796_W, 1, COL_DIM);
    fb_draw_text(4, TOUCH_SPLIT - 10, 1, COL_DIM, COL_BLACK, "tap above = backspace");
    fb_draw_text(4, TOUCH_SPLIT + 3,  1, COL_DIM, COL_BLACK, "tap below = space");

    int col = 0, row = 0;
    for (int i = 0; i < s_len && row < TEXT_ROWS; i++) {
        char c = s_text[i];
        if (c == '\n') { row++; col = 0; continue; }
        if (col >= TEXT_COLS) { row++; col = 0; }
        if (row >= TEXT_ROWS) break;
        char s[2] = { c, 0 };
        fb_draw_text(col * CHAR_W, row * LINE_H, TEXT_SCALE,
                     COL_WHITE, COL_BLACK, s);
        col++;
    }
    if (row < TEXT_ROWS)   /* cursor underline at the next cell */
        fb_fill_rect(col * CHAR_W, row * LINE_H + LINE_H - 2,
                     CHAR_W, 2, COL_WHITE);
}

static void append_char(char c)
{
    if (s_len < TEXT_MAX - 1) { s_text[s_len++] = c; s_text_dirty = true; }
}

static void handle_buttons(void)
{
    uartkbd_event_t ev;
    while (uartkbd_next_event(&ev)) {
        DIAG("uartkbd btn %d %s\n", (int)ev.btn, ev.pressed ? "down" : "up");
        if (!ev.pressed) continue;
        if (ev.btn <= UARTKBD_BTN_RED)
            fw2kb_press(&s_kb, (fw2kb_btn)ev.btn);   /* GREY..RED == GRAY..RED */
        else if (ev.btn == UARTKBD_BTN_PAGE)
            fw2kb_press(&s_kb, FW2KB_BTN_AI);        /* page cycle / chord cancel */
    }
}

static void handle_touch(void)
{
    /* Touch-down edge only. The soft-button rectangles are display-only, not
     * touch targets: the whole screen is one space/backspace surface, so a
     * touch on the bar is just a touch low on the screen (= space). */
    static bool was_down = false;
    uint16_t x, y;
    bool down = ft6336_poll(&x, &y);
    if (down && !was_down)
        fw2kb_touch(&s_kb, (int)x, (int)y);
    was_down = down;
}

static void handle_fw2kb_events(void)
{
    fw2kb_event ev;
    while (fw2kb_next_event(&s_kb, &ev)) {
        switch (ev.key) {
        case FW2KB_KEY_CHAR:      append_char(ev.ch); DIAG("fw2kb char '%c'\n", ev.ch); break;
        case FW2KB_KEY_BACKSPACE: if (s_len) { s_len--; s_text_dirty = true; } break;
        case FW2KB_KEY_ENTER:     append_char('\n'); break;
        case FW2KB_KEY_TAB:       for (int i = 0; i < 4; i++) append_char(' '); break;
        default: DIAG("fw2kb key %d\n", (int)ev.key); break;
        }
    }
}

int main(void)
{
    board_init();
    size_t psram_bytes = psram_init();
    if (psram_bytes < (size_t)ST7796_W * ST7796_H * 2) {
        DIAG("hello_keyboard: PSRAM absent/too small (%u bytes) - halting\n",
             (unsigned)psram_bytes);
        for (;;) tight_loop_contents();
    }
    st7796_init();
    board_backlight_set(1);
    ft6336_init();
    uartkbd_init();
    fw2kb_init(&s_kb);
    fw2kb_set_touch_threshold(&s_kb, TOUCH_SPLIT);
    DIAG("hello_keyboard up\n");

    uint64_t next_link_log = 0;
    while (true) {
        uartkbd_task();
        handle_buttons();
        handle_touch();
        handle_fw2kb_events();

        if (bar_changed()) s_bar_dirty = true;
        if (s_text_dirty) { draw_text_area(); s_text_dirty = false; s_fb_dirty = true; }
        if (s_bar_dirty)  { draw_bar();       s_bar_dirty  = false; s_fb_dirty = true; }
        if (s_fb_dirty && !st7796_flush_busy()) {
            s_fb_dirty = false;   /* changes landing mid-flush re-dirty and coalesce */
            st7796_flush_async(0, 0, ST7796_W - 1, ST7796_H - 1, s_fb, NULL);
        }

        uint64_t now = time_us_64();
        if (now >= next_link_log) {
            DIAG("uartkbd frames=%u errors=%u flags=%x\n",
                 (unsigned)uartkbd_frames(), (unsigned)uartkbd_errors(),
                 (unsigned)uartkbd_flags());
            next_link_log = now + 1000000;
        }
        sleep_ms(2);
    }
}
