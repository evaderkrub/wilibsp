// tests/test_dvi_osd.c — host tests for the pure DVI OSD geometry (dvi_osd.c has
// no Pico SDK dependency). Covers the letterbox strip-height decision and the
// rect clipping the text/progress helpers are built on.
#include "display/dvi_osd.h"
#include "test_util.h"

int main(void) {
    // --- dvi_osd_strip_h: real bottom margin when the bar is >= 8px ---
    bool overlay = true;
    ASSERT_EQ(dvi_osd_strip_h(320, 288, &overlay), 16);   // (320-288)/2 = 16
    ASSERT_TRUE(overlay == false);

    // Thin bar (< 8px) -> overlay fallback, DVI_OSD_OVERLAY_H, overlay=true.
    overlay = false;
    ASSERT_EQ(dvi_osd_strip_h(272, 270, &overlay), DVI_OSD_OVERLAY_H);  // bar=1
    ASSERT_TRUE(overlay == true);

    // Movie fills the region -> bar=0 -> overlay fallback.
    overlay = false;
    ASSERT_EQ(dvi_osd_strip_h(320, 320, &overlay), DVI_OSD_OVERLAY_H);
    ASSERT_TRUE(overlay == true);

    // NULL overlay pointer is allowed; large margin returned verbatim.
    ASSERT_EQ(dvi_osd_strip_h(480, 320, NULL), 80);       // (480-320)/2 = 80

    // --- dvi_osd_fill_rect: clipping. Region is 16 wide x 20 tall, stride 16. ---
    uint16_t fb[16 * 20];
    for (int i = 0; i < 16 * 20; i++) fb[i] = 0;

    // Top-left overhang: rect (-2,-3) 5x5 clips to cols 0..2, rows 0..1.
    dvi_osd_fill_rect(fb, 16, 16, 20, -2, -3, 5, 5, 0xABCD);
    ASSERT_EQ(fb[0 * 16 + 0], 0xABCD);   // inside
    ASSERT_EQ(fb[1 * 16 + 2], 0xABCD);   // inside, far corner of the clipped rect
    ASSERT_EQ(fb[2 * 16 + 0], 0x0000);   // row 2 not filled (h clipped to 2)
    ASSERT_EQ(fb[0 * 16 + 3], 0x0000);   // col 3 not filled (w clipped to 3)

    // Bottom-right overhang: rect (14,18) 5x5 clips to cols 14..15, rows 18..19.
    dvi_osd_fill_rect(fb, 16, 16, 20, 14, 18, 5, 5, 0x1234);
    ASSERT_EQ(fb[18 * 16 + 14], 0x1234);
    ASSERT_EQ(fb[19 * 16 + 15], 0x1234);

    // Fully out of bounds: nothing written, no crash.
    uint16_t before = fb[0];
    dvi_osd_fill_rect(fb, 16, 16, 20, 20, 0, 4, 4, 0xFFFF);
    ASSERT_EQ(fb[0], before);

    TEST_RETURN();
}
