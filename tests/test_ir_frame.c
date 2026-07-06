#include "test_util.h"
#include "ir_synth.h"
#include "ir_frame.h"
#include "ir_decode.h"

int main(void) {
    ir_frame_builder_t fb;
    ir_frame_t f;
    uint32_t t[IR_MAX_TIMINGS];
    ir_message_t m;

    // A synthesized NEC frame fed as (mark, space) durations, terminated by a
    // long idle space, comes back out as one frame that decodes.
    uint32_t n = synth_nec32(0xF708FB04u, t);
    ir_frame_builder_init(&fb);
    bool emitted = false;
    for (uint32_t i = 0; i < n; i++)
        emitted |= ir_frame_feed(&fb, t[i], (i & 1u) == 0, &f);
    ASSERT_TRUE(!emitted);                        // no gap seen yet
    ASSERT_TRUE(ir_frame_feed(&fb, 50000, false, &f));   // idle gap ends it
    ASSERT_EQ(f.count, n);
    ASSERT_TRUE(!f.overflow);
    ASSERT_TRUE(ir_decode(f.durs, f.count, &m));
    ASSERT_EQ(m.command, 0x08);

    // Leading idle + noise glitches before the frame are discarded.
    ir_frame_builder_init(&fb);
    ASSERT_TRUE(!ir_frame_feed(&fb, 0, true, &f));       // zero-length mark
    ASSERT_TRUE(!ir_frame_feed(&fb, 2000000, false, &f)); // long idle
    ASSERT_TRUE(!ir_frame_feed(&fb, 40, true, &f));      // sub-noise glitch mark
    ASSERT_TRUE(!ir_frame_feed(&fb, 500000, false, &f)); // more idle
    for (uint32_t i = 0; i < n; i++) ir_frame_feed(&fb, t[i], (i & 1u) == 0, &f);
    ASSERT_TRUE(ir_frame_feed(&fb, IR_SAT_US, false, &f));
    ASSERT_EQ(f.count, n);

    // Two frames in one stream come out as two frames.
    ir_frame_builder_init(&fb);
    uint32_t frames = 0;
    for (int rep = 0; rep < 2; rep++) {
        for (uint32_t i = 0; i < n; i++)
            if (ir_frame_feed(&fb, t[i], (i & 1u) == 0, &f)) frames++;
        if (ir_frame_feed(&fb, 60000, false, &f)) frames++;
    }
    ASSERT_EQ(frames, 2);

    // Overflow: a stream longer than IR_MAX_TIMINGS sets the flag, keeps count capped.
    ir_frame_builder_init(&fb);
    for (uint32_t i = 0; i < IR_MAX_TIMINGS + 40; i++)
        ir_frame_feed(&fb, 500, (i & 1u) == 0, &f);
    ASSERT_TRUE(ir_frame_feed(&fb, 60000, false, &f));
    ASSERT_TRUE(f.overflow);
    ASSERT_EQ(f.count, IR_MAX_TIMINGS);

    TEST_RETURN();
}
