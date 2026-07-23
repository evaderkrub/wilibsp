// Host test: loopback robustness — additive white noise at decreasing SNR and
// sample-rate skew (+/-1%, covering the real 16009-vs-16000 Hz split with 16x
// margin). Deterministic xorshift PRNG: reproducible across runs/platforms.
#include "afsk_mod.h"
#include "afsk_demod.h"
#include "frame.h"
#include "test_util.h"
#include <stdlib.h>
#include <string.h>

static uint32_t s_rng = 0x1234ABCDu;
static uint32_t xorshift(void) {
    uint32_t x = s_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return s_rng = x;
}
// Approx-uniform noise in [-amp, +amp].
static int32_t noise(int32_t amp) {
    return (int32_t)(xorshift() % (2u * (uint32_t)amp + 1u)) - amp;
}

static int decode_ok(const int16_t *pcm, unsigned n, const char *text) {
    afsk_demod_t d; frame_parser_t p; frame_msg_t m;
    uint8_t bytes[64];
    afsk_demod_init(&d); frame_parser_init(&p);
    int got = 0;
    for (unsigned off = 0; off < n; off += 512) {
        unsigned chunk = (n - off > 512) ? 512 : (n - off);
        unsigned nb = afsk_demod_process(&d, pcm + off, chunk, bytes, sizeof bytes);
        for (unsigned i = 0; i < nb; i++)
            if (frame_parser_push(&p, bytes[i], &m) == 1) got++;
    }
    return got >= 1 && m.len == strlen(text) && memcmp(m.payload, text, m.len) == 0;
}

// Nearest-neighbour resample by Q16 ratio (simulates TX/RX clock mismatch).
static unsigned resample(const int16_t *in, unsigned n, uint32_t ratio_q16,
                         int16_t *out, unsigned max_out) {
    unsigned k = 0;
    for (uint64_t pos = 0; (pos >> 16) < n && k < max_out; pos += ratio_q16)
        out[k++] = in[pos >> 16];
    return k;
}

int main(void) {
    const char *text = "THE QUICK BROWN FOX 0123456789";
    uint8_t fb[FRAME_MAX_BYTES];
    unsigned nb = frame_build(0x42, FRAME_TYPE_MSG,
                              (const uint8_t *)text, (uint8_t)strlen(text), fb);
    unsigned cap = afsk_mod_max_samples(nb);
    int16_t *clean = malloc(cap * sizeof(int16_t));
    int16_t *work  = malloc((cap + cap / 50 + 8) * sizeof(int16_t));
    unsigned n = afsk_mod_render(fb, nb, clean);

    // SNR sweep. Signal RMS ~ 24000/sqrt(2) ~ 16970 (mark; space slightly less
    // with pre-emphasis). Uniform noise in [-A,A] has RMS A/sqrt(3):
    // A = 16970*sqrt(3)/10^(SNR/20). 20 dB -> A~2939, 10 dB -> A~9294.
    struct { int amp; int must_pass; } lv[] = {
        { 2939,  1 },  // 20 dB SNR: required
        { 9294,  1 },  // 10 dB SNR: required
        { 16528, 0 },  //  5 dB SNR: informational only
    };
    for (unsigned t = 0; t < 3; t++) {
        int pass = 1;
        for (int trial = 0; trial < 5; trial++) {       // 5 noise seeds each
            for (unsigned i = 0; i < n; i++) {
                int32_t v = (int32_t)clean[i] + noise(lv[t].amp);
                work[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
            }
            if (!decode_ok(work, n, text)) { pass = 0; break; }
        }
        if (lv[t].must_pass) ASSERT_TRUE(pass);
        else printf("info: 5dB SNR %s\n", pass ? "decoded" : "not decoded (ok)");
    }

    // Clock skew: +/-1% resample must still decode (clean signal).
    unsigned m1 = resample(clean, n, 66219, work, cap + cap / 50);  // x0.99 fast RX
    ASSERT_TRUE(decode_ok(work, m1, text));
    unsigned m2 = resample(clean, n, 64881, work, cap + cap / 50);  // x1.01 slow RX
    ASSERT_TRUE(decode_ok(work, m2, text));

    free(clean); free(work);
    TEST_RETURN();
}
