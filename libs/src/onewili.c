#include "onewili.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── command assembly ──────────────────────────────────────────────────── */

static ow_status ow__cat(char* dst, size_t cap, size_t* pos, const char* s) {
    size_t n = strlen(s);
    if (*pos + n + 1 > cap) return OW_ERR_ARG;
    memcpy(dst + *pos, s, n); *pos += n; dst[*pos] = 0;
    return OW_OK;
}
static ow_status ow__cat_int(char* d, size_t c, size_t* p, long v) {
    char b[32]; snprintf(b, sizeof b, " %ld", v); return ow__cat(d, c, p, b);
}
static ow_status ow__cat_hex(char* d, size_t c, size_t* p, unsigned long v, int w) {
    char b[40]; snprintf(b, sizeof b, " %0*lX", w, v); return ow__cat(d, c, p, b);
}
static ow_status ow__cat_bool(char* d, size_t c, size_t* p, bool v) {
    return ow__cat(d, c, p, v ? " 1" : " 0");
}
static ow_status ow__cat_float(char* d, size_t c, size_t* p, double v) {
    char b[48]; snprintf(b, sizeof b, " %g", v); return ow__cat(d, c, p, b);
}
static ow_status ow__cat_str(char* d, size_t c, size_t* p, const char* s) {
    ow_status r = ow__cat(d, c, p, " ");
    return r != OW_OK ? r : ow__cat(d, c, p, s);
}
static ow_status ow__cat_bytes(char* d, size_t c, size_t* p,
                               const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        char b[8]; snprintf(b, sizeof b, " %02X", (unsigned)data[i]);
        ow_status r = ow__cat(d, c, p, b);
        if (r != OW_OK) return r;
    }
    return OW_OK;
}

/* ── framing: [<path> <hexTsNs> <seq> <response...> <ok>] ─────────────── */

static int ow__is_frame(const char* s) {
    if (s[0] != '[' || s[1] == '*') return 0;
    return s[1] == '?' || (s[1] >= 'A' && s[1] <= 'Z') || (s[1] >= 'a' && s[1] <= 'z');
}

static ow_status ow__parse_frame(const char* line, char* resp, size_t cap, int* ok) {
    size_t len = strlen(line);
    if (len < 5 || line[0] != '[' || line[len - 1] != ']') return OW_ERR_PROTOCOL;
    const char* p = line + 1;
    const char* end = line + len - 1;
    int spaces = 0;
    const char* body = NULL;
    for (const char* q = p; q < end; ++q)
        if (*q == ' ' && ++spaces == 3) { body = q + 1; break; }
    if (!body) return OW_ERR_PROTOCOL;
    const char* last = end;                 /* trailing " <ok>" token */
    while (last > body && last[-1] != ' ') --last;
    if (last <= body) return OW_ERR_PROTOCOL;
    *ok = (last[0] == '1');
    size_t rlen = (size_t)((last - 1) - body);
    if (rlen >= cap) return OW_ERR_BUFFER;
    memcpy(resp, body, rlen); resp[rlen] = 0;
    return OW_OK;
}

/* ── spontaneous text events ──────────────────────────────────────────── */

static int ow__is_event_line(const char* s) {
    return s[0] == '[' && s[1] == '*';
}

static void ow__evq_push(ow_device* dev, const char* line) {
    size_t slot;
    if (dev->evq_count == OW_TEXT_EVENT_QUEUE) {
        dev->evq_head = (dev->evq_head + 1) % OW_TEXT_EVENT_QUEUE;
        --dev->evq_count;
        ++dev->dropped_text_events;
    }
    slot = (dev->evq_head + dev->evq_count) % OW_TEXT_EVENT_QUEUE;
    strncpy(dev->evq[slot], line, OW_RESP_MAX - 1);
    dev->evq[slot][OW_RESP_MAX - 1] = 0;
    ++dev->evq_count;
}

/* Accumulate transport reads into dev->line until one '\n'-terminated line is
 * complete; the line (without EOL) is left at dev->line[0..] and the remainder
 * shifted down for the next call. */
static ow_status ow__read_line(ow_device* dev, char* out, size_t outcap,
                               uint32_t timeout_ms) {
    for (;;) {
        int routed = 0;
        for (size_t i = 0; i < dev->line_len; ++i) {
            if (dev->line[i] == '\n') {
                size_t linelen = i;
                if (linelen && dev->line[linelen - 1] == '\r') --linelen;
                if (linelen >= outcap) return OW_ERR_BUFFER;
                memcpy(out, dev->line, linelen); out[linelen] = 0;
                memmove(dev->line, dev->line + i + 1, dev->line_len - i - 1);
                dev->line_len -= i + 1;
                if (ow__is_event_line(out)) { ow__evq_push(dev, out); routed = 1; break; }
                return OW_OK;
            }
        }
        if (routed) continue;   /* rescan the shifted buffer for more lines */
        if (dev->line_len + 1 >= sizeof dev->line) return OW_ERR_BUFFER;
        {
            int n = dev->t.read(dev->t.ctx, (uint8_t*)dev->line + dev->line_len,
                                sizeof dev->line - dev->line_len - 1, timeout_ms);
            if (n == 0) return OW_ERR_TIMEOUT;
            if (n < 0) return OW_ERR_IO;
            dev->line_len += (size_t)n;
        }
    }
}

/* Send one command (0x02 reset prefix + one-shot path) and wait for the next
 * standard response frame. resp receives the frame's response middle. */
static ow_status ow__call(ow_device* dev, const char* cmd,
                          char* resp, size_t respcap) {
    uint8_t out[OW_CMD_MAX + 2];
    size_t clen = strlen(cmd);
    if (clen + 2 > sizeof out) return OW_ERR_ARG;
    out[0] = 0x02;                          /* reset to root + quiet */
    memcpy(out + 1, cmd, clen);
    out[1 + clen] = '\n';
    dev->line_len = 0;                      /* flush stale input */
    if (dev->t.write(dev->t.ctx, out, clen + 2) < 0) return OW_ERR_IO;
    for (;;) {
        char linebuf[OW_RESP_MAX];
        ow_status r = ow__read_line(dev, linebuf, sizeof linebuf,
                                    OW_DEFAULT_TIMEOUT_MS);
        if (r != OW_OK) return r;
        if (!linebuf[0] || !ow__is_frame(linebuf)) continue;
        int ok = 0;
        r = ow__parse_frame(linebuf, resp, respcap, &ok);
        if (r != OW_OK) return r;
        return ok ? OW_OK : OW_ERR_FAILED;
    }
}

