#include "test_util.h"
#include "ir_synth.h"
#include "ir_decode.h"
#include "ir_encode.h"

int main(void) {
    uint32_t t[IR_MAX_TIMINGS];
    ir_message_t m, all[4];

    // Every protocol dispatches to itself through ir_decode().
    ir_message_t cases[] = {
        {IR_PROTO_NEC, 0x04, 0x08, false},
        {IR_PROTO_NECEXT, 0xBEEF, 0x1234, false},
        {IR_PROTO_SAMSUNG32, 0x07, 0x02, false},
        {IR_PROTO_SIRC, 0x01, 0x15, false},
        {IR_PROTO_SIRC15, 0x97, 0x15, false},
        {IR_PROTO_SIRC20, 0x1A37, 0x15, false},
        {IR_PROTO_RCA, 0x0A, 0x55, false},
        {IR_PROTO_RC5, 0x05, 0x35, false},
        {IR_PROTO_RC6, 0x07, 0x0D, false},
        {IR_PROTO_KASEIKYO, 0x2002, 0x30D, false},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        uint32_t n = ir_encode(&cases[i], t, IR_MAX_TIMINGS);
        ASSERT_TRUE(n > 0);
        ASSERT_TRUE(ir_decode(t, n, &m));
        ASSERT_EQ(m.protocol, cases[i].protocol);
        ASSERT_EQ(m.address, cases[i].address);
        ASSERT_EQ(m.command, cases[i].command);
    }

    // Garbage: no decode.
    uint32_t g[7] = {300, 300, 300, 300, 300, 300, 300};
    ASSERT_TRUE(!ir_decode(g, 7, &m));
    ASSERT_EQ(ir_decode_all(g, 7, all, 4), 0);

    // ir_decode_all returns at least the dispatcher's answer.
    uint32_t n = ir_encode(&cases[0], t, IR_MAX_TIMINGS);
    uint32_t k = ir_decode_all(t, n, all, 4);
    ASSERT_TRUE(k >= 1);
    ASSERT_EQ(all[0].protocol, IR_PROTO_NEC);

    TEST_RETURN();
}
