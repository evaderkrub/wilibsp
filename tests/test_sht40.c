// tests/test_sht40.c — host tests for the SHT40 pure conversion (CRC + formulas).
#include "sensors/sht40.h"
#include "test_util.h"

// Independent CRC-8 (poly 0x31, init 0xFF) — same algorithm implemented
// separately so the test cross-checks the driver rather than echoing it.
static uint8_t crc8(uint8_t a, uint8_t b) {
    uint8_t d[2] = { a, b }, c = 0xFF;
    for (int i = 0; i < 2; i++) {
        c ^= d[i];
        for (int k = 0; k < 8; k++)
            c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x31) : (uint8_t)(c << 1);
    }
    return c;
}

int main(void) {
    float t, rh; bool crc;

    // 0. Anchor the CRC itself: Sensirion's documented vector CRC({0xBE,0xEF}) = 0x92.
    ASSERT_EQ(crc8(0xBE, 0xEF), 0x92);

    // 1. Known frame 0xBEEF/0xBEEF with valid CRCs: both formulas.
    //    t = -45 + 175*48879/65535 = 85.5177 C; rh = -6 + 125*48879/65535 = 87.2269 %.
    uint8_t f1[6] = { 0xBE, 0xEF, 0x92, 0xBE, 0xEF, 0x92 };
    ASSERT_TRUE(sht40_convert(f1, &t, &rh, &crc));
    ASSERT_TRUE(crc);
    ASSERT_NEAR(t, 85.5177f, 0.01);
    ASSERT_NEAR(rh, 87.2269f, 0.01);

    // 2. Corrupted CRC byte -> crc_ok false, convert returns false.
    uint8_t f2[6] = { 0xBE, 0xEF, 0x93, 0xBE, 0xEF, 0x92 };
    ASSERT_TRUE(!sht40_convert(f2, &t, &rh, &crc));
    ASSERT_TRUE(!crc);

    // 3. All-zero ticks: temp = -45 C exactly; raw rh = -6 % clamps to 0.
    uint8_t f3[6] = { 0x00, 0x00, crc8(0x00, 0x00), 0x00, 0x00, crc8(0x00, 0x00) };
    ASSERT_TRUE(sht40_convert(f3, &t, &rh, &crc));
    ASSERT_NEAR(t, -45.0f, 0.001);
    ASSERT_NEAR(rh, 0.0f, 0.001);

    // 4. All-ones ticks: temp = +130 C; raw rh = 119 % clamps to 100.
    uint8_t f4[6] = { 0xFF, 0xFF, crc8(0xFF, 0xFF), 0xFF, 0xFF, crc8(0xFF, 0xFF) };
    ASSERT_TRUE(sht40_convert(f4, &t, &rh, &crc));
    ASSERT_NEAR(t, 130.0f, 0.001);
    ASSERT_NEAR(rh, 100.0f, 0.001);

    TEST_RETURN();
}
