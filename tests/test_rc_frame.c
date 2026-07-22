// Host test: air-frame build + byte-at-a-time parse, error paths.
#include "frame.h"
#include "test_util.h"
#include <string.h>

static int feed(frame_parser_t *p, const uint8_t *b, unsigned n, frame_msg_t *out) {
    int last = 0;
    for (unsigned i = 0; i < n; i++) {
        int r = frame_parser_push(p, b[i], out);
        if (r != 0) last = r;
    }
    return last;
}

int main(void) {
    uint8_t buf[FRAME_MAX_BYTES];
    frame_parser_t p;
    frame_msg_t m;

    // Round trip.
    unsigned n = frame_build(0xA7, FRAME_TYPE_MSG, (const uint8_t *)"HI", 2, buf);
    ASSERT_EQ(n, FRAME_TRAINING + 1 + 3 + 2 + 2);
    ASSERT_EQ(buf[0], 0x55);                    // training
    ASSERT_EQ(buf[FRAME_TRAINING], FRAME_SYNC); // sync
    frame_parser_init(&p);
    ASSERT_EQ(feed(&p, buf, n, &m), 1);
    ASSERT_EQ(m.sender, 0xA7);
    ASSERT_EQ(m.type, FRAME_TYPE_MSG);
    ASSERT_EQ(m.len, 2);
    ASSERT_TRUE(memcmp(m.payload, "HI", 2) == 0);

    // Corrupted payload byte -> -1 (CRC), then a following good frame still parses.
    uint8_t bad[FRAME_MAX_BYTES];
    memcpy(bad, buf, n);
    bad[FRAME_TRAINING + 4] ^= 0x01;
    frame_parser_init(&p);
    ASSERT_EQ(feed(&p, bad, n, &m), -1);
    ASSERT_EQ(feed(&p, buf, n, &m), 1);         // parser re-locks

    // Oversized len field -> -1, back to hunt.
    uint8_t junk[5] = { FRAME_SYNC, 0x01, FRAME_TYPE_MSG, 0xFF, 0x00 };
    frame_parser_init(&p);
    ASSERT_EQ(feed(&p, junk, 5, &m), -1);

    // Max-length payload round trip.
    uint8_t big[FRAME_MAX_PAYLOAD];
    for (int i = 0; i < FRAME_MAX_PAYLOAD; i++) big[i] = (uint8_t)i;
    n = frame_build(0x01, FRAME_TYPE_MSG, big, FRAME_MAX_PAYLOAD, buf);
    ASSERT_EQ(n, (unsigned)FRAME_MAX_BYTES);
    frame_parser_init(&p);
    ASSERT_EQ(feed(&p, buf, n, &m), 1);
    ASSERT_EQ(m.len, FRAME_MAX_PAYLOAD);
    ASSERT_TRUE(memcmp(m.payload, big, FRAME_MAX_PAYLOAD) == 0);

    // Zero-length payload rejected at build time.
    ASSERT_EQ(frame_build(0x01, FRAME_TYPE_MSG, big, 0, buf), 0);

    // Wire-level len==0 must be rejected by the parser (build-side guard is
    // tested above; this pins the parser's own range check).
    uint8_t z[5] = { FRAME_SYNC, 0x01, FRAME_TYPE_MSG, 0x00, 0x00 };
    frame_parser_init(&p);
    ASSERT_EQ(feed(&p, z, 5, &m), -1);

    TEST_RETURN();
}
