#include "test_util.h"
#include "ir_synth.h"
#include "ir_decode.h"
#include "ir_encode.h"

int main(void) {
    uint32_t t[IR_MAX_TIMINGS];
    ir_message_t m;

    // RC5 round-trip (cmd < 64 so S2 field bit is 1 / plain RC5).
    ir_message_t r5 = {IR_PROTO_RC5, 0x05, 0x35, false};
    uint32_t n = ir_encode(&r5, t, IR_MAX_TIMINGS);
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(ir_decode_rc5(t, n, &m));
    ASSERT_EQ(m.protocol, IR_PROTO_RC5);
    ASSERT_EQ(m.address, 0x05); ASSERT_EQ(m.command, 0x35);

    // RC5X: command bit 6 set travels via inverted S2.
    ir_message_t r5x = {IR_PROTO_RC5, 0x10, 0x7A, false};
    n = ir_encode(&r5x, t, IR_MAX_TIMINGS);
    ASSERT_TRUE(ir_decode_rc5(t, n, &m));
    ASSERT_EQ(m.command, 0x7A);

    // RC5 with jitter.
    n = ir_encode(&r5, t, IR_MAX_TIMINGS); jitter(t, n);
    ASSERT_TRUE(ir_decode_rc5(t, n, &m));
    ASSERT_EQ(m.command, 0x35);

    // RC6 round-trip.
    ir_message_t r6 = {IR_PROTO_RC6, 0x07, 0x0D, false};
    n = ir_encode(&r6, t, IR_MAX_TIMINGS);
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(ir_decode_rc6(t, n, &m));
    ASSERT_EQ(m.protocol, IR_PROTO_RC6);
    ASSERT_EQ(m.address, 0x07); ASSERT_EQ(m.command, 0x0D);

    // RC6 with jitter.
    n = ir_encode(&r6, t, IR_MAX_TIMINGS); jitter(t, n);
    ASSERT_TRUE(ir_decode_rc6(t, n, &m));
    ASSERT_EQ(m.command, 0x0D);

    // Cross-rejection: an RC6 frame is not RC5, an NEC frame is neither.
    n = ir_encode(&r6, t, IR_MAX_TIMINGS);
    ASSERT_TRUE(!ir_decode_rc5(t, n, &m));
    n = synth_nec32(0xF708FB04u, t);
    ASSERT_TRUE(!ir_decode_rc5(t, n, &m));
    ASSERT_TRUE(!ir_decode_rc6(t, n, &m));

    TEST_RETURN();
}
