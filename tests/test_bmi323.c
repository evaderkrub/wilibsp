// tests/test_bmi323.c — host tests for the BMI323 scale conversions (pure).
// value = raw / 32768 * range. Driver configures ±4 g and ±500 dps.
#include "sensors/bmi323.h"
#include "test_util.h"

int main(void) {
    // 1. Zero raw -> zero.
    ASSERT_NEAR(bmi323_accel_g(0, 4), 0.0f, 1e-9);
    ASSERT_NEAR(bmi323_gyro_dps(0, 500), 0.0f, 1e-9);

    // 2. Negative full-scale is exact: -32768/32768 * range = -range.
    ASSERT_NEAR(bmi323_accel_g(-32768, 4), -4.0f, 1e-6);
    ASSERT_NEAR(bmi323_gyro_dps(-32768, 500), -500.0f, 1e-4);

    // 3. Positive full-scale: 32767/32768 * range (one LSB shy of +range).
    ASSERT_NEAR(bmi323_accel_g(32767, 4), 3.99988f, 1e-3);
    ASSERT_NEAR(bmi323_gyro_dps(32767, 500), 499.985f, 0.1);

    // 4. Midpoint: 16384/32768 = 0.5 of range.
    ASSERT_NEAR(bmi323_accel_g(16384, 4), 2.0f, 1e-5);
    ASSERT_NEAR(bmi323_gyro_dps(16384, 500), 250.0f, 1e-3);

    // 5. Range parameter is respected (±2 g variant).
    ASSERT_NEAR(bmi323_accel_g(16384, 2), 1.0f, 1e-5);

    TEST_RETURN();
}