/* ── response token decoding ──────────────────────────────────────────── */

static char* ow__tok(char** cur) {
    char* s = *cur;
    while (*s == ' ') ++s;
    if (!*s) return NULL;
    char* e = s;
    while (*e && *e != ' ') ++e;
    if (*e) { *e = 0; *cur = e + 1; } else { *cur = e; }
    return s;
}
static ow_status ow__tok_long(char** cur, long* out, int base) {
    char* t = ow__tok(cur);
    if (!t) return OW_ERR_PROTOCOL;
    char* end = NULL;
    *out = strtol(t, &end, base);
    return (end && *end == 0) ? OW_OK : OW_ERR_PROTOCOL;
}
static ow_status ow__tok_ulong(char** cur, unsigned long* out, int base) {
    char* t = ow__tok(cur);
    if (!t) return OW_ERR_PROTOCOL;
    char* end = NULL;
    *out = strtoul(t, &end, base);
    return (end && *end == 0) ? OW_OK : OW_ERR_PROTOCOL;
}
static ow_status ow__tok_double(char** cur, double* out) {
    char* t = ow__tok(cur);
    if (!t) return OW_ERR_PROTOCOL;
    char* end = NULL;
    *out = strtod(t, &end);
    return (end && *end == 0) ? OW_OK : OW_ERR_PROTOCOL;
}
static ow_status ow__rest_bytes(char** cur, uint8_t* buf, size_t cap, size_t* n) {
    if (!buf || !n) return OW_ERR_ARG;
    *n = 0;
    for (;;) {
        char* t = ow__tok(cur);
        if (!t) return OW_OK;
        char* end = NULL;
        unsigned long v = strtoul(t, &end, 16);
        if (!end || *end != 0 || v > 0xFF) return OW_ERR_PROTOCOL;
        if (*n >= cap) return OW_ERR_BUFFER;
        buf[(*n)++] = (uint8_t)v;
    }
}
static ow_status ow__rest_str(char** cur, char* buf, size_t cap) {
    if (!buf) return OW_ERR_ARG;
    while (**cur == ' ') ++*cur;
    if (strlen(*cur) >= cap) return OW_ERR_BUFFER;
    strcpy(buf, *cur);
    *cur += strlen(*cur);
    return OW_OK;
}

/* Some commands decode nothing - keep -Wunused-function quiet either way. */
typedef int ow__unused_guard;

/* ── open/close ───────────────────────────────────────────────────────── */

ow_status ow_open(ow_device* dev, const ow_transport* transport) {
    if (!dev || !transport || !transport->write || !transport->read)
        return OW_ERR_ARG;
    dev->t = *transport;
    dev->line_len = 0;
    dev->evq_head = dev->evq_count = 0;
    dev->dropped_text_events = 0;
    {
        const uint8_t reset[2] = {0x02, '\n'};
        if (dev->t.write(dev->t.ctx, reset, 2) < 0) return OW_ERR_IO;
    }
    return OW_OK;
}

void ow_close(ow_device* dev) {
    if (!dev || !dev->t.write) return;
    const uint8_t reset[1] = {0x02};
    (void)dev->t.write(dev->t.ctx, reset, 1);
}

int ow_poll_text_line(ow_device* dev, char* id, size_t id_cap,
                      char* args, size_t args_cap) {
    if (!dev || !id || !args || id_cap == 0 || args_cap == 0)
        return -(int)OW_ERR_ARG;
    if (dev->evq_count == 0) {
        /* One zero-timeout pass; ow__read_line routes any event lines it
         * completes into the queue and returns OW_ERR_TIMEOUT when only
         * events (or nothing) arrived. */
        char linebuf[OW_RESP_MAX];
        ow_status r = ow__read_line(dev, linebuf, sizeof linebuf, 0);
        if (r != OW_OK && r != OW_ERR_TIMEOUT) return -(int)r;
        /* A non-event frame outside a call has no waiter: drop it. */
    }
    if (dev->evq_count == 0) return 0;
    {
        const char* line = dev->evq[dev->evq_head];
        const char* s = line + 2;               /* skip "[*" */
        size_t n = strlen(s);
        size_t idend = 0, idlen, alen;
        const char* a;
        if (n && s[n - 1] == ']') --n;          /* drop trailing ']' */
        while (idend < n && s[idend] != ' ') ++idend;
        idlen = idend;
        if (idlen >= id_cap) idlen = id_cap - 1;
        memcpy(id, s, idlen); id[idlen] = 0;
        a = s + idend;
        while (*a == ' ' && (size_t)(a - s) < n) ++a;
        alen = n - (size_t)(a - s);
        if (alen >= args_cap) alen = args_cap - 1;
        memcpy(args, a, alen); args[alen] = 0;
    }
    dev->evq_head = (dev->evq_head + 1) % OW_TEXT_EVENT_QUEUE;
    --dev->evq_count;
    return 1;
}

