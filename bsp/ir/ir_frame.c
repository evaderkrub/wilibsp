// bsp/ir/ir_frame.c
#include "ir_frame.h"

void ir_frame_builder_init(ir_frame_builder_t *fb) {
    fb->cur.count = 0;
    fb->cur.overflow = false;
    fb->in_frame = false;
}

static void push(ir_frame_builder_t *fb, uint32_t dur) {
    if (fb->cur.count < IR_MAX_TIMINGS) fb->cur.durs[fb->cur.count++] = dur;
    else fb->cur.overflow = true;
}

bool ir_frame_feed(ir_frame_builder_t *fb, uint32_t dur_us, bool is_mark,
                   ir_frame_t *out) {
    if (!fb->in_frame) {
        // Waiting for a frame to start: a plausible mark opens it.
        if (is_mark && dur_us >= IR_NOISE_US && dur_us < IR_SAT_US) {
            fb->cur.count = 0;
            fb->cur.overflow = false;
            push(fb, dur_us);
            fb->in_frame = true;
        }
        return false;
    }
    if (is_mark) {
        push(fb, dur_us);
        return false;
    }
    // Space: gap or saturation terminates the frame (trailing space dropped).
    if (dur_us >= IR_GAP_US || dur_us >= IR_SAT_US) {
        fb->in_frame = false;
        if (fb->cur.count < 3) return false;      // too short to be a signal
        *out = fb->cur;
        return true;
    }
    push(fb, dur_us);
    return false;
}
