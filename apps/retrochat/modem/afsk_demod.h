// modem/afsk_demod.h — Bell 202 software demodulator, pure integer.
// Chain: DC block -> delayed-product discriminator (delay 7 @ 16 kHz) -> LPF ->
// sign decision -> Q16 bit PLL -> UART deframer. Amplitude-independent: the
// decision is the SIGN of the averaged product, and the PLL locks on decision
// transitions, so no AGC stage is needed.
#ifndef RC_AFSK_DEMOD_H
#define RC_AFSK_DEMOD_H
#include <stdint.h>

#define AFSK_DEMOD_DELAY 7

typedef struct afsk_demod {
    int16_t dl[AFSK_DEMOD_DELAY];  // discriminator delay line
    unsigned dli;
    int32_t hp_y;                  // DC-block state
    int16_t hp_x1;
    int32_t lpf;                   // discriminator LPF state
    uint32_t pll;                  // Q16 bit phase (bit boundary at 0)
    int last_dec;
    int bit_idx;                   // -1 = hunting start bit, else 0..8
    unsigned shift;
    int16_t peak;                  // decaying |signal| peak (UI level meter)
} afsk_demod_t;

void afsk_demod_init(afsk_demod_t *d);

// Feed PCM samples; decoded bytes are appended to bytes_out (up to max_out).
// Returns the number of bytes written. State persists across calls: feed a
// continuous stream in any chunk sizes.
unsigned afsk_demod_process(afsk_demod_t *d, const int16_t *pcm, unsigned n,
                            uint8_t *bytes_out, unsigned max_out);

#endif
