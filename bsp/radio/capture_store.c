// src/radio/capture_store.c
#include "radio/capture_store.h"

static uint32_t    *s_buf;
static uint32_t     s_cap;
static uint32_t     s_len;
static uint32_t     s_freq;
static cc1101_mod_t s_mod;
static bool         s_start_level;
static int          s_antenna;       // ANT_* routed at record time (opaque here)
static uint64_t     s_total_ticks;   // 64-bit: a near-full idle-heavy clip overflows 32-bit us

void capture_store_init(uint32_t *buf, uint32_t capacity) {
    s_buf = buf; s_cap = capacity;
    s_len = 0; s_total_ticks = 0;
    s_freq = 0; s_mod = CC1101_MOD_ASK_OOK; s_start_level = false; s_antenna = 0;
}

void capture_store_begin(uint32_t freq, cc1101_mod_t mod, bool start_level, int antenna) {
    s_len = 0; s_total_ticks = 0;
    s_freq = freq; s_mod = mod; s_start_level = start_level; s_antenna = antenna;
}

uint32_t capture_store_append(const uint32_t *durs, uint32_t n) {
    uint32_t room = (s_len < s_cap) ? (s_cap - s_len) : 0u;
    uint32_t take = (n < room) ? n : room;
    for (uint32_t i = 0; i < take; i++) {
        s_buf[s_len + i] = durs[i];
        s_total_ticks += durs[i];
    }
    s_len += take;
    return take;
}

bool            capture_store_full(void)        { return s_len >= s_cap; }
uint32_t        capture_store_len(void)         { return s_len; }
const uint32_t *capture_store_data(void)        { return s_buf; }
uint32_t        capture_store_freq(void)        { return s_freq; }
cc1101_mod_t    capture_store_mod(void)         { return s_mod; }
bool            capture_store_start_level(void) { return s_start_level; }
int             capture_store_antenna(void)     { return s_antenna; }
uint64_t        capture_store_total_ticks(void) { return s_total_ticks; }
