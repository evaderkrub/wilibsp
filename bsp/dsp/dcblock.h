// src/dsp/dcblock.h — one-pole DC-blocking high-pass for int16 PCM.
// y[n] = x[n] - x[n-1] + R*y[n-1]. R=0.90 -> cutoff ~250 Hz @ 16 kHz, keeps 1 kHz.
// The FW2 PDM idle stream is heavily DC-biased and the room/handling carries
// strong sub-200 Hz energy; a gentle blocker leaves a leakage skirt that floods
// the low FFT bins and crushes the tone SNR. An aggressive high-pass removes it.
// Applied in the analysis path only (not the CIC).
#ifndef DCBLOCK_H
#define DCBLOCK_H
#include <stdint.h>

#ifndef DCBLOCK_R
#define DCBLOCK_R 0.90f
#endif

// In-place high-pass filter `n` samples of `buf`. Stateless across calls
// (each block re-initialises from buf[0]).
void dcblock_inplace(int16_t* buf, unsigned n);

#endif // DCBLOCK_H
