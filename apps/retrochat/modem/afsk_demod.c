#include "afsk_demod.h"
#include "afsk_mod.h"   // AFSK_BAUD / AFSK_FS: bit clock must match the modulator
#include <string.h>

#define PLL_INC ((uint32_t)(((uint64_t)AFSK_BAUD << 16) / AFSK_FS))

void afsk_demod_init(afsk_demod_t *d) {
    memset(d, 0, sizeof *d);
    d->last_dec = 1;    // idle = mark
    d->bit_idx  = -1;
}

static inline int16_t sat16(int32_t v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

unsigned afsk_demod_process(afsk_demod_t *d, const int16_t *pcm, unsigned n,
                            uint8_t *bytes_out, unsigned max_out) {
    unsigned cnt = 0;
    for (unsigned i = 0; i < n; i++) {
        // One-pole DC block: y = x - x1 + y1*7/8  (fc ~ 320 Hz @ 16 kHz).
        int32_t y = (int32_t)pcm[i] - d->hp_x1 + d->hp_y - (d->hp_y >> 3);
        d->hp_x1 = pcm[i];
        d->hp_y  = y;
        int16_t s = sat16(y);

        // Peak tracker for the UI signal meter (decay ~ 1/1024 per sample).
        int aa = (s < 0) ? -(int)s : (int)s;
        int16_t a = (aa > 32767) ? 32767 : (int16_t)aa;
        if (a > d->peak) d->peak = a;
        else if (d->peak > 0) d->peak -= (int16_t)((d->peak >> 10) + 1);

        // Delayed-product discriminator: sign(mean(x[n]*x[n-7])) flips between
        // 1200 Hz (negative) and 2200 Hz (positive).
        int16_t sd = d->dl[d->dli];
        d->dl[d->dli] = s;
        d->dli = (d->dli + 1u) % AFSK_DEMOD_DELAY;
        int32_t prod = ((int32_t)s * sd) >> 8;
        d->lpf += (prod - d->lpf) >> 3;
        int dec = (d->lpf < 0) ? 1 : 0;   // negative product = mark = 1

        // Bit PLL (second-order, frequency-tracking): nudge phase 1/4 of the way to
        // a boundary on each decision transition (proportional term), and adjust the
        // free-running rate (pll_freq, integral term) so a constant TX/RX clock
        // offset settles to zero steady-state phase error instead of a fixed lag;
        // the bit decision itself is taken once per bit, by majority vote over
        // the middle half of the bit period (below), not a single sample.
        uint32_t prev = d->pll;
        d->pll += (uint32_t)((int32_t)PLL_INC + d->pll_freq);
        if (dec != d->last_dec) {
            int32_t ph = (int32_t)(d->pll & 0xFFFFu);
            if (ph > 0x8000) ph -= 0x10000;
            d->pll -= (uint32_t)(ph >> 2);
            d->pll_freq -= ph >> 8;
            if (d->pll_freq > (int32_t)(PLL_INC / 50))  d->pll_freq = (int32_t)(PLL_INC / 50);
            if (d->pll_freq < -(int32_t)(PLL_INC / 50)) d->pll_freq = -(int32_t)(PLL_INC / 50);
            d->last_dec = dec;
        }
        // Bit decision by majority vote over the middle half of the bit period
        // (phase 0x4000..0xC000) instead of a single mid-bit sample: integrates
        // out both additive noise and the mid-bit discriminator chatter that
        // ringing acoustic channels produce. Neither window edge wraps phase 0,
        // so plain threshold-crossing checks are safe.
        uint32_t ph_prev = prev & 0xFFFFu, ph_now = d->pll & 0xFFFFu;
        if (ph_prev < 0x4000u && ph_now >= 0x4000u) { d->vote = 0; d->vcnt = 0; }
        if (ph_now >= 0x4000u && ph_now < 0xC000u)  { d->vote += dec; d->vcnt++; }
        if (ph_prev < 0xC000u && ph_now >= 0xC000u) {
            int bit = (d->vcnt > 0) && (2 * d->vote >= d->vcnt);
            // UART deframer, LSB first.
            if (d->bit_idx < 0) {
                if (bit == 0) { d->bit_idx = 0; d->shift = 0; }  // start bit
            } else if (d->bit_idx < 8) {
                d->shift |= (unsigned)bit << d->bit_idx;
                d->bit_idx++;
            } else {
                if (bit == 1 && cnt < max_out)                   // stop bit valid
                    bytes_out[cnt++] = (uint8_t)d->shift;
                d->bit_idx = -1;                                 // framing err -> resync
            }
        }
    }
    return cnt;
}
