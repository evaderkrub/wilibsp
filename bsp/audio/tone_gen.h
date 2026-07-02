// src/audio/tone_gen.h — pure sine sample generator for the codec tone source.
#ifndef TONE_GEN_H
#define TONE_GEN_H
#include <stdint.h>
void tone_gen_fill(int16_t* buf, unsigned n, float hz, float fs, float* phase);
#endif
