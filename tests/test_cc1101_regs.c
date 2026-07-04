#include "test_util.h"
#include "radio/cc1101_regs.h"

int main(void) {
    // 433.92 MHz: FREQ = round(433920000 * 65536 / 26000000) = 1093745 = 0x10B071
    cc1101_freq_regs_t r = cc1101_freq_to_regs(433920000u);
    ASSERT_EQ(r.f2, 0x10);
    ASSERT_EQ(r.f1, 0xB0);
    ASSERT_EQ(r.f0, 0x71);

    // RSSI -> dBm
    ASSERT_EQ(cc1101_rssi_to_dbm(0),   -74);   // 0/2 - 74
    ASSERT_EQ(cc1101_rssi_to_dbm(96),  -26);   // 96/2 - 74 = 48-74
    ASSERT_EQ(cc1101_rssi_to_dbm(128), -138);  // (128-256)/2 - 74 = -64-74
    ASSERT_EQ(cc1101_rssi_to_dbm(129), -137);  // odd negative: (129-256)/2 = -63 (trunc) - 74

    // Modulation -> MDMCFG2 MOD_FORMAT bits
    ASSERT_EQ(cc1101_mdmcfg2_mod_bits(CC1101_MOD_2FSK),    0x00);
    ASSERT_EQ(cc1101_mdmcfg2_mod_bits(CC1101_MOD_GFSK),    0x10);
    ASSERT_EQ(cc1101_mdmcfg2_mod_bits(CC1101_MOD_ASK_OOK), 0x30);
    ASSERT_EQ(cc1101_mdmcfg2_mod_bits(CC1101_MOD_4FSK),    0x40);
    ASSERT_EQ(cc1101_mdmcfg2_mod_bits(CC1101_MOD_MSK),     0x70);

    // Band limits (interior, gaps, and inclusive boundaries)
    ASSERT_TRUE(cc1101_freq_in_band(433920000u));
    ASSERT_TRUE(cc1101_freq_in_band(868000000u));
    ASSERT_TRUE(cc1101_freq_in_band(300000000u));  // band-1 lower boundary
    ASSERT_TRUE(cc1101_freq_in_band(348000000u));  // band-1 upper boundary
    ASSERT_TRUE(!cc1101_freq_in_band(360000000u)); // 348-387 gap
    ASSERT_TRUE(!cc1101_freq_in_band(500000000u)); // gap between 464 and 779
    ASSERT_TRUE(!cc1101_freq_in_band(950000000u)); // above 928
    ASSERT_TRUE(!cc1101_freq_in_band(100000000u)); // below range
    TEST_RETURN();
}
