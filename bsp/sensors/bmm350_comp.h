#ifndef BMM350_COMP_H
#define BMM350_COMP_H
#include <stdint.h>

// Per-part OTP-derived compensation coefficients (see fwcom setup_otp.rthon).
typedef struct {
    float offx, offy, offz, toffs;
    float sensx, sensy, sensz, tsens;
    float tcox, tcoy, tcoz;
    float tcsx, tcsy, tcsz;
    float t0;
    float cxy, cyx, czx, czy;
} bmm350_coeff_t;

// Sign-correct a 24-bit raw value (>= 2^23 -> negative).
int32_t bmm350_sign24(uint32_t raw);

// Parse the 11 OTP words (addresses 0x0D,0x0E,0x0F,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x18)
// into compensation coefficients. (== fwcom kParseBody.)
void bmm350_parse_otp(const uint16_t w[11], bmm350_coeff_t *c);

// Apply LSB scaling + temperature + per-axis + cross-axis compensation to raw counts.
// Outputs compensated x/y/z in microtesla and temperature in C. (== fwcom kCompBody.)
void bmm350_compensate(int32_t rx, int32_t ry, int32_t rz, int32_t rt,
                       const bmm350_coeff_t *c, float *x, float *y, float *z, float *temp);
#endif
