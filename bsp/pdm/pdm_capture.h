// src/pdm/pdm_capture.h — 4-channel PDM capture (2 data lines x 2 clock phases).
#ifndef PDM_CAPTURE_H
#define PDM_CAPTURE_H
#include <stdint.h>

#define PDM_NUM_MICS 4
enum { MIC_A = 0, MIC_B, MIC_C, MIC_D };

void pdm_capture_init(void);

// Non-blocking: decimate all raw PDM available between the read cursor and the
// live DMA write cursor, writing up to `max` PCM samples per mic into
// out[ch][0..]. Returns the count written (0 if nothing new). SINGLE consumer
// only (shared CIC state) — call from one core. The capture DMA free-runs, so
// successive pulls return a gap-free stream.
unsigned pdm_capture_pull(int16_t* out[PDM_NUM_MICS], unsigned max);

// Blocking convenience: spin on pdm_capture_pull() until `frames` collected.
unsigned pdm_capture_block(int16_t* out[PDM_NUM_MICS], unsigned frames);

#endif // PDM_CAPTURE_H
