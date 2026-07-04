// src/radio/ook_tx.h — drive GDO0 with a captured OOK duration timeline for RF
// re-transmit. The CC1101 must already be in async-transparent OOK TX
// (cc1101_tx_ook_start); this owns GPIO32 while sending.
#ifndef OOK_TX_H
#define OOK_TX_H
#include <stdint.h>
#include <stdbool.h>

// Clamp a single duration so a pathological saturated-idle value can't hang the
// blocking transmit. Real OOK symbols/gaps are far shorter than this.
#define OOK_TX_MAX_US 100000u

uint32_t ook_tx_clamp_us(uint32_t us);   // pure, host-tested

// Blocking: drive GDO0 to each level (starting at start_level, toggling per
// duration) for that many microseconds, using accumulated absolute deadlines so
// the burst can't drift. Leaves the pin low (carrier off) at the end.
void     ook_tx_send(const uint32_t *durs, uint32_t n, bool start_level);
#endif
