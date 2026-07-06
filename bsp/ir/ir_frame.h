// bsp/ir/ir_frame.h — split a raw mark/space duration stream into IR frames.
// Pure logic (no Pico SDK): the capture driver feeds durations one at a time;
// a space >= IR_GAP_US (or PIO counter saturation) terminates the frame.
#ifndef IR_FRAME_H
#define IR_FRAME_H
#include "ir_types.h"

#define IR_NOISE_US 100u          // marks shorter than this are glitches
#define IR_GAP_US   10000u        // a space this long ends a frame
#define IR_SAT_US   0x40000000u   // capture counter saturation = idle line

typedef struct {
    uint32_t durs[IR_MAX_TIMINGS];
    uint32_t count;
    bool overflow;                // frame was longer than IR_MAX_TIMINGS
} ir_frame_t;

typedef struct {
    ir_frame_t cur;
    bool in_frame;
} ir_frame_builder_t;

void ir_frame_builder_init(ir_frame_builder_t *fb);
// Feed one duration. is_mark = the line was active (TSOP low) for dur_us.
// Returns true when a completed frame was copied into *out.
bool ir_frame_feed(ir_frame_builder_t *fb, uint32_t dur_us, bool is_mark,
                   ir_frame_t *out);
#endif