ow_status ow_io_gpio_set_io_high(ow_device* dev, int32_t pin)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\g\\s")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)pin)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_gpio_set_io_low(ow_device* dev, int32_t pin)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\g\\l")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)pin)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_gpio_set_io_toggle(ow_device* dev, int32_t pin)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\g\\t")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)pin)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_gpio_set_pwm(ow_device* dev, int32_t gpio_number, double freq, double duty)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\g\\p")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)gpio_number)) != OW_OK) return r;
    if ((r = ow__cat_float(cmd, sizeof cmd, &pos, freq)) != OW_OK) return r;
    if ((r = ow__cat_float(cmd, sizeof cmd, &pos, duty)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_gpio_read_all(ow_device* dev, uint32_t* gpiostate)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\g\\u")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    char* cur = resp;
    { unsigned long v; if ((r = ow__tok_ulong(&cur, &v, 16)) != OW_OK) return r;
      if (gpiostate) *gpiostate = (uint32_t)v; }
    return OW_OK;
}

ow_status ow_io_gpio_stream_io(ow_device* dev, int32_t reportratems)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\g\\o")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)reportratems)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_gpio_toggle_hsbdio(ow_device* dev, int32_t pin)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\g\\e")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)pin)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_gpio_show_io_direction_settings(ow_device* dev)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\g\\a")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_uart_u_art_write(ow_device* dev, const uint8_t* data_bytes, size_t data_bytes_len)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\u\\w")) != OW_OK) return r;
    if ((r = ow__cat_bytes(cmd, sizeof cmd, &pos, data_bytes, data_bytes_len)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_uart_toggle_stream(ow_device* dev, uint8_t* data_bytes, size_t data_bytes_cap, size_t* data_bytes_len)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\u\\r")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    char* cur = resp;
    if ((r = ow__rest_bytes(&cur, data_bytes, data_bytes_cap, data_bytes_len)) != OW_OK) return r;
    return OW_OK;
}

ow_status ow_io_uart_uart_enable_api_mode(ow_device* dev)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\u\\t")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_uart_show_uart_settings(ow_device* dev)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\u\\s")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_mdio_mdio_poll_sfp(ow_device* dev)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\m\\a")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_mdio_mdio_read_sfp(ow_device* dev, uint8_t device_address, const uint8_t* register_address, size_t register_address_len, uint32_t* sfp_response)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\m\\b")) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)device_address, 2)) != OW_OK) return r;
    if ((r = ow__cat_bytes(cmd, sizeof cmd, &pos, register_address, register_address_len)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    char* cur = resp;
    { unsigned long v; if ((r = ow__tok_ulong(&cur, &v, 16)) != OW_OK) return r;
      if (sfp_response) *sfp_response = (uint32_t)v; }
    return OW_OK;
}

ow_status ow_io_mdio_mdio_write_sfp(ow_device* dev, uint8_t device_address, const uint8_t* register_address, size_t register_address_len, const uint8_t* data_bytes, size_t data_bytes_len)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\m\\c")) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)device_address, 2)) != OW_OK) return r;
    if ((r = ow__cat_bytes(cmd, sizeof cmd, &pos, register_address, register_address_len)) != OW_OK) return r;
    if ((r = ow__cat_bytes(cmd, sizeof cmd, &pos, data_bytes, data_bytes_len)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_mdio_mdiormwsfp(ow_device* dev, uint8_t device_address, const uint8_t* register_address, size_t register_address_len, const uint8_t* mask_bytes, size_t mask_bytes_len, const uint8_t* data_bytes, size_t data_bytes_len)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\m\\e")) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)device_address, 2)) != OW_OK) return r;
    if ((r = ow__cat_bytes(cmd, sizeof cmd, &pos, register_address, register_address_len)) != OW_OK) return r;
    if ((r = ow__cat_bytes(cmd, sizeof cmd, &pos, mask_bytes, mask_bytes_len)) != OW_OK) return r;
    if ((r = ow__cat_bytes(cmd, sizeof cmd, &pos, data_bytes, data_bytes_len)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_mdio_mdio_poll(ow_device* dev)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\m\\y")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_mdio_mdio_read22(ow_device* dev, uint8_t phy_address, uint8_t register_address, uint32_t* mdio_response)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\m\\g")) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)phy_address, 2)) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)register_address, 2)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    char* cur = resp;
    { unsigned long v; if ((r = ow__tok_ulong(&cur, &v, 16)) != OW_OK) return r;
      if (mdio_response) *mdio_response = (uint32_t)v; }
    return OW_OK;
}

