# toggleled

Toggles **main-CPU GPIO 25** every 500 ms from the display CPU, using the
OneWili C API (`libs/onewili`) over the FwGUI display link (UART0,
8 Mbaud, hardware flow control on GPIO 0-3).

Requires the main CPU to run the stock FreeWili 2 firmware (it carries the
OneWili display bridge). Opening the link cannot fail (it only sends a reset byte; there is no
handshake), so a missing bridge surfaces later: the first toggle times out
after ~5 s and reports over RTT (`fw rtt`). With hardware flow control, a
dead peer can then stall further sends.

Build/flash: `fw build toggleled` / `fw flash toggleled`.
