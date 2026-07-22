/*
 * uartkbd — FW2 UART keyboard binding: UART1 @ 62500 8N1 on GPIO38 (TX,
 * claimed but never driven) / GPIO39 (RX). Frames arrive unsolicited;
 * RX-only. Polled — call uartkbd_task() every main-loop iteration.
 */
#ifndef UARTKBD_H
#define UARTKBD_H

#include "uartkbd_parse.h"

void     uartkbd_init(void);
void     uartkbd_task(void);
bool     uartkbd_next_event(uartkbd_event_t *ev);
uint16_t uartkbd_buttons(void);
uint8_t  uartkbd_flags(void);
uint32_t uartkbd_frames(void);
uint32_t uartkbd_errors(void);

#endif /* UARTKBD_H */
