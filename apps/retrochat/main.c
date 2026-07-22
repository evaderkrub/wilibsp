// RetroChat — acoustic-modem broadcast chat for the Free-Wili 2.
// TX: Bell 202 AFSK (1200/2200 Hz, 1200 baud) out the speaker via streamed I2S DMA.
// RX: core 1 demodulates the onboard PDM mic stream continuously (broadcast party
// line; no pairing). Long-press the status bar to toggle acoustic self-test
// (device decodes its own speaker). Diagnostics: fw rtt.
#include "fw2.h"
#include "platform/diag.h"
#include "platform/psram.h"
#include "pico/stdlib.h"
#include <string.h>

#include "audio_glue.h"
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
    audio_glue_init();            // codec + duplex + capture drain + PDM + core 1
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
            ui_add_message(m.sender, text, m.sender == proto_self_id());
            DIAG("rc: rx from %02X len=%u\n", m.sender, m.len);
        }

        ui_action_t a = ui_poll();
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
            next_stats = make_timeout_time_ms(500);
        }
        sleep_ms(10);
    }
}
