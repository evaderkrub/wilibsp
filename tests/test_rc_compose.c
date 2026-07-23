// Host test: compose module — chord typing into a draft, nav-pad space/
// backspace, send/cancel semantics, 200-char cap. fw2kb starts in MODE_ALL
// on the lowercase page: groups abcde|fghij|klmno|pqrst|uvwxy, so
// GREY,GREY = 'a'; YELLOW,GREEN = 'h'; YELLOW,BLUE = 'i'.
#include "compose.h"
#include "test_util.h"
#include <string.h>

static compose_t c;

static compose_result_t press(uartkbd_btn_t b) {
    compose_result_t r = compose_button(&c, b, true);
    compose_button(&c, b, false);   // releases must be ignored
    return r;
}

int main(void) {
    compose_init(&c);
    ASSERT_TRUE(!compose_active(&c));
    ASSERT_TRUE(strcmp(compose_draft(&c), "") == 0);

    // Chord 'h': YELLOW selects group fghij (enters compose), GREEN picks 'h'.
    ASSERT_TRUE(press(UARTKBD_BTN_YELLOW) == COMPOSE_CHANGED);
    ASSERT_TRUE(compose_active(&c));
    ASSERT_TRUE(press(UARTKBD_BTN_GREEN) == COMPOSE_CHANGED);
    ASSERT_TRUE(strcmp(compose_draft(&c), "h") == 0);

    // 'i' then nav-right space then 'a'.
    press(UARTKBD_BTN_YELLOW); press(UARTKBD_BTN_BLUE);
    ASSERT_TRUE(press(UARTKBD_BTN_NAV_RIGHT) == COMPOSE_CHANGED);
    press(UARTKBD_BTN_GREY); press(UARTKBD_BTN_GREY);
    ASSERT_TRUE(strcmp(compose_draft(&c), "hi a") == 0);

    // Nav-left backspace.
    ASSERT_TRUE(press(UARTKBD_BTN_NAV_LEFT) == COMPOSE_CHANGED);
    ASSERT_TRUE(strcmp(compose_draft(&c), "hi ") == 0);

    // Touch: above threshold 120 = backspace, below = space.
    ASSERT_TRUE(compose_touch(&c, 240, 40) == COMPOSE_CHANGED);   // backspace
    ASSERT_TRUE(strcmp(compose_draft(&c), "hi") == 0);
    ASSERT_TRUE(compose_touch(&c, 240, 200) == COMPOSE_CHANGED);  // space
    ASSERT_TRUE(strcmp(compose_draft(&c), "hi ") == 0);

    // Send requires non-empty draft; draft survives until compose_clear().
    ASSERT_TRUE(press(UARTKBD_BTN_NAV_CENTER) == COMPOSE_SEND);
    ASSERT_TRUE(strcmp(compose_draft(&c), "hi ") == 0);
    compose_clear(&c);
    ASSERT_TRUE(!compose_active(&c));
    ASSERT_TRUE(strcmp(compose_draft(&c), "") == 0);

    // Empty-draft send is a no-op (and does not enter compose).
    ASSERT_TRUE(press(UARTKBD_BTN_NAV_CENTER) == COMPOSE_NONE);
    ASSERT_TRUE(!compose_active(&c));

    // PAGE alone enters compose (shows the bar) without typing.
    ASSERT_TRUE(press(UARTKBD_BTN_PAGE) == COMPOSE_CHANGED);
    ASSERT_TRUE(compose_active(&c));
    ASSERT_TRUE(compose_draft(&c)[0] == '\0');

    // CANCEL discards and exits.
    press(UARTKBD_BTN_GREY); press(UARTKBD_BTN_YELLOW);           // 'b'
    ASSERT_TRUE(press(UARTKBD_BTN_CANCEL) == COMPOSE_CANCELLED);
    ASSERT_TRUE(!compose_active(&c));
    ASSERT_TRUE(compose_draft(&c)[0] == '\0');

    // Touch while not composing does nothing.
    ASSERT_TRUE(compose_touch(&c, 240, 200) == COMPOSE_NONE);

    // Labels: idle shows the five lowercase groups.
    {
        const char *labels[5];
        compose_labels(&c, labels);
        ASSERT_TRUE(strcmp(labels[0], "abcde") == 0);
        ASSERT_TRUE(strcmp(labels[4], "uvwxy") == 0);
    }

    // 200-char cap: fill with 'a' chords, then one more is dropped.
    compose_init(&c);
    for (int i = 0; i < 201; i++) { press(UARTKBD_BTN_GREY); press(UARTKBD_BTN_GREY); }
    ASSERT_TRUE(strlen(compose_draft(&c)) == FRAME_MAX_PAYLOAD);

    // Unassigned buttons are ignored.
    ASSERT_TRUE(press(UARTKBD_BTN_HOME) == COMPOSE_NONE);
    ASSERT_TRUE(press(UARTKBD_BTN_OK) == COMPOSE_NONE);
    ASSERT_TRUE(press(UARTKBD_BTN_NAV_UP) == COMPOSE_NONE);

    TEST_RETURN();
}
