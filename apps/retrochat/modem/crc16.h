// modem/crc16.h — CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) for air frames.
#ifndef RC_CRC16_H
#define RC_CRC16_H
#include <stdint.h>
#include <stddef.h>
uint16_t crc16_ccitt(const uint8_t *data, size_t len);
#endif
