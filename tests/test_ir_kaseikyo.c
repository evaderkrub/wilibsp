#include "test_util.h"
#include "ir_synth.h"
#include "ir_decode.h"
#include "ir_encode.h"

int main(void) {
    uint32_t t[IR_MAX_TIMINGS];
    ir_message_t m;

    // Panasonic vendor 0x2002 in the low 16 bits of the address field.
    ir_message_t in = {IR_PROTO_KASEIKYO, 0x2002, 0x30D, false};
    uint32_t n = ir_encode(&in, t, IR_MAX_TIMINGS);
    ASSERT_EQ(n, 99);
    ASSERT_TRUE(ir_decode_kaseikyo(t, n, &m));
    ASSERT_EQ(m.protocol, IR_PROTO_KASEIKYO);
    ASSERT_EQ(m.address, 0x2002);
    ASSERT_EQ(m.command, 0x30D);

    jitter(t, n);
    ASSERT_TRUE(ir_decode_kaseikyo(t, n, &m));
    ASSERT_EQ(m.command, 0x30D);

    // NEC must not decode as Kaseikyo (wrong header ratio + bit count).
    n = synth_nec32(0xF708FB04u, t);
    ASSERT_TRUE(!ir_decode_kaseikyo(t, n, &m));

    TEST_RETURN();
}
