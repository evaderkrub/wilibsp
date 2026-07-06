// tests/test_ir_golden.c — golden timing fixtures captured from real remotes
// (Learn-screen raw dumps over RTT, hardware session 2026-07-06; see
// docs/hardware-notes.md). Closes the Plan 1 follow-up: decoders verified
// against off-board hardware, not just our own encoder. Expected values come
// from the live decode lines RTT printed alongside each dump.
#include "test_util.h"
#include "ir_decode.h"

// Dave's bench remote #1 (extended-NEC device, address 0xC7EA), key -> 0xE619.
static const uint32_t FIX_NECEXT_E619[] = {
    8953, 4454, 561, 559, 562, 1663, 561, 561,
    559, 1665, 560, 561, 559, 1667, 562, 1662,
    562, 1665, 559, 1665, 559, 1667, 562, 1662,
    562, 561, 559, 561, 560, 561, 559, 1665,
    559, 1667, 561, 1663, 562, 559, 561, 561,
    559, 1665, 560, 1665, 559, 563, 557, 563,
    562, 559, 561, 559, 562, 1664, 560, 1665,
    559, 561, 559, 563, 558, 1666, 562, 1663,
    561, 1665, 559,
};

// Same remote, a different key -> 0xFC03.
static const uint32_t FIX_NECEXT_FC03[] = {
    8955, 4456, 559, 560, 561, 1664, 560, 563,
    561, 1664, 560, 561, 560, 1666, 562, 1665,
    559, 1665, 559, 1667, 562, 1664, 560, 1667,
    561, 559, 562, 560, 560, 561, 559, 1667,
    561, 1665, 560, 1666, 562, 1665, 559, 561,
    559, 562, 559, 563, 561, 559, 562, 561,
    559, 561, 560, 561, 559, 563, 562, 1664,
    560, 1665, 559, 1667, 561, 1665, 559, 1665,
    560, 1666, 562,
};

// Bench remote #2 (a second extended-NEC device, address 0xC0E0).
static const uint32_t FIX_NECEXT_C0E0[] = {
    9086, 4327, 561, 559, 561, 559, 562, 561,
    559, 561, 559, 563, 562, 1664, 559, 1667,
    562, 1664, 560, 561, 559, 563, 561, 559,
    562, 561, 559, 561, 559, 563, 557, 1667,
    562, 1664, 560, 1666, 562, 1664, 560, 561,
    559, 1667, 561, 560, 561, 1665, 559, 563,
    562, 1663, 561, 561, 559, 561, 559, 1667,
    562, 561, 559, 1665, 559, 563, 562, 1664,
    560, 561, 559,
};

// Bench remote #3: a 35-edge signal (1 ms header, ~500/1480 us bits) that no
// v1 decoder recognizes. The negative fixture: it must fall through to RAW —
// never mis-decode — so it stays learn/replay-able as raw timings (verified
// live: learned, assigned to a remote button, and replayed on the bench).
static const uint32_t FIX_RAW_35[] = {
    1008, 1472, 507, 1480, 507, 483, 506, 1479,
    508, 1479, 509, 480, 509, 481, 508, 1479,
    508, 1479, 509, 480, 509, 1478, 509, 481,
    508, 481, 509, 1478, 509, 1478, 509, 481,
    508, 481, 509,
};

#define N(a) (sizeof(a) / sizeof((a)[0]))

int main(void) {
    ir_message_t m;

    ASSERT_TRUE(ir_decode(FIX_NECEXT_E619, N(FIX_NECEXT_E619), &m));
    ASSERT_TRUE(m.protocol == IR_PROTO_NECEXT);
    ASSERT_TRUE(m.address == 0xC7EA);
    ASSERT_TRUE(m.command == 0xE619);

    ASSERT_TRUE(ir_decode(FIX_NECEXT_FC03, N(FIX_NECEXT_FC03), &m));
    ASSERT_TRUE(m.protocol == IR_PROTO_NECEXT);
    ASSERT_TRUE(m.address == 0xC7EA);
    ASSERT_TRUE(m.command == 0xFC03);

    ASSERT_TRUE(ir_decode(FIX_NECEXT_C0E0, N(FIX_NECEXT_C0E0), &m));
    ASSERT_TRUE(m.protocol == IR_PROTO_NECEXT);
    ASSERT_TRUE(m.address == 0xC0E0);
    ASSERT_TRUE(m.command == 0x54AB);

    // The unknown-protocol capture must not match ANY strict decoder.
    ASSERT_TRUE(!ir_decode(FIX_RAW_35, N(FIX_RAW_35), &m));

    TEST_RETURN();
}
