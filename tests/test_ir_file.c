#include "test_util.h"
#include "ir_file.h"
#include <string.h>
#include <stdio.h>

static const char *k_lines[] = {
    "Filetype: IR signals file",
    "Version: 1",
    "#",
    "name: Power",
    "type: parsed",
    "protocol: NEC",
    "address: 04 00 00 00",
    "command: 08 00 00 00",
    "#",
    "name: Vol_up",
    "type: parsed",
    "protocol: NECext",
    "address: EE 87 00 00",
    "command: 5D A2 00 00",
    "#",
    "name: Raw_1",
    "type: raw",
    "frequency: 38000",
    "duty_cycle: 0.330000",
    "data: 8996 4515 542 591 542 1701 542",
    "#",
    "name: Odd",                   // NEC42 is unsupported -> skipped
    "type: parsed",
    "protocol: NEC42",
    "address: 01 00 00 00",
    "command: 02 00 00 00",
    "#",
    "name: Broken",                // malformed address -> skipped
    "type: parsed",
    "protocol: NEC",
    "address: ZZ 00 00 00",
    "command: 02 00 00 00",
    "#",
    "name: Rc5x_btn",
    "type: parsed",
    "protocol: RC5X",
    "address: 10 00 00 00",
    "command: 4A 00 00 00",
};

