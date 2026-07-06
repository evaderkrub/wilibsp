#include "test_util.h"
#include "ir_synth.h"
#include "ir_decode.h"
#include "ir_encode.h"

// Round-trip helper: encode msg, decode with `dec`, compare all fields.
static void roundtrip(bool (*dec)(const uint32_t*, uint32_t, ir_message_t*),
                      ir_message_t in) {
    uint32_t t[IR_MAX_TIMINGS];
    ir_message_t m;
    uint32_t n = ir_encode(&in, t, IR_MAX_TIMINGS);
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(dec(t, n, &m));
    ASSERT_EQ(m.protocol, in.protocol);
    ASSERT_EQ(m.address, in.address);
    ASSERT_EQ(m.command, in.command);
    jitter(t, n);
    ASSERT_TRUE(dec(t, n, &m));           // survives 8% jitter
    ASSERT_EQ(m.command, in.command);
}

int main(void) {
    uint32_t t[IR_MAX_TIMINGS];
    ir_message_t m;

    roundtrip(ir_decode_samsung32, (ir_message_t){IR_PROTO_SAMSUNG32, 0x07, 0x02, false});
    roundtrip(ir_decode_sirc,      (ir_message_t){IR_PROTO_SIRC,   0x01,   0x15, false});
    roundtrip(ir_decode_sirc,      (ir_message_t){IR_PROTO_SIRC15, 0x97,   0x15, false});
    roundtrip(ir_decode_sirc,      (ir_message_t){IR_PROTO_SIRC20, 0x1A37, 0x15, false});
    roundtrip(ir_decode_rca,       (ir_message_t){IR_PROTO_RCA,    0x0A,   0x55, false});

    // Samsung32 with a broken command complement must NOT decode.
    ir_message_t bad = {IR_PROTO_SAMSUNG32, 0x07, 0x02, false};
    uint32_t n = ir_encode(&bad, t, IR_MAX_TIMINGS);
    t[2 + 2*31 + 1] = t[2 + 2*31 + 1] > 1000 ? 550 : 1650;  // flip last data bit's space
    ASSERT_TRUE(!ir_decode_samsung32(t, n, &m));

    // SIRC variant is chosen by exact edge count: a 12-bit frame is SIRC, not SIRC15.
    ir_message_t s12 = {IR_PROTO_SIRC, 0x1F, 0x7F, false};
    n = ir_encode(&s12, t, IR_MAX_TIMINGS);
    ASSERT_EQ(n, 25);
    ASSERT_TRUE(ir_decode_sirc(t, n, &m));
    ASSERT_EQ(m.protocol, IR_PROTO_SIRC);

    // NEC frame must not decode as Samsung32 (different header) or RCA.
    n = synth_nec32(0xF708FB04u, t);
    ASSERT_TRUE(!ir_decode_samsung32(t, n, &m));
    ASSERT_TRUE(!ir_decode_rca(t, n, &m));

    TEST_RETURN();
}
