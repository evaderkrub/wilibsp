#include "onewili_fwgui.h"
#include <string.h>
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "pico/time.h"

/* ── Link constants (FwGUI protocol; see the firmware's protocol.md) ───── */
#define OWFW_BAUD          8000000
#define OWFW_PIN_TX        1
#define OWFW_PIN_RX        0
#define OWFW_PIN_CTS       2
#define OWFW_PIN_RTS       3
#define OWFW_EVT_SYNC0     0xB0    /* display->main event frames  */
#define OWFW_EVT_SYNC1     0x1D
#define OWFW_CMD_SYNC0     0xBE    /* main->display command frames */
#define OWFW_CMD_SYNC1     0xBA
#define OWFW_EVT_TERM      0x18    /* FWGUI_EVENT_M_TERM_INPUT (24) */
#define OWFW_MARKER        0x01    /* OneWili chunk marker          */
#define OWFW_CHUNK_MAX     56      /* text bytes per event frame    */
#define OWFW_CMD_RESPONSE  0x5D    /* FWGUI_API_ONEWILL_RESPONSE    */
#define OWFW_CMD_BINARY    0x5E    /* FWGUI_API_ONEWILL_BINARY      */
#define OWFW_STREAM_MAX    1024    /* per-stream buffer             */
#define OWFW_FRAME_MAX     512     /* incoming command frame payload cap */

/* ── Per-stream byte FIFO ──────────────────────────────────────────────── */
typedef struct {
    uint8_t  buf[OWFW_STREAM_MAX];
    uint32_t head, count;
} owfw_fifo;

static owfw_fifo g_text;     /* 0x5D: responses + "[*" text events */
static owfw_fifo g_binary;   /* 0x5E: binary WILI event bytes      */
static uint32_t  g_dropped;

static void fifo_push_frame(owfw_fifo* f, const uint8_t* p, uint32_t n) {
    if (n > OWFW_STREAM_MAX - f->count) { g_dropped++; return; }  /* drop-newest, whole frame */
    for (uint32_t i = 0; i < n; i++) {
        f->buf[(f->head + f->count) % OWFW_STREAM_MAX] = p[i];
        f->count++;
    }
}

static uint32_t fifo_pop(owfw_fifo* f, uint8_t* out, uint32_t cap) {
    uint32_t n = f->count < cap ? f->count : cap;
    for (uint32_t i = 0; i < n; i++) {
        out[i] = f->buf[f->head];
        f->head = (f->head + 1) % OWFW_STREAM_MAX;
        f->count--;
    }
    return n;
}

/* ── RX: BE BA command-frame parser ────────────────────────────────────── */
/* frame: BE BA | len u16le | cmd u8 | payload[len] | cksum u16le
 * checksum = 16-bit additive sum of sync(2)+length(2)+cmd(1)+payload. */
typedef enum { RX_SYNC0, RX_SYNC1, RX_LEN0, RX_LEN1, RX_CMD, RX_PAYLOAD, RX_CK0, RX_CK1 } owfw_rx_state;

static struct {
    owfw_rx_state st;
    uint16_t len, got, sum, ck;
    uint8_t  cmd;
    uint8_t  payload[OWFW_FRAME_MAX];
    int      overlong;           /* payload > OWFW_FRAME_MAX: parse, discard */
} g_rx;

static void rx_byte(uint8_t b) {
    switch (g_rx.st) {
    case RX_SYNC0:
        if (b == OWFW_CMD_SYNC0) { g_rx.sum = b; g_rx.st = RX_SYNC1; }
        break;
    case RX_SYNC1:
        if (b == OWFW_CMD_SYNC1) { g_rx.sum += b; g_rx.st = RX_LEN0; }
        else g_rx.st = (b == OWFW_CMD_SYNC0) ? RX_SYNC1 : RX_SYNC0;
        break;
    case RX_LEN0: g_rx.sum += b; g_rx.len = b;               g_rx.st = RX_LEN1; break;
    case RX_LEN1:
        g_rx.sum += b; g_rx.len |= (uint16_t)(b << 8);
        g_rx.got = 0;
        g_rx.overlong = g_rx.len > OWFW_FRAME_MAX;
        g_rx.st = RX_CMD;
        break;
    case RX_CMD:
        g_rx.sum += b; g_rx.cmd = b;
        g_rx.st = g_rx.len ? RX_PAYLOAD : RX_CK0;
        break;
    case RX_PAYLOAD:
        g_rx.sum += b;
        if (!g_rx.overlong) g_rx.payload[g_rx.got] = b;
        if (++g_rx.got >= g_rx.len) g_rx.st = RX_CK0;
        break;
    case RX_CK0: g_rx.ck = b; g_rx.st = RX_CK1; break;
    case RX_CK1:
        g_rx.ck |= (uint16_t)(b << 8);
        if (g_rx.ck == g_rx.sum && !g_rx.overlong) {
            if (g_rx.cmd == OWFW_CMD_RESPONSE) fifo_push_frame(&g_text, g_rx.payload, g_rx.len);
            else if (g_rx.cmd == OWFW_CMD_BINARY) fifo_push_frame(&g_binary, g_rx.payload, g_rx.len);
            /* every other command code (GUI traffic) is discarded */
        }
        g_rx.st = RX_SYNC0;
        break;
    }
}

