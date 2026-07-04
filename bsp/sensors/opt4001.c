// src/sensors/opt4001.c — TI OPT4001 ambient light sensor over I2C1 (0x45, ADDR=high).
#include "sensors/opt4001.h"
#ifdef PICO_BUILD
#include "hardware/i2c.h"
#include "platform/diag.h"
#define OPT4001_I2C  i2c1
#endif

#define OPT4001_ADDR        0x45
#define OPT4001_REG_RESULT  0x00   // 0x00/0x01: exponent + 20-bit mantissa (+counter/crc)
#define OPT4001_REG_CONFIG  0x0A
#define OPT4001_REG_DEVID   0x11

// Package-dependent lux per ADC code. 437.5e-6 is the datasheet value for the common
// package; CONFIRM/CALIBRATE on hardware against a known light level (single constant).
#define OPT4001_LUX_FACTOR  437.5e-6f

float opt4001_lux(uint8_t exponent, uint32_t mantissa) {
    uint64_t adc_codes = (uint64_t)mantissa << exponent;
    return (float)adc_codes * OPT4001_LUX_FACTOR;
}

#ifdef PICO_BUILD
bool opt4001_init(void) {
    // CONFIG 0x3238: RANGE=auto(12), CONVERSION_TIME=100ms(8), MODE=continuous(3), LATCH=1.
    uint8_t cfg[3] = { OPT4001_REG_CONFIG, 0x32, 0x38 };
    bool ok = (i2c_write_blocking(OPT4001_I2C, OPT4001_ADDR, cfg, 3, false) == 3);
    uint8_t reg = OPT4001_REG_DEVID, id[2] = { 0, 0 };
    if (i2c_write_blocking(OPT4001_I2C, OPT4001_ADDR, &reg, 1, true) == 1)
        i2c_read_blocking(OPT4001_I2C, OPT4001_ADDR, id, 2, false);
    DIAG("opt4001: init %s id=0x%02x%02x\n", ok ? "ok" : "NAK", id[0], id[1]);
    return ok;
}

bool opt4001_read(float *lux) {
    uint8_t reg = OPT4001_REG_RESULT, b[4];
    if (i2c_write_blocking(OPT4001_I2C, OPT4001_ADDR, &reg, 1, true) != 1) return false;
    if (i2c_read_blocking(OPT4001_I2C, OPT4001_ADDR, b, 4, false) != 4) return false;
    // b[0]: EXPONENT[15:12] | RESULT_MSB[11:8]; b[1]: RESULT_MSB[7:0]; b[2]: RESULT_LSB[7:0]
    uint8_t  exponent   = b[0] >> 4;
    uint32_t result_msb = ((uint32_t)(b[0] & 0x0F) << 8) | b[1];   // 12 bits
    uint32_t mantissa   = (result_msb << 8) | b[2];                // 20 bits
    *lux = opt4001_lux(exponent, mantissa);
    return true;
}
#endif // PICO_BUILD
