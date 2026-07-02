// src/audio/audio_i2s_duplex.c — full-duplex I2S: PIO0 SM0 clocks the codec and
// both shifts the DAC out (GPIO5) and the ADC in (GPIO4). Tone playback is a
// zero-CPU DMA read-ring over a pre-filled buffer (two channels chained; the ring
// wraps the read address so no IRQ/re-arm is needed and the loop is seamless).
#include "audio/audio_i2s_duplex.h"
#include "platform/board.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/dma.h"
#include "i2s_duplex.pio.h"

#define DPX_PIO pio0
#define DPX_SM  0

static int s_tx_dma[2] = { -1, -1 };

void audio_i2s_duplex_init(uint32_t sample_rate) {
    // MCLK = 256*fs, 50% duty (codec MCLK-direct).
    gpio_set_function(PIN_AUDIO_MCLK, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(PIN_AUDIO_MCLK);
    uint32_t ticks = clock_get_hz(clk_sys) / (256u * sample_rate);
    pwm_set_wrap(slice, ticks - 1);
    pwm_set_gpio_level(PIN_AUDIO_MCLK, ticks / 2);
    pwm_set_enabled(slice, true);

    uint offset = pio_add_program(DPX_PIO, &i2s_duplex_program);
    pio_sm_config c = i2s_duplex_program_get_default_config(offset);

    sm_config_set_out_pins(&c, PIN_AUDIO_DATA, 1);        // GPIO5 = SPK_DIN (DAC)
    sm_config_set_in_pins(&c, PIN_AUDIO_DIN);             // GPIO4 = SPK_DOUT (ADC)
    sm_config_set_sideset_pins(&c, PIN_AUDIO_LRCK);       // bit0=LRCK(6), bit1=BCLK(7)
    sm_config_set_out_shift(&c, false, true, 32);         // MSB first, autopull 32
    sm_config_set_in_shift(&c, false, true, 32);          // MSB first, autopush 32

    pio_gpio_init(DPX_PIO, PIN_AUDIO_DATA);
    pio_gpio_init(DPX_PIO, PIN_AUDIO_LRCK);
    pio_gpio_init(DPX_PIO, PIN_AUDIO_BCLK);
    gpio_set_function(PIN_AUDIO_DIN, GPIO_FUNC_PIO0);     // ADC input pin

    pio_sm_init(DPX_PIO, DPX_SM, offset, &c);
    uint out_mask = (1u << PIN_AUDIO_DATA) | (3u << PIN_AUDIO_LRCK);  // 5,6,7 out
    pio_sm_set_pindirs_with_mask(DPX_PIO, DPX_SM, out_mask,
                                 out_mask | (1u << PIN_AUDIO_DIN));

    // 3 PIO instr/bit -> PIO clock = 96*fs. Derive clkdiv directly from clk_sys.
    float div = (float)clock_get_hz(clk_sys) / (96.0f * (float)sample_rate);
    pio_sm_set_clkdiv(DPX_PIO, DPX_SM, div);
    pio_sm_set_enabled(DPX_PIO, DPX_SM, true);
}

volatile const void *audio_i2s_duplex_rxf(void) {
    return (volatile const void *)&DPX_PIO->rxf[DPX_SM];
}
uint audio_i2s_duplex_rx_dreq(void) {
    return pio_get_dreq(DPX_PIO, DPX_SM, false);   // false = RX DREQ
}

void audio_i2s_duplex_play_loop(const uint32_t *buf, uint frames) {
    if (s_tx_dma[0] < 0) {
        s_tx_dma[0] = dma_claim_unused_channel(true);
        s_tx_dma[1] = dma_claim_unused_channel(true);
    }
    uint ring_bits = 0, bytes = frames * 4;
    while ((1u << ring_bits) < bytes) ring_bits++;   // log2(bytes); buf is aligned(bytes)
    uint tx_dreq = pio_get_dreq(DPX_PIO, DPX_SM, true);   // true = TX DREQ
    for (int i = 0; i < 2; i++) {
        dma_channel_config c = dma_channel_get_default_config(s_tx_dma[i]);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);
        channel_config_set_dreq(&c, tx_dreq);
        channel_config_set_ring(&c, false, ring_bits);   // wrap READ addr at 2^ring_bits
        channel_config_set_chain_to(&c, s_tx_dma[i ^ 1]);
        dma_channel_configure(s_tx_dma[i], &c, &DPX_PIO->txf[DPX_SM], buf, frames, false);
    }
    dma_channel_start(s_tx_dma[0]);
}

void audio_i2s_duplex_play_stop(void) {
    for (int i = 0; i < 2; i++)
        if (s_tx_dma[i] >= 0) dma_channel_abort(s_tx_dma[i]);
    // Clear the TX FIFO so the DAC sits at mid-scale (silence) during the baseline.
    pio_sm_clear_fifos(DPX_PIO, DPX_SM);
}
