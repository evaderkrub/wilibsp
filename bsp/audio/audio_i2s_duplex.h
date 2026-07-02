// src/audio/audio_i2s_duplex.h — full-duplex I2S on PIO0 SM0 (DAC out GPIO5, ADC in GPIO4).
#ifndef AUDIO_I2S_DUPLEX_H
#define AUDIO_I2S_DUPLEX_H
#include <stdint.h>
#include "hardware/pio.h"

// Start MCLK (256*fs PWM on GPIO22), load the duplex program on PIO0 SM0, set
// pindirs (GPIO5/6/7 out, GPIO4 in) + clkdiv, enable the SM. RX runs immediately;
// the TX FIFO outputs 0 (DAC mid-scale silence) until play_loop() is armed.
void audio_i2s_duplex_init(uint32_t sample_rate);

// RX FIFO plumbing for vu_capture (same shape as the old RX driver).
volatile const void *audio_i2s_duplex_rxf(void);
uint audio_i2s_duplex_rx_dreq(void);

// Arm a zero-CPU DMA read-ring that loops `buf` (frames, each uint32 = [L16|R16])
// to the TX FIFO forever. `buf` MUST be aligned to its byte size and the byte size
// MUST be a power of two (ring requirement). For 64 frames -> 256 bytes, aligned(256).
void audio_i2s_duplex_play_loop(const uint32_t *buf, uint frames);

// Stop playback: abort the TX DMA and clear the TX FIFO (DAC mid-scale silence).
void audio_i2s_duplex_play_stop(void);

#endif
