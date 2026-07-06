// bsp/ir/ir_tx.h — IR transmitter (PIO2 carrier modulator + one-shot DMA).
// Plays mark/space us timing arrays on PIN_IR_TX at a selectable carrier.
#ifndef IR_TX_H
#define IR_TX_H
#include <stdint.h>
#include <stdbool.h>

void ir_tx_init(uint32_t carrier_hz);       // claim SM + DMA (call once)
void ir_tx_set_carrier(uint32_t hz);        // 36000/38000/40000/56000 typical
// Starts an async send (packs into an internal buffer, one-shot DMA).
// False if a send is in flight, n == 0, or n > 512.
bool ir_tx_send(const uint32_t *durs_us, uint32_t n, uint32_t carrier_hz);
// True while DMA or the PIO FIFO still holds words. NOTE: goes false while
// the final word is still playing — allow a frame-length settle before
// re-arming capture (the loopback test waits for the RX gap instead), and a
// follow-up send with a different carrier_hz during that window retunes the
// divider under the tail. Allow a frame-length settle before re-sending at a
// new carrier.
bool ir_tx_busy(void);
void ir_tx_stop(void);                      // abort + force pin low
#endif
