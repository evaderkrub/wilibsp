// Host test: CRC-16/CCITT-FALSE known-answer vectors.
#include "crc16.h"
#include "test_util.h"

int main(void) {
    ASSERT_EQ(crc16_ccitt((const uint8_t *)"123456789", 9), 0x29B1); // canonical check value
    ASSERT_EQ(crc16_ccitt((const uint8_t *)"", 0), 0xFFFF);          // init value untouched
    ASSERT_EQ(crc16_ccitt((const uint8_t *)"A", 1), 0xB915);
    TEST_RETURN();
}
