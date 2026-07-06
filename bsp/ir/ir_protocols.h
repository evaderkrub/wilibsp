// bsp/ir/ir_protocols.h — shared timing matcher + protocol name table.
#ifndef IR_PROTOCOLS_H
#define IR_PROTOCOLS_H
#include "ir_types.h"

// True when `actual` is within expected*30% + 60us of `expected`.
// Wide enough for consumer-remote jitter, tight enough to separate bit cells.
static inline bool ir_match_us(uint32_t actual, uint32_t expected) {
    uint32_t tol = expected * 3u / 10u + 60u;
    uint32_t d = actual > expected ? actual - expected : expected - actual;
    return d <= tol;
}
#endif
