// src/sensors/bmi323.c — Bosch BMI323 IMU over I2C1 (0x68). 16-bit little-endian regs.
#include "sensors/bmi323.h"
#ifdef PICO_BUILD
#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include "platform/diag.h"
#define BMI323_I2C   i2c1
#endif

#define BMI323_ADDR      0x68
#define BMI323_REG_CHIPID 0x00
#define BMI323_REG_ACCCONF 0x20
#define BMI323_REG_GYRCONF 0x21
#define BMI323_REG_ACCDATA 0x03    // ACC_X,Y,Z then GYR_X,Y,Z (six 16-bit regs)
#define BMI323_REG_CMD     0x7E
#define BMI323_CMD_SOFTRESET 0xDEAF
#define BMI323_CHIPID_VAL  0x0043

#define BMI323_RANGE_G    4
#define BMI323_RANGE_DPS  500
#define BMI323_DUMMY      2        // BMI3xx I2C reads return 2 leading dummy bytes (confirmed on HW)

float bmi323_accel_g(int16_t raw, int range_g)    { return (float)raw / 32768.0f * (float)range_g; }
float bmi323_gyro_dps(int16_t raw, int range_dps) { return (float)raw / 32768.0f * (float)range_dps; }

#ifdef PICO_BUILD
// Write a 16-bit value (little-endian) to a register.
static bool reg_write16(uint8_t reg, uint16_t val) {
    uint8_t b[3] = { reg, (uint8_t)(val & 0xFF), (uint8_t)(val >> 8) };
    return i2c_write_blocking(BMI323_I2C, BMI323_ADDR, b, 3, false) == 3;
}
// Read `n16` 16-bit regs from `reg` into out[] (skips BMI323_DUMMY leading bytes).
static bool reg_read16(uint8_t reg, int16_t *out, int n16) {
    uint8_t buf[BMI323_DUMMY + 16];
    int total = BMI323_DUMMY + 2 * n16;
    if (i2c_write_blocking(BMI323_I2C, BMI323_ADDR, &reg, 1, true) != 1) return false;
    if (i2c_read_blocking(BMI323_I2C, BMI323_ADDR, buf, total, false) != total) return false;
    for (int i = 0; i < n16; i++) {
        int o = BMI323_DUMMY + 2 * i;
        out[i] = (int16_t)((uint16_t)buf[o] | ((uint16_t)buf[o + 1] << 8));
    }
    return true;
}

bool bmi323_init(void) {
    reg_write16(BMI323_REG_CMD, BMI323_CMD_SOFTRESET);
    sleep_ms(5);
    int16_t id;
    bool ok = reg_read16(BMI323_REG_CHIPID, &id, 1);
    DIAG("bmi323: chipid=0x%04x %s\n", (uint16_t)id,
         (ok && (id & 0xFF) == BMI323_CHIPID_VAL) ? "ok" : "??");
    // ACC_CONF: ±4 g, 100 Hz ODR, normal mode (bits[14:12]=4). GYR_CONF: ±500 dps, 100 Hz, normal.
    reg_write16(BMI323_REG_ACCCONF, 0x4018);   // mode=4 | range=1(±4g)<<4 | odr=8(100Hz)
    reg_write16(BMI323_REG_GYRCONF, 0x4028);   // mode=4 | range=2(±500)<<4 | odr=8(100Hz)
    sleep_ms(2);
    return ok;
}

bool bmi323_read(bmi323_reading_t *out) {
    out->valid = false;
    int16_t d[6];
    if (!reg_read16(BMI323_REG_ACCDATA, d, 6)) return false;
    out->ax = bmi323_accel_g(d[0], BMI323_RANGE_G);
    out->ay = bmi323_accel_g(d[1], BMI323_RANGE_G);
    out->az = bmi323_accel_g(d[2], BMI323_RANGE_G);
    out->gx = bmi323_gyro_dps(d[3], BMI323_RANGE_DPS);
    out->gy = bmi323_gyro_dps(d[4], BMI323_RANGE_DPS);
    out->gz = bmi323_gyro_dps(d[5], BMI323_RANGE_DPS);
    out->valid = true;
    return true;
}
#endif // PICO_BUILD
