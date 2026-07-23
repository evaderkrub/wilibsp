// modem/afsk_mod.h — Bell 202 AFSK modulator: 1200 Hz mark / 2200 Hz space,
// 1200 baud, 16 kHz int16 PCM. Phase-continuous DDS (32-bit phase accumulator,
// 256-entry sine LUT). Bit timing uses a Q16.16 accumulator so the non-integer
// 13.333 samples/bit is exact over any frame length.
#ifndef RC_AFSK_MOD_H
#define RC_AFSK_MOD_H
#include <stdint.h>

#define AFSK_FS         16000u
// 300 baud, not Bell 202's 1200: on-device mic captures showed the acoustic
// channel (micro-speaker ring-down + board coupling) smears each tone
// transition over ~25 samples (~1.5 ms). At 1200 baud that swallows whole
// bits; at 600 baud it still cost ~1 bit error per frame after long runs.
// At 300 baud a bit is 53 samples, so the discriminator + mid-bit majority
// vote sample well-settled tone. On-air tones and framing are unchanged.
#define AFSK_BAUD       300u
#define AFSK_MARK_HZ    1200u   // bit 1 / idle carrier
#define AFSK_SPACE_HZ   2200u   // bit 0
// Pre-emphasis: the FW2 micro-speaker reproduces 2200 Hz ~23% louder than
// 1200 Hz (measured from an on-device mic capture), which biases the RX
// discriminator toward space. Render mark at full amplitude and space at
// 13/16 of it so the tones arrive balanced at the mic.
#define AFSK_AMPL       24000   // mark / carrier amplitude
#define AFSK_SPACE_NUM  13      // space amplitude = AFSK_AMPL * 13 / 16
#define AFSK_CARRIER_MS 150u

// Worst-case output samples for nbytes (carrier + 10 bits/byte + tail + slack).
unsigned afsk_mod_max_samples(unsigned nbytes);

// Render carrier + UART-framed bytes (start 0, 8 data LSB-first, stop 1) + 4-bit
// mark tail. Returns samples written (<= afsk_mod_max_samples(nbytes)).
unsigned afsk_mod_render(const uint8_t *bytes, unsigned nbytes, int16_t *out);

#endif
