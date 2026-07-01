#include "platform/psram.h"
#include "platform/board.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/structs/qmi.h"
#include "hardware/structs/xip.h"

#define PSRAM_CMD_QUAD_END    0xF5
#define PSRAM_CMD_QUAD_ENABLE 0x35
#define PSRAM_CMD_READ_ID     0x9F
#define PSRAM_CMD_RSTEN       0x66
#define PSRAM_CMD_RST         0x99
#define PSRAM_CMD_QUAD_READ   0xEB
#define PSRAM_CMD_QUAD_WRITE  0x38
#define PSRAM_CMD_NOOP        0xFF
#define PSRAM_KGD             0x5D

#define PSRAM_MAX_SCK_HZ      109000000u
#define PSRAM_MAX_SELECT_FS64 125000000u
#define PSRAM_MIN_DESELECT_FS 50000000u
#define SEC_TO_FS             1000000000000000ll

static size_t psram_read_size(void){
    size_t size = 0;
    uint32_t isr = save_and_disable_interrupts();
    qmi_hw->direct_csr = 30u << QMI_DIRECT_CSR_CLKDIV_LSB | QMI_DIRECT_CSR_EN_BITS;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) { }
    qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
    qmi_hw->direct_tx = QMI_DIRECT_TX_OE_BITS
                      | QMI_DIRECT_TX_IWIDTH_VALUE_Q << QMI_DIRECT_TX_IWIDTH_LSB
                      | PSRAM_CMD_QUAD_END;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) { }
    (void)qmi_hw->direct_rx;
    qmi_hw->direct_csr &= ~QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
    qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
    uint8_t kgd = 0, eid = 0;
    for (size_t i = 0; i < 7; i++){
        qmi_hw->direct_tx = (i == 0) ? PSRAM_CMD_READ_ID : PSRAM_CMD_NOOP;
        while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_TXEMPTY_BITS) == 0) { }
        while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) { }
        if (i == 5)      kgd = (uint8_t)qmi_hw->direct_rx;
        else if (i == 6) eid = (uint8_t)qmi_hw->direct_rx;
        else             (void)qmi_hw->direct_rx;
    }
    qmi_hw->direct_csr &= ~(QMI_DIRECT_CSR_ASSERT_CS1N_BITS | QMI_DIRECT_CSR_EN_BITS);
    if (kgd == PSRAM_KGD){
        size = 1024u * 1024u;
        uint8_t size_id = eid >> 5;
        if (eid == 0x26 || size_id == 2) size *= 8;
        else if (size_id == 0)           size *= 2;
        else if (size_id == 1)           size *= 4;
    }
    restore_interrupts(isr);
    return size;
}

static void psram_set_timing(void){
    uint32_t sys_hz = (uint32_t)clock_get_hz(clk_sys);
    uint8_t  clkdiv = (uint8_t)((sys_hz + PSRAM_MAX_SCK_HZ - 1) / PSRAM_MAX_SCK_HZ);
    uint32_t fs_per_cycle = (uint32_t)(SEC_TO_FS / sys_hz);
    uint8_t  max_select   = (uint8_t)(PSRAM_MAX_SELECT_FS64 / fs_per_cycle);
    uint8_t  min_deselect = (uint8_t)((PSRAM_MIN_DESELECT_FS + fs_per_cycle - 1) / fs_per_cycle);
    uint32_t isr = save_and_disable_interrupts();
    qmi_hw->m[1].timing =
        QMI_M1_TIMING_PAGEBREAK_VALUE_1024 << QMI_M1_TIMING_PAGEBREAK_LSB |
        3u           << QMI_M1_TIMING_SELECT_HOLD_LSB |
        1u           << QMI_M1_TIMING_COOLDOWN_LSB |
        1u           << QMI_M1_TIMING_RXDELAY_LSB |
        max_select   << QMI_M1_TIMING_MAX_SELECT_LSB |
        min_deselect << QMI_M1_TIMING_MIN_DESELECT_LSB |
        clkdiv       << QMI_M1_TIMING_CLKDIV_LSB;
    restore_interrupts(isr);
}

