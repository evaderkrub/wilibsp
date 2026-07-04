// src/sensors/bmm350.c — Bosch BMM350 over I2C1 (0x14). Protocol + compensation ported from
// the hardware-validated FreeWili fwcom docs (bmm350-protocol-notes.md, setup_otp/loop_compensation).
#include "sensors/bmm350.h"
#include "sensors/bmm350_comp.h"
#include <math.h>
#ifdef PICO_BUILD
#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include "platform/diag.h"
#define BMM350_I2C   i2c1
#endif

#define BMM350_ADDR        0x14
#define REG_CHIPID         0x00
#define REG_PMU_CMD        0x06
#define REG_AGGR           0x04
#define REG_AXIS_EN        0x05
#define REG_CMD            0x7E
#define REG_OTP_CMD        0x50
#define REG_OTP_DATA       0x52   // MSB at 0x52, LSB at 0x53
#define REG_MAGDATA        0x31
#define CHIPID_VAL         0x33

#ifdef PICO_BUILD
static bmm350_coeff_t s_coeff;

static bool reg_write(uint8_t reg, uint8_t val) {
    uint8_t b[2] = { reg, val };
    return i2c_write_blocking(BMM350_I2C, BMM350_ADDR, b, 2, false) == 2;
}
#define BMM350_DUMMY 2   // BMM350 I2C reads return 2 leading dummy bytes (confirmed on HW)
static bool reg_read(uint8_t reg, uint8_t *out, int n) {
    uint8_t buf[BMM350_DUMMY + 16];
    if (i2c_write_blocking(BMM350_I2C, BMM350_ADDR, &reg, 1, true) != 1) return false;
    if (i2c_read_blocking(BMM350_I2C, BMM350_ADDR, buf, n + BMM350_DUMMY, false) != n + BMM350_DUMMY)
        return false;
    for (int i = 0; i < n; i++) out[i] = buf[BMM350_DUMMY + i];
    return true;
}

bool bmm350_init(void) {
    reg_write(REG_CMD, 0xB6); sleep_ms(24);            // soft reset
    uint8_t id = 0; reg_read(REG_CHIPID, &id, 1);
    bool ok = (id == CHIPID_VAL);

    // OTP download: 11 words at these addresses, in order, into w[0..10].
    static const uint8_t otp_addr[11] =
        { 0x0D,0x0E,0x0F,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x18 };
    uint16_t w[11];
    for (int i = 0; i < 11; i++) {
        reg_write(REG_OTP_CMD, (uint8_t)(0x20 | otp_addr[i]));
        sleep_ms(1);
        uint8_t d[2] = { 0, 0 };
        reg_read(REG_OTP_DATA, d, 2);
        w[i] = (uint16_t)((d[0] << 8) | d[1]);          // MSB,LSB
        sleep_ms(1);
    }
    bmm350_parse_otp(w, &s_coeff);
    reg_write(REG_OTP_CMD, 0x80); sleep_ms(1);          // OTP power off

    reg_write(REG_PMU_CMD, 0x07); sleep_ms(14);         // magnetic reset: bit reset
    reg_write(REG_PMU_CMD, 0x05); sleep_ms(18);         // flux-guide reset
    reg_write(REG_AGGR,    0x34); sleep_ms(2);          // ODR 100 Hz + averaging
    reg_write(REG_PMU_CMD, 0x02); sleep_ms(10);         // UPD_OAE (apply ODR/avg) — let PMU settle
    reg_write(REG_AXIS_EN, 0x07); sleep_ms(2);          // enable X,Y,Z
    reg_write(REG_PMU_CMD, 0x01); sleep_ms(40);         // normal mode

    DIAG("bmm350: chipid=0x%02x %s\n", id, ok ? "ok" : "??");
    return ok;
}

bool bmm350_read(bmm350_reading_t *out) {
    out->valid = false;
    uint8_t b[12];
    if (!reg_read(REG_MAGDATA, b, 12)) return false;    // X,Y,Z,Temp (24-bit each)
    int32_t rx = bmm350_sign24((uint32_t)b[0]  | (b[1]  << 8) | (b[2]  << 16));
    int32_t ry = bmm350_sign24((uint32_t)b[3]  | (b[4]  << 8) | (b[5]  << 16));
    int32_t rz = bmm350_sign24((uint32_t)b[6]  | (b[7]  << 8) | (b[8]  << 16));
    int32_t rt = bmm350_sign24((uint32_t)b[9]  | (b[10] << 8) | (b[11] << 16));
    bmm350_compensate(rx, ry, rz, rt, &s_coeff, &out->mx, &out->my, &out->mz, &out->temp_c);
    out->magnitude = sqrtf(out->mx * out->mx + out->my * out->my + out->mz * out->mz);
    out->valid = true;
    return true;
}
#endif // PICO_BUILD
