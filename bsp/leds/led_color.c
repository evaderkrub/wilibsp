// src/leds/led_color.c
// Adapted from evaderkrub/sensorview (BSD-3-Clause).
#include "leds/led_color.h"

uint32_t led_color_pack_grb(rgb_t c) {
    return ((uint32_t)c.g << 16) | ((uint32_t)c.r << 8) | (uint32_t)c.b;
}

uint8_t led_color_scale(uint8_t channel, uint8_t brightness) {
    return (uint8_t)(((uint16_t)channel * brightness + 127u) / 255u);
}
