#include "audio_glue.h"
#include "fw2.h"
#include "platform/diag.h"
#include "platform/psram.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include <string.h>

#include "modem/afsk_mod.h"
#include "modem/afsk_demod.h"
#include "modem/msgq.h"

#define RX_MIC           MIC_A          // physical order D,B,A,C: A is center-right
#define RC_TX_OUTPUT     CODEC_OUT_SPEAKER  // compile-time output routing knob
#define RX_CHUNK         512u           // 32 ms per pull @ 16 kHz
#define TX_TRAIL_SILENCE 4096u          // 256 ms stop slack before the stream wraps
// PSRAM layout: packed 32-bit I2S TX frames at base, int16 render scratch at +1 MB.
#define TX_BUF     ((uint32_t *)PSRAM_BASE)
#define TX_SCRATCH ((int16_t *)(PSRAM_BASE + (1u << 20)))

static volatile bool s_txing;           // core 0 sets, core 1 reads (half duplex)
static volatile bool s_selftest;
static volatile unsigned s_crc_err;
static volatile unsigned s_hb;          // core-1 loop heartbeat
static volatile unsigned s_qdrop;       // rx queue full, dropped (core-1 must not DIAG)
static volatile int s_peak;
// Bench diagnostics (read from core 0 over RTT): total PCM samples pulled,
// total demod bytes produced, and a max-hold of the demod peak so short TX
// bursts survive the 500 ms stats poll (s_peak itself decays in ~64 ms).
static volatile unsigned s_pcm_total;
static volatile unsigned s_bytes_total;
static volatile int s_peak_max;
static msgq_t s_q;
static absolute_time_t s_tx_end;
static bool s_playing;

static void core1_main(void) {
    static int16_t pcm[PDM_NUM_MICS][RX_CHUNK];
    afsk_demod_t dem;
    frame_parser_t fp;
    frame_msg_t msg;
    uint8_t bytes[64];
    bool was_tx = false;
    afsk_demod_init(&dem);
    frame_parser_init(&fp);
    for (;;) {
        s_hb++;
        int16_t *dst[PDM_NUM_MICS] = { pcm[0], pcm[1], pcm[2], pcm[3] };
        unsigned n = pdm_capture_pull(dst, RX_CHUNK);
        if (!n) { tight_loop_contents(); continue; }
        s_pcm_total += n;
        if (s_txing && !s_selftest) { was_tx = true; continue; }  // drain + discard own TX
        if (was_tx) {                     // fresh DSP state after our own noise
            afsk_demod_init(&dem);
            frame_parser_init(&fp);
            was_tx = false;
        }
        unsigned nb = afsk_demod_process(&dem, pcm[RX_MIC], n, bytes, sizeof bytes);
        s_peak = dem.peak;
        s_bytes_total += nb;
        if (dem.peak > s_peak_max) s_peak_max = dem.peak;
        for (unsigned i = 0; i < nb; i++) {
            int r = frame_parser_push(&fp, bytes[i], &msg);
            if (r == 1) {
                if (!msgq_push(&s_q, &msg)) s_qdrop++;
            } else if (r == -1) {
                s_crc_err++;
            }
        }
    }
}

bool audio_glue_init(void) {
    msgq_init(&s_q);
    codec_nau88c10_init();
    bool codec_ok = codec_nau88c10_input_ok();
    codec_nau88c10_dac_mute(false);
    audio_i2s_duplex_init(AFSK_FS);
    audio_capture_start();   // REQUIRED: drains the duplex RX FIFO. The PIO SM
                             // autopushes ADC bits; a full RX FIFO stalls the SM
                             // and kills TX. We never read these blocks.
    codec_nau88c10_speaker_low_power();   // silent until first TX
    pdm_capture_init();
    multicore_launch_core1(core1_main);
    DIAG("rc: audio glue up (mic=%d chunk=%u)\n", RX_MIC, RX_CHUNK);
    return codec_ok;
}

void audio_tx_text(uint8_t sender, const char *text) {
    static uint8_t fbytes[FRAME_MAX_BYTES];
    unsigned tlen = strlen(text);
    if (tlen > FRAME_MAX_PAYLOAD) tlen = FRAME_MAX_PAYLOAD;
    unsigned nb = frame_build(sender, FRAME_TYPE_MSG,
                              (const uint8_t *)text, (uint8_t)tlen, fbytes);
    if (!nb) return;
    unsigned ns = afsk_mod_render(fbytes, nb, TX_SCRATCH);
    unsigned total  = ns + TX_TRAIL_SILENCE;
    unsigned padded = (total + 8191u) & ~8191u;   // stream API: multiple of 8192
    for (unsigned i = 0; i < ns; i++) {
        uint16_t s = (uint16_t)TX_SCRATCH[i];
        TX_BUF[i] = ((uint32_t)s << 16) | s;      // same sample on L and R slots
    }
    memset(&TX_BUF[ns], 0, (padded - ns) * sizeof(uint32_t));
    if (!s_selftest) s_txing = true;
    codec_nau88c10_dac_mute(false);
    codec_nau88c10_set_output(RC_TX_OUTPUT);
    audio_i2s_duplex_play_stream_loop(TX_BUF, padded);
    // Stop deadline: render length + 150 ms. The I2S pipeline has ~25-30 ms of
    // start latency, so a tight deadline cuts the frame tail (the CRC!) off the
    // air; the buffer carries >=256 ms of trailing silence, so stopping late is
    // safe as long as it beats the stream-loop wrap.
    s_tx_end  = make_timeout_time_us((uint64_t)ns * 1000000ull / AFSK_FS + 150000ull);
    s_playing = true;
    DIAG("rc: tx %u bytes -> %u samples (%u padded)\n", nb, ns, padded);
}

bool audio_tx_busy(void) {
    if (!s_playing) return false;
    if (absolute_time_diff_us(get_absolute_time(), s_tx_end) > 0) return true;
    audio_i2s_duplex_play_stop();
    codec_nau88c10_speaker_low_power();
    s_txing   = false;
    s_playing = false;
    DIAG("rc: tx done\n");
    return false;
}

int audio_rx_pop(frame_msg_t *m) { return msgq_pop(&s_q, m); }
unsigned audio_rx_crc_errors(void) { return s_crc_err; }
unsigned audio_rx_heartbeat(void) { return s_hb; }
unsigned audio_rx_qdrops(void) { return s_qdrop; }
int audio_rx_peak(void) { return s_peak; }
unsigned audio_dbg_pcm_total(void) { return s_pcm_total; }
unsigned audio_dbg_bytes_total(void) { return s_bytes_total; }
int audio_dbg_peak_max(void) { int p = s_peak_max; s_peak_max = 0; return p; }
void audio_set_selftest(bool on) { s_selftest = on; }
bool audio_selftest(void) { return s_selftest; }