size_t psram_init(void){
    gpio_set_function(PIN_PSRAM_CS, GPIO_FUNC_XIP_CS1);
    size_t size = psram_read_size();
    if (size == 0) return 0;
    uint32_t isr = save_and_disable_interrupts();
    qmi_hw->direct_csr = 30u << QMI_DIRECT_CSR_CLKDIV_LSB | QMI_DIRECT_CSR_EN_BITS;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) { }
    for (uint8_t i = 0; i < 3; i++){
        qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
        qmi_hw->direct_tx = (i == 0) ? PSRAM_CMD_RSTEN
                          : (i == 1) ? PSRAM_CMD_RST
                                     : PSRAM_CMD_QUAD_ENABLE;
        while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) { }
        qmi_hw->direct_csr &= ~QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
        for (volatile size_t j = 0; j < 20; j++) __asm volatile("nop");
        (void)qmi_hw->direct_rx;
    }
    qmi_hw->direct_csr &= ~(QMI_DIRECT_CSR_ASSERT_CS1N_BITS | QMI_DIRECT_CSR_EN_BITS);
    restore_interrupts(isr);
    psram_set_timing();
    isr = save_and_disable_interrupts();
    qmi_hw->m[1].rfmt =
        QMI_M1_RFMT_PREFIX_WIDTH_VALUE_Q << QMI_M1_RFMT_PREFIX_WIDTH_LSB |
        QMI_M1_RFMT_ADDR_WIDTH_VALUE_Q   << QMI_M1_RFMT_ADDR_WIDTH_LSB   |
        QMI_M1_RFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M1_RFMT_SUFFIX_WIDTH_LSB |
        QMI_M1_RFMT_DUMMY_WIDTH_VALUE_Q  << QMI_M1_RFMT_DUMMY_WIDTH_LSB  |
        QMI_M1_RFMT_DUMMY_LEN_VALUE_24   << QMI_M1_RFMT_DUMMY_LEN_LSB    |
        QMI_M1_RFMT_DATA_WIDTH_VALUE_Q   << QMI_M1_RFMT_DATA_WIDTH_LSB   |
        QMI_M1_RFMT_PREFIX_LEN_VALUE_8   << QMI_M1_RFMT_PREFIX_LEN_LSB   |
        QMI_M1_RFMT_SUFFIX_LEN_VALUE_NONE << QMI_M1_RFMT_SUFFIX_LEN_LSB;
    qmi_hw->m[1].rcmd = (uint32_t)PSRAM_CMD_QUAD_READ << QMI_M1_RCMD_PREFIX_LSB;
    qmi_hw->m[1].wfmt =
        QMI_M1_WFMT_PREFIX_WIDTH_VALUE_Q << QMI_M1_WFMT_PREFIX_WIDTH_LSB |
        QMI_M1_WFMT_ADDR_WIDTH_VALUE_Q   << QMI_M1_WFMT_ADDR_WIDTH_LSB   |
        QMI_M1_WFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M1_WFMT_SUFFIX_WIDTH_LSB |
        QMI_M1_WFMT_DUMMY_WIDTH_VALUE_Q  << QMI_M1_WFMT_DUMMY_WIDTH_LSB  |
        QMI_M1_WFMT_DUMMY_LEN_VALUE_NONE << QMI_M1_WFMT_DUMMY_LEN_LSB    |
        QMI_M1_WFMT_DATA_WIDTH_VALUE_Q   << QMI_M1_WFMT_DATA_WIDTH_LSB   |
        QMI_M1_WFMT_PREFIX_LEN_VALUE_8   << QMI_M1_WFMT_PREFIX_LEN_LSB   |
        QMI_M1_WFMT_SUFFIX_LEN_VALUE_NONE << QMI_M1_WFMT_SUFFIX_LEN_LSB;
    qmi_hw->m[1].wcmd = (uint32_t)PSRAM_CMD_QUAD_WRITE << QMI_M1_WCMD_PREFIX_LSB;
    xip_ctrl_hw->ctrl |= XIP_CTRL_WRITABLE_M1_BITS;
    restore_interrupts(isr);
    return size;
}

int psram_selftest(size_t test_bytes){
    volatile uint32_t *p = (volatile uint32_t *)PSRAM_BASE;
    size_t n = test_bytes / 4;
    for (size_t i = 0; i < n; i++) p[i] = (uint32_t)(i * 2654435761u);
    for (size_t i = 0; i < n; i++)
        if (p[i] != (uint32_t)(i * 2654435761u)) return 0;
    return 1;
}
