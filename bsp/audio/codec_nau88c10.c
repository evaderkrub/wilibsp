// src/audio/codec_nau88c10.c — register sequence vendored from the proven FW2
// driver (rpAudioCodecnau88c10.cpp). One change: reg 0x07 SMPLR = 16 kHz
// (vendor ran 8 kHz = 0x000a). Codec is an I2S slave clocked MCLK-direct
// (reg 0x06 = 0x0000): we must supply MCLK = 256*fs (PWM, see audio_i2s.c).
#include "audio/codec_nau88c10.h"
#include "platform/board.h"
#include "platform/diag.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

#define CODEC_ADDR 26   // 0x1A, from the vendor driver

static void codec_write(uint8_t reg, uint16_t val) {
    // NAU88C10 register write: 7-bit register + 9-bit value packed as
    // byte0 = reg<<1 | val[8], byte1 = val[7:0] (vendor CODEC_IO_Write).
    uint8_t msg[2] = { (uint8_t)((reg << 1) | ((val >> 8) & 1)), (uint8_t)(val & 0xFF) };
    int rc = i2c_write_blocking(i2c1, CODEC_ADDR, msg, 2, false);
    if (rc != 2) DIAG("codec: i2c write reg 0x%02x failed (%d)\n", reg, rc);
}

void codec_nau88c10_init(void) {
    gpio_set_function(PIN_I2C1_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_I2C1_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_I2C1_SDA);
    gpio_pull_up(PIN_I2C1_SCL);
    i2c_init(i2c1, 400 * 1000);

    codec_write(0x00, 0x0000);   // software reset
    sleep_ms(10);
    // Power management
    codec_write(0x01, 0x015d);
    codec_write(0x02, 0x0015);
    codec_write(0x03, 0x00ED);
    // Audio control: 16-bit, slave, MCLK direct. AIFMT = LEFT-JUSTIFIED (0x08),
    // not I2S-standard (0x10): I2S-standard delays the MSB one BCLK after LRCK,
    // but our RX PIO (i2s_rx.pio) samples 16 bits starting AT the LRCK edge.
    // Left-justified puts the MSB on that first BCLK so the captured word aligns
    // (else every sample reads as real>>1 and the mic rails at full scale).
    codec_write(0x04, 0x0008);
    codec_write(0x05, 0x0000);
    codec_write(0x06, 0x0000);
    codec_write(0x07, 0x0006);   // SMPLR = 16 kHz (vendor 8 kHz was 0x000a)
    codec_write(0x08, 0x0000);
    codec_write(0x09, 0x0000);
    codec_write(0x0a, 0x0040);   // start DAC soft-muted; audio_stream_go() unmutes
    codec_write(0x0b, 0x00ff);   // DAC volume full scale
    codec_write(0x0c, 0x0000);
    codec_write(0x0d, 0x0000);
    codec_write(0x0e, 0x0108);
    codec_write(0x0f, 0x01ff);
    // Equalizer
    codec_write(0x12, 0x012c);
    codec_write(0x13, 0x002c);
    codec_write(0x14, 0x002c);
    codec_write(0x15, 0x002c);
    codec_write(0x16, 0x002c);
    // DAC limiter
    codec_write(0x18, 0x0032);
    codec_write(0x19, 0x0000);
    // Notch filter
    codec_write(0x1b, 0x0000);
    codec_write(0x1c, 0x0000);
    codec_write(0x1d, 0x0000);
    codec_write(0x1e, 0x0000);
    // ALC control
    codec_write(0x20, 0x0038);
    codec_write(0x21, 0x000b);
    codec_write(0x22, 0x0032);
    codec_write(0x23, 0x0000);
    // PLL (configured like the vendor but unused: reg 0x06 selects MCLK direct)
    codec_write(0x24, 0x0008);
    codec_write(0x25, 0x000c);
    codec_write(0x26, 0x0093);
    codec_write(0x27, 0x00e9);
    // Bypass
    codec_write(0x28, 0x0000);
    // Input/output mixer
    codec_write(0x2c, 0x0003);
    codec_write(0x2d, 0x0010);
    codec_write(0x2e, 0x0000);
    codec_write(0x2f, 0x0100);
    codec_write(0x30, 0x0000);
    codec_write(0x31, 0x0002);
    codec_write(0x32, 0x0001);
    codec_write(0x33, 0x0000);
    codec_write(0x34, 0x0040);
    codec_write(0x35, 0x0040);
    codec_nau88c10_set_output(CODEC_OUT_SPEAKER);

    DIAG("codec: nau88c10 init done (16 kHz, speaker)\n");
}

