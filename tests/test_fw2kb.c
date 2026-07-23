#include <string.h>
#include "fw2kb.h"
#include "test_util.h"

static void test_init_state(void)
{
    fw2kb_t kb;
    fw2kb_init(&kb);

    /* mode ALL starts on the lower page, in group state */
    const char *labels[5];
    fw2kb_get_labels(&kb, labels);
    CHECK(strcmp(labels[0], "abcde") == 0);
    CHECK(strcmp(labels[1], "fghij") == 0);
    CHECK(strcmp(labels[2], "klmno") == 0);
    CHECK(strcmp(labels[3], "pqrst") == 0);
    CHECK(strcmp(labels[4], "uvwxy") == 0);
    CHECK(!fw2kb_in_chord(&kb));

    /* no pending events */
    fw2kb_event ev;
    CHECK(!fw2kb_next_event(&kb, &ev));
}

static void test_set_mode_selects_page(void)
{
    fw2kb_t kb;
    fw2kb_init(&kb);
    const char *labels[5];

    fw2kb_set_mode(&kb, FW2KB_MODE_UPPER);
    fw2kb_get_labels(&kb, labels);
    CHECK(strcmp(labels[0], "ABCDE") == 0);

    fw2kb_set_mode(&kb, FW2KB_MODE_NUMBERS);
    fw2kb_get_labels(&kb, labels);
    CHECK(strcmp(labels[0], "01234") == 0);
    CHECK(strcmp(labels[2], "+^.<>") == 0);

    fw2kb_set_mode(&kb, FW2KB_MODE_HEX);
    fw2kb_get_labels(&kb, labels);
    CHECK(strcmp(labels[2], "ABCDE") == 0);
    CHECK(strcmp(labels[3], "F") == 0);
    CHECK(strcmp(labels[4], "") == 0);

    fw2kb_set_mode(&kb, FW2KB_MODE_LOWER);
    fw2kb_get_labels(&kb, labels);
    CHECK(strcmp(labels[0], "abcde") == 0);
}

static void test_reset_restores_defaults(void)
{
    fw2kb_t kb;
    fw2kb_init(&kb);
    fw2kb_set_mode(&kb, FW2KB_MODE_HEX);
    fw2kb_reset(&kb);

    const char *labels[5];
    fw2kb_get_labels(&kb, labels);
    CHECK(strcmp(labels[0], "abcde") == 0);   /* back to mode ALL, lower page */
    CHECK(!fw2kb_in_chord(&kb));
}

/* Expected page contents, duplicated from the firmware tables so the test
 * is an independent check of the port. Row order: upper, lower, numbers,
 * symbols, hex. */
static const char *k_expect[5][5] = {
    { "ABCDE", "FGHIJ", "KLMNO", "PQRST", "UVWXY" },
    { "abcde", "fghij", "klmno", "pqrst", "uvwxy" },
    { "01234", "56789", "+^.<>", "=*/~-", "()[]z" },
    { "!@#$%", "^&_`~", "{}\\|;", "\"',=?", "/z"   },
    { "01234", "56789", "ABCDE", "F",     ""      },
};

/* Put kb in group state on page p (0=upper 1=lower 2=numbers 3=symbols 4=hex) */
static void goto_page(fw2kb_t *kb, int p)
{
    switch (p) {
        case 0: fw2kb_set_mode(kb, FW2KB_MODE_UPPER); break;
        case 1: fw2kb_set_mode(kb, FW2KB_MODE_LOWER); break;
        case 2: fw2kb_set_mode(kb, FW2KB_MODE_NUMBERS); break;
        case 3: fw2kb_set_mode(kb, FW2KB_MODE_UPPER);
                fw2kb_press(kb, FW2KB_BTN_CTRL_DOWN);  break; /* upper -> symbols */
        case 4: fw2kb_set_mode(kb, FW2KB_MODE_HEX); break;
    }
}

/* Pop exactly one CHAR event and return its char; 0 if no event; -1 if wrong kind */
static int pop_char(fw2kb_t *kb)
{
    fw2kb_event ev;
    if (!fw2kb_next_event(kb, &ev)) return 0;
    if (ev.key != FW2KB_KEY_CHAR) return -1;
    return (unsigned char)ev.ch;
}

static void test_chord_basic(void)
{
    fw2kb_t kb;
    fw2kb_init(&kb);   /* lower page */

    /* 'm' = group klmno (GREEN), then slot 2 (GREEN) */
    fw2kb_press(&kb, FW2KB_BTN_GREEN);
    CHECK(fw2kb_in_chord(&kb));

    const char *labels[5];
    fw2kb_get_labels(&kb, labels);
    CHECK(strcmp(labels[0], "k") == 0);
    CHECK(strcmp(labels[1], "l") == 0);
    CHECK(strcmp(labels[2], "m") == 0);
    CHECK(strcmp(labels[3], "n") == 0);
    CHECK(strcmp(labels[4], "o") == 0);
    CHECK(pop_char(&kb) == 0);          /* nothing emitted yet */

    fw2kb_press(&kb, FW2KB_BTN_GREEN);
    CHECK(!fw2kb_in_chord(&kb));
    CHECK(pop_char(&kb) == 'm');

    fw2kb_get_labels(&kb, labels);      /* back to groups */
    CHECK(strcmp(labels[2], "klmno") == 0);
}

