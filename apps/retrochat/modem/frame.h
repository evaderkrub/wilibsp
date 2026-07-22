// modem/frame.h — RetroChat air frame: [0x55 x8][0x7E][sender][type][len][payload][crc16].
// CRC-16/CCITT-FALSE over sender..payload. Parser is a byte-fed state machine that
// re-hunts for 0x7E after any error; a payload byte that happens to be 0x7E can only
// cause a false lock that the CRC then rejects.
#ifndef RC_FRAME_H
#define RC_FRAME_H
#include <stdint.h>

#define FRAME_MAX_PAYLOAD 200
#define FRAME_SYNC        0x7E
#define FRAME_TYPE_MSG    0x01
#define FRAME_TRAINING    8
#define FRAME_MAX_BYTES   (FRAME_TRAINING + 1 + 3 + FRAME_MAX_PAYLOAD + 2)

typedef struct {
    uint8_t sender, type, len;
    uint8_t payload[FRAME_MAX_PAYLOAD];
} frame_msg_t;

// Writes the full air frame into out (>= FRAME_MAX_BYTES). Returns byte count,
// or 0 if len is out of range 1..FRAME_MAX_PAYLOAD.
unsigned frame_build(uint8_t sender, uint8_t type,
                     const uint8_t *payload, uint8_t len, uint8_t *out);

typedef struct frame_parser {
    uint8_t state;        // 0 hunt sync, 1 header, 2 payload+crc
    uint8_t hdr[3];
    uint8_t idx;
    uint16_t body_left;
    uint8_t crc_buf[2];
    frame_msg_t cur;
} frame_parser_t;

void frame_parser_init(frame_parser_t *p);
// Feed one byte. Returns 1 when a valid frame is complete (copied to *out),
// -1 on a detected error (bad len or CRC mismatch), 0 otherwise.
int frame_parser_push(frame_parser_t *p, uint8_t b, frame_msg_t *out);

#endif
