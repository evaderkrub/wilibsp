// src/audio/codec_nau88c10.h — NAU88C10 codec bring-up over I2C1.
#ifndef CODEC_NAU88C10_H
#define CODEC_NAU88C10_H

#include <stdbool.h>

// Init I2C1 (GPIO 26/27, 400 kHz) and write the full register sequence:
// I2S 16-bit slave, MCLK-direct (no PLL), 16 kHz filters, mono speaker out.
// I2C failures DIAG the failing register and continue (video must still play).
void codec_nau88c10_init(void);

// Bring-up diagnostic (the vendor readID() equivalent): DIAG-dump registers
// 0x00..0x45. Proves the codec answers on the bus; reg 0x3F should read
// nonzero (silicon revision) on a live part.
void codec_nau88c10_dump(void);

// DAC soft-mute (DACMT, reg 0x0A bit 6): ramps the DAC down/up to suppress
// pops while the I2S clocks stop/start (file-swap seam, boot, abort).
void codec_nau88c10_dac_mute(bool mute);

// Audio output destination. SPEAKER is the init default (mono speaker out).
typedef enum { CODEC_OUT_SPEAKER = 0, CODEC_OUT_HEADPHONE = 1 } codec_out_t;

// Route the DAC to the speaker or the headphone/line output by reprogramming
// the NAU88C10 output mixer/enables (regs 0x36-0x38, 0x45). Safe to call
// during playback. If headphone is not physically wired this still updates the
// codec state (the UI reflects it); on-device validation is pending.
void codec_nau88c10_set_output(codec_out_t out);

// Log the output-routing registers (R3/0x03, R54/0x36 spk vol, R56/0x38 HP enable,
// R69/0x45 spk boost) over RTT — used to confirm a set_output() switch took effect.
void codec_nau88c10_log_output(void);

// Boot-time bring-up check for the MIC/ADC path: returns true iff the part
// answers (reg 0x3F silicon revision != 0) AND reg 0x02 == 0x0015 (ADCEN+PGAEN+
// BSTEN). DIAGs each value. Call after codec_nau88c10_init().
bool codec_nau88c10_input_ok(void);

#endif
