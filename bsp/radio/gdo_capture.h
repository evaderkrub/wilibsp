// src/radio/gdo_capture.h
#ifndef GDO_CAPTURE_H
#define GDO_CAPTURE_H
#include <stdint.h>
#include <stdbool.h>

// Off-bus GDO0 edge capture (PIO + DMA). Free-running once started; never touches
// SPI1 / PIN_LCD_CS. Durations are ~1 us ticks the line held each level.
void     gdo_capture_init(void);                       // claim PIO/SM/DMA (once)
void     gdo_capture_start(void);                       // latch level, arm PIO+DMA
void     gdo_capture_stop(void);                        // halt PIO+DMA
void     gdo_capture_attach_pin(void);                  // re-route GDO0 to the PIO (after TX drove it as SIO)
uint32_t gdo_capture_drain(uint32_t *dst, uint32_t max);// copy new durations, return count
#endif
