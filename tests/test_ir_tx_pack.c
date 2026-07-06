#include "test_util.h"
#include "ir_tx_pack.h"

int main(void) {
    uint32_t w[16];

    // 1000us mark + 500us space at 38 kHz: 38 periods and 19 periods.
    uint32_t durs[2] = {1000, 500};
    ASSERT_EQ(ir_tx_pack(durs, 2, 38000, w, 16), 2);
    ASSERT_EQ(w[0], (1u << 31) | (38u - 1u));   // even index = mark
    ASSERT_EQ(w[1], (19u - 1u));                // odd index = space

    // Sub-period duration clamps to 1 carrier period, never 0.
    uint32_t tiny[1] = {3};
    ASSERT_EQ(ir_tx_pack(tiny, 1, 38000, w, 16), 1);
    ASSERT_EQ(w[0], (1u << 31) | 0u);

    // Rounding: 563us at 38kHz = 21.394 periods -> 21.
    uint32_t nec_mark[1] = {563};
    ASSERT_EQ(ir_tx_pack(nec_mark, 1, 38000, w, 16), 1);
    ASSERT_EQ(w[0], (1u << 31) | (21u - 1u));

    // Overflow of the output buffer and empty input both return 0.
    uint32_t four[4] = {100, 100, 100, 100};
    ASSERT_EQ(ir_tx_pack(four, 4, 38000, w, 2), 0);
    ASSERT_EQ(ir_tx_pack(four, 0, 38000, w, 16), 0);

    TEST_RETURN();
}
