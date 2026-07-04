#ifndef CC1101_REGS_H
#define CC1101_REGS_H
#include <stdint.h>
#include <stdbool.h>

typedef struct { uint8_t f2, f1, f0; } cc1101_freq_regs_t;
typedef enum {
    CC1101_MOD_2FSK = 0, CC1101_MOD_GFSK = 1, CC1101_MOD_ASK_OOK = 3,
    CC1101_MOD_4FSK = 4, CC1101_MOD_MSK = 7
} cc1101_mod_t;

cc1101_freq_regs_t cc1101_freq_to_regs(uint32_t hz);
int     cc1101_rssi_to_dbm(uint8_t raw);
uint8_t cc1101_mdmcfg2_mod_bits(cc1101_mod_t m);
bool    cc1101_freq_in_band(uint32_t hz);
#endif
