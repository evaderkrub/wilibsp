// src/sensors/sht40.c — Sensirion SHT40 temp/humidity over I2C1 (0x44).
#include "sensors/sht40.h"
#ifdef PICO_BUILD
#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include "platform/diag.h"
#define SHT40_I2C  i2c1
#endif

#define SHT40_ADDR        0x44
#define SHT40_CMD_MEASURE 0xFD   // measure, high precision (~8.2 ms)
#define SHT40_CMD_SERIAL  0x89   // read serial number (presence check)

static uint8_t sht40_crc8(const uint8_t *d, int n) {
    uint8_t c = 0xFF;
    for (int i = 0; i < n; i++) {
        c ^= d[i];
        for (int b = 0; b < 8; b++)
            c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x31) : (uint8_t)(c << 1);
    }
    return c;
}

bool sht40_convert(const uint8_t raw[6], float *temp_c, float *rh_pct, bool *crc_ok) {
    bool ok = (sht40_crc8(&raw[0], 2) == raw[2]) && (sht40_crc8(&raw[3], 2) == raw[5]);
    *crc_ok = ok;
    uint16_t t_ticks  = (uint16_t)((raw[0] << 8) | raw[1]);
    uint16_t rh_ticks = (uint16_t)((raw[3] << 8) | raw[4]);
    *temp_c = -45.0f + 175.0f * (float)t_ticks / 65535.0f;
    float rh = -6.0f + 125.0f * (float)rh_ticks / 65535.0f;
    if (rh < 0.0f) rh = 0.0f; else if (rh > 100.0f) rh = 100.0f;
    *rh_pct = rh;
    return ok;
}

#ifdef PICO_BUILD
bool sht40_init(void) {
    uint8_t cmd = SHT40_CMD_SERIAL, sn[6];
    bool ok = (i2c_write_blocking(SHT40_I2C, SHT40_ADDR, &cmd, 1, false) == 1);
    sleep_ms(2);
    ok = ok && (i2c_read_blocking(SHT40_I2C, SHT40_ADDR, sn, 6, false) == 6);
    DIAG("sht40: init %s\n", ok ? "ok" : "NAK");
    return ok;
}

bool sht40_read(sht40_reading_t *out) {
    out->valid = false;
    uint8_t cmd = SHT40_CMD_MEASURE;
    if (i2c_write_blocking(SHT40_I2C, SHT40_ADDR, &cmd, 1, false) != 1) return false;
    sleep_ms(10);   // high-precision conversion time
    uint8_t raw[6];
    if (i2c_read_blocking(SHT40_I2C, SHT40_ADDR, raw, 6, false) != 6) return false;
    bool crc;
    sht40_convert(raw, &out->temp_c, &out->rh_pct, &crc);
    out->valid = crc;
    return crc;
}
#endif // PICO_BUILD
