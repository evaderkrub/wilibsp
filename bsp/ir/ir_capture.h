// bsp/ir/ir_capture.h — IR receiver edge capture (PIO2 + endless DMA ring).
// TSOP-style demodulated input on PIN_IR_RX: idle HIGH, mark = LOW.
// Free-running once started; poll from the main loop, no interrupts.
#ifndef IR_CAPTURE_H
#define IR_CAPTURE_H
#include "ir_frame.h"

void ir_capture_init(void);            // claim PIO SM + DMA (call once)
void ir_capture_start(void);           // arm PIO + DMA ring
void ir_capture_stop(void);            // halt (e.g. while transmitting)
// Drain new edge durations into the frame builder. Returns true when a
// complete frame (burst ended by a >=IR_GAP_US gap) is ready in *out.
bool ir_capture_poll(ir_frame_t *out);
// Cumulative overrun count since init. An overrun means ir_capture_poll found
// the DMA ring more than half full (poll starvation): the backlog was dropped
// and capture restarted, because a full lap cannot be detected after the fact
// and an odd missed word count would invert mark/space parity from then on.
// Keep poll cadence such that this stays 0 (the ring covers roughly a second
// at worst-case edge rates, e.g. RC6/Kaseikyo).
uint32_t ir_capture_overruns(void);
#endif
