#include "radio/cc1101.h"
#include "platform/spi_bus.h"
#include "platform/board.h"
#include "platform/diag.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

#define BUS_SPI spi1

// Header byte bits
#define CC_READ   0x80
#define CC_BURST  0x40

// Strobes
#define SRES   0x30
#define SCAL   0x33
#define SRX    0x34
#define STX    0x35
#define SIDLE  0x36
#define SNOP   0x3D

// Status registers (read with CC_READ | CC_BURST)
#define PARTNUM_REG 0x30
#define VERSION_REG 0x31
#define RSSI_REG    0x34

// Config registers
#define FREQ2   0x0D
#define FREQ1   0x0E
#define FREQ0   0x0F
#define MDMCFG2 0x12
#define IOCFG0   0x02
#define PKTCTRL0 0x08
#define FREND0   0x22
#define AGCCTRL2 0x1B
#define PATABLE  0x3E

// ---- SPI primitives (bus must already be acquired by caller) ----

static uint8_t xfer(uint8_t b) {
    uint8_t rx;
    spi_write_read_blocking(BUS_SPI, &b, &rx, 1);
    return rx;
}

static void wr_reg(uint8_t addr, uint8_t val) {
    spi_bus_cc1101_cs(true);
    xfer(addr);
    xfer(val);
    spi_bus_cc1101_cs(false);
}

static uint8_t rd_reg(uint8_t addr) {
    spi_bus_cc1101_cs(true);
    xfer(addr | CC_READ);
    uint8_t v = xfer(SNOP);
    spi_bus_cc1101_cs(false);
    return v;
}

static uint8_t rd_status(uint8_t addr) {
    // Status registers 0x30-0x3D require both R/W and burst bits set
    spi_bus_cc1101_cs(true);
    xfer(addr | CC_READ | CC_BURST);
    uint8_t v = xfer(SNOP);
    spi_bus_cc1101_cs(false);
    return v;
}

static void strobe(uint8_t s) {
    spi_bus_cc1101_cs(true);
    xfer(s);
    spi_bus_cc1101_cs(false);
}

// Burst-write the two OOK PA levels: PATABLE[0] = '0' (carrier off),
// PATABLE[1] = '1' (carrier on / power).
static void wr_patable(uint8_t off, uint8_t on) {
    spi_bus_cc1101_cs(true);
    xfer(PATABLE | CC_BURST);
    xfer(off);
    xfer(on);
    spi_bus_cc1101_cs(false);
}

// ---- Base config table (433-band RX, TI SmartRF-derived) ----

static const uint8_t BASE_CFG[][2] = {
    {0x00, 0x2E}, {0x02, 0x2E},                            // IOCFG2/IOCFG0 = 3-state
    {0x0B, 0x06},                                           // FSCTRL1
    {0x0C, 0x00},                                           // FSCTRL0
    {0x0D, 0x10}, {0x0E, 0xB0}, {0x0F, 0x71},              // FREQ2/1/0 = 433.92 MHz default
    {0x10, 0x8C},                                           // MDMCFG4 (chan bw ~203 kHz)
    {0x11, 0x22},                                           // MDMCFG3
    {0x12, 0x30},                                           // MDMCFG2 = ASK/OOK
    {0x13, 0x22}, {0x14, 0xF8},                            // MDMCFG1/0
    {0x18, 0x08},                                           // MCSM0 = NO auto-cal (fast hop; cc1101_calibrate() once per band)
    {0x19, 0x16},                                           // FOCCFG
    {0x1B, 0x43}, {0x1C, 0x40}, {0x1D, 0x91},              // AGCCTRL2/1/0
    {0x21, 0x56}, {0x22, 0x10},                            // FREND1/0
    {0x23, 0xE9}, {0x24, 0x2A}, {0x25, 0x00}, {0x26, 0x1F}, // FSCAL3..0
    {0x2C, 0x81}, {0x2D, 0x35}, {0x2E, 0x09},              // TEST2/1/0
};

// ---- Public API ----

bool cc1101_init(void) {
    spi_bus_acquire_cc1101();
    // Manual reset. We strobe SRES directly rather than doing the TI SO-low
    // handshake (CSn low -> wait SO low -> SRES -> wait SO low) because on this
    // board the I/O expander powers the radio rail and selects the antenna long
    // before this runs, so the crystal is already settled. If a cold/marginal
    // boot ever shows an intermittent init failure, add the SO-low spins here.
    strobe(SRES);
    sleep_ms(1);
    for (unsigned i = 0; i < sizeof(BASE_CFG) / sizeof(BASE_CFG[0]); i++)
        wr_reg(BASE_CFG[i][0], BASE_CFG[i][1]);
    uint8_t part = rd_status(PARTNUM_REG);
    uint8_t ver  = rd_status(VERSION_REG);
    spi_bus_release_cc1101();
    DIAG("cc1101: PARTNUM=0x%02X VERSION=0x%02X\n", (unsigned)part, (unsigned)ver);
    // Presence check: PARTNUM is 0x00 on a real CC1101, but 0x00 is also what a
    // dead/floating bus returns, so also require a VERSION that is neither all-0s
    // nor all-1s. We deliberately do NOT pin VERSION to 0x14 — TI documents it as
    // changeable across silicon revs, so an exact match would reject a good part.
    return part == 0x00 && ver != 0x00 && ver != 0xFF;
}

