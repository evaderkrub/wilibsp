# PDM microphone driver (`bsp/pdm/`, `bsp/dsp/`)

4-channel PDM MEMS microphone capture: one PIO state machine (pio1) drives the
shared 1.024 MHz mic clock (GPIO 28) and samples two data lines (GPIO 29/30)
mid-phase on both clock levels — 2 mics per line × 2 lines. A free-running
DMA channel (write-address ring, **no IRQ**) streams raw PDM words into a
32 KiB ring; `pdm_capture_pull()` decimates through four integer 3rd-order
CIC filters (R=64) to gap-free 16 kHz int16 PCM per mic.

**These are NOT the I2S codec mic** (3.5 mm jack / NAU88C10, `bsp/audio/`) —
different hardware, different driver.

## API (`pdm/pdm_capture.h`)

- `pdm_capture_init()` — powers the mics (`ioexp_mic_pwr(true)` + 50 ms),
  claims a pio1 SM + one DMA channel, starts free-running capture.
  Precondition: `ioexp_init()` has run (`board_init()` does).
- `pdm_capture_pull(int16_t* out[PDM_NUM_MICS], unsigned max)` — non-blocking;
  decimates everything new, returns PCM frames written per mic (0 if none).
- `pdm_capture_block(out, frames)` — blocking convenience wrapper.

**Single consumer, one core** (shared CIC state). If the consumer stalls
> ~64 ms the ring is silently overwritten (no flag) — see facts.md.

## DSP helpers (`dsp/`)

- `dsp/cic.h` — 3rd-order CIC decimator, pure integer, host-tested
  (`tests/test_cic.c`). Full-scale DC plateaus at ±16384 (6 dB headroom).
- `dsp/dcblock.h` — one-pole DC-blocking high-pass (R=0.90, ~250 Hz cutoff
  @ 16 kHz), host-tested. The PDM idle stream is heavily DC-biased: run this
  on every block before RMS/FFT work. Analysis path only — not inside the CIC.

## Channel ↔ position map

`MIC_A..MIC_D` are line/phase indices (SIG1-high, SIG1-low, SIG2-high,
SIG2-low). Physical left-to-right order on the board is **D, B, A, C**
(bench-measured). Spatial DSP (DOA/beamforming — app domain, not BSP) must
remap; see the `microphonearray` repo's `PHYS[]` table.

## Demo

`apps/hello_mics` — per-mic RMS/peak over RTT; see its README for pass
criteria.
