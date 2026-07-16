// src/ui/lvgl_port.c — LVGL v9.3 display port for FreeWili2 (RP2350B) + ST7796.
//
// Buffer sizing note:
//   In LVGL v9, lv_color_t is always a 3-byte RGB888 struct regardless of
//   LV_COLOR_DEPTH.  For an RGB565 display (2 bytes per pixel) the draw buffers
//   must be declared as uint8_t and sized with (LV_COLOR_DEPTH / 8) = 2, NOT
//   sizeof(lv_color_t) = 3.  Using lv_color_t would waste 50% RAM and overflow
//   the 512 KB SRAM region even at 40 lines; uint8_t at 20 lines fits with ~12 KB
//   headroom.
//
// Async flush handshake:
//   flush_cb() byte-swaps the region and kicks the non-blocking DMA.
//   The DMA_IRQ_0 handler in st7796.c calls flush_ready_isr() after the SPI
//   drains and CS is raised.  flush_ready_isr() is the ONLY caller of
//   lv_display_flush_ready() — calling it in flush_cb() would corrupt the
//   handshake.
#include "lvgl_port.h"
#include "display/st7796.h"
#include "input/ft6336.h"
#include "platform/diag.h"
#include "lvgl.h"   /* exposes lv_theme_default_init when LV_USE_THEME_DEFAULT=1 */
#include "pico/stdlib.h"

_Static_assert(LV_COLOR_DEPTH == 16,
    "lvgl_port draw buffers + flush byte-swap assume RGB565 (2 B/px)");

/* ---- buffer sizing ---- */
#define HOR       480
#define VER       320
/* 10 lines per buffer: 2 × 10 × 480 × 2 B = 19 200 B total. Trimmed from 20 to
   free SRAM for the Monitor feature (copy_to_ram: all code+data+bss in 512 KB).
   The waterfall/Monitor traces are direct-blit, so LVGL only renders header chrome
   through these buffers — 10 lines is ample. */
#define BUF_LINES 10

/* Static pixel buffers.  uint8_t at 2 bytes/pixel (RGB565).
   Aligned to 4 bytes as required by LV_DRAW_BUF_ALIGN in lv_conf.h. */
static uint8_t buf1[HOR * BUF_LINES * (LV_COLOR_DEPTH / 8)] __attribute__((aligned(4)));
static uint8_t buf2[HOR * BUF_LINES * (LV_COLOR_DEPTH / 8)] __attribute__((aligned(4)));

/* Global display handle — needed by the ISR callback. */
static lv_display_t *g_disp;

/* Global indev handle for the FT6336 touch panel. */
static lv_indev_t *g_indev;
static volatile uint32_t g_flush_count;

/* ---- Touch input device read callback ---- */
static void indev_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    uint16_t x, y;
    if (ft6336_poll(&x, &y)) {
        data->point.x = (int32_t)x;
        data->point.y = (int32_t)y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* ---- LVGL tick callback ---- */
static uint32_t tick_cb(void)
{
    return to_ms_since_boot(get_absolute_time());
}

/* ---- LVGL log callback → RTT ---- */
static void log_cb(lv_log_level_t level, const char *msg)
{
    (void)level;
    DIAG("lv: %s", msg);
}

/* ---- DMA completion ISR shim ---- */
/* Called from st7796's DMA_IRQ_0 handler after SPI drains + CS raised. */
static void flush_ready_isr(void)
{
    lv_display_flush_ready(g_disp);
}

/* ---- LVGL flush callback ---- */
/* px_map contains rendered pixels in LVGL native RGB565 (little-endian).
   The ST7796 panel expects big-endian RGB565, so byte-swap before the DMA. */
static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)disp;
    if (g_flush_count++ == 0) DIAG("orca_browser: first LVGL flush %d,%d-%d,%d\n", area->x1, area->y1, area->x2, area->y2);
    uint32_t w = (uint32_t)(area->x2 - area->x1 + 1);
    uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);

    /* Byte-swap each pixel to big-endian RGB565 for the panel. */
    lv_draw_sw_rgb565_swap(px_map, w * h);

    /* Kick the non-blocking DMA flush; flush_ready_isr fires when done. */
    st7796_flush_async((uint16_t)area->x1, (uint16_t)area->y1,
                       (uint16_t)area->x2, (uint16_t)area->y2,
                       (const uint16_t *)px_map, flush_ready_isr);
    /* DO NOT call lv_display_flush_ready() here — the DMA IRQ does it. */
}

/* ---- Public API ---- */

void lvgl_port_init(void)
{
    /* Route LVGL log output to RTT. */
    lv_log_register_print_cb(log_cb);

    /* Use hardware millisecond counter for LVGL timing. */
    lv_tick_set_cb(tick_cb);

    /* Create the display and install partial double buffers.
       sizeof(buf1) = HOR * BUF_LINES * 2 bytes. */
    g_disp = lv_display_create(HOR, VER);
    lv_display_set_flush_cb(g_disp, flush_cb);
    lv_display_set_buffers(g_disp, buf1, buf2, sizeof(buf1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    /* Apply dark default theme (cyan primary, grey secondary). */
    lv_theme_t *th = lv_theme_default_init(g_disp,
        lv_palette_main(LV_PALETTE_CYAN), lv_palette_main(LV_PALETTE_GREY),
        true /*dark*/, &lv_font_montserrat_14);
    lv_display_set_theme(g_disp, th);

    /* Initialise the FT6336 touch controller and register it as an LVGL indev. */
    ft6336_init();
    g_indev = lv_indev_create();
    lv_indev_set_type(g_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(g_indev, indev_read_cb);
}

void lvgl_port_run(void)
{
    lv_timer_handler();
}
