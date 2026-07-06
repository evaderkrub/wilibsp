// bsp/ir/ir_encode.h — protocol encoders -> mark/space us timing arrays.
#ifndef IR_ENCODE_H
#define IR_ENCODE_H
#include "ir_types.h"
// Renders one frame. Returns the timing count, or 0 on unsupported
// protocol / buffer overflow. Output follows the mark-first convention.
uint32_t ir_encode(const ir_message_t *msg, uint32_t *t, uint32_t max);
#endif
