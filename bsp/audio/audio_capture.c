// bsp/audio/audio_capture.c — two DMA channels ping-pong frames from the I2S RX
// FIFO into two block buffers. The completion IRQ (SHARED on DMA_IRQ_0) flags the
// just-filled buffer; audio_capture_block() hands it to the consumer. Generalized
// from the source repo's vu_capture.c (which exposed only a per-block peak).
#include "audio/audio_capture.h"
#include "audio/audio_i2s_duplex.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include <stddef.h>

static uint32_t s_buf[2][AUDIO_CAPTURE_BLOCK_FRAMES];
static int s_dma[2];
static volatile int s_done = -1;    // index of a freshly filled buffer, or -1
static volatile bool s_ready = false;

static void start_channel(int ch, int other_ch, uint32_t *dst) {
    dma_channel_config c = dma_channel_get_default_config(ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, audio_i2s_duplex_rx_dreq());
    channel_config_set_chain_to(&c, other_ch);   // ping-pong
    dma_channel_configure(ch, &c, dst, audio_i2s_duplex_rxf(),
                          AUDIO_CAPTURE_BLOCK_FRAMES, false);
}

static void dma_irq(void) {
    for (int i = 0; i < 2; i++) {
        if (dma_channel_get_irq0_status(s_dma[i])) {
            dma_channel_acknowledge_irq0(s_dma[i]);
            // Rearm this channel for its next turn (the other is now running).
            dma_channel_set_write_addr(s_dma[i], s_buf[i], false);
            s_done = i;
            s_ready = true;
            break;
        }
    }
}

void audio_capture_start(void) {
    s_dma[0] = dma_claim_unused_channel(true);
    s_dma[1] = dma_claim_unused_channel(true);
    start_channel(s_dma[0], s_dma[1], s_buf[0]);
    start_channel(s_dma[1], s_dma[0], s_buf[1]);

    dma_channel_set_irq0_enabled(s_dma[0], true);
    dma_channel_set_irq0_enabled(s_dma[1], true);
    // SHARED handler: the ST7796 flush already owns a shared handler on DMA_IRQ_0
    // and each handler acts only on its own channel's status (board.h invariant).
    irq_add_shared_handler(DMA_IRQ_0, dma_irq,
                           PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_enabled(DMA_IRQ_0, true);

    dma_channel_start(s_dma[0]);   // channel 1 is chained from channel 0
}

bool audio_capture_block_ready(void) { return s_ready; }

// NOTE: s_done/s_ready form a "latest-wins" handshake, not a per-block queue. If a
// completion IRQ lands between the !s_ready check and the s_done read below, the
// caller receives the newer block and the intervening one is skipped — no torn
// pointer (both are single-word volatile accesses), just intentional freshest-block
// semantics rather than strict every-block accounting.
const uint32_t *audio_capture_block(void) {
    if (!s_ready) return NULL;
    int idx = s_done;
    s_ready = false;
    if (idx < 0) return NULL;
    return s_buf[idx];
}
