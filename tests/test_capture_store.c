// tests/test_capture_store.c
#include "test_util.h"
#include "radio/capture_store.h"

int main(void) {
    static uint32_t buf[8];
    capture_store_init(buf, 8);
    ASSERT_EQ((int)capture_store_len(), 0);
    ASSERT_TRUE(!capture_store_full());

    capture_store_begin(433920000u, CC1101_MOD_ASK_OOK, true, 7);
    ASSERT_EQ((long)capture_store_freq(), 433920000L);
    ASSERT_EQ((int)capture_store_mod(), (int)CC1101_MOD_ASK_OOK);
    ASSERT_TRUE(capture_store_start_level());
    ASSERT_EQ(capture_store_antenna(), 7);

    uint32_t a[3] = { 10, 20, 30 };
    ASSERT_EQ((int)capture_store_append(a, 3), 3);
    ASSERT_EQ((int)capture_store_len(), 3);
    ASSERT_EQ((int)capture_store_total_ticks(), 60);
    ASSERT_EQ((int)capture_store_data()[1], 20);

    // Append crossing capacity: room = 5, offer 6 -> takes 5, now full.
    uint32_t b[6] = { 1, 2, 3, 4, 5, 6 };
    ASSERT_EQ((int)capture_store_append(b, 6), 5);
    ASSERT_EQ((int)capture_store_len(), 8);
    ASSERT_TRUE(capture_store_full());
    ASSERT_EQ((int)capture_store_append(a, 1), 0);        // no room left
    ASSERT_EQ((int)capture_store_total_ticks(), 75);      // 60 + 1+2+3+4+5

    // begin() resets length, ticks, and metadata.
    capture_store_begin(315000000u, CC1101_MOD_2FSK, false, 3);
    ASSERT_EQ((int)capture_store_len(), 0);
    ASSERT_EQ((int)capture_store_total_ticks(), 0);
    ASSERT_TRUE(!capture_store_full());
    ASSERT_TRUE(!capture_store_start_level());
    ASSERT_EQ((int)capture_store_mod(), (int)CC1101_MOD_2FSK);
    ASSERT_EQ(capture_store_antenna(), 3);
    TEST_RETURN();
}
