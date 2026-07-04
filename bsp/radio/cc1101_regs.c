#include "cc1101_regs.h"
#define XTAL_HZ 26000000u

cc1101_freq_regs_t cc1101_freq_to_regs(uint32_t hz) {
    // round(f_hz * 2^16 / 26e6): add half-divisor before the integer divide
    uint32_t f = (uint32_t)((((uint64_t)hz << 16) + (XTAL_HZ / 2u)) / XTAL_HZ);
    cc1101_freq_regs_t r = { (uint8_t)(f >> 16), (uint8_t)(f >> 8), (uint8_t)f };
    return r;
}

int cc1101_rssi_to_dbm(uint8_t raw) {
    int v = (raw >= 128) ? (int)raw - 256 : (int)raw;
    return v / 2 - 74;
}

uint8_t cc1101_mdmcfg2_mod_bits(cc1101_mod_t m) {
    return (uint8_t)((m & 0x07) << 4);
}

bool cc1101_freq_in_band(uint32_t hz) {
    return (hz >= 300000000u && hz <= 348000000u) ||
           (hz >= 387000000u && hz <= 464000000u) ||
           (hz >= 779000000u && hz <= 928000000u);
}
