#include "ui.h"
#include "fw2.h"
#include "protocol.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>

#define SCALE      2
#define CELL_W     (6 * SCALE)     // 12 px
#define CELL_H     (8 * SCALE)     // 16 px
#define STATUS_H   24
#define CHAT_Y     STATUS_H
#define CHAT_LINES 12
#define CHAT_H     (CHAT_LINES * CELL_H)          // 192 -> chat ends at y=216
#define GRID_Y     (CHAT_Y + CHAT_H)
#define GRID_COLS  3
#define GRID_ROWS  3
#define BTN_W      (ST7796_W / GRID_COLS)         // 160
#define BTN_H      ((ST7796_H - GRID_Y) / GRID_ROWS) // 34
#define LINE_CHARS 40
#define TAG_CHARS  3                              // "A7 "

static inline uint16_t rgb565_be(uint8_t r, uint8_t g, uint8_t b) {
    uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return (uint16_t)((c >> 8) | (c << 8));
}

#define COL_BG      rgb565_be(0, 0, 32)
#define COL_STATUS  rgb565_be(24, 24, 72)
#define COL_BTN     rgb565_be(40, 40, 96)
#define COL_BTN_TXT rgb565_be(255, 255, 255)
#define COL_OWN     rgb565_be(255, 255, 255)

static uint16_t sender_color(uint8_t id) {
    static const uint8_t pal[8][3] = {
        { 255, 120, 120 }, { 120, 255, 120 }, { 120, 180, 255 }, { 255, 255, 120 },
        { 255, 140, 255 }, { 120, 255, 255 }, { 255, 180, 100 }, { 180, 255, 180 },
    };
    const uint8_t *c = pal[id & 7];
    return rgb565_be(c[0], c[1], c[2]);
}

// Chat line ring. Each stored line is pre-formatted to <= LINE_CHARS chars.
typedef struct { char text[LINE_CHARS + 1]; uint16_t color; bool own; } chat_line_t;
static chat_line_t s_lines[CHAT_LINES];
static unsigned s_nlines;
static uint8_t s_self;

static void chat_redraw(void) {
    st7796_fill_rect(0, CHAT_Y, ST7796_W, CHAT_H, COL_BG);
    for (unsigned i = 0; i < s_nlines; i++) {
        const chat_line_t *l = &s_lines[i];
        int len = (int)strlen(l->text);
        int x = l->own ? (ST7796_W - len * CELL_W) : 0;
        st7796_draw_text(x, CHAT_Y + (int)i * CELL_H, SCALE, l->color, COL_BG, l->text);
    }
}

static void push_line(const char *text, uint16_t color, bool own) {
    if (s_nlines == CHAT_LINES) {
        memmove(&s_lines[0], &s_lines[1], (CHAT_LINES - 1) * sizeof(chat_line_t));
        s_nlines--;
    }
    chat_line_t *l = &s_lines[s_nlines++];
    strncpy(l->text, text, LINE_CHARS);
    l->text[LINE_CHARS] = '\0';
    l->color = color;
    l->own = own;
}

void ui_add_message(uint8_t sender, const char *text, bool own) {
    uint16_t color = own ? COL_OWN : sender_color(sender);
    char line[LINE_CHARS + 1];
    unsigned tlen = strlen(text);
    // First line carries the sender tag; long payloads wrap to continuation lines.
    unsigned first = LINE_CHARS - TAG_CHARS;
    snprintf(line, sizeof line, "%02X %.*s", sender, (int)first, text);
    push_line(line, color, own);
    for (unsigned off = first; off < tlen; off += LINE_CHARS) {
        snprintf(line, sizeof line, "%.*s", LINE_CHARS, text + off);
        push_line(line, color, own);
    }
    chat_redraw();
}

void ui_set_status(bool txing, bool selftest) {
    st7796_fill_rect(0, 0, 200, STATUS_H, COL_STATUS);
    st7796_draw_text(4, 4, SCALE, COL_BTN_TXT, COL_STATUS,
                     txing ? "TX..." : (selftest ? "SELFTEST" : "LISTENING"));
}

void ui_set_stats(unsigned crc_err, int peak) {
    char buf[24];
    // 8-step signal bar from the demod peak tracker.
    int bars = 0;
    for (int t = 256; t <= peak && bars < 8; t <<= 1) bars++;
    snprintf(buf, sizeof buf, "E%-4u S%d ID%02X", crc_err % 10000u, bars, s_self);
    st7796_fill_rect(200, 0, ST7796_W - 200, STATUS_H, COL_STATUS);
    st7796_draw_text(204, 4, SCALE, COL_BTN_TXT, COL_STATUS, buf);
}

static void grid_draw(void) {
    for (int i = 0; i < 9; i++) {
        int cx = (i % GRID_COLS) * BTN_W, cy = GRID_Y + (i / GRID_COLS) * BTN_H;
        st7796_fill_rect(cx + 1, cy + 1, BTN_W - 2, BTN_H - 2, COL_BTN);
        const char *lbl = (i < PROTO_NUM_CANNED) ? proto_canned[i] : "RESEND";
        int len = (int)strlen(lbl);
        st7796_draw_text(cx + (BTN_W - len * CELL_W) / 2,
                         cy + (BTN_H - CELL_H) / 2, SCALE, COL_BTN_TXT, COL_BTN, lbl);
    }
}

void ui_init(uint8_t self_id) {
    s_self = self_id;
    s_nlines = 0;
    st7796_fill_screen(COL_BG);
    ui_set_status(false, false);
    ui_set_stats(0, 0);
    grid_draw();
}

// Touch: act on release for taps; long-press (>=1 s) in the status bar toggles
// self-test (fires while still held, then swallows the release).
ui_action_t ui_poll(void) {
    static bool was_down, swallow;
    static uint16_t down_x, down_y;
    static absolute_time_t down_t;
    uint16_t x, y;
    bool down = ft6336_poll(&x, &y);

    if (down && !was_down) {           // press
        was_down = true; swallow = false;
        down_x = x; down_y = y; down_t = get_absolute_time();
        return UI_NONE;
    }
    if (down && was_down && !swallow && down_y < STATUS_H &&
        absolute_time_diff_us(down_t, get_absolute_time()) >= 1000000) {
        swallow = true;                // long-press fired; ignore the release
        return UI_SELFTEST_TOGGLE;
    }
    if (!down && was_down) {           // release
        was_down = false;
        if (swallow) return UI_NONE;
        if (down_y >= GRID_Y) {
            int cell = (down_y - GRID_Y) / BTN_H * GRID_COLS + down_x / BTN_W;
            if (cell >= 0 && cell < PROTO_NUM_CANNED) return (ui_action_t)cell;
            if (cell == 8) return UI_RESEND;
        }
    }
    return UI_NONE;
}
