// hello_dvi — on-hardware smoke test for the harvested plain-DVI driver
// (bsp/display/hstx_dvi) + the DVI OSD. RTT-only. Paints an 8-bar colour test
// pattern + a 1px box outline into the 480x288 video region (centered in the
// 640x480 DVI frame), draws an OSD title + an animating progress bar in the
// bottom letterbox margin, and streams it out the DVI connector (GPIO 12-19).
//
// Pass criteria on a DVI monitor: locks to 640x480, 8 vertical colour bars
// (white, yellow, cyan, green, magenta, red, blue, black) inside a white box,
// black borders around, and a progress bar sweeping left->right along the
// bottom. RTT prints "hello_dvi: DVI up, clk_hstx=126000 kHz".
#include "fw2.h"
#include "platform/diag.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"

#define VID_W 480
#define VID_H 288

// Eight classic colour-bar RGB565 values (native little-endian, as stored).
static const uint16_t BARS[8] = {
    0xFFFF, // white
    0xFFE0, // yellow
    0x07FF, // cyan
    0x07E0, // green
    0xF81F, // magenta
    0xF800, // red
    0x001F, // blue
    0x0000, // black
};

// Paint 8 vertical colour bars across the video region, then a 1px white box
// outline around the whole region, into the strided native-endian framebuffer.
static void paint_test_pattern(void) {
    uint16_t *base = hstx_dvi_video_base();
    int stride = hstx_dvi_video_stride();
    int w = hstx_dvi_video_w();
    int h = hstx_dvi_video_h();
    for (int y = 0; y < h; y++) {
        uint16_t *row = base + (size_t)y * stride;
        for (int x = 0; x < w; x++) row[x] = BARS[(x * 8) / w];
    }
    // 1px white box outline.
    for (int x = 0; x < w; x++) {
        base[x] = 0xFFFF;
        base[(size_t)(h - 1) * stride + x] = 0xFFFF;
    }
    for (int y = 0; y < h; y++) {
        base[(size_t)y * stride] = 0xFFFF;
        base[(size_t)y * stride + (w - 1)] = 0xFFFF;
    }
}

int main(void) {
    board_init_clk(252000);             // 252 MHz -> exact 25.2 MHz DVI pixel clock
                                        // (the board default via board_init() is 250)
    hstx_dvi_init(VID_W, VID_H);        // start the scanout (480x288 in 640x480)
    DIAG("hello_dvi: DVI up, clk_hstx=%u kHz\n",
         (unsigned)(clock_get_hz(clk_hstx) / 1000u));

    paint_test_pattern();

    // OSD title, centered in the bottom letterbox margin below the movie.
    uint16_t *rbase = hstx_dvi_region_base();
    int rstride = hstx_dvi_video_stride();
    int region_h = hstx_dvi_region_h();
    dvi_osd_text_msg(rbase, rstride, VID_W, region_h, VID_H, "FREEWILI 2 DVI");
    sleep_ms(1500);

    // Animate a progress bar across the bottom margin so motion proves the
    // scanout is live and reading the framebuffer continuously.
    uint32_t t = 0;
    for (;;) {
        dvi_osd_progress(rbase, rstride, VID_W, region_h, VID_H,
                         t % 101u, 100u, "DVI");
        t++;
        sleep_ms(50);
    }
}
