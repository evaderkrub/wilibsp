// compose.h — chord-keyboard draft editor for RetroChat. Pure logic (no
// hardware includes): feeds uartkbd button events and touch points into the
// fw2kb engine and a bounded draft buffer. The caller performs the actual
// send/UI work when told to.
#ifndef RC_COMPOSE_H
#define RC_COMPOSE_H
#include <stdbool.h>
#include "keyboard/fw2kb.h"
#include "input/uartkbd_parse.h"
#include "modem/frame.h"

typedef enum {
    COMPOSE_NONE = 0,     // nothing the caller needs to act on
    COMPOSE_CHANGED,      // draft text / labels changed -> redraw compose bar
    COMPOSE_SEND,         // NAV_CENTER with a non-empty draft -> caller sends,
                          // then calls compose_clear() on success
    COMPOSE_CANCELLED,    // CANCEL pressed -> draft discarded, compose exited
} compose_result_t;

typedef struct {
    fw2kb_t kb;
    char draft[FRAME_MAX_PAYLOAD + 1];
    unsigned len;
    bool active;
} compose_t;

void compose_init(compose_t *c);
// Feed one uartkbd event (press AND release edges; releases are ignored
// internally). Color buttons + PAGE enter compose mode implicitly.
compose_result_t compose_button(compose_t *c, uartkbd_btn_t btn, bool pressed);
// Feed a touch-down point; only acts while composing (space/backspace split).
compose_result_t compose_touch(compose_t *c, int x, int y);
bool        compose_active(const compose_t *c);
const char *compose_draft(const compose_t *c);
void        compose_clear(compose_t *c);
// Current five soft-button labels (group names, or chars mid-chord).
void        compose_labels(compose_t *c, const char *labels[5]);

#endif
