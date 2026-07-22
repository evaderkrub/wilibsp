// hello_keyboard — fw2kb two-press chord keyboard on the ST7796, driven by
// the FW2 UART keyboard (GREY/YELLOW/GREEN/BLUE/RED chords, PAGE cycles
// pages) with touch space/backspace (below/above the split line).
// Layout (480x320 landscape): text area y 0-271, button bar y 272-319.
#include <string.h>
#include "fw2.h"
#include "platform/diag.h"
#include "pico/stdlib.h"

/* RGB565 colors, byte-swapped to wire (big-endian) order per st7796.h */
#define COL_BLACK  0x0000
#define COL_WHITE  0xFFFF
#define COL_GRAY   0x1084   /* 0x8410 */
#define COL_YELLOW 0xE0FF   /* 0xFFE0 */
#define COL_GREEN  0xE007   /* 0x07E0 */
#define COL_BLUE   0x1F00   /* 0x001F */
#define COL_RED    0x00F8   /* 0xF800 */
#define COL_DIM    0xE739   /* 0x39E7 */

#define BAR_Y       272
#define BAR_H       48
#define BTN_W       96
#define TOUCH_SPLIT 136     /* fw2kb threshold: y > 136 = space, else backspace */

#define TEXT_SCALE 2
#define CHAR_W     (6 * TEXT_SCALE)
#define LINE_H     (8 * TEXT_SCALE)
#define TEXT_COLS  (ST7796_W / CHAR_W)
#define TEXT_ROWS  (BAR_Y / LINE_H)
#define TEXT_MAX   1024

static fw2kb_t s_kb;
static char    s_text[TEXT_MAX];
static int     s_len;
static bool    s_text_dirty = true;
static bool    s_bar_dirty  = true;
static char    s_bar_cache[5][6];

static const uint16_t k_btn_cols[5] =
    { COL_GRAY, COL_YELLOW, COL_GREEN, COL_BLUE, COL_RED };

static void draw_bar(void)
{
    const char *labels[5];
    fw2kb_get_labels(&s_kb, labels);
    for (int i = 0; i < 5; i++) {
        int x = i * BTN_W;
        st7796_fill_rect(x, BAR_Y, BTN_W, BAR_H, k_btn_cols[i]);
        int len = (int)strlen(labels[i]);
        if (len > 5) len = 5;
        int tx = x + (BTN_W - len * CHAR_W) / 2;
        int ty = BAR_Y + (BAR_H - 8 * TEXT_SCALE) / 2;
        st7796_draw_text(tx, ty, TEXT_SCALE, COL_BLACK, k_btn_cols[i], labels[i]);
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
    st7796_fill_rect(0, 0, ST7796_W, BAR_Y, COL_BLACK);
    st7796_fill_rect(0, TOUCH_SPLIT, ST7796_W, 1, COL_DIM);
    st7796_draw_text(4, TOUCH_SPLIT - 10, 1, COL_DIM, COL_BLACK, "tap above = backspace");
    st7796_draw_text(4, TOUCH_SPLIT + 3,  1, COL_DIM, COL_BLACK, "tap below = space");

    int col = 0, row = 0;
    for (int i = 0; i < s_len && row < TEXT_ROWS; i++) {
        char c = s_text[i];
        if (c == '\n') { row++; col = 0; continue; }
        if (col >= TEXT_COLS) { row++; col = 0; }
        if (row >= TEXT_ROWS) break;
        char s[2] = { c, 0 };
        st7796_draw_text(col * CHAR_W, row * LINE_H, TEXT_SCALE,
                         COL_WHITE, COL_BLACK, s);
        col++;
    }
    if (row < TEXT_ROWS)   /* cursor underline at the next cell */
        st7796_fill_rect(col * CHAR_W, row * LINE_H + LINE_H - 2,
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
    static bool was_down = false;
    uint16_t x, y;
    bool down = ft6336_poll(&x, &y);
    if (down && !was_down && y < BAR_Y)              /* touch-down edge only */
        fw2kb_touch(&s_kb, (int)x, (int)y);
    was_down = down;
}

static void handle_fw2kb_events(void)
{
    fw2kb_event ev;
    while (fw2kb_next_event(&s_kb, &ev)) {
        switch (ev.key) {
        case FW2KB_KEY_CHAR:      append_char(ev.ch); break;
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
        if (s_text_dirty) { draw_text_area(); s_text_dirty = false; }
        if (s_bar_dirty)  { draw_bar();       s_bar_dirty  = false; }

        uint64_t now = time_us_64();
        if (now >= next_link_log) {
            DIAG("uartkbd frames=%u errors=%u\n",
                 (unsigned)uartkbd_frames(), (unsigned)uartkbd_errors());
            next_link_log = now + 1000000;
        }
        sleep_ms(2);
    }
}