ow_status ow_io_mdio_mdio_write22(ow_device* dev, uint8_t phy_address, uint8_t register_address, const uint8_t* data_bytes, size_t data_bytes_len)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\m\\i")) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)phy_address, 2)) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)register_address, 2)) != OW_OK) return r;
    if ((r = ow__cat_bytes(cmd, sizeof cmd, &pos, data_bytes, data_bytes_len)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_mdio_mdiormw22(ow_device* dev, uint8_t phy_address, uint8_t register_address, const uint8_t* mask_bytes, size_t mask_bytes_len, const uint8_t* data_bytes, size_t data_bytes_len)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\m\\j")) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)phy_address, 2)) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)register_address, 2)) != OW_OK) return r;
    if ((r = ow__cat_bytes(cmd, sizeof cmd, &pos, mask_bytes, mask_bytes_len)) != OW_OK) return r;
    if ((r = ow__cat_bytes(cmd, sizeof cmd, &pos, data_bytes, data_bytes_len)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_mdio_mdio_read45(ow_device* dev, uint8_t phy_address, uint8_t mmd_address, uint32_t register_address, uint32_t* mdio_response)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\m\\k")) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)phy_address, 2)) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)mmd_address, 2)) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)register_address, 4)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    char* cur = resp;
    { unsigned long v; if ((r = ow__tok_ulong(&cur, &v, 16)) != OW_OK) return r;
      if (mdio_response) *mdio_response = (uint32_t)v; }
    return OW_OK;
}

ow_status ow_io_mdio_mdio_write45(ow_device* dev, uint8_t phy_address, uint8_t mmd_address, uint32_t register_address, const uint8_t* data_bytes, size_t data_bytes_len)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\m\\l")) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)phy_address, 2)) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)mmd_address, 2)) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)register_address, 4)) != OW_OK) return r;
    if ((r = ow__cat_bytes(cmd, sizeof cmd, &pos, data_bytes, data_bytes_len)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_mdio_mdiormw45(ow_device* dev, uint8_t phy_address, uint8_t mmd_address, uint32_t register_address, const uint8_t* mask_bytes, size_t mask_bytes_len, const uint8_t* data_bytes, size_t data_bytes_len)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\m\\m")) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)phy_address, 2)) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)mmd_address, 2)) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)register_address, 4)) != OW_OK) return r;
    if ((r = ow__cat_bytes(cmd, sizeof cmd, &pos, mask_bytes, mask_bytes_len)) != OW_OK) return r;
    if ((r = ow__cat_bytes(cmd, sizeof cmd, &pos, data_bytes, data_bytes_len)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_mdio_mdio_read_emu(ow_device* dev, uint8_t phy_address, uint8_t mmd_address, uint32_t register_address, uint32_t* mdio_response)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\m\\n")) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)phy_address, 2)) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)mmd_address, 2)) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)register_address, 4)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    char* cur = resp;
    { unsigned long v; if ((r = ow__tok_ulong(&cur, &v, 16)) != OW_OK) return r;
      if (mdio_response) *mdio_response = (uint32_t)v; }
    return OW_OK;
}

ow_status ow_io_mdio_mdio_write_emu(ow_device* dev, uint8_t phy_address, uint8_t mmd_address, uint32_t register_address, const uint8_t* data_bytes, size_t data_bytes_len)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\m\\o")) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)phy_address, 2)) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)mmd_address, 2)) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)register_address, 4)) != OW_OK) return r;
    if ((r = ow__cat_bytes(cmd, sizeof cmd, &pos, data_bytes, data_bytes_len)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_mdio_mdiormw_emu(ow_device* dev, uint8_t phy_address, uint8_t mmd_address, uint32_t register_address, const uint8_t* mask_bytes, size_t mask_bytes_len, const uint8_t* data_bytes, size_t data_bytes_len)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\m\\p")) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)phy_address, 2)) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)mmd_address, 2)) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)register_address, 4)) != OW_OK) return r;
    if ((r = ow__cat_bytes(cmd, sizeof cmd, &pos, mask_bytes, mask_bytes_len)) != OW_OK) return r;
    if ((r = ow__cat_bytes(cmd, sizeof cmd, &pos, data_bytes, data_bytes_len)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_sensors_enable_accel_stream(ow_device* dev, int32_t stream_rate_ms)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\s\\o")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)stream_rate_ms)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_i2c_i2c_write(ow_device* dev, uint8_t address, uint8_t register_, const uint8_t* data_bytes, size_t data_bytes_len)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\i\\w")) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)address, 2)) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)register_, 2)) != OW_OK) return r;
    if ((r = ow__cat_bytes(cmd, sizeof cmd, &pos, data_bytes, data_bytes_len)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_i2c_i2c_read(ow_device* dev, uint8_t* i2crepsone, size_t i2crepsone_cap, size_t* i2crepsone_len)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\i\\r")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    char* cur = resp;
    if ((r = ow__rest_bytes(&cur, i2crepsone, i2crepsone_cap, i2crepsone_len)) != OW_OK) return r;
    return OW_OK;
}

ow_status ow_io_i2c_i2c_poll(ow_device* dev)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\i\\p")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_i2c_show_i2c_settings(ow_device* dev)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\i\\s")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_spi_s_pi_write(ow_device* dev, const uint8_t* data_bytes, size_t data_bytes_len, uint8_t* spi_response, size_t spi_response_cap, size_t* spi_response_len)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\e\\w")) != OW_OK) return r;
    if ((r = ow__cat_bytes(cmd, sizeof cmd, &pos, data_bytes, data_bytes_len)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    char* cur = resp;
    if ((r = ow__rest_bytes(&cur, spi_response, spi_response_cap, spi_response_len)) != OW_OK) return r;
    return OW_OK;
}

ow_status ow_io_spi_show_spi_settings(ow_device* dev)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\e\\s")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_canfd_enable_canfd_stream(ow_device* dev, int32_t channel, int32_t enabled)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\c\\o")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)channel)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)enabled)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_canfd_write_canfd(ow_device* dev, int32_t channel, uint32_t arb_id, int32_t can_fd, int32_t xtd_id, const uint8_t* data_in, size_t data_in_len)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\c\\w")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)channel)) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)arb_id, 8)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)can_fd)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)xtd_id)) != OW_OK) return r;
    if ((r = ow__cat_bytes(cmd, sizeof cmd, &pos, data_in, data_in_len)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_canfd_write_canfd_periodic(ow_device* dev, int32_t index, int32_t enable, int32_t period, int32_t channel, uint32_t arb_id, int32_t can_fd, int32_t xtd_id, const uint8_t* data_in, size_t data_in_len)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\c\\p")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)index)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)enable)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)period)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)channel)) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)arb_id, 8)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)can_fd)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)xtd_id)) != OW_OK) return r;
    if ((r = ow__cat_bytes(cmd, sizeof cmd, &pos, data_in, data_in_len)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_canfd_setup_filter(ow_device* dev, int32_t channel, int32_t index, int32_t enable, int32_t xtd_id, uint32_t mask, uint32_t accept, uint32_t maskb0, uint32_t accept_b0, uint32_t maskb1, uint32_t accept_b1)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\c\\f")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)channel)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)index)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)enable)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)xtd_id)) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)mask, 8)) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)accept, 8)) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)maskb0, 8)) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)accept_b0, 8)) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)maskb1, 8)) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)accept_b1, 8)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_canfd_read_can_registers(ow_device* dev, int32_t channel, uint32_t start_address, int32_t word_count, char* registers, size_t registers_cap)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\c\\r")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)channel)) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)start_address, 8)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)word_count)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    char* cur = resp;
    if ((r = ow__rest_str(&cur, registers, registers_cap)) != OW_OK) return r;
    return OW_OK;
}

