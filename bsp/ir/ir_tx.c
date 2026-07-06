// bsp/ir/ir_tx.c
#include "ir/ir_tx.h"
#include "ir_tx_pack.h"
#include "platform/board.h"
#include "platform/diag.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "ir_tx.pio.h"

#define IR_TX_MAX_WORDS 512u
static PIO  s_pio = pio2;                   // shares pio2 with ir_capture
static uint s_sm, s_offset;
static int  s_dma = -1;
static uint32_t s_words[IR_TX_MAX_WORDS];

void ir_tx_init(uint32_t carrier_hz) {
    s_offset = pio_add_program(s_pio, &ir_tx_program);
    s_sm = pio_claim_unused_sm(s_pio, true);
    ir_tx_program_init(s_pio, s_sm, s_offset, PIN_IR_TX);
    ir_tx_set_carrier(carrier_hz);
    s_dma = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(s_dma);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(s_pio, s_sm, true));
    dma_channel_configure(s_dma, &c, &s_pio->txf[s_sm], s_words, 0, false);
    pio_sm_set_enabled(s_pio, s_sm, true);  // idles stalled on `out`, pin low
    DIAG("ir_tx: init pio2 sm=%u dma=%d pin=%d\n", (unsigned)s_sm, s_dma, PIN_IR_TX);
}

void ir_tx_set_carrier(uint32_t carrier_hz) {
    float div = (float)clock_get_hz(clk_sys) / (12.0f * (float)carrier_hz);
    pio_sm_set_clkdiv(s_pio, s_sm, div);
}

bool ir_tx_busy(void) {
    return dma_channel_is_busy(s_dma) || !pio_sm_is_tx_fifo_empty(s_pio, s_sm);
}

bool ir_tx_send(const uint32_t *durs_us, uint32_t n, uint32_t carrier_hz) {
    if (ir_tx_busy()) return false;
    uint32_t nw = ir_tx_pack(durs_us, n, carrier_hz, s_words, IR_TX_MAX_WORDS);
    if (!nw) return false;
    ir_tx_set_carrier(carrier_hz);
    dma_channel_set_read_addr(s_dma, s_words, false);
    dma_channel_set_trans_count(s_dma, nw, true);   // trigger
    return true;
}

void ir_tx_stop(void) {
    dma_channel_abort(s_dma);
    pio_sm_set_enabled(s_pio, s_sm, false);   // halt mid-segment execution
    pio_sm_restart(s_pio, s_sm);              // clear OSR shift-counter residue
    pio_sm_clear_fifos(s_pio, s_sm);
    pio_sm_exec(s_pio, s_sm, pio_encode_set(pio_pins, 0));          // pin low
    pio_sm_exec(s_pio, s_sm, pio_encode_jmp(s_offset));             // PC -> program start
    pio_sm_set_enabled(s_pio, s_sm, true);    // re-enable; stalls on `out` (FIFO empty), pin stays low
}