static void test_chord_every_slot_every_page(void)
{
    static const fw2kb_btn btns[5] = { FW2KB_BTN_GRAY, FW2KB_BTN_YELLOW,
        FW2KB_BTN_GREEN, FW2KB_BTN_BLUE, FW2KB_BTN_RED };

    for (int p = 0; p < 5; p++) {
        for (int g = 0; g < 5; g++) {
            for (int i = 0; i < 5; i++) {
                fw2kb_t kb;
                fw2kb_init(&kb);
                goto_page(&kb, p);
                fw2kb_press(&kb, btns[g]);
                fw2kb_press(&kb, btns[i]);

                const char *grp = k_expect[p][g];
                char want = ((size_t)i < strlen(grp)) ? grp[i] : 0;
                int got = pop_char(&kb);
                CHECK(got == (unsigned char)want);
                CHECK(!fw2kb_in_chord(&kb));   /* empty slot still ends chord */
            }
        }
    }
}

static void test_empty_slot_emits_nothing(void)
{
    fw2kb_t kb;
    fw2kb_init(&kb);
    fw2kb_set_mode(&kb, FW2KB_MODE_HEX);

    fw2kb_press(&kb, FW2KB_BTN_BLUE);   /* group "F" */
    fw2kb_press(&kb, FW2KB_BTN_RED);    /* slot 4 -> past end of "F" */

    fw2kb_event ev;
    CHECK(!fw2kb_next_event(&kb, &ev)); /* deviation: no NUL char emitted */
    CHECK(!fw2kb_in_chord(&kb));
}

static const char *first_label(fw2kb_t *kb)
{
    static const char *labels[5];
    fw2kb_get_labels(kb, labels);
    return labels[0];
}

static void test_cycle_mode_all(void)
{
    fw2kb_t kb;
    fw2kb_init(&kb);                                   /* ALL, lower */
    CHECK(strcmp(first_label(&kb), "abcde") == 0);
    fw2kb_press(&kb, FW2KB_BTN_CTRL_DOWN);
    CHECK(strcmp(first_label(&kb), "01234") == 0);     /* numbers */
    fw2kb_press(&kb, FW2KB_BTN_CTRL_DOWN);
    CHECK(strcmp(first_label(&kb), "ABCDE") == 0);     /* wraps to upper */
    fw2kb_press(&kb, FW2KB_BTN_CTRL_DOWN);
    CHECK(strcmp(first_label(&kb), "abcde") == 0);     /* lower again */
}

static void test_cycle_mode_lower_toggles_numbers(void)
{
    fw2kb_t kb;
    fw2kb_init(&kb);
    fw2kb_set_mode(&kb, FW2KB_MODE_LOWER);
    fw2kb_press(&kb, FW2KB_BTN_CTRL_DOWN);
    CHECK(strcmp(first_label(&kb), "01234") == 0);
    fw2kb_press(&kb, FW2KB_BTN_CTRL_DOWN);
    CHECK(strcmp(first_label(&kb), "abcde") == 0);
}

static void test_cycle_mode_upper_toggles_symbols(void)
{
    fw2kb_t kb;
    fw2kb_init(&kb);
    fw2kb_set_mode(&kb, FW2KB_MODE_UPPER);
    fw2kb_press(&kb, FW2KB_BTN_CTRL_DOWN);
    CHECK(strcmp(first_label(&kb), "!@#$%") == 0);
    fw2kb_press(&kb, FW2KB_BTN_CTRL_DOWN);
    CHECK(strcmp(first_label(&kb), "ABCDE") == 0);
}

static void test_cycle_clamped_modes(void)
{
    fw2kb_t kb;
    fw2kb_init(&kb);

    fw2kb_set_mode(&kb, FW2KB_MODE_NUMBERS);
    fw2kb_press(&kb, FW2KB_BTN_CTRL_DOWN);
    CHECK(strcmp(first_label(&kb), "01234") == 0);     /* pinned */
    fw2kb_press(&kb, FW2KB_BTN_GRAY);                  /* verify it's the */
    fw2kb_press(&kb, FW2KB_BTN_YELLOW);                /* numbers page:   */
    CHECK(pop_char(&kb) == '1');                       /* "01234"[1]      */

    fw2kb_set_mode(&kb, FW2KB_MODE_HEX);
    fw2kb_press(&kb, FW2KB_BTN_CTRL_DOWN);
    fw2kb_press(&kb, FW2KB_BTN_GREEN);                 /* "ABCDE" */
    fw2kb_press(&kb, FW2KB_BTN_RED);
    CHECK(pop_char(&kb) == 'E');                       /* pinned to hex */
}

