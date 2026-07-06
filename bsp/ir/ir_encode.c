// bsp/ir/ir_encode.c — pure-logic protocol encoders (no Pico SDK).
#include "ir_encode.h"

// Bounds-checked append; *n set to UINT32_MAX poisons the frame on overflow.
static void emit(uint32_t *t, uint32_t *n, uint32_t max, uint32_t dur) {
    if (*n >= max) { *n = UINT32_MAX; return; }
    t[(*n)++] = dur;
}

static uint32_t enc_pd(uint32_t *t, uint32_t max, uint32_t hdr_mark, uint32_t hdr_space,
                       uint32_t mark, uint32_t space0, uint32_t space1,
                       uint64_t v, uint32_t n_bits, uint32_t stop_mark) {
    uint32_t n = 0;
    emit(t, &n, max, hdr_mark); emit(t, &n, max, hdr_space);
    for (uint32_t i = 0; i < n_bits && n != UINT32_MAX; i++) {
        emit(t, &n, max, mark);
        emit(t, &n, max, ((v >> i) & 1u) ? space1 : space0);
    }
    if (n != UINT32_MAX) emit(t, &n, max, stop_mark);
    return n == UINT32_MAX ? 0 : n;
}

static uint32_t enc_nec(const ir_message_t *m, uint32_t *t, uint32_t max) {
    if (m->repeat) {
        if (max < 3) return 0;
        t[0] = 9000; t[1] = 2250; t[2] = 560;
        return 3;
    }
    uint64_t v;
    if (m->protocol == IR_PROTO_NEC) {
        uint8_t a = (uint8_t)m->address, c = (uint8_t)m->command;
        v = a | ((uint64_t)(uint8_t)~a << 8) | ((uint64_t)c << 16) |
            ((uint64_t)(uint8_t)~c << 24);
    } else { // NECext
        v = (m->address & 0xFFFFu) | ((uint64_t)(m->command & 0xFFFFu) << 16);
    }
    return enc_pd(t, max, 9000, 4500, 560, 560, 1690, v, 32, 560);
}

static uint32_t enc_samsung32(const ir_message_t *m, uint32_t *t, uint32_t max) {
    uint8_t a = (uint8_t)m->address, c = (uint8_t)m->command;
    uint64_t v = a | ((uint64_t)a << 8) | ((uint64_t)c << 16) |
                 ((uint64_t)(uint8_t)~c << 24);
    return enc_pd(t, max, 4500, 4500, 550, 550, 1650, v, 32, 550);
}

static uint32_t enc_sirc(const ir_message_t *m, uint32_t *t, uint32_t max) {
    uint32_t bits = m->protocol == IR_PROTO_SIRC ? 12 :
                    m->protocol == IR_PROTO_SIRC15 ? 15 : 20;
    uint64_t v = (m->command & 0x7Fu) | ((uint64_t)m->address << 7);
    uint32_t n = 0;
    emit(t, &n, max, 2400); emit(t, &n, max, 600);
    for (uint32_t i = 0; i < bits && n != UINT32_MAX; i++) {
        emit(t, &n, max, ((v >> i) & 1u) ? 1200 : 600);     // pulse-width mark
        if (i + 1 < bits) emit(t, &n, max, 600);            // separator space
    }
    return n == UINT32_MAX ? 0 : n;
}

static uint32_t enc_rca(const ir_message_t *m, uint32_t *t, uint32_t max) {
    uint32_t a = m->address & 0xFu, c = m->command & 0xFFu;
    uint64_t v = ((uint64_t)a << 20) | ((uint64_t)c << 12) |
                 ((uint64_t)(a ^ 0xFu) << 8) | (c ^ 0xFFu);
    // enc_pd emits LSB-first; RCA is MSB-first, so pre-reverse the 24 bits.
    uint64_t rv = 0;
    for (uint32_t i = 0; i < 24; i++) if ((v >> i) & 1u) rv |= 1ull << (23 - i);
    return enc_pd(t, max, 4000, 4000, 500, 1000, 2000, rv, 24, 500);
}