int main(void) {
    ir_file_parser_t p;
    ir_file_entry_t e[8];
    unsigned n = 0;

    ir_file_parser_init(&p);
    for (unsigned i = 0; i < sizeof k_lines / sizeof k_lines[0]; i++)
        if (ir_file_parser_line(&p, k_lines[i], &e[n])) n++;
    if (ir_file_parser_finish(&p, &e[n])) n++;

    ASSERT_EQ(n, 4);
    ASSERT_EQ(p.skipped, 2);                       // NEC42 + broken address

    ASSERT_TRUE(strcmp(e[0].name, "Power") == 0);
    ASSERT_TRUE(!e[0].is_raw);
    ASSERT_EQ(e[0].msg.protocol, IR_PROTO_NEC);
    ASSERT_EQ(e[0].msg.address, 0x04);
    ASSERT_EQ(e[0].msg.command, 0x08);

    ASSERT_EQ(e[1].msg.protocol, IR_PROTO_NECEXT);
    ASSERT_EQ(e[1].msg.address, 0x87EE);           // little-endian bytes
    ASSERT_EQ(e[1].msg.command, 0xA25D);

    ASSERT_TRUE(e[2].is_raw);
    ASSERT_EQ(e[2].frequency, 38000);
    ASSERT_EQ(e[2].timing_count, 7);
    ASSERT_EQ(e[2].timings[0], 8996);
    ASSERT_EQ(e[2].timings[6], 542);

    ASSERT_EQ(e[3].msg.protocol, IR_PROTO_RC5);    // RC5X folds into RC5
    ASSERT_EQ(e[3].msg.command, 0x4A);

    // Windows line endings tolerated.
    ir_file_parser_init(&p);
    ir_file_entry_t w;
    ASSERT_TRUE(!ir_file_parser_line(&p, "name: CrLf\r", &w));
    ASSERT_TRUE(!ir_file_parser_line(&p, "type: parsed\r", &w));
    ASSERT_TRUE(!ir_file_parser_line(&p, "protocol: NEC\r", &w));
    ASSERT_TRUE(!ir_file_parser_line(&p, "address: 01 00 00 00\r", &w));
    ASSERT_TRUE(!ir_file_parser_line(&p, "command: 02 00 00 00\r", &w));
    ASSERT_TRUE(ir_file_parser_finish(&p, &w));
    ASSERT_TRUE(strcmp(w.name, "CrLf") == 0);
    ASSERT_EQ(w.msg.address, 0x01);

    // Oversized raw data caps at IR_MAX_TIMINGS, excess ignored, entry kept.
    static char big[8192];
    int off = snprintf(big, sizeof big, "data:");
    for (int i = 0; i < 600; i++) off += snprintf(big + off, sizeof big - off, " 500");
    ir_file_parser_init(&p);
    ir_file_parser_line(&p, "name: Big", &w);
    ir_file_parser_line(&p, "type: raw", &w);
    ir_file_parser_line(&p, "frequency: 38000", &w);
    ir_file_parser_line(&p, big, &w);
    ASSERT_TRUE(ir_file_parser_finish(&p, &w));
    ASSERT_EQ(w.timing_count, IR_MAX_TIMINGS);

    // Protocol name mapping directly.
    ASSERT_EQ(ir_file_protocol_from_name("Samsung32"), IR_PROTO_SAMSUNG32);
    ASSERT_EQ(ir_file_protocol_from_name("SIRC20"), IR_PROTO_SIRC20);
    ASSERT_EQ(ir_file_protocol_from_name("NEC42"), IR_PROTO_UNKNOWN);
    ASSERT_EQ(ir_file_protocol_from_name("Kaseikyo"), IR_PROTO_KASEIKYO);

    // ---- Writer round-trips (Task 5) ----
    static char fbuf[8192];
    uint32_t len = ir_file_format_header(fbuf, sizeof fbuf);
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(fbuf, "Filetype: IR signals file") == fbuf);

    ir_file_entry_t in;
    memset(&in, 0, sizeof in);
    strcpy(in.name, "Power");
    in.is_raw = false;
    in.msg.protocol = IR_PROTO_NECEXT;
    in.msg.address = 0x87EE;
    in.msg.command = 0xA25D;
    uint32_t elen = ir_file_format_entry(&in, fbuf + len, sizeof fbuf - len);
    ASSERT_TRUE(elen > 0);

    ir_file_entry_t raw;
    memset(&raw, 0, sizeof raw);
    strcpy(raw.name, "Zap");
    raw.is_raw = true;
    raw.frequency = 40000;
    raw.timing_count = 5;
    raw.timings[0] = 8996; raw.timings[1] = 4515; raw.timings[2] = 542;
    raw.timings[3] = 1701; raw.timings[4] = 542;
    uint32_t rlen = ir_file_format_entry(&raw, fbuf + len + elen, sizeof fbuf - len - elen);
    ASSERT_TRUE(rlen > 0);

    // Parse the formatted text back: split on '\n' and feed line by line.
    ir_file_parser_t rp;
    ir_file_parser_init(&rp);
    ir_file_entry_t back[4];
    unsigned nb = 0;
    char *save = fbuf;
    for (char *nl; (nl = strchr(save, '\n')) != NULL; save = nl + 1) {
        *nl = 0;
        if (ir_file_parser_line(&rp, save, &back[nb])) nb++;
    }
    if (ir_file_parser_finish(&rp, &back[nb])) nb++;

    ASSERT_EQ(nb, 2);
    ASSERT_EQ(rp.skipped, 0);
    ASSERT_TRUE(strcmp(back[0].name, "Power") == 0);
    ASSERT_EQ(back[0].msg.protocol, IR_PROTO_NECEXT);
    ASSERT_EQ(back[0].msg.address, 0x87EE);
    ASSERT_EQ(back[0].msg.command, 0xA25D);
    ASSERT_TRUE(back[1].is_raw);
    ASSERT_EQ(back[1].frequency, 40000);
    ASSERT_EQ(back[1].timing_count, 5);
    ASSERT_EQ(back[1].timings[3], 1701);

    // Unknown protocol refuses to format; tiny buffer refuses cleanly.
    ir_file_entry_t bad = in;
    bad.msg.protocol = IR_PROTO_UNKNOWN;
    ASSERT_EQ(ir_file_format_entry(&bad, fbuf, sizeof fbuf), 0);
    char tiny[16];
    ASSERT_EQ(ir_file_format_entry(&in, tiny, sizeof tiny), 0);

    // ---- Raw frequency clamp (final-review fix) ----
    // "frequency: -1" sign-wraps via strtoul to 4294967295 -> out of band -> skipped.
    ir_file_parser_init(&p);
    ir_file_entry_t neg;
    ir_file_parser_line(&p, "name: NegFreq", &neg);
    ir_file_parser_line(&p, "type: raw", &neg);
    ir_file_parser_line(&p, "frequency: -1", &neg);
    ir_file_parser_line(&p, "data: 8996 4515 542 591 542 1701 542", &neg);
    ASSERT_TRUE(!ir_file_parser_finish(&p, &neg));
    ASSERT_EQ(p.skipped, 1);

    // Absurdly large frequency (not a wrap, just out of band) -> skipped.
    ir_file_parser_init(&p);
    ir_file_entry_t huge;
    ir_file_parser_line(&p, "name: HugeFreq", &huge);
    ir_file_parser_line(&p, "type: raw", &huge);
    ir_file_parser_line(&p, "frequency: 999999999", &huge);
    ir_file_parser_line(&p, "data: 8996 4515 542 591 542 1701 542", &huge);
    ASSERT_TRUE(!ir_file_parser_finish(&p, &huge));
    ASSERT_EQ(p.skipped, 1);

    // Writer: raw entry with frequency 0 or too few timings refuses to format.
    ir_file_entry_t zero_freq = raw;
    zero_freq.frequency = 0;
    ASSERT_EQ(ir_file_format_entry(&zero_freq, fbuf, sizeof fbuf), 0);

    ir_file_entry_t short_timing = raw;
    short_timing.timing_count = 2;
    ASSERT_EQ(ir_file_format_entry(&short_timing, fbuf, sizeof fbuf), 0);

    TEST_RETURN();
}