// Output routing for the NAU88C10 on the FreeWili-2 board, derived EMPIRICALLY
// on-device (2026-06-14), not from a datasheet (reg 0x45 puts this beyond the
// NAU8810 map). Findings from A/B testing on the bench:
//   - reg 0x38 is the headphone enable: 0x04 = HP OFF (speaker only),
//     0x01 = HP ON. (0x38 does NOT gate the speaker.)
//   - The speaker plays whenever R3 (0x03) has its speaker bits set; init
//     leaves R3 = 0xED so the speaker is always on unless we clear it.
//   - The headphone shares the speaker output amp: clearing the R3 output-driver
//     bits (tried R3=0x2D / 0x29) silenced BOTH. So the amp (R3=0xED) must stay
//     on for headphone, and the speaker is silenced via its own volume register.
//   - reg 0x36 is the speaker volume (post-split): muting it silences the
//     speaker while the shared amp + headphone (0x38=0x01) stay alive.
// Speaker-only  = R3 0xED + 0x36 0x3F (spk vol) + 0x38 0x04 (HP off).  [proven]
// Headphone-only= R3 0xED + 0x36 0x40 (spk muted) + 0x38 0x01 (HP on).
#define NAU_38_HP_OFF       0x0004u   // headphone amp off (speaker-only)
#define NAU_38_HP_ON        0x0001u   // headphone amp on
#define NAU_36_SPK_FULL     0x003Fu   // speaker volume full scale
#define NAU_36_SPK_MUTE     0x0040u   // speaker volume mute (bit6)

void codec_nau88c10_set_output(codec_out_t out) {
    if (out == CODEC_OUT_SPEAKER) {
        codec_write(0x03, 0x00ED);             // output amp + mixer enabled
        codec_write(0x36, NAU_36_SPK_FULL);    // speaker volume full scale
        codec_write(0x37, 0x0000);             // mono/headphone mixer path off
        codec_write(0x38, NAU_38_HP_OFF);      // headphone amp off
        codec_write(0x45, 0x0005);             // 5V speaker boost
        DIAG("codec: output -> speaker only\n");
    } else {
        codec_write(0x03, 0x00ED);             // keep shared output amp on (HP needs it)
        codec_write(0x36, NAU_36_SPK_MUTE);    // mute the speaker (HP unaffected)
        codec_write(0x37, 0x0000);
        codec_write(0x38, NAU_38_HP_ON);       // headphone amp on
        codec_write(0x45, 0x0000);             // no 5V speaker boost on headphone
        DIAG("codec: output -> headphone only\n");
    }
}

void codec_nau88c10_dump(void) {
    // Vendor readID(): register N is read by addressing byte N<<1 (9-bit regs).
    for (uint8_t reg = 0; reg <= 0x45; reg++) {
        uint8_t addr_byte = (uint8_t)(reg << 1);
        uint8_t val[2] = { 0, 0 };
        int rc = i2c_write_blocking(i2c1, CODEC_ADDR, &addr_byte, 1, true);
        if (rc != 1) { DIAG("codec: dump write reg 0x%02x failed (%d)\n", reg, rc); return; }
        rc = i2c_read_blocking(i2c1, CODEC_ADDR, val, 2, false);
        if (rc != 2) { DIAG("codec: dump read 0x%02x failed (%d)\n", reg, rc); return; }
        DIAG("codec: reg 0x%02x = 0x%03x\n", reg, (unsigned)(((val[0] & 0x01) << 8) | val[1]));
    }
}

void codec_nau88c10_dac_mute(bool mute) {
    codec_write(0x0a, mute ? 0x0040 : 0x0000);   // DACMT
}

static uint16_t codec_read(uint8_t reg) {
    uint8_t addr_byte = (uint8_t)(reg << 1);
    uint8_t val[2] = { 0, 0 };
    if (i2c_write_blocking(i2c1, CODEC_ADDR, &addr_byte, 1, true) != 1) return 0xFFFF;
    if (i2c_read_blocking(i2c1, CODEC_ADDR, val, 2, false) != 2) return 0xFFFF;
    return (uint16_t)(((val[0] & 0x01) << 8) | val[1]);
}

void codec_nau88c10_log_output(void) {
    // Read back the output-routing regs. Note 0x45 is beyond the NAU8810 map and may
    // not read back its written value; 0x03/0x36/0x38 are the meaningful confirmation.
    DIAG("codec: out regs R3=0x%03x R36=0x%03x R38=0x%03x R45=0x%03x\n",
         (unsigned)codec_read(0x03), (unsigned)codec_read(0x36),
         (unsigned)codec_read(0x38), (unsigned)codec_read(0x45));
}

bool codec_nau88c10_input_ok(void) {
    uint16_t rev = codec_read(0x3F);
    uint16_t pm2 = codec_read(0x02);
    DIAG("codec: rev(0x3F)=0x%03x pm2(0x02)=0x%03x\n", (unsigned)rev, (unsigned)pm2);
    bool ok = (rev != 0 && rev != 0xFFFF) && (pm2 == 0x0015);
    if (!ok) DIAG("codec: INPUT PATH NOT READY (expect rev!=0, pm2=0x015)\n");
    return ok;
}