// Merge a half-bit level array into mark/space durations. Drops leading and
// trailing space runs (invisible over the air). Returns count or 0.
static uint32_t merge_halves(const uint8_t *hb, uint32_t nh, uint32_t half_us,
                             uint32_t *t, uint32_t *n, uint32_t max) {
    uint32_t i = 0;
    while (i < nh && hb[i] == 0) i++;
    while (i < nh) {
        uint8_t lvl = hb[i];
        uint32_t units = 0;
        while (i < nh && hb[i] == lvl) { units++; i++; }
        if (lvl == 0 && i >= nh) break;                  // trailing space
        emit(t, n, max, units * half_us);
    }
    return *n == UINT32_MAX ? 0 : *n;
}

static uint32_t enc_rc5(const ir_message_t *m, uint32_t *t, uint32_t max) {
    uint32_t s2 = ((m->command >> 6) & 1u) ^ 1u;         // RC5X extension
    uint32_t bits = (1u << 13) | (s2 << 12) |            // S1=1, S2, toggle=0
                    ((m->address & 0x1Fu) << 6) | (m->command & 0x3Fu);
    uint8_t hb[28];
    for (uint32_t b = 0; b < 14; b++) {
        uint32_t bit = (bits >> (13 - b)) & 1u;
        hb[2*b]     = bit ? 0 : 1;                       // 1 = space->mark
        hb[2*b + 1] = bit ? 1 : 0;
    }
    uint32_t n = 0;
    return merge_halves(hb, 28, 889, t, &n, max);
}

static uint32_t enc_rc6(const ir_message_t *m, uint32_t *t, uint32_t max) {
    uint8_t hb[44]; uint32_t p = 0;
    // start=1, mode=000, toggle=0 (double width), then addr8+cmd8 MSB-first.
    hb[p++] = 1; hb[p++] = 0;                            // start: mark->space = 1
    for (int i = 0; i < 3; i++) { hb[p++] = 0; hb[p++] = 1; }   // mode 0
    hb[p++] = 0; hb[p++] = 0; hb[p++] = 1; hb[p++] = 1;  // toggle 0, double width
    uint32_t v = ((m->address & 0xFFu) << 8) | (m->command & 0xFFu);
    for (int b = 15; b >= 0; b--) {
        uint32_t bit = (v >> b) & 1u;
        hb[p++] = bit ? 1 : 0;
        hb[p++] = bit ? 0 : 1;
    }
    uint32_t n = 0;
    emit(t, &n, max, 2666); emit(t, &n, max, 889);       // leader
    return merge_halves(hb, 44, 444, t, &n, max);
}

static uint32_t enc_kaseikyo(const ir_message_t *m, uint32_t *t, uint32_t max) {
    uint64_t v = (m->address & 0xFFFFFFu) | ((uint64_t)(m->command & 0xFFFFFFu) << 24);
    return enc_pd(t, max, 3456, 1728, 432, 432, 1296, v, 48, 432);
}

uint32_t ir_encode(const ir_message_t *msg, uint32_t *t, uint32_t max) {
    switch (msg->protocol) {
    case IR_PROTO_NEC:
    case IR_PROTO_NECEXT:    return enc_nec(msg, t, max);
    case IR_PROTO_SAMSUNG32: return enc_samsung32(msg, t, max);
    case IR_PROTO_SIRC:
    case IR_PROTO_SIRC15:
    case IR_PROTO_SIRC20:    return enc_sirc(msg, t, max);
    case IR_PROTO_RCA:       return enc_rca(msg, t, max);
    case IR_PROTO_RC5:       return enc_rc5(msg, t, max);
    case IR_PROTO_RC6:       return enc_rc6(msg, t, max);
    case IR_PROTO_KASEIKYO:  return enc_kaseikyo(msg, t, max);
    default:                 return 0;   // unknown/unsupported protocol
    }
}
