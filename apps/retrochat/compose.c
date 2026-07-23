#include "compose.h"
#include <string.h>

// Touch split: y > this = space, y <= this = backspace. Mid chat area
// (status bar 24 + chat 192 -> grid at 216; split at 120).
#define COMPOSE_TOUCH_SPLIT 120

void compose_init(compose_t *c) {
    memset(c, 0, sizeof *c);
    fw2kb_init(&c->kb);
    fw2kb_set_touch_threshold(&c->kb, COMPOSE_TOUCH_SPLIT);
}

static compose_result_t drain(compose_t *c) {
    // Pull fw2kb events into the draft; report if anything user-visible moved.
    bool changed = false;
    fw2kb_event ev;
    while (fw2kb_next_event(&c->kb, &ev)) {
        if (ev.key == FW2KB_KEY_CHAR && c->len < FRAME_MAX_PAYLOAD) {
            c->draft[c->len++] = ev.ch;
            c->draft[c->len] = '\0';
            changed = true;
        } else if (ev.key == FW2KB_KEY_BACKSPACE && c->len) {
            c->draft[--c->len] = '\0';
            changed = true;
        }
    }
    return changed ? COMPOSE_CHANGED : COMPOSE_NONE;
}

compose_result_t compose_button(compose_t *c, uartkbd_btn_t btn, bool pressed) {
    if (!pressed) return COMPOSE_NONE;
    switch (btn) {
    case UARTKBD_BTN_GREY: case UARTKBD_BTN_YELLOW: case UARTKBD_BTN_GREEN:
    case UARTKBD_BTN_BLUE: case UARTKBD_BTN_RED:
        c->active = true;
        fw2kb_press(&c->kb, (fw2kb_btn)btn);   // GREY..RED == GRAY..RED
        drain(c);
        return COMPOSE_CHANGED;               // labels change even mid-chord
    case UARTKBD_BTN_PAGE:
        c->active = true;
        fw2kb_press(&c->kb, FW2KB_BTN_AI);    // cycle page / cancel half-chord
        return COMPOSE_CHANGED;
    case UARTKBD_BTN_NAV_LEFT:
        if (!c->active || !c->len) return COMPOSE_NONE;
        c->draft[--c->len] = '\0';
        return COMPOSE_CHANGED;
    case UARTKBD_BTN_NAV_RIGHT:
        if (!c->active || c->len >= FRAME_MAX_PAYLOAD) return COMPOSE_NONE;
        c->draft[c->len++] = ' '; c->draft[c->len] = '\0';
        return COMPOSE_CHANGED;
    case UARTKBD_BTN_NAV_CENTER:
        return (c->active && c->len) ? COMPOSE_SEND : COMPOSE_NONE;
    case UARTKBD_BTN_CANCEL:
        if (!c->active) return COMPOSE_NONE;
        compose_clear(c);
        return COMPOSE_CANCELLED;
    default:
        return COMPOSE_NONE;
    }
}

compose_result_t compose_touch(compose_t *c, int x, int y) {
    if (!c->active) return COMPOSE_NONE;
    // fw2kb_touch() unconditionally queues a space or backspace event based
    // solely on y vs. the threshold (see fw2kb.c) -- it does not gate on
    // page/mode. drain() alone reports the outcome (COMPOSE_NONE when the
    // event was a no-op backspace on an empty draft or a space on a full
    // draft); no manual fallback is needed or correct here.
    fw2kb_touch(&c->kb, x, y);
    return drain(c);
}

bool compose_active(const compose_t *c) { return c->active; }
const char *compose_draft(const compose_t *c) { return c->draft; }

void compose_clear(compose_t *c) {
    c->len = 0;
    c->draft[0] = '\0';
    c->active = false;
    fw2kb_reset(&c->kb);
}

void compose_labels(compose_t *c, const char *labels[5]) {
    fw2kb_get_labels(&c->kb, labels);
}
