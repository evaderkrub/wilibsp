// bsp/audio/audio_capture.h — free-running ping-pong DMA from the I2S RX FIFO into
// PCM block buffers. Generalized from the source repo's vu_capture (peak-only) to
// expose the latest completed 32-bit-frame [L16|R16] PCM block to any consumer.
// The VU meter is one such consumer (vu_peak() in vu_meter.h).
#ifndef AUDIO_CAPTURE_H
#define AUDIO_CAPTURE_H
#include <stdint.h>
#include <stdbool.h>

#define AUDIO_CAPTURE_BLOCK_FRAMES 256   // ~16 ms at 16 kHz
#define AUDIO_MIC_I2S_SLOT         1     // right slot: the mono NAU88C10 ADC
                                         // streams here (left slot reads silent 0)

// audio_i2s_duplex_init() must already be running (RX FIFO live). Claims two DMA
// channels chained ping-pong into two block buffers and installs a SHARED handler
// on DMA_IRQ_0 (the ST7796 flush also shares this line — see board.h invariant).
void audio_capture_start(void);

// True once a new block has completed since the last audio_capture_block().
bool audio_capture_block_ready(void);

// Pointer to the most-recently-completed block: AUDIO_CAPTURE_BLOCK_FRAMES frames
// of uint32 [L16|R16]. Clears the ready flag. Returns NULL if no block is ready.
const uint32_t *audio_capture_block(void);

#endif // AUDIO_CAPTURE_H
