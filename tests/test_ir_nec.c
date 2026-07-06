#include "test_util.h"
#include "ir_synth.h"
#include "ir_decode.h"
#include "ir_encode.h"

int main(void) {
    uint32_t t[IR_MAX_TIMINGS];
    ir_message_t m;

    // NEC addr 0x04 cmd 0x08 -> frame value LSB-first: 04 FB 08 F7
    uint32_t n = synth_nec32(0xF708FB04u, t);
    ASSERT_EQ(n, 67);
    ASSERT_TRUE(ir_decode_nec(t, n, &m));
    ASSERT_EQ(m.protocol, IR_PROTO_NEC);
    ASSERT_EQ(m.address, 0x04);
    ASSERT_EQ(m.command, 0x08);
    ASSERT_TRUE(!m.repeat);

    // Same frame with 8% jitter still decodes.
    n = synth_nec32(0xF708FB04u, t); jitter(t, n);
    ASSERT_TRUE(ir_decode_nec(t, n, &m));
    ASSERT_EQ(m.command, 0x08);

    // Broken complements -> NECext (16-bit addr, 16-bit cmd), never NEC.
    n = synth_nec32(0x00563412u, t);
    ASSERT_TRUE(ir_decode_nec(t, n, &m));
    ASSERT_EQ(m.protocol, IR_PROTO_NECEXT);
    ASSERT_EQ(m.address, 0x3412);
    ASSERT_EQ(m.command, 0x0056);

    // NEC repeat frame: 9000 / 2250 / 560.
    uint32_t r[3] = {9000, 2250, 560};
    ASSERT_TRUE(ir_decode_nec(r, 3, &m));
    ASSERT_TRUE(m.repeat);

    // Garbage does not decode.
    uint32_t g[5] = {100, 100, 100, 100, 100};
    ASSERT_TRUE(!ir_decode_nec(g, 5, &m));

    // Encode -> decode round-trip, NEC and NECext.
    ir_message_t in = {IR_PROTO_NEC, 0x5A, 0xC3, false};
    n = ir_encode(&in, t, IR_MAX_TIMINGS);
    ASSERT_EQ(n, 67);
    ASSERT_TRUE(ir_decode_nec(t, n, &m));
    ASSERT_EQ(m.protocol, IR_PROTO_NEC);
    ASSERT_EQ(m.address, 0x5A); ASSERT_EQ(m.command, 0xC3);

    ir_message_t inx = {IR_PROTO_NECEXT, 0xBEEF, 0x1234, false};
    n = ir_encode(&inx, t, IR_MAX_TIMINGS);
    ASSERT_TRUE(ir_decode_nec(t, n, &m));
    ASSERT_EQ(m.protocol, IR_PROTO_NECEXT);
    ASSERT_EQ(m.address, 0xBEEF); ASSERT_EQ(m.command, 0x1234);

    // Repeat-frame encode round-trip.
    ir_message_t rin = {IR_PROTO_NEC, 0, 0, true};
    n = ir_encode(&rin, t, IR_MAX_TIMINGS);
    ASSERT_EQ(n, 3);
    ASSERT_TRUE(ir_decode_nec(t, n, &m));
    ASSERT_TRUE(m.repeat);

    TEST_RETURN();
}
