// bsp/ir/ir_file.c — pure logic, no Pico SDK.
#include "ir_file.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const struct { const char *name; ir_protocol_t proto; } k_names[] = {
    {"NEC", IR_PROTO_NEC},       {"NECext", IR_PROTO_NECEXT},
    {"Samsung32", IR_PROTO_SAMSUNG32},
    {"RC5", IR_PROTO_RC5},       {"RC5X", IR_PROTO_RC5},
    {"RC6", IR_PROTO_RC6},
    {"SIRC", IR_PROTO_SIRC},     {"SIRC15", IR_PROTO_SIRC15},
    {"SIRC20", IR_PROTO_SIRC20},
    {"Kaseikyo", IR_PROTO_KASEIKYO}, {"RCA", IR_PROTO_RCA},
};

ir_protocol_t ir_file_protocol_from_name(const char *name) {
    for (unsigned i = 0; i < sizeof k_names / sizeof k_names[0]; i++)
        if (strcmp(name, k_names[i].name) == 0) return k_names[i].proto;
    return IR_PROTO_UNKNOWN;
}

static const char *skip_ws(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

// Copy src into dst (max bytes incl. NUL), trimming trailing \r/\n/space/tab
// (fgets/f_gets-shaped callers hand lines with the newline still attached).
static void copy_trim(char *dst, size_t max, const char *src) {
    size_t n = 0;
    while (src[n] && n < max - 1) { dst[n] = src[n]; n++; }
    dst[n] = 0;
    while (n > 0 && (dst[n-1] == '\r' || dst[n-1] == '\n' ||
                     dst[n-1] == ' ' || dst[n-1] == '\t'))
        dst[--n] = 0;
}

// "EE 87 00 00" -> 0x87EE (little-endian byte list, 1..4 bytes).
static bool parse_hex_le(const char *s, uint32_t *out) {
    uint32_t v = 0;
    int i = 0;
    for (; i < 4; i++) {
        s = skip_ws(s);
        if (*s == 0 || *s == '\r') break;
        char *end;
        unsigned long b = strtoul(s, &end, 16);
        if (end == s || b > 0xFFu) return false;
        v |= (uint32_t)b << (8 * i);
        s = end;
    }
    if (i == 0) return false;
    *out = v;
    return true;
}

static bool entry_valid(const ir_file_parser_t *p) {
    if (!p->in_entry) return false;
    if (p->cur.is_raw) {
        // Carrier sanity: consumer IR lives at 30-56 kHz; accept a generous band.
        // Out-of-band (incl. strtoul sign-wrap of negative values) => entry skipped,
        // keeping garbage away from pio_sm_set_clkdiv (spec §8 TX safety).
        return p->cur.frequency >= IR_FILE_FREQ_MIN && p->cur.frequency <= IR_FILE_FREQ_MAX
            && p->cur.timing_count >= 3;
    }
    return p->have_proto && p->have_addr && p->have_cmd;
}

// Emit the pending entry if valid, count it as skipped if it existed but
// wasn't. Returns whether *out was written.
static bool emit_pending(ir_file_parser_t *p, ir_file_entry_t *out) {
    bool ok = entry_valid(p);
    if (ok) *out = p->cur;
    else if (p->in_entry) p->skipped++;
    p->in_entry = false;
    return ok;
}

void ir_file_parser_init(ir_file_parser_t *p) { memset(p, 0, sizeof *p); }

bool ir_file_parser_line(ir_file_parser_t *p, const char *line, ir_file_entry_t *out) {
    if (line[0] == '#') return false;              // entry separator / comment
    const char *colon = strchr(line, ':');
    if (!colon) return false;                      // blank or junk line
    char key[16];
    size_t klen = (size_t)(colon - line);
    if (klen == 0 || klen >= sizeof key) return false;
    memcpy(key, line, klen);
    key[klen] = 0;
    const char *val = skip_ws(colon + 1);

    bool emitted = false;
    if (strcmp(key, "name") == 0) {
        emitted = emit_pending(p, out);
        memset(&p->cur, 0, sizeof p->cur);
        p->have_proto = p->have_addr = p->have_cmd = false;
        copy_trim(p->cur.name, IR_FILE_MAX_NAME, val);
        p->in_entry = true;
    } else if (!p->in_entry) {
        // Header lines (Filetype/Version) or keys before any entry: ignore.
    } else if (strcmp(key, "type") == 0) {
        char t[8];
        copy_trim(t, sizeof t, val);
        p->cur.is_raw = strcmp(t, "raw") == 0;
    } else if (strcmp(key, "protocol") == 0) {
        char nm[16];
        copy_trim(nm, sizeof nm, val);
        p->cur.msg.protocol = ir_file_protocol_from_name(nm);
        p->have_proto = p->cur.msg.protocol != IR_PROTO_UNKNOWN;
    } else if (strcmp(key, "address") == 0) {
        p->have_addr = parse_hex_le(val, &p->cur.msg.address);
    } else if (strcmp(key, "command") == 0) {
        p->have_cmd = parse_hex_le(val, &p->cur.msg.command);
    } else if (strcmp(key, "frequency") == 0) {
        p->cur.frequency = (uint32_t)strtoul(val, NULL, 10);
    } else if (strcmp(key, "data") == 0) {
        const char *s = val;
        while (*s && p->cur.timing_count < IR_MAX_TIMINGS) {
            char *end;
            unsigned long us = strtoul(s, &end, 10);
            if (end == s) break;
            p->cur.timings[p->cur.timing_count++] = (uint32_t)us;
            s = end;
        }
        // Timings past IR_MAX_TIMINGS are ignored: they can't round-trip
        // through capture/TX buffers anyway (spec: cap, don't crash).
    }
    // duty_cycle and unknown keys: intentionally ignored.
    return emitted;
}

bool ir_file_parser_finish(ir_file_parser_t *p, ir_file_entry_t *out) {
    return emit_pending(p, out);
}

uint32_t ir_file_format_header(char *buf, uint32_t max) {
    int n = snprintf(buf, max, "Filetype: IR signals file\nVersion: 1\n");
    return (n > 0 && (uint32_t)n < max) ? (uint32_t)n : 0;
}

uint32_t ir_file_format_entry(const ir_file_entry_t *e, char *buf, uint32_t max) {
    int n;
    if (!e->is_raw) {
        if (e->msg.protocol == IR_PROTO_UNKNOWN || e->msg.protocol >= IR_PROTO_COUNT)
            return 0;
        unsigned a = e->msg.address, c = e->msg.command;
        n = snprintf(buf, max,
            "#\nname: %s\ntype: parsed\nprotocol: %s\n"
            "address: %02X %02X %02X %02X\ncommand: %02X %02X %02X %02X\n",
            e->name, ir_protocol_name(e->msg.protocol),
            a & 0xFFu, (a >> 8) & 0xFFu, (a >> 16) & 0xFFu, (a >> 24) & 0xFFu,
            c & 0xFFu, (c >> 8) & 0xFFu, (c >> 16) & 0xFFu, (c >> 24) & 0xFFu);
        return (n > 0 && (uint32_t)n < max) ? (uint32_t)n : 0;
    }
    if (e->frequency < IR_FILE_FREQ_MIN || e->frequency > IR_FILE_FREQ_MAX || e->timing_count < 3)
        return 0;
    n = snprintf(buf, max,
        "#\nname: %s\ntype: raw\nfrequency: %lu\nduty_cycle: 0.330000\ndata:",
        e->name, (unsigned long)e->frequency);
    if (n <= 0 || (uint32_t)n >= max) return 0;
    uint32_t len = (uint32_t)n;
    for (uint32_t i = 0; i < e->timing_count; i++) {
        n = snprintf(buf + len, max - len, " %lu", (unsigned long)e->timings[i]);
        if (n <= 0 || len + (uint32_t)n >= max) return 0;
        len += (uint32_t)n;
    }
    n = snprintf(buf + len, max - len, "\n");
    if (n <= 0 || len + (uint32_t)n >= max) return 0;
    return len + (uint32_t)n;
}
