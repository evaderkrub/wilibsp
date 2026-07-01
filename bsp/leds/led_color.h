// src/leds/led_color.h
// Adapted from evaderkrub/sensorview (BSD-3-Clause).
#ifndef LED_COLOR_H
#define LED_COLOR_H
#include <stdint.h>

typedef struct { uint8_t r, g, b; } rgb_t;

// Pack into WS2812 byte order in the low 24 bits: (g<<16)|(r<<8)|b.
uint32_t led_color_pack_grb(rgb_t c);
// Scale an 8-bit channel by brightness/255, rounded to nearest.
uint8_t  led_color_scale(uint8_t channel, uint8_t brightness);
#endif