uint8_t cc1101_read_version(void) {
    spi_bus_acquire_cc1101();
    uint8_t v = rd_status(VERSION_REG);
    spi_bus_release_cc1101();
    return v;
}

void cc1101_set_frequency(uint32_t hz) {
    cc1101_freq_regs_t r = cc1101_freq_to_regs(hz);
    spi_bus_acquire_cc1101();
    strobe(SIDLE);
    wr_reg(FREQ2, r.f2);
    wr_reg(FREQ1, r.f1);
    wr_reg(FREQ0, r.f0);
    spi_bus_release_cc1101();
}

void cc1101_set_modulation(cc1101_mod_t m) {
    spi_bus_acquire_cc1101();
    uint8_t v = rd_reg(MDMCFG2);
    v = (uint8_t)((v & ~0x70u) | cc1101_mdmcfg2_mod_bits(m));
    wr_reg(MDMCFG2, v);
    spi_bus_release_cc1101();
}

void cc1101_strobe_rx(void) {
    spi_bus_acquire_cc1101();
    strobe(SIDLE);
    strobe(SRX);
    spi_bus_release_cc1101();
}

// Manual PLL/VCO calibration at the CURRENT frequency. With auto-cal disabled
// (MCSM0=0x08) the radio hops without recalibrating every SRX, so each RX entry is
// fast (PLL settle only). Call once per band (at band center) before sweeping; the
// single cal is valid across a narrow ISM span. Frees the bus during the ~720 us
// cal so the display can flush meanwhile.
void cc1101_calibrate(void) {
    spi_bus_acquire_cc1101();
    strobe(SIDLE);
    strobe(SCAL);
    spi_bus_release_cc1101();
    sleep_us(800);   // manual cal completes (~720 us) with the bus released
}

// Monitor (async transparent RX): GDO0 outputs the raw sliced data stream, which
// the gdo_capture PIO samples off-bus. IOCFG0=0x0D (async serial data out),
// PKTCTRL0=0x30 (async serial mode, no CRC/whitening). Frequency + modulation are
// set first; a single manual cal makes the synth valid (MCSM0 auto-cal is off).
void cc1101_monitor_rx(uint32_t hz, cc1101_mod_t mod) {
    cc1101_set_frequency(hz);
    cc1101_set_modulation(mod);
    cc1101_calibrate();
    spi_bus_acquire_cc1101();
    wr_reg(IOCFG0, 0x0D);
    wr_reg(PKTCTRL0, 0x30);
    // OOK squelch: cap AGC gain (MAX_DVGA_GAIN=11, MAX_LNA_GAIN=011) so the AGC
    // can't amplify the empty-channel noise floor up to the OOK slicer threshold
    // (which is what makes async RX spew noise on GDO0). A real, louder signal
    // still clears the reduced gain. Monitor-only; restored on stop.
    wr_reg(AGCCTRL2, 0xDB);
    strobe(SRX);
    spi_bus_release_cc1101();
}

// Leave Monitor cleanly so the Analyzer sweep is unaffected: idle the radio and
// restore GDO0 to its 3-state default.
void cc1101_monitor_stop(void) {
    spi_bus_acquire_cc1101();
    strobe(SIDLE);
    wr_reg(IOCFG0, 0x2E);
    wr_reg(AGCCTRL2, 0x43);   // restore BASE_CFG AGC (Analyzer sensitivity)
    spi_bus_release_cc1101();
}

int cc1101_read_rssi_dbm(void) {
    spi_bus_acquire_cc1101();
    uint8_t raw = rd_status(RSSI_REG);
    spi_bus_release_cc1101();
    return cc1101_rssi_to_dbm(raw);
}

// OOK transmit (async transparent): the modem keys the carrier from the GDO0 data
// input. IOCFG0=0x2E 3-states the chip's GDO0 output driver so the MCU can drive
// the pin as TX data (ook_tx_send owns GPIO32). PATABLE[0]=off, PATABLE[1]=power;
// FREND0 PA_POWER=1 selects both. Frequency/OOK set + calibrated first.
void cc1101_tx_ook_start(uint32_t hz) {
    cc1101_set_frequency(hz);
    cc1101_set_modulation(CC1101_MOD_ASK_OOK);
    cc1101_calibrate();
    spi_bus_acquire_cc1101();
    wr_reg(PKTCTRL0, 0x30);     // async serial mode
    wr_reg(FREND0, 0x11);       // PA_POWER=1 -> modulator uses PATABLE[0..1]
    wr_patable(0x00, 0xC0);     // OOK: off / ~max power
    wr_reg(IOCFG0, 0x2E);       // 3-state GDO0 output; MCU drives it as TX data-in
    strobe(STX);
    spi_bus_release_cc1101();
}

// Leave TX cleanly and restore the RX/Analyzer register state.
void cc1101_tx_ook_stop(void) {
    spi_bus_acquire_cc1101();
    strobe(SIDLE);
    wr_reg(IOCFG0, 0x0D);       // async serial data OUT (RX)
    wr_reg(AGCCTRL2, 0x43);     // BASE_CFG AGC
    wr_reg(FREND0, 0x10);       // BASE_CFG PA_POWER
    spi_bus_release_cc1101();
}
