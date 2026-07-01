// src/leds/ws2812_driver.h — adapted from evaderkrub/sensorview (BSD-3-Clause).
#ifndef WS2812_DRIVER_H
#define WS2812_DRIVER_H
#include <stdint.h>
#include "hardware/pio.h"
#include "leds/led_color.h"

#define WS2812_NUM_PIXELS 16
// BSP-wide alias for the LED count (single source of truth = 16, verified board).
#define FW2_LED_COUNT WS2812_NUM_PIXELS

void    ws2812_init(PIO pio, uint sm, uint gpio);
void    ws2812_set_pixel(uint i, rgb_t c);
void    ws2812_fill(rgb_t c);
void    ws2812_clear(void);
void    ws2812_set_brightness(uint8_t level);
uint8_t ws2812_get_brightness(void);
void    ws2812_show(void);
#endif
