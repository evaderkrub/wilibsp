// audio_glue.h — binds the modem library to the FW2 audio hardware.
// TX: render whole air frame into PSRAM, play once via the streamed I2S DMA loop,
//     stop on a duration deadline (the stream API loops, so the buffer carries
//     >=256 ms of trailing silence as stop slack).
// RX: core 1 exclusively pulls the PDM mics and runs demod -> frame parser -> SPSC
//     queue. During own TX the samples are pulled and discarded (half duplex),
//     unless self-test mode is on.
#ifndef RC_AUDIO_GLUE_H
#define RC_AUDIO_GLUE_H
#include <stdint.h>
#include <stdbool.h>
#include "modem/frame.h"

bool audio_glue_init(void);
void audio_tx_text(uint8_t sender, const char *text);
bool audio_tx_busy(void);
int  audio_rx_pop(frame_msg_t *m);
unsigned audio_rx_crc_errors(void);
unsigned audio_rx_heartbeat(void);
unsigned audio_rx_qdrops(void);
int  audio_rx_peak(void);
// Bench diagnostics: cumulative PDM samples pulled, cumulative demod byte
// output, and max-held demod peak (clears on read).
unsigned audio_dbg_pcm_total(void);
unsigned audio_dbg_bytes_total(void);
int  audio_dbg_peak_max(void);
void audio_set_selftest(bool on);
bool audio_selftest(void);

#endif