ow_status ow_io_canfd_set_can_register(ow_device* dev, int32_t channel, uint32_t start_address, int32_t byte_count, uint32_t word_to_write)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\c\\s")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)channel)) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)start_address, 8)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)byte_count)) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)word_to_write, 8)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_analog_in_enable_analog_in_stream(ow_device* dev, int32_t stream_rate_ms)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\j\\s")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)stream_rate_ms)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_analog_out_set_analog_output(ow_device* dev, int32_t channel, double value)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\a\\s")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)channel)) != OW_OK) return r;
    if ((r = ow__cat_float(cmd, sizeof cmd, &pos, value)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_analog_out_set_trigger_window(ow_device* dev, double value_low, double value_high)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\a\\t")) != OW_OK) return r;
    if ((r = ow__cat_float(cmd, sizeof cmd, &pos, value_low)) != OW_OK) return r;
    if ((r = ow__cat_float(cmd, sizeof cmd, &pos, value_high)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_analog_out_set_enable_trigger(ow_device* dev)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\a\\e")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_analog_out_set_v_prog_vout(ow_device* dev, int32_t enable, double set_voltage)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\a\\u")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)enable)) != OW_OK) return r;
    if ((r = ow__cat_float(cmd, sizeof cmd, &pos, set_voltage)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_analog_out_set_glitch(ow_device* dev, int32_t nano_seconds)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\a\\g")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)nano_seconds)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_logic_player_setup_player(ow_device* dev, int32_t sample_rate_ns, int32_t sample_count, int32_t pin_start, int32_t pin_stop, int32_t start_mode, int32_t trigger_pin, bool loop)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\p\\c")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)sample_rate_ns)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)sample_count)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)pin_start)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)pin_stop)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)start_mode)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)trigger_pin)) != OW_OK) return r;
    if ((r = ow__cat_bool(cmd, sizeof cmd, &pos, loop)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_logic_player_setup_analog(ow_device* dev, int32_t mask, int32_t analog_rate_ns, int32_t analog_resolution)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\p\\a")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)mask)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)analog_rate_ns)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)analog_resolution)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_logic_player_load_file(ow_device* dev, const char* file_path)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\p\\l")) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, file_path)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_logic_player_start(ow_device* dev)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\p\\s")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_logic_player_stop(ow_device* dev)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\p\\e")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_logic_analyzer_setup_logic_analyzer(ow_device* dev, int32_t sample_rate_ns, int32_t sample_count, int32_t pin_start, int32_t pin_stop, int32_t trigger_pin, int32_t trigger_type, int32_t rearm)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\b\\c")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)sample_rate_ns)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)sample_count)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)pin_start)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)pin_stop)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)trigger_pin)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)trigger_type)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)rearm)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_logic_analyzer_setup_analog(ow_device* dev, int32_t analog_mask, int32_t analog_rate_ns, int32_t analog_res)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\b\\a")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)analog_mask)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)analog_rate_ns)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)analog_res)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_logic_analyzer_start(ow_device* dev)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\b\\s")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_logic_analyzer_stop(ow_device* dev)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\b\\e")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_logic_analyzer_trigger(ow_device* dev, int32_t trigger_type)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\b\\t")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)trigger_type)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_wil_eye_take_picture(ow_device* dev, int32_t destination, const char* filename)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\f\\t")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)destination)) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, filename)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_wil_eye_start_recording_video(ow_device* dev, const char* filename)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\f\\v")) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, filename)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_wil_eye_stop_recording_video(ow_device* dev)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\f\\s")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_wil_eye_toggle_ai_detection_stream(ow_device* dev, int32_t ai_stream_mode)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\f\\a")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)ai_stream_mode)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_wil_eye_set_zoom_level(ow_device* dev, int32_t zoom)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\f\\m")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)zoom)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_wil_eye_set_contrast(ow_device* dev, int32_t contrast)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\f\\c")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)contrast)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_wil_eye_set_saturation(ow_device* dev, int32_t saturation)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\f\\i")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)saturation)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_wil_eye_set_brightness(ow_device* dev, int32_t brightness)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\f\\b")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)brightness)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_wil_eye_set_hue(ow_device* dev, int32_t hue)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\f\\u")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)hue)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_wil_eye_set_resolution(ow_device* dev, int32_t resolutionstate)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\f\\y")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)resolutionstate)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_wil_eye_set_flash_state(ow_device* dev, bool flash)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\f\\l")) != OW_OK) return r;
    if ((r = ow__cat_bool(cmd, sizeof cmd, &pos, flash)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_audio_play_audio_file(ow_device* dev, const char* file_path)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\k\\f")) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, file_path)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_audio_record_audio_file(ow_device* dev, const char* file_name)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\k\\r")) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, file_name)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_audio_play_audio_asset(ow_device* dev, const char* asset_name)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\k\\a")) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, asset_name)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_audio_enable_audio_stream(ow_device* dev, int32_t enable)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\k\\s")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)enable)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_audio_numbers_to_speech(ow_device* dev, double number)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\k\\n")) != OW_OK) return r;
    if ((r = ow__cat_float(cmd, sizeof cmd, &pos, number)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_audio_tone(ow_device* dev, double frequency, double duration_ms, double amplitude)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\k\\t")) != OW_OK) return r;
    if ((r = ow__cat_float(cmd, sizeof cmd, &pos, frequency)) != OW_OK) return r;
    if ((r = ow__cat_float(cmd, sizeof cmd, &pos, duration_ms)) != OW_OK) return r;
    if ((r = ow__cat_float(cmd, sizeof cmd, &pos, amplitude)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_io_audio_speak(ow_device* dev, const char* text)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "i\\k\\v")) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, text)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_set_led_color(ow_device* dev, int32_t ledindex, int32_t red, int32_t green, int32_t blue, int32_t duration, ow_ow_led_manager_led_mode mode)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\s")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)ledindex)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)red)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)green)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)blue)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)duration)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)mode)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_show_fwi_image(ow_device* dev, const char* filename)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\l")) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, filename)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_clear_display(ow_device* dev)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\t")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_show_text(ow_device* dev, const char* texttodisplay)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\p")) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, texttodisplay)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_read_all(ow_device* dev)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\u")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_stream_io(ow_device* dev, int32_t pin)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\o")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)pin)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_show_image_asset_by_id(ow_device* dev, int32_t image_id)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\a")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)image_id)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_panels_add_panel(ow_device* dev, bool use_tile, int32_t tile_id, const char* color, bool show_menu)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\c\\a")) != OW_OK) return r;
    if ((r = ow__cat_bool(cmd, sizeof cmd, &pos, use_tile)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)tile_id)) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, color)) != OW_OK) return r;
    if ((r = ow__cat_bool(cmd, sizeof cmd, &pos, show_menu)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_panels_add_panel_picklist(ow_device* dev, bool use_tile, int32_t tile_id, int32_t icon_id, int32_t log_index, const char* back_color, const char* fore_color, const char* caption)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\c\\b")) != OW_OK) return r;
    if ((r = ow__cat_bool(cmd, sizeof cmd, &pos, use_tile)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)tile_id)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)icon_id)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)log_index)) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, back_color)) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, fore_color)) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, caption)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_panels_show_panel(ow_device* dev, int32_t index)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\c\\c")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)index)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_controls_add_led(ow_device* dev, int32_t index, int32_t x, int32_t y, int32_t color, int32_t size, bool inital_value)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\b\\a")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)index)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)x)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)y)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)color)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)size)) != OW_OK) return r;
    if ((r = ow__cat_bool(cmd, sizeof cmd, &pos, inital_value)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_controls_add_log_list(ow_device* dev, int32_t index, int32_t log, int32_t x, int32_t y, int32_t width, int32_t height, int32_t font_type, int32_t font_size, const char* back_color, const char* fore_color, bool list_mode)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\b\\b")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)index)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)log)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)x)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)y)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)width)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)height)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)font_type)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)font_size)) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, back_color)) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, fore_color)) != OW_OK) return r;
    if ((r = ow__cat_bool(cmd, sizeof cmd, &pos, list_mode)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_controls_add_plot(ow_device* dev, int32_t index, int32_t plot_data_index_bit_field, int32_t x, int32_t y, int32_t width, int32_t height, int32_t min_y, int32_t max_y, const char* back_color)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\b\\c")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)index)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)plot_data_index_bit_field)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)x)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)y)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)width)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)height)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)min_y)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)max_y)) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, back_color)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_controls_add_number(ow_device* dev, int32_t index, int32_t x, int32_t y, int32_t width, int32_t font_type, int32_t font_size, const char* fore_color, const char* back_color, bool is_float, int32_t float_digit_count, bool is_hex_format, bool is_unsigned)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\b\\l")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)index)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)x)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)y)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)width)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)font_type)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)font_size)) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, fore_color)) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, back_color)) != OW_OK) return r;
    if ((r = ow__cat_bool(cmd, sizeof cmd, &pos, is_float)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)float_digit_count)) != OW_OK) return r;
    if ((r = ow__cat_bool(cmd, sizeof cmd, &pos, is_hex_format)) != OW_OK) return r;
    if ((r = ow__cat_bool(cmd, sizeof cmd, &pos, is_unsigned)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_controls_add_text(ow_device* dev, int32_t index, int32_t x, int32_t y, int32_t font_type, int32_t font_size, const char* fore_color, const char* back_color, const char* text)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\b\\e")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)index)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)x)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)y)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)font_type)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)font_size)) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, fore_color)) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, back_color)) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, text)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_controls_add_bargraph(ow_device* dev, int32_t index, int32_t x, int32_t y, int32_t width, int32_t height, int32_t min, int32_t max, const char* bar_color)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\b\\f")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)index)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)x)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)y)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)width)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)height)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)min)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)max)) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, bar_color)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_controls_add_meter(ow_device* dev, int32_t index, int32_t x, int32_t y, int32_t width, int32_t height, int32_t min, int32_t max, const char* needle_color)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\b\\g")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)index)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)x)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)y)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)width)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)height)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)min)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)max)) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, needle_color)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_controls_add_button(ow_device* dev, int32_t index, int32_t x, int32_t y, int32_t width, int32_t height, const char* fore_color, const char* back_color, const char* text)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\b\\i")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)index)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)x)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)y)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)width)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)height)) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, fore_color)) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, back_color)) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, text)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_controls_add_picture(ow_device* dev, int32_t index, int32_t x, int32_t y, int32_t picture_id)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\b\\j")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)index)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)x)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)y)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)picture_id)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_controls_add_picture_from_file(ow_device* dev, int32_t index, int32_t x, int32_t y, const char* picture_path)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\b\\k")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)index)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)x)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)y)) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, picture_path)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_control_properties_set_control_value_text(ow_device* dev, int32_t index, const char* text)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\e\\a")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)index)) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, text)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_control_properties_set_control_value_int(ow_device* dev, int32_t index, int32_t value)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\e\\b")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)index)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)value)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_control_properties_set_control_value_float(ow_device* dev, int32_t index, double value)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\e\\c")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)index)) != OW_OK) return r;
    if ((r = ow__cat_float(cmd, sizeof cmd, &pos, value)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_control_properties_set_list_item_text(ow_device* dev, int32_t log_index, int32_t list_item, int32_t color, const char* text)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\e\\k")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)log_index)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)list_item)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)color)) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, text)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_control_properties_set_control_value_min_max_int(ow_device* dev, int32_t index, bool enable, int32_t min, int32_t max)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\e\\e")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)index)) != OW_OK) return r;
    if ((r = ow__cat_bool(cmd, sizeof cmd, &pos, enable)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)min)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)max)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_control_properties_set_control_value_min_max_float(ow_device* dev, int32_t index, bool enable, double min, double max)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\e\\l")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)index)) != OW_OK) return r;
    if ((r = ow__cat_bool(cmd, sizeof cmd, &pos, enable)) != OW_OK) return r;
    if ((r = ow__cat_float(cmd, sizeof cmd, &pos, min)) != OW_OK) return r;
    if ((r = ow__cat_float(cmd, sizeof cmd, &pos, max)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_control_properties_set_plot_data(ow_device* dev, int32_t plot_data_index, int32_t settings, int32_t value)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\e\\f")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)plot_data_index)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)settings)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)value)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_control_properties_set_list_item_selected(ow_device* dev, int32_t log_index, int32_t list_index)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\e\\g")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)log_index)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)list_index)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_control_properties_set_list_item_top_index(ow_device* dev, int32_t log_item, int32_t list_index)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\e\\i")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)log_item)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)list_index)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_control_properties_set_control_property(ow_device* dev, int32_t index, int32_t property, int32_t value)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\e\\j")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)index)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)property)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)value)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_dialogs_message_box(ow_device* dev, int32_t auto_close_half_sec, bool show_ok, bool show_ok_cancel, bool show_none, int32_t picture_index, const char* message)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\f\\a")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)auto_close_half_sec)) != OW_OK) return r;
    if ((r = ow__cat_bool(cmd, sizeof cmd, &pos, show_ok)) != OW_OK) return r;
    if ((r = ow__cat_bool(cmd, sizeof cmd, &pos, show_ok_cancel)) != OW_OK) return r;
    if ((r = ow__cat_bool(cmd, sizeof cmd, &pos, show_none)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)picture_index)) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, message)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_dialogs_set_dialog_description(ow_device* dev, const char* description)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\f\\b")) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, description)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_dialogs_progress_bar(ow_device* dev, int32_t picture_index, bool ok_to_close, bool auto_close_at100, int32_t auto_close_half_sec, const char* title)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\f\\c")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)picture_index)) != OW_OK) return r;
    if ((r = ow__cat_bool(cmd, sizeof cmd, &pos, ok_to_close)) != OW_OK) return r;
    if ((r = ow__cat_bool(cmd, sizeof cmd, &pos, auto_close_at100)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)auto_close_half_sec)) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, title)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_dialogs_number_edit(ow_device* dev, int32_t min, int32_t max, int32_t initial, bool use_min_max, bool is_unsigned, bool hex_fomat, const char* message)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\f\\k")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)min)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)max)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)initial)) != OW_OK) return r;
    if ((r = ow__cat_bool(cmd, sizeof cmd, &pos, use_min_max)) != OW_OK) return r;
    if ((r = ow__cat_bool(cmd, sizeof cmd, &pos, is_unsigned)) != OW_OK) return r;
    if ((r = ow__cat_bool(cmd, sizeof cmd, &pos, hex_fomat)) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, message)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_dialogs_number_edit_float(ow_device* dev, double min, double max, double initial, bool use_min_max, int32_t digit_count, const char* message)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\f\\e")) != OW_OK) return r;
    if ((r = ow__cat_float(cmd, sizeof cmd, &pos, min)) != OW_OK) return r;
    if ((r = ow__cat_float(cmd, sizeof cmd, &pos, max)) != OW_OK) return r;
    if ((r = ow__cat_float(cmd, sizeof cmd, &pos, initial)) != OW_OK) return r;
    if ((r = ow__cat_bool(cmd, sizeof cmd, &pos, use_min_max)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)digit_count)) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, message)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_dialogs_text_edit(ow_device* dev, const char* message, const char* inital_value)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\f\\f")) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, message)) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, inital_value)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_dialogs_pick_list(ow_device* dev, int32_t log_index, const char* message)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\f\\g")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)log_index)) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, message)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_gui_dialogs_show_text_editor(ow_device* dev, int32_t editor_type, const char* message, const char* inital_value, bool* basic)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\f\\i")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)editor_type)) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, message)) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, inital_value)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    char* cur = resp;
    { long v; if ((r = ow__tok_long(&cur, &v, 10)) != OW_OK) return r;
      if (basic) *basic = (v != 0); }
    return OW_OK;
}

