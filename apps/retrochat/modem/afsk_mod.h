// modem/afsk_mod.h — Bell 202 AFSK modulator: 1200 Hz mark / 2200 Hz space,
// 1200 baud, 16 kHz int16 PCM. Phase-continuous DDS (32-bit phase accumulator,
// 256-entry sine LUT). Bit timing uses a Q16.16 accumulator so the non-integer
// 13.333 samples/bit is exact over any frame length.
#ifndef RC_AFSK_MOD_H
#define RC_AFSK_MOD_H
#include <stdint.h>

#define AFSK_FS         16000u
#define AFSK_BAUD       1200u
#define AFSK_MARK_HZ    1200u   // bit 1 / idle carrier
#define AFSK_SPACE_HZ   2200u   // bit 0
#define AFSK_AMPL       12000
#define AFSK_CARRIER_MS 150u

// Worst-case output samples for nbytes (carrier + 10 bits/byte + tail + slack).
unsigned afsk_mod_max_samples(unsigned nbytes);

// Render carrier + UART-framed bytes (start 0, 8 data LSB-first, stop 1) + 4-bit
// mark tail. Returns samples written (<= afsk_mod_max_samples(nbytes)).
unsigned afsk_mod_render(const uint8_t *bytes, unsigned nbytes, int16_t *out);

#endif
