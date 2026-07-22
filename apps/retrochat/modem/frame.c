#include "frame.h"
#include "crc16.h"
#include <string.h>

unsigned frame_build(uint8_t sender, uint8_t type,
                     const uint8_t *payload, uint8_t len, uint8_t *out) {
    if (len < 1 || len > FRAME_MAX_PAYLOAD) return 0;
    unsigned n = 0;
    for (int i = 0; i < FRAME_TRAINING; i++) out[n++] = 0x55;
    out[n++] = FRAME_SYNC;
    unsigned crc_start = n;
    out[n++] = sender;
    out[n++] = type;
    out[n++] = len;
    memcpy(&out[n], payload, len);
    n += len;
    uint16_t crc = crc16_ccitt(&out[crc_start], 3u + len);
    out[n++] = (uint8_t)(crc >> 8);
    out[n++] = (uint8_t)(crc & 0xFF);
    return n;
}

void frame_parser_init(frame_parser_t *p) { memset(p, 0, sizeof *p); }

int frame_parser_push(frame_parser_t *p, uint8_t b, frame_msg_t *out) {
    switch (p->state) {
    case 0:
        if (b == FRAME_SYNC) { p->state = 1; p->idx = 0; }
        return 0;
    case 1:
        p->hdr[p->idx++] = b;
        if (p->idx == 3) {
            p->cur.sender = p->hdr[0];
            p->cur.type   = p->hdr[1];
            p->cur.len    = p->hdr[2];
            if (p->cur.len < 1 || p->cur.len > FRAME_MAX_PAYLOAD) {
                p->state = 0;
                return -1;
            }
            p->state = 2;
            p->body_left = (uint16_t)p->cur.len + 2u;
        }
        return 0;
    default:
        if (p->body_left > 2)
            p->cur.payload[p->cur.len - (p->body_left - 2u)] = b;
        else
            p->crc_buf[2u - p->body_left] = b;
        if (--p->body_left == 0) {
            p->state = 0;
            uint8_t hp[3 + FRAME_MAX_PAYLOAD];
            hp[0] = p->cur.sender; hp[1] = p->cur.type; hp[2] = p->cur.len;
            memcpy(&hp[3], p->cur.payload, p->cur.len);
            uint16_t crc = crc16_ccitt(hp, 3u + p->cur.len);
            uint16_t rx  = (uint16_t)(((uint16_t)p->crc_buf[0] << 8) | p->crc_buf[1]);
            if (crc == rx) { *out = p->cur; return 1; }
            return -1;
        }
        return 0;
    }
}
