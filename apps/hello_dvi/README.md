# hello_dvi

On-hardware smoke test for the harvested plain-DVI driver (`bsp/display/hstx_dvi`)
and the DVI OSD (`bsp/display/dvi_osd`). RTT-only diagnostics.

Paints an 8-bar colour test pattern + a 1px white box outline into the 480×288
video region (centered in the 640×480 DVI frame) and draws an OSD title plus an
animating progress bar in the bottom letterbox margin, streamed out the DVI
connector on GPIO 12–19.

## Run

    fw build hello_dvi
    fw flash hello_dvi
    fw rtt        # expect: "hello_dvi: DVI up, clk_hstx=126000 kHz"

## Pass criteria (bench monitor over DVI)

- Locks to 640×480.
- Eight vertical colour bars (white, yellow, cyan, green, magenta, red, blue,
  black), left→right, inside a white box outline, with black borders around.
- Colours correct (validates the native-endian pixel path and the FreeWili
  differential polarity).
- A progress bar sweeps left→right along the bottom margin (proves the scanout
  is continuously reading the framebuffer).

No monitor attached = nothing to observe (no HPD/EDID); RTT still prints the
"DVI up" line and the board does not crash.
