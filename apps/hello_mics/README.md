# hello_mics

On-hardware smoke test for the 4-channel PDM microphone driver
(`bsp/pdm/pdm_capture`). RTT-only — no display.

    fw build hello_mics
    fw flash hello_mics
    fw rtt

Prints per-mic RMS + peak (after DC-blocking) ~3x/s. Expected:

- All 4 channels alive with low ambient noise at rest.
- Tapping / speaking directly at each mic position spikes that channel.
  Physical left-to-right order is **D, B, A, C** (bench-measured in the
  source `microphonearray` repo), not A-D.
- A channel stuck at rms=0 (or frozen values) indicates the RP2350
  pad-isolation trap or the MIC_PWR expander rail regressed.
