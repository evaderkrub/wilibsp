# toggleled

Toggles **main-CPU GPIO 25** every 500 ms from the display CPU, using the
OneWili C API (`libs/onewili`) over the FwGUI display link (UART0,
8 Mbaud, hardware flow control on GPIO 0-3).

Requires the main CPU to run the stock FreeWili 2 firmware (it carries the
OneWili display bridge). If the link won't open, the app retries every
second and reports over RTT (`fw rtt`).

Build/flash: `fw build toggleled` / `fw flash toggleled`.