ow_status ow_gui_dialogs_set_progess_dialog_value(ow_device* dev, int32_t value0_to100)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "g\\f\\j")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)value0_to100)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_hardware_do_something(ow_device* dev)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "h\\s")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_hardware_system_enable_battery_stream(ow_device* dev, int32_t enable)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "h\\a\\o")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)enable)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_wireless_nfc_enable_reader(ow_device* dev, int32_t enable)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "w\\n\\r")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)enable)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_wireless_esp32_flasher_enter_bootloader(ow_device* dev, int32_t upgrade_transmission_rate)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "w\\a\\b")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)upgrade_transmission_rate)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_wireless_esp32_flasher_enter_application(ow_device* dev)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "w\\a\\r")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_wireless_esp32_flasher_get_i_dand_security(ow_device* dev, int32_t* esp_chip_id, int32_t* version, bool* sb_en, bool* sbar_en, bool* sdm_en, bool* sbrk_1, bool* sbrk_2, bool* sbrk_3, bool* jtag_sw_dis, bool* jtag_hw_dis, bool* flash_enc_en, bool* dcache_dis, bool* icache_dis)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "w\\a\\i")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    char* cur = resp;
    { long v; if ((r = ow__tok_long(&cur, &v, 10)) != OW_OK) return r;
      if (esp_chip_id) *esp_chip_id = (int32_t)v; }
    { long v; if ((r = ow__tok_long(&cur, &v, 10)) != OW_OK) return r;
      if (version) *version = (int32_t)v; }
    { long v; if ((r = ow__tok_long(&cur, &v, 10)) != OW_OK) return r;
      if (sb_en) *sb_en = (v != 0); }
    { long v; if ((r = ow__tok_long(&cur, &v, 10)) != OW_OK) return r;
      if (sbar_en) *sbar_en = (v != 0); }
    { long v; if ((r = ow__tok_long(&cur, &v, 10)) != OW_OK) return r;
      if (sdm_en) *sdm_en = (v != 0); }
    { long v; if ((r = ow__tok_long(&cur, &v, 10)) != OW_OK) return r;
      if (sbrk_1) *sbrk_1 = (v != 0); }
    { long v; if ((r = ow__tok_long(&cur, &v, 10)) != OW_OK) return r;
      if (sbrk_2) *sbrk_2 = (v != 0); }
    { long v; if ((r = ow__tok_long(&cur, &v, 10)) != OW_OK) return r;
      if (sbrk_3) *sbrk_3 = (v != 0); }
    { long v; if ((r = ow__tok_long(&cur, &v, 10)) != OW_OK) return r;
      if (jtag_sw_dis) *jtag_sw_dis = (v != 0); }
    { long v; if ((r = ow__tok_long(&cur, &v, 10)) != OW_OK) return r;
      if (jtag_hw_dis) *jtag_hw_dis = (v != 0); }
    { long v; if ((r = ow__tok_long(&cur, &v, 10)) != OW_OK) return r;
      if (flash_enc_en) *flash_enc_en = (v != 0); }
    { long v; if ((r = ow__tok_long(&cur, &v, 10)) != OW_OK) return r;
      if (dcache_dis) *dcache_dis = (v != 0); }
    { long v; if ((r = ow__tok_long(&cur, &v, 10)) != OW_OK) return r;
      if (icache_dis) *icache_dis = (v != 0); }
    return OW_OK;
}

