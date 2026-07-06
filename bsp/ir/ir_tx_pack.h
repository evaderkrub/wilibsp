// bsp/ir/ir_tx_pack.h — us durations -> IR TX PIO FIFO words (pure logic).
// Word: bit31 = 1 mark / 0 space; bits[30:0] = carrier periods - 1.
#ifndef IR_TX_PACK_H
#define IR_TX_PACK_H
#include <stdint.h>
// Returns the word count (== n), or 0 on empty input / n > max.
uint32_t ir_tx_pack(const uint32_t *durs_us, uint32_t n, uint32_t carrier_hz,
                    uint32_t *words, uint32_t max);
#endif
