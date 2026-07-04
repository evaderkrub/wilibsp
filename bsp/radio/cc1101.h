#ifndef CC1101_H
#define CC1101_H
#include <stdint.h>
#include <stdbool.h>
#include "radio/cc1101_regs.h"

bool    cc1101_init(void);
uint8_t cc1101_read_version(void);
void    cc1101_set_frequency(uint32_t hz);
void    cc1101_set_modulation(cc1101_mod_t m);
void    cc1101_strobe_rx(void);
void    cc1101_calibrate(void);   // manual PLL cal at current freq (once per band; MCSM0 auto-cal is off)
int     cc1101_read_rssi_dbm(void);
void cc1101_monitor_rx(uint32_t hz, cc1101_mod_t mod);  // async-transparent RX; GDO0 = data
void cc1101_monitor_stop(void);                         // SIDLE + restore IOCFG0
void cc1101_tx_ook_start(uint32_t hz);                  // async-transparent OOK TX; MCU drives GDO0
void cc1101_tx_ook_stop(void);                          // SIDLE + restore RX regs
#endif
