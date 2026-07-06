// bsp/ir/ir_file.h — Flipper .ir signal file parser + writer (pure logic).
// Line-based: the caller reads lines (f_gets on firmware, arrays in tests)
// and feeds them one at a time. Malformed or unsupported entries are counted
// in `skipped` and never emitted — community DB content must not crash us.
#ifndef IR_FILE_H
#define IR_FILE_H
#include "ir_types.h"

#define IR_FILE_MAX_NAME 32u

// Consumer IR carrier band, generously bounded (real remotes cluster around
// 30-56 kHz). Anything outside this range in a raw entry's `frequency:` line
// is rejected rather than fed to pio_sm_set_clkdiv, which silently wraps
// out-of-range dividers under -DNDEBUG (e.g. strtoul sign-wrapping a
// negative value to ~4.29e9) and can wedge the TX LED on for hours.
#define IR_FILE_FREQ_MIN 10000u
#define IR_FILE_FREQ_MAX 150000u

typedef struct {
    char     name[IR_FILE_MAX_NAME];
    bool     is_raw;
    ir_message_t msg;                  // valid when !is_raw
    uint32_t frequency;                // raw only, Hz (0 = absent)
    uint32_t timings[IR_MAX_TIMINGS];  // raw only, mark-first us
    uint32_t timing_count;
} ir_file_entry_t;

typedef struct {
    ir_file_entry_t cur;
    bool in_entry;
    bool have_proto, have_addr, have_cmd;
    uint32_t skipped;                  // malformed/unsupported entries dropped
} ir_file_parser_t;

void ir_file_parser_init(ir_file_parser_t *p);
// Feed one NUL-terminated line (trailing \r/\n tolerated). Returns true when the
// PREVIOUS entry completed (triggered by this line starting the next one).
bool ir_file_parser_line(ir_file_parser_t *p, const char *line, ir_file_entry_t *out);
// Flush at end-of-file; true if a final entry was emitted.
bool ir_file_parser_finish(ir_file_parser_t *p, ir_file_entry_t *out);

// Flipper protocol name -> enum. IR_PROTO_UNKNOWN for unsupported (e.g. NEC42).
// RC5X maps to IR_PROTO_RC5 (command bit 6 carries the S2 field, matching our
// encoder/decoder convention).
ir_protocol_t ir_file_protocol_from_name(const char *name);

// Writer: Flipper-compatible text. Both return bytes written (no NUL
// accounting surprises: the NUL is written but not counted), or 0 when the
// buffer is too small or the entry is not representable (unknown protocol).
uint32_t ir_file_format_header(char *buf, uint32_t max);
uint32_t ir_file_format_entry(const ir_file_entry_t *e, char *buf, uint32_t max);
#endif
