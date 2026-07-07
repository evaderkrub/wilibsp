// src/display/dvi_osd.h — pure software OSD rasterizer for the DVI video region.
// Draws status text and a progress bar into a caller-supplied strided
// native-endian RGB565 framebuffer (the HSTX video region), reusing font5x7.
// No Pico SDK dependency: host-testable. All coordinates are region-space
// (origin = region pixel(0,0)); the blit only writes x in [0, region_w) so it
// never touches the HSTX command words interleaved between rows.
#ifndef DVI_OSD_H
#define DVI_OSD_H
#include <stdint.h>
#include <stdbool.h>

// Strip height used when the movie fills the region (no letterbox margin); the
// OSD is then overlaid on the bottom video rows and must be repainted per frame.
#define DVI_OSD_OVERLAY_H 18

// Bottom OSD strip height for a movie_h-tall movie centered in a region_h-tall
// region. Returns the real bottom-margin height when letterboxed; otherwise
// DVI_OSD_OVERLAY_H. *overlay (may be NULL) is set true in the no-margin case.
int dvi_osd_strip_h(int region_h, int movie_h, bool *overlay);

// Fill a rectangle (clipped to the region) with a native-endian RGB565 color.
void dvi_osd_fill_rect(uint16_t *base, int stride, int region_w, int region_h,
                       int x, int y, int w, int h, uint16_t color);

// Draw text (5x7 font, scale 1-4) at (x,y). fg/bg are native-endian RGB565; the
// full glyph cell is painted (bg behind every glyph). Whole string is skipped
// from the first glyph that would overflow the region (matches the LCD).
void dvi_osd_text(uint16_t *base, int stride, int region_w, int region_h,
                  int x, int y, int scale, uint16_t fg, uint16_t bg,
                  const char *s);

// Draw a status message centered in the bottom OSD strip.
void dvi_osd_text_msg(uint16_t *base, int stride, int region_w, int region_h,
                      int movie_h, const char *msg);

// Draw a progress bar (filled = cur/total) with a label line in the bottom strip.
void dvi_osd_progress(uint16_t *base, int stride, int region_w, int region_h,
                      int movie_h, uint32_t cur, uint32_t total,
                      const char *text);

// Re-black the bottom OSD strip (margin-mode erase).
void dvi_osd_clear_strip(uint16_t *base, int stride, int region_w, int region_h,
                         int movie_h);

#endif // DVI_OSD_H