static void owfw_pump(void) {
    while (uart_is_readable(uart0))
        rx_byte((uint8_t)uart_getc(uart0));
}

/* ── TX: wrap command bytes into marked M_TERM_INPUT event frames ──────── */
/* frame: B0 1D | len u16le | payload | cksum u16le, where payload =
 * event code + marker + count + text and len counts payload EXCLUDING the
 * event code (per protocol.md); checksum covers sync+length+payload. */
static void owfw_send_chunk(const uint8_t* text, uint8_t n) {
    uint8_t f[2 + 2 + 3 + OWFW_CHUNK_MAX + 2];
    uint16_t len = (uint16_t)(2 + n);            /* marker + count + text */
    uint32_t k = 0;
    f[k++] = OWFW_EVT_SYNC0; f[k++] = OWFW_EVT_SYNC1;
    f[k++] = (uint8_t)(len & 0xFF); f[k++] = (uint8_t)(len >> 8);
    f[k++] = OWFW_EVT_TERM;
    f[k++] = OWFW_MARKER;
    f[k++] = n;
    memcpy(&f[k], text, n); k += n;
    uint16_t sum = 0;
    for (uint32_t i = 0; i < k; i++) sum = (uint16_t)(sum + f[i]);
    f[k++] = (uint8_t)(sum & 0xFF); f[k++] = (uint8_t)(sum >> 8);
    uart_write_blocking(uart0, f, k);
}

static int owfw_write(void* ctx, const uint8_t* data, size_t len) {
    (void)ctx;
    size_t off = 0;
    while (off < len) {
        size_t n = len - off;
        if (n > OWFW_CHUNK_MAX) n = OWFW_CHUNK_MAX;
        owfw_send_chunk(data + off, (uint8_t)n);
        off += n;
    }
    return (int)len;
}

static int owfw_read_stream(owfw_fifo* f, uint8_t* buf, size_t cap, uint32_t timeout_ms) {
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    for (;;) {
        owfw_pump();
        if (f->count) return (int)fifo_pop(f, buf, (uint32_t)cap);
        if (timeout_ms == 0 || time_reached(deadline)) return 0;
    }
}

static int owfw_read_text(void* ctx, uint8_t* buf, size_t cap, uint32_t timeout_ms) {
    (void)ctx; return owfw_read_stream(&g_text, buf, cap, timeout_ms);
}
static int owfw_read_binary(void* ctx, uint8_t* buf, size_t cap, uint32_t timeout_ms) {
    (void)ctx; return owfw_read_stream(&g_binary, buf, cap, timeout_ms);
}

/* ── Public API ────────────────────────────────────────────────────────── */
ow_status ow_open_fwgui(ow_device* dev) {
    memset(&g_rx, 0, sizeof g_rx);
    memset(&g_text, 0, sizeof g_text);
    memset(&g_binary, 0, sizeof g_binary);
    g_dropped = 0;
    uart_init(uart0, 8000000);   /* OWFW_BAUD */
    gpio_set_function(OWFW_PIN_TX,  GPIO_FUNC_UART);
    gpio_set_function(OWFW_PIN_RX,  GPIO_FUNC_UART);
    gpio_set_function(OWFW_PIN_CTS, GPIO_FUNC_UART);
    gpio_set_function(OWFW_PIN_RTS, GPIO_FUNC_UART);
    uart_set_hw_flow(uart0, true, true);
    ow_transport t;
    t.ctx = 0;
    t.write = owfw_write;
    t.read = owfw_read_text;
    return ow_open(dev, &t);
}

ow_transport ow_fwgui_binary_transport(void) {
    ow_transport t;
    t.ctx = 0;
    t.write = 0;                  /* the binary stream is read-only */
    t.read = owfw_read_binary;
    return t;
}

uint32_t ow_fwgui_dropped_frames(void) { return g_dropped; }