ow_status ow_wireless_esp32_flasher_read_flash_size(ow_device* dev, int32_t* flash_size_bytes)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "w\\a\\k")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    char* cur = resp;
    { long v; if ((r = ow__tok_long(&cur, &v, 10)) != OW_OK) return r;
      if (flash_size_bytes) *flash_size_bytes = (int32_t)v; }
    return OW_OK;
}

ow_status ow_wireless_esp32_flasher_read_esp32mac(ow_device* dev, char* esp32_mac, size_t esp32_mac_cap)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "w\\a\\m")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    char* cur = resp;
    if ((r = ow__rest_str(&cur, esp32_mac, esp32_mac_cap)) != OW_OK) return r;
    return OW_OK;
}

ow_status ow_wireless_esp32_flasher_erase_all_flash(ow_device* dev)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "w\\a\\e")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_wireless_esp32_flasher_start_flash_operations(ow_device* dev, uint32_t offset, int32_t size, int32_t block_size)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "w\\a\\f")) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)offset, 8)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)size)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)block_size)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_wireless_esp32_flasher_stop_flash_operation(ow_device* dev, bool reboot)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "w\\a\\p")) != OW_OK) return r;
    if ((r = ow__cat_bool(cmd, sizeof cmd, &pos, reboot)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_wireless_esp32_flasher_flash_write(ow_device* dev, const uint8_t* flash_data, size_t flash_data_len)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "w\\a\\o")) != OW_OK) return r;
    if ((r = ow__cat_bytes(cmd, sizeof cmd, &pos, flash_data, flash_data_len)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_wireless_esp32_flasher_flash_read(ow_device* dev, uint32_t offset, int32_t size)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "w\\a\\j")) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)offset, 8)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)size)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_wireless_esp32_flasher_start_write_memory_operations(ow_device* dev, uint32_t offset, uint32_t memory_block, int32_t block_size)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "w\\a\\y")) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)offset, 8)) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)memory_block, 8)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)block_size)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_wireless_esp32_flasher_memory_write(ow_device* dev, uint32_t offset, uint32_t memory_block, int32_t block_size)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "w\\a\\0")) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)offset, 8)) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)memory_block, 8)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)block_size)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_wireless_esp32_flasher_stop_memory_operation(ow_device* dev, uint32_t entry_address)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "w\\a\\t")) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)entry_address, 8)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_wireless_esp32_flasher_register_write(ow_device* dev, uint32_t offset, uint32_t value)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "w\\a\\g")) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)offset, 8)) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)value, 8)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_wireless_esp32_flasher_register_read(ow_device* dev, uint32_t offset, uint32_t* memory_block)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "w\\a\\c")) != OW_OK) return r;
    if ((r = ow__cat_hex(cmd, sizeof cmd, &pos, (unsigned long)offset, 8)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    char* cur = resp;
    { unsigned long v; if ((r = ow__tok_ulong(&cur, &v, 16)) != OW_OK) return r;
      if (memory_block) *memory_block = (uint32_t)v; }
    return OW_OK;
}

ow_status ow_wireless_esp32_flasher_flash_default(ow_device* dev)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "w\\a\\n")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_wireless_wifi_toggle_events(ow_device* dev)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "w\\w\\r")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_wireless_wifi_on_start_access_point(ow_device* dev, const char* ssid, const char* password, int32_t authmode, bool hidessid)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "w\\w\\a")) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, ssid)) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, password)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)authmode)) != OW_OK) return r;
    if ((r = ow__cat_bool(cmd, sizeof cmd, &pos, hidessid)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_wireless_wifi_on_discconect_from_station(ow_device* dev)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "w\\w\\t")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_wireless_wifi_get_connected_devices(ow_device* dev)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "w\\w\\g")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_wireless_wifi_on_connect_to_station(ow_device* dev, const char* ssid, const char* password)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "w\\w\\c")) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, ssid)) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, password)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_wireless_wifi_on_discconect_from_station_2(ow_device* dev)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "w\\w\\f")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_wireless_wifi_on_scan_for_access_points(ow_device* dev)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "w\\w\\s")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_wireless_wifi_on_get_wif_info(ow_device* dev)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "w\\w\\p")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_wireless_wifi_wifi_open_settings(ow_device* dev)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "w\\w\\e")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_wireless_bluetooth_le_on_start_bt_advertising(ow_device* dev, const char* hostname)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "w\\b\\a")) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, hostname)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_wireless_bluetooth_le_on_stop_bt_advertising(ow_device* dev)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "w\\b\\t")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_wireless_bluetooth_le_on_scan_bt_devices(ow_device* dev, int32_t durationms)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "w\\b\\s")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)durationms)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_wireless_bluetooth_le_on_enable_terminal(ow_device* dev)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "w\\b\\e")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_wireless_bluetooth_le_ble_open_settings(ow_device* dev)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "w\\b\\b")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_wireless_ir_enable_ir_stream(ow_device* dev, int32_t enable)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "w\\i\\o")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)enable)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_wireless_ir_send_ir_data(ow_device* dev, int32_t ir_code)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "w\\i\\a")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)ir_code)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_scripting_launch_script(ow_device* dev)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "s\\a")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_scripting_zoom_io_enable_rx_stream(ow_device* dev, int32_t enable)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "s\\b\\o")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)enable)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_scripting_zoom_io_send_data(ow_device* dev, int32_t delay, const uint8_t* data, size_t data_len)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "s\\b\\w")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)delay)) != OW_OK) return r;
    if ((r = ow__cat_bytes(cmd, sizeof cmd, &pos, data, data_len)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_scripting_zoom_io_update_table_data(ow_device* dev, int32_t table_index, int32_t delay, const uint8_t* data, size_t data_len)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "s\\b\\u")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)table_index)) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)delay)) != OW_OK) return r;
    if ((r = ow__cat_bytes(cmd, sizeof cmd, &pos, data, data_len)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_scripting_zoom_io_enable_schedule_table(ow_device* dev, int32_t number_of_entries)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "s\\b\\p")) != OW_OK) return r;
    if ((r = ow__cat_int(cmd, sizeof cmd, &pos, (long)number_of_entries)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_scripting_zoom_io_compile_test(ow_device* dev)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "s\\b\\c")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_scripting_zoom_io_run_zio(ow_device* dev, const char* path)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "s\\b\\r")) != OW_OK) return r;
    if ((r = ow__cat_str(cmd, sizeof cmd, &pos, path)) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_scripting_zoom_io_stop_zio(ow_device* dev)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "s\\b\\s")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_apps_launch_app(ow_device* dev)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "a\\a")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}

ow_status ow_linux_enable_linux_cpu(ow_device* dev)
{
    char cmd[OW_CMD_MAX]; size_t pos = 0;
    char resp[OW_RESP_MAX];
    ow_status r;
    if ((r = ow__cat(cmd, sizeof cmd, &pos, "l\\a")) != OW_OK) return r;
    if ((r = ow__call(dev, cmd, resp, sizeof resp)) != OW_OK) return r;
    (void)resp;
    return OW_OK;
}
