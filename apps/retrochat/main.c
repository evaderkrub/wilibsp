// RetroChat — acoustic-modem broadcast chat for the Free-Wili 2.
// TX: 300 baud AFSK using Bell 202 tones (1200/2200 Hz) out the speaker via
// streamed I2S DMA.
// RX: core 1 demodulates the onboard PDM mic stream continuously (broadcast party
// line; no pairing). Long-press the status bar to toggle acoustic self-test
// (device decodes its own speaker). Diagnostics: fw rtt.
#include "fw2.h"
#include "platform/diag.h"
#include "platform/psram.h"
#include "pico/stdlib.h"
#include <string.h>

#include "audio_glue.h"
#include "compose.h"
#include "protocol.h"
#include "ui.h"

static void fatal_screen(const char *msg) {
    st7796_fill_screen(0x00F8);   // big-endian red
    st7796_draw_text(8, 8, 3, 0xFFFF, 0x00F8, msg);
    DIAG("rc: FATAL %s\n", msg);
    for (;;) tight_loop_contents();
}

int main(void) {
    board_init();                 // 250 MHz, clk_peri re-source, I2C1, ioexp
    st7796_init();
    board_backlight_set(1);
    DIAG("\n=== retrochat boot ===\n");

    if (psram_init() == 0) fatal_screen("PSRAM FAIL");
    if (!ft6336_init())    fatal_screen("TOUCH FAIL");

    proto_init();
    ui_init(proto_self_id());
    if (!audio_glue_init()) fatal_screen("CODEC FAIL");   // codec + duplex + capture drain + PDM + core 1
    uartkbd_init();
    static compose_t s_compose;      // static: keeps main()'s stack tiny
    compose_init(&s_compose);
    {   // the chord label bar is always visible; draw its initial state
        const char *labels[5];
        compose_labels(&s_compose, labels);
        ui_kb_bar(labels);
    }
    DIAG("rc: up, id=%02X\n", proto_self_id());

    bool last_busy = false;
    unsigned last_err = 0;
    absolute_time_t next_stats = get_absolute_time();

    for (;;) {
        bool busy = audio_tx_busy();
        if (busy != last_busy) {
            ui_set_status(busy, audio_selftest());
            last_busy = busy;
        }

        frame_msg_t m;
        while (audio_rx_pop(&m)) {
            char text[FRAME_MAX_PAYLOAD + 1];
            memcpy(text, m.payload, m.len);
            text[m.len] = '\0';
            // In self-test the acoustic echo must render as a RECEIVED line so
            // the loopback is visible on screen, not just over RTT.
            bool own = (m.sender == proto_self_id()) && !audio_selftest();
            ui_add_message(m.sender, text, own);
            DIAG("rc: rx from %02X len=%u\n", m.sender, m.len);
        }

        // Physical chord keyboard.
        uartkbd_task();
        bool was_composing = compose_active(&s_compose);
        compose_result_t cr = COMPOSE_NONE;
        uartkbd_event_t kev;
        while (uartkbd_next_event(&kev)) {
            compose_result_t r = compose_button(&s_compose, kev.btn, kev.pressed);
            if (r != COMPOSE_NONE) cr = r;   // last meaningful result wins
        }
        // While composing, touch feeds space/backspace instead of the grid.
        if (compose_active(&s_compose)) {
            static bool kb_was_down;
            uint16_t tx, ty;
            bool tdown = ft6336_poll(&tx, &ty);
            if (tdown && !kb_was_down) {
                compose_result_t r = compose_touch(&s_compose, (int)tx, (int)ty);
                if (r != COMPOSE_NONE) cr = r;
            }
            kb_was_down = tdown;
        }
        if (cr == COMPOSE_SEND && !busy) {
            proto_send_text(compose_draft(&s_compose));
            ui_add_message(proto_self_id(), compose_draft(&s_compose), true);
            compose_clear(&s_compose);
            ui_compose_hide();
            const char *labels[5];   // bar stays: refresh reset labels
            compose_labels(&s_compose, labels);
            ui_kb_bar(labels);
            DIAG("rc: kbd send\n");
        } else if (cr == COMPOSE_CANCELLED) {
            ui_compose_hide();
            const char *labels[5];
            compose_labels(&s_compose, labels);
            ui_kb_bar(labels);
        } else if (cr == COMPOSE_CHANGED) {
            const char *labels[5];
            compose_labels(&s_compose, labels);
            ui_compose_show(compose_draft(&s_compose), labels);
        }
        if (!was_composing && compose_active(&s_compose)) ui_poll_reset();

        ui_action_t a = compose_active(&s_compose) ? UI_NONE : ui_poll();
        if (a == UI_SELFTEST_TOGGLE) {
            audio_set_selftest(!audio_selftest());
            ui_set_status(busy, audio_selftest());
            DIAG("rc: selftest=%d\n", (int)audio_selftest());
        } else if (a != UI_NONE && !busy) {
            if (a == UI_RESEND) proto_resend();
            else {
                proto_send_canned((int)a);
                ui_add_message(proto_self_id(), proto_canned[(int)a], true);
            }
        }

        if (absolute_time_diff_us(get_absolute_time(), next_stats) <= 0) {
            unsigned err = audio_rx_crc_errors();
            ui_set_stats(err, audio_rx_peak());
            if (err != last_err) DIAG("rc: crc errors=%u\n", err);
            last_err = err;
            DIAG("rc: hb=%u qdrop=%u pcm=%u bytes=%u pkmax=%d kf=%u ke=%u\n",
                 audio_rx_heartbeat(), audio_rx_qdrops(),
                 audio_dbg_pcm_total(), audio_dbg_bytes_total(),
                 audio_dbg_peak_max(), uartkbd_frames(), uartkbd_errors());
            next_stats = make_timeout_time_ms(500);
        }
        sleep_ms(10);
    }
}
