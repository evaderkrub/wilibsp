#ifndef OPT4001_H
#define OPT4001_H
#include <stdint.h>
#include <stdbool.h>

// Pure: OPT4001 ambient-light conversion. ADC_CODES = mantissa << exponent;
// lux = ADC_CODES * LUX_FACTOR (package-dependent constant, see opt4001.c).
float opt4001_lux(uint8_t exponent, uint32_t mantissa);

bool opt4001_init(void);       // configure continuous mode + presence check (device id)
bool opt4001_read(float *lux); // read result registers and convert to lux
#endif