static void test_ctrl_down_cancels_chord(void)
{
    fw2kb_t kb;
    fw2kb_init(&kb);
    fw2kb_press(&kb, FW2KB_BTN_BLUE);                  /* start chord "pqrst" */
    CHECK(fw2kb_in_chord(&kb));
    fw2kb_press(&kb, FW2KB_BTN_CTRL_DOWN);
    CHECK(!fw2kb_in_chord(&kb));
    CHECK(strcmp(first_label(&kb), "abcde") == 0);     /* page unchanged */
    fw2kb_event ev;
    CHECK(!fw2kb_next_event(&kb, &ev));                /* nothing emitted */
}

static void test_ai_aliases_ctrl_down(void)
{
    fw2kb_t kb;
    fw2kb_init(&kb);
    fw2kb_press(&kb, FW2KB_BTN_AI);
    CHECK(strcmp(first_label(&kb), "01234") == 0);     /* cycled like CTRL_DOWN */
}

static void test_ctrl_up_is_noop(void)
{
    fw2kb_t kb;
    fw2kb_init(&kb);
    fw2kb_press(&kb, FW2KB_BTN_CTRL_UP);
    CHECK(strcmp(first_label(&kb), "abcde") == 0);
    CHECK(!fw2kb_in_chord(&kb));
    fw2kb_event ev;
    CHECK(!fw2kb_next_event(&kb, &ev));
}

static void test_touch_default_threshold(void)
{
    fw2kb_t kb;
    fw2kb_init(&kb);
    fw2kb_event ev;

    fw2kb_touch(&kb, 10, 261);          /* strictly below the line -> space */
    CHECK(fw2kb_next_event(&kb, &ev));
    CHECK(ev.key == FW2KB_KEY_CHAR && ev.ch == ' ');

    fw2kb_touch(&kb, 10, 260);          /* on the line -> backspace */
    CHECK(fw2kb_next_event(&kb, &ev));
    CHECK(ev.key == FW2KB_KEY_BACKSPACE);

    fw2kb_touch(&kb, 300, 0);           /* top of screen -> backspace */
    CHECK(fw2kb_next_event(&kb, &ev));
    CHECK(ev.key == FW2KB_KEY_BACKSPACE);
}

static void test_touch_custom_threshold(void)
{
    fw2kb_t kb;
    fw2kb_init(&kb);
    fw2kb_set_touch_threshold(&kb, 100);
    fw2kb_event ev;

    fw2kb_touch(&kb, 0, 101);
    CHECK(fw2kb_next_event(&kb, &ev));
    CHECK(ev.key == FW2KB_KEY_CHAR && ev.ch == ' ');

    fw2kb_touch(&kb, 0, 99);
    CHECK(fw2kb_next_event(&kb, &ev));
    CHECK(ev.key == FW2KB_KEY_BACKSPACE);
}

static void test_ring_overflow_drops_oldest(void)
{
    fw2kb_t kb;
    fw2kb_init(&kb);

    /* queue 5 spaces then 4 backspaces = 9 events into an 8-slot ring */
    for (int i = 0; i < 5; i++) fw2kb_touch(&kb, 0, 300);
    for (int i = 0; i < 4; i++) fw2kb_touch(&kb, 0, 0);

    fw2kb_event ev;
    int spaces = 0, backs = 0, total = 0;
    while (fw2kb_next_event(&kb, &ev)) {
        total++;
        if (ev.key == FW2KB_KEY_CHAR && ev.ch == ' ') spaces++;
        if (ev.key == FW2KB_KEY_BACKSPACE) backs++;
    }
    CHECK(total == 8);
    CHECK(spaces == 4);    /* oldest space was dropped */
    CHECK(backs == 4);
}

int main(void)
{
    test_init_state();
    test_set_mode_selects_page();
    test_reset_restores_defaults();
    test_chord_basic();
    test_chord_every_slot_every_page();
    test_empty_slot_emits_nothing();
    test_cycle_mode_all();
    test_cycle_mode_lower_toggles_numbers();
    test_cycle_mode_upper_toggles_symbols();
    test_cycle_clamped_modes();
    test_ctrl_down_cancels_chord();
    test_ai_aliases_ctrl_down();
    test_ctrl_up_is_noop();
    test_touch_default_threshold();
    test_touch_custom_threshold();
    test_ring_overflow_drops_oldest();
    if (test_failures() == 0) printf("test_fw2kb: all passed\n");
    return test_failures();
}
