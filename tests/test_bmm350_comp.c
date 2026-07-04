// tests/test_bmm350_comp.c — host tests for the BMM350 pure compensation math
// (bmm350_comp.c is 100% pure: sign24, OTP parse, compensation pipeline).
#include "sensors/bmm350_comp.h"
#include "test_util.h"

int main(void) {
    // 1. sign24 boundaries.
    ASSERT_EQ(bmm350_sign24(0u), 0);
    ASSERT_EQ(bmm350_sign24(0x7FFFFFu), 8388607);
    ASSERT_EQ(bmm350_sign24(0x800000u), -8388608);
    ASSERT_EQ(bmm350_sign24(0xFFFFFFu), -1);

    // 2. OTP parse of all-zero words: every coefficient hits its formula's
    //    zero-input value (incl. the hard-coded biases sensy=+0.01,
    //    tcsz=-0.0001, t0=23.0).
    uint16_t w0[11] = {0};
    bmm350_coeff_t c;
    bmm350_parse_otp(w0, &c);
    ASSERT_NEAR(c.offx, 0.0f, 1e-9);  ASSERT_NEAR(c.offy, 0.0f, 1e-9);
    ASSERT_NEAR(c.offz, 0.0f, 1e-9);  ASSERT_NEAR(c.toffs, 0.0f, 1e-9);
    ASSERT_NEAR(c.sensx, 0.0f, 1e-9); ASSERT_NEAR(c.sensy, 0.01f, 1e-6);
    ASSERT_NEAR(c.sensz, 0.0f, 1e-9); ASSERT_NEAR(c.tsens, 0.0f, 1e-9);
    ASSERT_NEAR(c.tcsz, -0.0001f, 1e-7);
    ASSERT_NEAR(c.t0, 23.0f, 1e-5);
    ASSERT_NEAR(c.cxy, 0.0f, 1e-9);   ASSERT_NEAR(c.czy, 0.0f, 1e-9);

    // 3. OTP parse spot values: w[1]=0x0FFF -> offx = fix_sign(0xFFF,12) = -1;
    //    w[10]=0x0200 -> t0 = 512/512 + 23 = 24.
    uint16_t w1[11] = {0};
    w1[1] = 0x0FFF; w1[10] = 0x0200;
    bmm350_parse_otp(w1, &c);
    ASSERT_NEAR(c.offx, -1.0f, 1e-6);
    ASSERT_NEAR(c.t0, 24.0f, 1e-5);

    // 4. Compensation with the all-zero-OTP coefficients:
    bmm350_parse_otp(w0, &c);
    float x, y, z, t;
    //    4a. All-zero raw -> all-zero outputs (temp 0 too).
    bmm350_compensate(0, 0, 0, 0, &c, &x, &y, &z, &t);
    ASSERT_NEAR(x, 0.0f, 1e-6); ASSERT_NEAR(y, 0.0f, 1e-6);
    ASSERT_NEAR(z, 0.0f, 1e-6); ASSERT_NEAR(t, 0.0f, 1e-6);
    //    4b. Linearity in x (offsets/tcs zero for x): doubling rx doubles x.
    float x1, x2, dummy;
    bmm350_compensate(1000000, 0, 0, 0, &c, &x1, &y, &z, &t);
    bmm350_compensate(2000000, 0, 0, 0, &c, &x2, &y, &z, &t);
    ASSERT_NEAR(x2, 2.0f * x1, 1e-3 * (x2 > 0 ? x2 : -x2) + 1e-6);
    //    4c. The sensy=+0.01 bias: equal rx=ry -> y = 1.01 * x (same LSB_XY).
    bmm350_compensate(1000000, 1000000, 0, 0, &c, &x, &y, &dummy, &t);
    ASSERT_NEAR(y, 1.01f * x, 1e-3);
    //    4d. x scaling is positive and sane: rx=2^20 -> x = LSB_XY * 2^20,
    //        which must land in (0, 10000) uT for this LSB definition.
    ASSERT_TRUE(x1 > 0.0f && x1 < 10000.0f);

    TEST_RETURN();
}
