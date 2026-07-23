# retrochat — acoustic-modem broadcast chat

Sends chat messages as AFSK (Bell 202 tones: 1200 Hz mark / 2200 Hz space, at
300 baud) from the speaker; every Free-Wili 2 in earshot decodes them on its
PDM mics and shows them in a touch chat UI. Broadcast party line: no pairing,
fire-and-forget + CRC-16 (bad frames dropped, `E` counter in the status bar
ticks). It sounds exactly like you hope it does.

![RetroChat running in acoustic self-test: the typed message "hi" was sent
through the speaker and decoded back through the mic (left bubble). Canned
messages above the always-on chord-keyboard bar.](docs/screenshot.jpg)

## Use

- Tap a canned-message button to broadcast it; `RESEND` repeats the last one.
- Status bar: `LISTENING`/`TX...`, `E<n>` CRC-error count, `S<n>` mic level,
  `ID<xx>` this device's sender tag (low byte of the RP2350 unique id).
- **Acoustic self-test:** long-press the status bar (>= 1 s). In `SELFTEST`
  the demodulator stays live during TX, so tapping any canned button makes the
  device decode its own transmission through the air — a one-device
  end-to-end check. Long-press again to exit.
- **Typing:** the chord-keyboard label bar is always visible at the bottom
  of the screen. Each character is a two-press chord on the five color
  buttons: the first press picks a group (shown in the bar), the second
  picks the character within it; `PAGE` cycles pages or cancels a
  half-chord. While composing, the canned grid is replaced by the draft
  line. `NAV_CENTER` sends the draft, `NAV_LEFT` backspaces, `NAV_RIGHT`
  inserts a space; touch also works (lower half of the screen = space,
  upper half = backspace). `CANCEL` discards the draft and restores the
  canned grid.

## Test sequence

1. `fw test` — modem host tests (loopback, noise, clock skew).
2. Flash one device, enable self-test, tap `HI`: the message must appear as a
   received line and RTT shows `rc: rx from <id>`.
3. Two devices ~1 m apart in a quiet room: messages appear on the other
   device after a second or two of warble. Push distance/noise and note the
   working envelope; the `E` counter shows CRC drops.

## Bench validation record (2026-07-23, one device + PC)

Verified on hardware:

- Boot, codec, touch, PSRAM; PDM capture at exactly 16 kHz (sample counter vs
  wall clock); core-1 demod loop healthy, zero queue drops.
- TX: correct spectrum (carrier measured at 1201 Hz on the device's own mic),
  correct duration, streamed from PSRAM through the exactly-2-chunk
  stream-loop path (BSP fix 7f0f19c exercised every message).
- **Acoustic self-test loopback clean**: repeated `rc: rx from 05` decodes,
  matching received bubbles on screen, `E0` across many consecutive frames.
- Frame parser rejects ambient noise (no false frames over many minutes).
- PC reference modem (Python port, bit-exact demod) round-trips WAVs and
  decodes on-device captures. PC-speaker -> device reached sync+header
  reliably at arm's length but stayed CRC-marginal with laptop speakers; note
  that webcam mics (EMEET) mangle AFSK with noise-suppression DSP, so use the
  device mic as ground truth when debugging.

Still to record with two devices: the ~1 m chat check and the working-range
envelope.

### Why 300 baud, not Bell 202's 1200 (bench findings)

On-device mic captures of the device's own TX show each tone transition is
smeared over ~25 samples (~1.5 ms) by micro-speaker ring-down and board
coupling. At 1200 baud (13.3 samples/bit) that swallows whole bits; at 600
baud it still left ~1 bit error per frame after long same-tone runs (every
frame failed CRC). At 300 baud (53 samples/bit) the discriminator plus the
mid-bit majority vote sample fully settled tone and frames decode cleanly. A
canned message is under a second of warble. To revisit, change `AFSK_BAUD` —
the demod bit clock and all host tests follow the constant.

Two more hardware-driven fixes from the same bench session:

- **TX pre-emphasis**: space is rendered at 13/16 of mark amplitude because
  the micro-speaker reproduces 2200 Hz noticeably louder than 1200 Hz, which
  biased the discriminator toward space.
- **TX stop deadline** is render length + 150 ms: the I2S pipeline has
  ~25-30 ms of start latency and the previous +30 ms deadline cut the CRC
  bytes off the air.

## Bench workflow notes (single device, nobody at the screen)

- `python tools/fw.py build retrochat` — the app argument is required
  (plain `fw.py build` builds `hello_display`).
- Reflashing over a running RetroChat fails with `Failed to call ROM function
  batch`; run openocd with `-c init -c rescue_reset -c "program ... verify
  reset exit"` — `rescue_reset` parks the chip in bootrom first.
- The onboard debugger's USB link is disturbed while RetroChat runs (speaker
  bursts); openocd's background RTT polling dies even though short command
  batches work. Workaround: read the SEGGER RTT ring directly in openocd's
  init batch (dump between `RdOff` and `WrOff`, then advance `RdOff`) —
  short sessions issued back-to-back are reliable.
- RTT stats line (every 500 ms): `rc: hb=<core1 loops> qdrop=<queue drops>
  pcm=<samples pulled> bytes=<demod bytes> pkmax=<max demod peak since last>
  kf=<uart keyboard frames> ke=<uart keyboard errors>`.
