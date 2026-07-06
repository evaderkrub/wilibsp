// bsp/ir/ir_tx_pack.c
#include "ir_tx_pack.h"

uint32_t ir_tx_pack(const uint32_t *durs_us, uint32_t n, uint32_t carrier_hz,
                    uint32_t *words, uint32_t max) {
    if (!n || n > max) return 0;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t p = ((uint64_t)durs_us[i] * carrier_hz + 500000u) / 1000000u;
        if (p == 0) p = 1;
        if (p > 0x7FFFFFFFu) return 0;
        uint32_t mark = (i & 1u) ? 0u : 1u;   // even index = mark
        words[i] = (mark << 31) | (uint32_t)(p - 1u);
    }
    return n;
}
