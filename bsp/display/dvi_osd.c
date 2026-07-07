// src/display/dvi_osd.c — pure software OSD rasterizer (see dvi_osd.h).
#include "display/dvi_osd.h"
#include "display/font5x7.h"
#include <stddef.h>

int dvi_osd_strip_h(int region_h, int movie_h, bool *overlay) {
    int bar = (region_h - movie_h) / 2;
    // Margin mode needs room for a scale-1 glyph row (8px). A thinner bar
    // (e.g. a 270-tall movie in the 272 region -> 1px) would center the text
    // past the region edge and the draw clips to NOTHING — fall back to the
    // per-frame overlay instead.
    if (bar >= 8) { if (overlay) *overlay = false; return bar; }
    if (overlay) *overlay = true;
    return DVI_OSD_OVERLAY_H;
}

void dvi_osd_fill_rect(uint16_t *base, int stride, int region_w, int region_h,
                       int x, int y, int w, int h, uint16_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > region_w) w = region_w - x;
    if (y + h > region_h) h = region_h - y;
    if (w <= 0 || h <= 0) return;
    for (int r = 0; r < h; r++) {
        uint16_t *row = base + (size_t)(y + r) * stride;
        for (int c = 0; c < w; c++) row[x + c] = color;
    }
}

void dvi_osd_text(uint16_t *base, int stride, int region_w, int region_h,
                  int x, int y, int scale, uint16_t fg, uint16_t bg,
                  const char *s) {
    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;
    const int w = 6 * scale, h = 8 * scale;
    for (; *s; s++, x += w) {
        if (x + w > region_w || y + h > region_h || x < 0 || y < 0) break;
        char c = *s;
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        const uint8_t *cols = (c >= FONT5X7_FIRST && c <= FONT5X7_LAST)
                                  ? font5x7[c - FONT5X7_FIRST]
                                  : font5x7[0];
        for (int gy = 0; gy < h; gy++) {
            uint16_t *row = base + (size_t)(y + gy) * stride + x;
            int fr = gy / scale;                 // font row 0..7 (7 = gap)
            for (int gx = 0; gx < w; gx++) {
                int col = gx / scale;            // font col 0..5 (5 = gap)
                bool on = col < 5 && fr < 7 && ((cols[col] >> fr) & 1);
                row[gx] = on ? fg : bg;
            }
        }
    }
}

void dvi_osd_text_msg(uint16_t *base, int stride, int region_w, int region_h,
                      int movie_h, const char *msg) {
    int strip = dvi_osd_strip_h(region_h, movie_h, NULL);
    if (strip <= 0) return;
    int scale = strip >= 18 ? 2 : 1;
    int y = region_h - strip / 2 - 4 * scale; // center the 8*scale-tall glyph cell vertically in the strip
    dvi_osd_text(base, stride, region_w, region_h, 8, y, scale,
                 0xFFFF, 0x0000, msg);
}

void dvi_osd_progress(uint16_t *base, int stride, int region_w, int region_h,
                      int movie_h, uint32_t cur, uint32_t total,
                      const char *text) {
    int strip = dvi_osd_strip_h(region_h, movie_h, NULL);
    if (strip <= 0) return;
    int y0 = region_h - strip;

    // The DVI letterbox strips are thin (the region is only 288 tall), so the
    // LCD's text-above-a-tall-bar layout would drop the bar on top of the label.
    // Lay the time label and the bar out SIDE BY SIDE on one vertically-centered
    // band: label on the left, bar filling the rest of the width. The label is
    // drawn only when an 8px glyph row fits the strip (>= 8px); thinner strips
    // show the bar alone.
    int band_h = strip < 8 ? strip : 8;
    int band_y = y0 + (strip - band_h) / 2;     // center the band in the strip

    int bar_x = 8;
    if (band_h >= 8) {                           // room for the 8px label row
        dvi_osd_text(base, stride, region_w, region_h, 8, band_y, 1,
                     0xFFFF, 0x0000, text);
        int label_w = 0;
        for (const char *p = text; *p; p++) label_w += 6;   // 6px per glyph cell
        bar_x = 8 + label_w + 4;                 // 4px gap after the label
    }
    int bar_w = region_w - 8 - bar_x;
    if (bar_w < 1) return;                        // label filled the row; no bar

    uint32_t fill = total ? (uint32_t)((uint64_t)bar_w * cur / total) : 0;
    if (fill > (uint32_t)bar_w) fill = (uint32_t)bar_w;
    dvi_osd_fill_rect(base, stride, region_w, region_h, bar_x, band_y,
                      bar_w, band_h, 0x0C63);                  // track
    if (fill)
        dvi_osd_fill_rect(base, stride, region_w, region_h, bar_x, band_y,
                          (int)fill, band_h, 0xFFFF);           // done
}

void dvi_osd_clear_strip(uint16_t *base, int stride, int region_w, int region_h,
                         int movie_h) {
    int strip = dvi_osd_strip_h(region_h, movie_h, NULL);
    if (strip <= 0) return;
    dvi_osd_fill_rect(base, stride, region_w, region_h, 0, region_h - strip,
                      region_w, strip, 0x0000);
}
