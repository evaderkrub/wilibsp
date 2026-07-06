// tests/ir_synth.h — build nominal protocol frames as mark/space us arrays.
#ifndef IR_SYNTH_H
#define IR_SYNTH_H
#include <stdint.h>

// NEC: 9000/4500 header, bits mark 560 + space 560(0)/1690(1) LSB-first,
// 32 data bits (addr, ~addr, cmd, ~cmd), 560 stop mark. Returns count (67).
static uint32_t synth_nec32(uint32_t v32, uint32_t *t) {
    uint32_t n = 0;
    t[n++] = 9000; t[n++] = 4500;
    for (int i = 0; i < 32; i++) {
        t[n++] = 560;
        t[n++] = (v32 >> i) & 1u ? 1690 : 560;
    }
    t[n++] = 560;
    return n;
}

// Apply +/-8% alternating jitter in place (decode must survive real slop).
static void jitter(uint32_t *t, uint32_t n) {
    for (uint32_t i = 0; i < n; i++)
        t[i] = (i & 1u) ? t[i] * 108u / 100u : t[i] * 92u / 100u;
}
#endif
