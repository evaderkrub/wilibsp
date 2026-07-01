// src/leds/led_ui.c
#include "leds/led_ui.h"
#include "gfx/palette.h"

#define LED_LVL_MIN            20u  // ~8% — deep breathe floor so the 0.1 s pulse is visible
#define LED_LVL_MAX           128u  // ~50% — breathe ceiling / fade start level
#define LED_BREATHE_MS        100u  // breathe period while receiving (0.1 s pulse)
#define LED_FADE_MS           400u  // LED_LVL_MAX -> 0 decay when the signal stops
#define LED_MONITOR_MS         10u  // Monitor LED refresh throttle (smooth 0.1 s pulse)
#define LED_SPECTRUM_REVERSED   0   // set 1 if the physical LED order runs right->left

// Expand RGB565 -> 8-bit rgb_t (matches the waterfall's palette colors).
static inline rgb_t rgb565_to_rgb(uint16_t c) {
    rgb_t o;
    o.r = (uint8_t)(((c >> 11) & 0x1Fu) << 3);
    o.g = (uint8_t)(((c >> 5)  & 0x3Fu) << 2);
    o.b = (uint8_t)((c & 0x1Fu) << 3);
    return o;
}

uint8_t led_fade_step(led_fade_t *st, bool present, uint32_t now_ms) {
    if (!st->init) { st->level = 0; st->last_ms = now_ms; st->init = true; }
    uint32_t dt = now_ms - st->last_ms;
    st->last_ms = now_ms;
    if (present) {                                // receiving -> breathe 20%..50%
        uint32_t half  = LED_BREATHE_MS / 2u;
        uint32_t phase = now_ms % LED_BREATHE_MS;
        uint32_t tri   = (phase < half) ? phase : (LED_BREATHE_MS - phase);  // 0..half..0
        st->level = (uint8_t)(LED_LVL_MIN + (uint32_t)(LED_LVL_MAX - LED_LVL_MIN) * tri / half);
    } else if (st->level) {                       // signal stopped -> fade to 0
        uint32_t dec = (dt * LED_LVL_MAX) / LED_FADE_MS;   // LED_LVL_MAX -> 0 over LED_FADE_MS
        st->level = (dec >= st->level) ? 0 : (uint8_t)(st->level - dec);
    }
    return st->level;
}

void led_spectrum_map(const int *disp, int n, rgb_t out[7]) {
    for (int i = 0; i < 7; i++) {
        int lo = i * n / 7;
        int hi = (i + 1) * n / 7;
        if (hi <= lo) hi = lo + 1;
        int mx = disp[lo];
        for (int k = lo + 1; k < hi && k < n; k++) if (disp[k] > mx) mx = disp[k];
        int idx = LED_SPECTRUM_REVERSED ? (6 - i) : i;
        out[idx] = rgb565_to_rgb(inferno_rgb565(mx));
    }
}

#ifndef HOST_TEST
#include "leds/ws2812_driver.h"
#include "platform/board.h"
#include "hardware/pio.h"

static led_fade_t s_fade;
static uint8_t    s_last_lvl;
static uint32_t   s_next_ms;

static void led_reset_state(void) { s_fade.init = false; s_last_lvl = 0; s_next_ms = 0; }

void led_ui_init(void) {
    ws2812_init(pio1, (uint)pio_claim_unused_sm(pio1, true), PIN_LED_DATA);
    led_reset_state();
}

void led_ui_clear(void) {
    ws2812_clear(); ws2812_show();
    led_reset_state();
}

void led_ui_spectrum(const int *disp, int n) {
    rgb_t px[7];
    led_spectrum_map(disp, n, px);
    for (uint i = 0; i < 7u; i++) ws2812_set_pixel(i, px[i]);
    rgb_t off = {0, 0, 0};
    for (uint i = 7u; i < WS2812_NUM_PIXELS; i++) ws2812_set_pixel(i, off);
    ws2812_show();
}

// Green while a signal is present, then a smooth fade to off. Called each Monitor
// loop; the show() is throttled (~40 fps) and skipped when the level is unchanged.
void led_ui_monitor(bool present, uint32_t now_ms) {
    uint8_t lvl = led_fade_step(&s_fade, present, now_ms);
    if ((int32_t)(now_ms - s_next_ms) < 0) return;   // throttle the expensive show()
    s_next_ms = now_ms + LED_MONITOR_MS;
    if (lvl == s_last_lvl) return;                    // nothing visibly changed
    s_last_lvl = lvl;
    rgb_t green = {0, lvl, 0};
    ws2812_fill(green);
    ws2812_show();
}
#endif
