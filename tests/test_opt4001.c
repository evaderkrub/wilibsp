// tests/test_opt4001.c — host tests for the OPT4001 lux conversion (pure).
// lux = (mantissa << exponent) * 437.5e-6 (package-dependent factor).
#include "sensors/opt4001.h"
#include "test_util.h"

int main(void) {
    // 1. Zero mantissa -> 0 lux regardless of exponent.
    ASSERT_NEAR(opt4001_lux(0, 0), 0.0f, 1e-9);
    ASSERT_NEAR(opt4001_lux(12, 0), 0.0f, 1e-9);

    // 2. Unit mantissa at exponent 0 = the raw factor.
    ASSERT_NEAR(opt4001_lux(0, 1), 437.5e-6f, 1e-9);

    // 3. Exponent property: each +1 exponent doubles the lux.
    ASSERT_NEAR(opt4001_lux(1, 12345), 2.0f * opt4001_lux(0, 12345), 1e-4);
    ASSERT_NEAR(opt4001_lux(8, 999), 256.0f * opt4001_lux(0, 999), 1e-2);

    // 4. Spot value: (1000 << 5) * 437.5e-6 = 32000 * 437.5e-6 = 14.0 lux.
    ASSERT_NEAR(opt4001_lux(5, 1000), 14.0f, 1e-3);

    TEST_RETURN();
}
