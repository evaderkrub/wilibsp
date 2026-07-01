// src/leds/led_ui.h
#ifndef LED_UI_H
#define LED_UI_H
#include <stdint.h>
#include <stdbool.h>
#include "leds/led_color.h"

// --- pure, host-testable helpers ---
typedef struct { uint8_t level; uint32_t last_ms; bool init; } led_fade_t;

// Signal-follow envelope. While `present`, the level breathes between ~8% and
// ~50% (triangle wave) to show activity; when the signal stops it fades to 0.
// Returns the current 0..255 level.
uint8_t led_fade_step(led_fade_t *st, bool present, uint32_t now_ms);

// Map disp[0..n-1] to the first 7 LEDs: 7 segment-maxes through the waterfall
// palette (inferno_rgb565 -> rgb_t). Only out[0..6] are written.
void led_spectrum_map(const int *disp, int n, rgb_t out[7]);

// --- driver-backed indicator (target only) ---
void led_ui_init(void);                          // ws2812 on pio1 + clear
void led_ui_clear(void);                         // all LEDs off (screen switch / Home)
void led_ui_monitor(bool present, uint32_t now_ms); // Monitor: green while signal, fade out
void led_ui_spectrum(const int *disp, int n);    // Analyzer: first 7 LEDs
#endif
