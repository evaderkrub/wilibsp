#include "sensors/bmm350_comp.h"

static int fix_sign(int v, int bits) {
    int p = (bits == 8) ? 128 : (bits == 12) ? 2048 : (bits == 16) ? 32768 : 0;
    if (v >= p) v -= p * 2;
    return v;
}

int32_t bmm350_sign24(uint32_t raw) {
    return (raw >= 0x800000u) ? (int32_t)raw - 0x1000000 : (int32_t)raw;
}

// w index -> OTP address: 0:0x0D 1:0x0E 2:0x0F 3:0x10 4:0x11 5:0x12 6:0x13 7:0x14 8:0x15 9:0x16 10:0x18
void bmm350_parse_otp(const uint16_t w[11], bmm350_coeff_t *c) {
    int offx = (w[1] & 0x0FFF);
    int offy = ((w[1] & 0xF000) >> 4) + (w[2] & 0x00FF);
    int offz = (w[2] & 0x0F00) + (w[3] & 0x00FF);
    c->offx = (float)fix_sign(offx, 12);
    c->offy = (float)fix_sign(offy, 12);
    c->offz = (float)fix_sign(offz, 12);
    c->toffs = fix_sign(w[0] & 0x00FF, 8) / 5.0f;
    c->sensx = fix_sign((w[3] & 0xFF00) >> 8, 8) / 256.0f;
    c->sensy = fix_sign(w[4] & 0x00FF, 8) / 256.0f + 0.01f;
    c->sensz = fix_sign((w[4] & 0xFF00) >> 8, 8) / 256.0f;
    c->tsens = fix_sign((w[0] & 0xFF00) >> 8, 8) / 512.0f;
    c->tcox = fix_sign(w[5] & 0x00FF, 8) / 32.0f;
    c->tcoy = fix_sign(w[6] & 0x00FF, 8) / 32.0f;
    c->tcoz = fix_sign(w[7] & 0x00FF, 8) / 32.0f;
    c->tcsx = fix_sign((w[5] & 0xFF00) >> 8, 8) / 16384.0f;
    c->tcsy = fix_sign((w[6] & 0xFF00) >> 8, 8) / 16384.0f;
    c->tcsz = fix_sign((w[7] & 0xFF00) >> 8, 8) / 16384.0f - 0.0001f;
    c->t0 = fix_sign(w[10], 16) / 512.0f + 23.0f;
    c->cxy = fix_sign(w[8] & 0x00FF, 8) / 800.0f;
    c->cyx = fix_sign((w[8] & 0xFF00) >> 8, 8) / 800.0f;
    c->czx = fix_sign(w[9] & 0x00FF, 8) / 800.0f;
    c->czy = fix_sign((w[9] & 0xFF00) >> 8, 8) / 800.0f;
}

void bmm350_compensate(int32_t rx, int32_t ry, int32_t rz, int32_t rt,
                       const bmm350_coeff_t *c, float *ox, float *oy, float *oz, float *otemp) {
    const float adc_gain = 1.0f / 1.5f;
    const float lut_gain = 0.714607238769531f;
    const float power = 1000000.0f / 1048576.0f;
    const float LSB_XY = power / (14.55f * 19.46f * adc_gain * lut_gain);
    const float LSB_Z  = power / (9.0f  * 31.0f  * adc_gain * lut_gain);
    const float LSB_T  = 1.0f / (0.00204f * adc_gain * lut_gain * 1048576.0f);

    float x = (float)rx * LSB_XY;
    float y = (float)ry * LSB_XY;
    float z = (float)rz * LSB_Z;
    float t = (float)rt * LSB_T;
    if (t > 0.0f) t -= 25.49f; else if (t < 0.0f) t += 25.49f;
    t = (1.0f + c->tsens) * t + c->toffs;

    x = x * (1.0f + c->sensx); x += c->offx; x += c->tcox * (t - c->t0); x /= (1.0f + c->tcsx * (t - c->t0));
    y = y * (1.0f + c->sensy); y += c->offy; y += c->tcoy * (t - c->t0); y /= (1.0f + c->tcsy * (t - c->t0));
    z = z * (1.0f + c->sensz); z += c->offz; z += c->tcoz * (t - c->t0); z /= (1.0f + c->tcsz * (t - c->t0));

    float den = 1.0f - c->cyx * c->cxy;
    *ox = (x - c->cxy * y) / den;
    *oy = (y - c->cyx * x) / den;
    *oz = z + (x * (c->cyx * c->czy - c->czx) - y * (c->czy - c->cxy * c->czx)) / den;
    *otemp = t;
}
