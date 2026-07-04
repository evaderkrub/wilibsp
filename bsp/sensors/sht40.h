#ifndef SHT40_H
#define SHT40_H
#include <stdint.h>
#include <stdbool.h>

typedef struct { float temp_c, rh_pct; bool valid; } sht40_reading_t;

// Pure: convert a 6-byte SHT4x measurement frame (T_MSB,T_LSB,T_CRC,RH_MSB,RH_LSB,RH_CRC)
// to degrees C and %RH (clamped 0..100). *crc_ok is the AND of both CRC-8 checks.
// Returns *crc_ok (true only when both CRCs are valid).
bool sht40_convert(const uint8_t raw[6], float *temp_c, float *rh_pct, bool *crc_ok);

bool sht40_init(void);                 // presence check over I2C1 (reads serial number)
bool sht40_read(sht40_reading_t *out); // trigger high-precision measure + read + convert
#endif
