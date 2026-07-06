// bsp/ir/ir_decode.h — protocol decoders over mark/space us timing arrays.
// Input convention: t[0] is a mark; entries alternate mark/space.
// Each decoder returns true and fills *out only on a strict-tolerance match.
#ifndef IR_DECODE_H
#define IR_DECODE_H
#include "ir_types.h"

bool ir_decode_nec(const uint32_t *t, uint32_t n, ir_message_t *out);       // NEC + NECext + repeat
bool ir_decode_samsung32(const uint32_t *t, uint32_t n, ir_message_t *out); // Task 3
bool ir_decode_sirc(const uint32_t *t, uint32_t n, ir_message_t *out);      // Task 3 (12/15/20)
bool ir_decode_rca(const uint32_t *t, uint32_t n, ir_message_t *out);       // Task 3
bool ir_decode_rc5(const uint32_t *t, uint32_t n, ir_message_t *out);       // Task 4
bool ir_decode_rc6(const uint32_t *t, uint32_t n, ir_message_t *out);       // Task 4
bool ir_decode_kaseikyo(const uint32_t *t, uint32_t n, ir_message_t *out);  // Task 5

// Dispatcher (Task 6): first strict match in a fixed order.
bool ir_decode(const uint32_t *t, uint32_t n, ir_message_t *out);
// All strict matches (ambiguity surfaced to the UI, spec section 8). Returns count.
uint32_t ir_decode_all(const uint32_t *t, uint32_t n, ir_message_t *out, uint32_t max);
#endif
