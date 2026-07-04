// src/radio/gdo_capture.c
#include "radio/gdo_capture.h"
#include "platform/board.h"
#include "platform/diag.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "gdo_capture.pio.h"

#define GDO_RING_LEN 4096u                 // power of two for DMA ring wrap
_Static_assert((1u << 14) == GDO_RING_LEN * sizeof(uint32_t),
    "DMA ring size_bits (14 in channel_config_set_ring) must match GDO_RING_LEN*sizeof(uint32_t)");
static uint32_t s_ring[GDO_RING_LEN] __attribute__((aligned(GDO_RING_LEN * sizeof(uint32_t))));
static PIO   s_pio = pio2;   // ADAPTATION: subghz used pio0; wilibsp reserves pio0=audio, pio1=LEDs
static uint  s_sm, s_offset;
static int   s_dma = -1;
static uint32_t s_tail;                    // next unread index into s_ring

void gdo_capture_init(void) {
    // Reach GPIO32: PIO GPIO base window 16..47 (needs PICO_PIO_USE_GPIO_BASE=1).
    pio_set_gpio_base(s_pio, 16);
    s_offset = pio_add_program(s_pio, &gdo_capture_program);
    s_sm = pio_claim_unused_sm(s_pio, true);
    gdo_capture_program_init(s_pio, s_sm, s_offset, PIN_CC1101_GDO0);

    s_dma = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(s_dma);
    channel_config_set_read_increment(&c, false);          // read the PIO FIFO
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, pio_get_dreq(s_pio, s_sm, false));  // RX dreq
    // Ring on the write address: size_bits counts BYTES, not words.
    // GDO_RING_LEN words * 4 B = 16384 B = 2^14, so wrap every 16384 bytes = 4096 words.
    channel_config_set_ring(&c, true, 14);   // wrap every 16384 bytes = 4096 words
    dma_channel_configure(s_dma, &c, s_ring, &s_pio->rxf[s_sm], 0xFFFFFFFFu, false);
    DIAG("gdo_capture: init pio2 sm=%u dma=%d\n", (unsigned)s_sm, s_dma);
}

void gdo_capture_start(void) {
    s_tail = 0;
    pio_sm_clear_fifos(s_pio, s_sm);
    pio_sm_restart(s_pio, s_sm);
    dma_channel_set_write_addr(s_dma, s_ring, false);
    // 0xFFFFFFFF trans_count = RP2350 MODE=0xF (ENDLESS): transfer forever into the
    // ring. Progress is read from write_addr (see dma_head), not transfer_count,
    // which does not decrement in ENDLESS mode.
    dma_channel_set_trans_count(s_dma, 0xFFFFFFFFu, false);
    dma_channel_start(s_dma);
    pio_sm_set_enabled(s_pio, s_sm, true);
}

void gdo_capture_stop(void) {
    pio_sm_set_enabled(s_pio, s_sm, false);
    dma_channel_abort(s_dma);
}

// Re-assert GDO0 (GPIO32) as a PIO input after OOK TX temporarily drove it as an
// SIO output. Mirrors the pin setup in gdo_capture_program_init; call before
// gdo_capture_start() to resume capture.
void gdo_capture_attach_pin(void) {
    pio_gpio_init(s_pio, PIN_CC1101_GDO0);
    pio_sm_set_consecutive_pindirs(s_pio, s_sm, PIN_CC1101_GDO0, 1, false);
}

// Current ring write index, derived from the DMA WRITE_ADDR. We do NOT use
// transfer_count: on RP2350 a 0xFFFFFFFF trans_count selects MODE=0xF (ENDLESS),
// in which transfer_count does not decrement — so write_addr is the only reliable
// progress indicator. write_addr wraps inside the 16384-byte ring, so subtracting
// the aligned base and dividing by the word size gives the head index directly.
static uint32_t dma_head(void) {
    uint32_t off = dma_channel_hw_addr(s_dma)->write_addr - (uint32_t)(uintptr_t)s_ring;
    return (off / sizeof(uint32_t)) & (GDO_RING_LEN - 1u);
}

uint32_t gdo_capture_drain(uint32_t *dst, uint32_t max) {
    uint32_t head = dma_head();
    uint32_t n = 0;
    while (s_tail != head && n < max) {
        dst[n++] = s_ring[s_tail];
        s_tail = (s_tail + 1u) & (GDO_RING_LEN - 1u);
    }
    return n;
}
