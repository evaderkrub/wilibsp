/* WILI binary-frame parser for the OneWili C API (single-header; define
 * OW_BIN_IMPLEMENTATION in exactly one .c file). Stream format per frame:
 *   bytes 0-3   marker "WILI" (0x57 0x49 0x4C 0x49)
 *   bytes 4-5   repeat_count (LE u16)
 *   bytes 6-7   header_type  (LE u16)
 *   bytes 8-11  errorbit<<31 | payload_length (LE u32)
 *   then payload_length payload bytes.
 * Incremental: state persists across feed() calls; on marker mismatch or an
 * oversize length the parser drops one byte and rescans (resyncs counter).
 * Fixed memory, no allocation - compiles for embedded targets as-is.
 * This file must stay valid C11 AND C++ (menutool unit-tests compile it). */
#ifndef ONEWILI_BINARY_FRAMING_H
#define ONEWILI_BINARY_FRAMING_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OW_BIN_HEADER_SIZE 12u
#ifndef OW_BIN_MAX_PAYLOAD
#define OW_BIN_MAX_PAYLOAD 4096u
#endif

typedef struct ow_bin_frame {
    uint16_t header_type;
    uint16_t repeat_count;
    const uint8_t* payload;   /* into the parser buffer; valid until next feed */
    uint32_t payload_len;
    bool error;               /* header length-word bit 31 */
} ow_bin_frame;

typedef struct ow_bin_parser {
    uint8_t  hdr[OW_BIN_HEADER_SIZE];
    uint32_t hdr_fill;
    uint8_t  payload[OW_BIN_MAX_PAYLOAD];
    uint32_t payload_fill;
    uint32_t payload_len;     /* expected, from the accepted header */
    uint16_t header_type, repeat_count;
    bool     error;
    bool     in_payload;
    uint32_t resyncs;         /* bytes dropped hunting for a valid header */
} ow_bin_parser;

void ow_bin_parser_init(ow_bin_parser* p);

/* Consume input; returns bytes consumed (<= len). Sets *out_ready = 1 and
 * fills *out when a complete frame is available - call again with the
 * remaining bytes (data + consumed). *out_ready = 0 means all input consumed
 * with no complete frame yet. */
size_t ow_bin_parser_feed(ow_bin_parser* p, const uint8_t* data, size_t len,
                          ow_bin_frame* out, int* out_ready);

#ifdef __cplusplus
}
#endif
#endif /* ONEWILI_BINARY_FRAMING_H */

#ifdef OW_BIN_IMPLEMENTATION
#ifdef __cplusplus
extern "C" {
#endif

static int ow_bin__marker_ok(const uint8_t* h, uint32_t n) {
    static const uint8_t m[4] = {0x57u, 0x49u, 0x4Cu, 0x49u};   /* "WILI" */
    uint32_t i;
    for (i = 0; i < n && i < 4u; ++i)
        if (h[i] != m[i]) return 0;
    return 1;
}

/* Drop the first header byte and keep dropping until the remaining prefix
 * could still be a marker. */
static void ow_bin__resync(ow_bin_parser* p) {
    do {
        memmove(p->hdr, p->hdr + 1, --p->hdr_fill);
        ++p->resyncs;
    } while (p->hdr_fill && !ow_bin__marker_ok(p->hdr, p->hdr_fill));
}

void ow_bin_parser_init(ow_bin_parser* p) {
    memset(p, 0, sizeof *p);
}

size_t ow_bin_parser_feed(ow_bin_parser* p, const uint8_t* data, size_t len,
                          ow_bin_frame* out, int* out_ready) {
    size_t used = 0;
    *out_ready = 0;
    while (used < len) {
        if (!p->in_payload) {
            p->hdr[p->hdr_fill++] = data[used++];
            if (!ow_bin__marker_ok(p->hdr, p->hdr_fill)) {
                ow_bin__resync(p);
                continue;
            }
            if (p->hdr_fill < OW_BIN_HEADER_SIZE) continue;
            p->repeat_count = (uint16_t)((uint16_t)p->hdr[4] | ((uint16_t)p->hdr[5] << 8));
            p->header_type  = (uint16_t)((uint16_t)p->hdr[6] | ((uint16_t)p->hdr[7] << 8));
            {
                uint32_t lw = (uint32_t)p->hdr[8] | ((uint32_t)p->hdr[9] << 8) |
                              ((uint32_t)p->hdr[10] << 16) | ((uint32_t)p->hdr[11] << 24);
                p->error = (lw & 0x80000000u) != 0u;
                p->payload_len = lw & 0x7FFFFFFFu;
            }
            if (p->payload_len > OW_BIN_MAX_PAYLOAD) {
                ow_bin__resync(p);   /* bogus length: not a real header */
                continue;
            }
            p->hdr_fill = 0;
            p->payload_fill = 0;
            p->in_payload = true;
        }
        if (p->in_payload) {
            uint32_t want = p->payload_len - p->payload_fill;
            size_t avail = len - used;
            uint32_t take = (uint32_t)(avail < (size_t)want ? avail : (size_t)want);
            memcpy(p->payload + p->payload_fill, data + used, take);
            p->payload_fill += take;
            used += take;
            if (p->payload_fill == p->payload_len) {
                out->header_type  = p->header_type;
                out->repeat_count = p->repeat_count;
                out->payload      = p->payload;
                out->payload_len  = p->payload_len;
                out->error        = p->error;
                p->in_payload = false;
                *out_ready = 1;
                return used;
            }
        }
    }
    /* A zero-length frame whose header ended exactly at the buffer end. */
    if (p->in_payload && p->payload_fill == p->payload_len) {
        out->header_type  = p->header_type;
        out->repeat_count = p->repeat_count;
        out->payload      = p->payload;
        out->payload_len  = p->payload_len;
        out->error        = p->error;
        p->in_payload = false;
        *out_ready = 1;
    }
    return used;
}

#ifdef __cplusplus
}
#endif
#endif /* OW_BIN_IMPLEMENTATION */
