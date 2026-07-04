// tests/test_ook_tx.c
#include "test_util.h"
#include "radio/ook_tx.h"

int main(void) {
    ASSERT_EQ((int)ook_tx_clamp_us(0), 0);
    ASSERT_EQ((int)ook_tx_clamp_us(50000), 50000);
    ASSERT_EQ((int)ook_tx_clamp_us(OOK_TX_MAX_US), (int)OOK_TX_MAX_US);
    ASSERT_EQ((int)ook_tx_clamp_us(OOK_TX_MAX_US + 1u), (int)OOK_TX_MAX_US);
    ASSERT_EQ((int)ook_tx_clamp_us(0xFFFFFFFFu), (int)OOK_TX_MAX_US);
    TEST_RETURN();
}
