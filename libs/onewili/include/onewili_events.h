/* OneWili event types - generated from the firmware sources. Do not edit.
 * Binary events: decoded WILI frames (see onewili_binary.h / ow_binary_poll).
 * Text events: "[*<id> <args>]" lines from the main port (ow_poll_text_event). */
#ifndef ONEWILI_EVENTS_H
#define ONEWILI_EVENTS_H
#include "onewili.h"
#ifdef __cplusplus
extern "C" {
#endif

#ifndef OW_EVENT_ID_MAX
#define OW_EVENT_ID_MAX 64
#endif

/* A text event line "[*<id> <args>]" split into id + raw args. */
typedef struct ow_text_event {
    char id[OW_EVENT_ID_MAX];
    char args[OW_RESP_MAX];
} ow_text_event;

/* gpioReport - Periodic GPIO bitfield report (binary API).  apiFrame_gpioReport, 12 bytes. */
typedef struct ow_evt_gpio_report {
    uint64_t time_stamp_ns;   /* ui64TimeStampNs @ 0 */
    uint32_t gpio_bitfield;   /* uiGpioBitfield @ 8 */
    bool error;   /* frame header error bit */
} ow_evt_gpio_report;

typedef enum ow_event_kind {
    OW_EV_NONE = 0,
    OW_EV_TEXT,
    OW_EV_GPIO_REPORT,
} ow_event_kind;

typedef struct ow_event {
    ow_event_kind kind;
    union {
        ow_text_event text;
        ow_evt_gpio_report gpio_report;
    } u;
} ow_event;

/* Decode one gpioReport payload. OW_ERR_PROTOCOL on bad length. */
ow_status ow_decode_gpio_report(const uint8_t* payload, uint32_t payload_len, bool error, ow_evt_gpio_report* out);

/* header_type -> decoder table, used by ow_binary_poll. */
typedef struct ow_event_decoder {
    uint16_t header_type;
    uint32_t payload_size;
    const char* name;
    ow_status (*decode)(const uint8_t* payload, uint32_t payload_len,
                        bool error, ow_event* out);
} ow_event_decoder;
extern const ow_event_decoder ow_event_decoders[];
extern const size_t ow_event_decoder_count;

/* Poll for a text event; fills out->u.text and sets kind = OW_EV_TEXT.
 * Returns 1 = filled, 0 = none pending, negative = -(ow_status). */
int ow_poll_text_event(ow_device* dev, ow_event* out);

#ifdef __cplusplus
}
#endif
#endif /* ONEWILI_EVENTS_H */
