#include "afsk_demod.h"
#include <string.h>

#define PLL_INC ((uint32_t)(((uint64_t)1200u << 16) / 16000u))  // 4915

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
        int16_t a = (int16_t)(s < 0 ? -s : s);
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
        // sample the decision once per bit at mid-phase.
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
        if ((prev & 0xFFFFu) < 0x8000u && (d->pll & 0xFFFFu) >= 0x8000u) {
            // UART deframer, LSB first.
            if (d->bit_idx < 0) {
                if (dec == 0) { d->bit_idx = 0; d->shift = 0; }  // start bit
            } else if (d->bit_idx < 8) {
                d->shift |= (unsigned)dec << d->bit_idx;
                d->bit_idx++;
            } else {
                if (dec == 1 && cnt < max_out)                   // stop bit valid
                    bytes_out[cnt++] = (uint8_t)d->shift;
                d->bit_idx = -1;                                 // framing err -> resync
            }
        }
    }
    return cnt;
}
