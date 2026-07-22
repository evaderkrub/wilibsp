# retrochat — acoustic-modem broadcast chat

Sends chat messages as Bell 202 AFSK (1200 Hz mark / 2200 Hz space, 1200 baud)
from the speaker; every Free-Wili 2 in earshot decodes them on its PDM mics and
shows them in a touch chat UI. Broadcast party line: no pairing, fire-and-forget
+ CRC-16 (bad frames dropped, `E` counter in the status bar ticks).

## Use

- Tap a canned-message button to broadcast it; `RESEND` repeats the last one.
- Status bar: `LISTENING`/`TX...`, `E<n>` CRC-error count, `S<n>` mic level,
  `ID<xx>` this device's sender tag (low byte of the RP2350 unique id).
- **Acoustic self-test:** long-press the status bar (>= 1 s). In `SELFTEST`
  the demodulator stays live during TX, so tapping any canned button makes the
  device decode its own transmission through the air — a one-device
  end-to-end check. Long-press again to exit.

## Test sequence

1. `fw test` — modem host tests (loopback, noise, clock skew).
2. Flash one device, enable self-test, tap `HI`: the message must appear as a
   received line and RTT shows `rc: rx from <id>`.
3. Two devices ~1 m apart in a quiet room: messages appear on the other
   device in under a second of warble. Push distance/noise and note the
   working envelope; the `E` counter shows CRC drops.
