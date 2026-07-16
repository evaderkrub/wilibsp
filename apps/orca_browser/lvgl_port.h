// src/ui/lvgl_port.h — LVGL v9.3 display port for FreeWili2 (RP2350B).
// Registers RTT log cb, tick cb, creates the LVGL display with partial
// double buffers and the async DMA flush callback to st7796.
#ifndef LVGL_PORT_H
#define LVGL_PORT_H

// Call AFTER st7796_init() + lv_init().
// Registers the RTT log print cb, tick cb, creates the display with
// partial double buffers, and installs the async-DMA flush callback.
void lvgl_port_init(void);

// Service call: runs lv_timer_handler(). Call repeatedly in the main loop.
// (Input indev read is added in Task 5.)
void lvgl_port_run(void);

#endif /* LVGL_PORT_H */
