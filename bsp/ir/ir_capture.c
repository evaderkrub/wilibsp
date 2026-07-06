// bsp/ir/ir_capture.c — gdo_capture (subghz/wilibsp) adapted for the IR pin.
#include "ir/ir_capture.h"
#include "platform/board.h"
#include "platform/diag.h"
#include "platform/ioexp.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/timer.h"
#include "ir_capture.pio.h"

#define IR_RING_LEN 4096u
_Static_assert((1u << 14) == IR_RING_LEN * sizeof(uint32_t),
    "DMA ring size_bits (14) must match IR_RING_LEN*sizeof(uint32_t)");
static uint32_t s_ring[IR_RING_LEN] __attribute__((aligned(IR_RING_LEN * sizeof(uint32_t))));
static PIO  s_pio = pio2;              // pio2 = IR (pio0 audio, pio1 LEDs)
static uint s_sm, s_offset;
static int  s_dma = -1;
static uint32_t s_tail;
static bool s_next_is_low;             // PIO pushes low-duration first
static ir_frame_builder_t s_fb;
static uint64_t s_last_edge_us;
static uint32_t s_overruns;      // see ir_capture_overruns()

void ir_capture_init(void) {
    // Power the IR rail (feeds the IR receiver, and possibly the TX LED
    // driver) — gated behind the PCAL6524 (P2_0), off at power-on. Owned
    // here the way pdm_capture_init owns MIC_PWR.
    ioexp_ir_pwr(true);
    s_offset = pio_add_program(s_pio, &ir_capture_program);
    s_sm = pio_claim_unused_sm(s_pio, true);
    ir_capture_program_init(s_pio, s_sm, s_offset, PIN_IR_RX);

    s_dma = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(s_dma);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, pio_get_dreq(s_pio, s_sm, false));
    channel_config_set_ring(&c, true, 14);   // wrap every 16384 B = 4096 words
    dma_channel_configure(s_dma, &c, s_ring, &s_pio->rxf[s_sm], 0xFFFFFFFFu, false);
    DIAG("ir_capture: init pio2 sm=%u dma=%d pin=%d\n", (unsigned)s_sm, s_dma, PIN_IR_RX);
}

void ir_capture_start(void) {
    s_tail = 0;
    s_next_is_low = true;
    ir_frame_builder_init(&s_fb);
    s_last_edge_us = time_us_64();
    pio_sm_clear_fifos(s_pio, s_sm);
    pio_sm_restart(s_pio, s_sm);
    // Restart does NOT reset the PC or scratch X: after a stop the SM sits
    // mid-loop (usually high_loop, counting idle), so re-enabling it would
    // emit a stale HIGH duration first and shift every mark/space label by
    // one (seen on hardware: bogus ~22 ms leading "mark" on every capture
    // after the first). Jump to the program start so measurement begins
    // fresh in the low-first order s_next_is_low assumes.
    pio_sm_exec(s_pio, s_sm, pio_encode_jmp(s_offset));
    dma_channel_set_write_addr(s_dma, s_ring, false);
    // 0xFFFFFFFF trans_count = RP2350 ENDLESS mode; progress comes from
    // write_addr (transfer_count does not decrement in this mode).
    dma_channel_set_trans_count(s_dma, 0xFFFFFFFFu, false);
    dma_channel_start(s_dma);
    pio_sm_set_enabled(s_pio, s_sm, true);
}

void ir_capture_stop(void) {
    pio_sm_set_enabled(s_pio, s_sm, false);
    dma_channel_abort(s_dma);
}

static uint32_t dma_head(void) {
    uint32_t off = dma_channel_hw_addr(s_dma)->write_addr - (uint32_t)(uintptr_t)s_ring;
    return (off / sizeof(uint32_t)) & (IR_RING_LEN - 1u);
}

bool ir_capture_poll(ir_frame_t *out) {
    uint32_t head = dma_head();
    uint32_t backlog = (head - s_tail) & (IR_RING_LEN - 1u);
    if (backlog >= IR_RING_LEN / 2u) {
        // Starved: cannot rule out a DMA lap, so parity is untrustworthy.
        s_overruns++;
        DIAG("ir_capture: overrun (backlog %lu), resync\n", (unsigned long)backlog);
        ir_capture_start();              // clean restart: PIO PC, DMA, parity, builder
        return false;
    }
    bool drained = false;
    while (s_tail != head) {
        drained = true;
        uint32_t dur = s_ring[s_tail];
        s_tail = (s_tail + 1u) & (IR_RING_LEN - 1u);
        // TSOP: low = mark. The PIO alternates low/high starting low.
        bool is_mark = s_next_is_low;
        s_next_is_low = !s_next_is_low;
        if (ir_frame_feed(&s_fb, dur, is_mark, out)) {
            s_last_edge_us = time_us_64();
            return true;
        }
    }
    if (drained) {
        s_last_edge_us = time_us_64();
        return false;
    }
    // No new edges. The PIO only pushes a duration when a level ENDS, so after
    // a frame's final mark the terminating space word never arrives until the
    // NEXT transmission starts. Force-close an open frame after 15 ms of idle.
    if (s_fb.in_frame && time_us_64() - s_last_edge_us > 15000u) {
        s_last_edge_us = time_us_64();
        return ir_frame_feed(&s_fb, IR_GAP_US, false, out);
    }
    return false;
}

uint32_t ir_capture_overruns(void) { return s_overruns; }
