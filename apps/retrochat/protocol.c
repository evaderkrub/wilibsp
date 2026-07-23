#include "protocol.h"
#include "audio_glue.h"
#include "pico/unique_id.h"
#include <string.h>

// Edit freely; font now covers full printable ASCII; keep entries <= 12 chars.
const char *const proto_canned[PROTO_NUM_CANNED] = {
    "HI", "YES", "NO", "LOL", "OMW", "WHERE?", "OK", "BYE",
};

static uint8_t s_id;
static char s_last[FRAME_MAX_PAYLOAD + 1];

void proto_init(void) {
    pico_unique_board_id_t bid;
    pico_get_unique_board_id(&bid);
    s_id = bid.id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 1];
    s_last[0] = '\0';
}

uint8_t proto_self_id(void) { return s_id; }

void proto_send_text(const char *text) {
    strncpy(s_last, text, FRAME_MAX_PAYLOAD);
    s_last[FRAME_MAX_PAYLOAD] = '\0';
    audio_tx_text(s_id, s_last);
}

void proto_send_canned(int idx) {
    if (idx < 0 || idx >= PROTO_NUM_CANNED) return;
    proto_send_text(proto_canned[idx]);
}

void proto_resend(void) {
    if (s_last[0]) audio_tx_text(s_id, s_last);
}
