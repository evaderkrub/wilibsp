// Host test: mod -> demod clean loopback recovers the exact air frame,
// and the decision chain is amplitude-independent (decodes at 5% level).
#include "afsk_mod.h"
#include "afsk_demod.h"
#include "frame.h"
#include "test_util.h"
#include <stdlib.h>
#include <string.h>

// Run PCM through demod + frame parser; return 1 iff exactly the expected
// message comes out.
static int decode_expect(const int16_t *pcm, unsigned n, const char *text) {
    afsk_demod_t d;
    frame_parser_t p;
    frame_msg_t m;
    uint8_t bytes[64];
    afsk_demod_init(&d);
    frame_parser_init(&p);
    int got = 0;
    for (unsigned off = 0; off < n; off += 256) {
        unsigned chunk = (n - off > 256) ? 256 : (n - off);
        unsigned nb = afsk_demod_process(&d, pcm + off, chunk, bytes, sizeof bytes);
        for (unsigned i = 0; i < nb; i++)
            if (frame_parser_push(&p, bytes[i], &m) == 1) got++;
    }
    return got == 1 && m.len == strlen(text) &&
           memcmp(m.payload, text, m.len) == 0;
}

int main(void) {
    const char *text = "HELLO RETROCHAT 123";
    uint8_t fb[FRAME_MAX_BYTES];
    unsigned nb = frame_build(0xA7, FRAME_TYPE_MSG,
                              (const uint8_t *)text, (uint8_t)strlen(text), fb);
    ASSERT_TRUE(nb > 0);
    int16_t *pcm = malloc(afsk_mod_max_samples(nb) * sizeof(int16_t));
    unsigned n = afsk_mod_render(fb, nb, pcm);

    ASSERT_TRUE(decode_expect(pcm, n, text));               // clean, full level

    int16_t *quiet = malloc(n * sizeof(int16_t));           // 5% amplitude
    for (unsigned i = 0; i < n; i++) quiet[i] = (int16_t)(pcm[i] / 20);
    ASSERT_TRUE(decode_expect(quiet, n, text));

    // Leading garbage (silence + DC offset) then the frame: still decodes.
    unsigned pre = 4000;
    int16_t *shifted = malloc((n + pre) * sizeof(int16_t));
    for (unsigned i = 0; i < pre; i++) shifted[i] = 900;    // DC step
    memcpy(shifted + pre, pcm, n * sizeof(int16_t));
    ASSERT_TRUE(decode_expect(shifted, n + pre, text));

    free(pcm); free(quiet); free(shifted);
    TEST_RETURN();
}
