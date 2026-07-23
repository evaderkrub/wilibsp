#include <string.h>
#include "fw2kb.h"
#include "test_util.h"

#define S 0x02   /* HID LShift */

/* Feed usage+mods, expect exactly one CHAR event with the given char */
static void expect_char(uint8_t usage, uint8_t mods, char want)
{
    fw2kb_t kb;
    fw2kb_init(&kb);
    fw2kb_event ev;
    if (!fw2kb_hid(&kb, usage, mods)) {
        printf("FAIL: usage 0x%02x mods 0x%02x unmapped (want '%c')\n",
               usage, mods, want);
        g_test_failures++;
        return;
    }
    CHECK(fw2kb_next_event(&kb, &ev));
    CHECK(ev.key == FW2KB_KEY_CHAR);
    if (ev.ch != want) {
        printf("FAIL: usage 0x%02x mods 0x%02x -> '%c', want '%c'\n",
               usage, mods, ev.ch, want);
        g_test_failures++;
    }
    CHECK(!fw2kb_next_event(&kb, &ev));   /* exactly one event */
}

static void expect_key(uint8_t usage, uint8_t mods, fw2kb_key want)
{
    fw2kb_t kb;
    fw2kb_init(&kb);
    fw2kb_event ev;
    CHECK(fw2kb_hid(&kb, usage, mods));
    CHECK(fw2kb_next_event(&kb, &ev));
    CHECK(ev.key == want);
}

static void test_letters_and_digits(void)
{
    for (char c = 'a'; c <= 'z'; c++) expect_char((uint8_t)(0x04 + (c - 'a')), 0, c);
    for (char c = 'A'; c <= 'Z'; c++) expect_char((uint8_t)(0x04 + (c - 'A')), S, c);
    for (char c = '1'; c <= '9'; c++) expect_char((uint8_t)(0x1e + (c - '1')), 0, c);
    expect_char(0x27, 0, '0');
}

static void test_punctuation(void)
{
    /* independent US-layout expectations */
    static const struct { char ch; uint8_t usage; uint8_t mod; } k[] = {
        {' ',0x2c,0}, {'!',0x1e,S}, {'"',0x34,S}, {'#',0x20,S}, {'$',0x21,S},
        {'%',0x22,S}, {'&',0x24,S}, {'\'',0x34,0}, {'(',0x26,S}, {')',0x27,S},
        {'*',0x25,S}, {'+',0x2e,S}, {',',0x36,0}, {'-',0x2d,0}, {'.',0x37,0},
        {'/',0x38,0}, {':',0x33,S}, {';',0x33,0}, {'<',0x36,S}, {'=',0x2e,0},
        {'>',0x37,S}, {'?',0x38,S}, {'@',0x1f,S}, {'[',0x2f,0}, {'\\',0x31,0},
        {']',0x30,0}, {'^',0x23,S}, {'_',0x2d,S}, {'`',0x35,0}, {'{',0x2f,S},
        {'|',0x31,S}, {'}',0x30,S}, {'~',0x35,S},
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++)
        expect_char(k[i].usage, k[i].mod, k[i].ch);
}

static void test_nav_and_edit_keys(void)
{
    expect_key(0x28, 0, FW2KB_KEY_ENTER);
    expect_key(0x2A, 0, FW2KB_KEY_BACKSPACE);
    expect_key(0x2B, 0, FW2KB_KEY_TAB);
    expect_key(0x4A, 0, FW2KB_KEY_HOME);
    expect_key(0x4B, 0, FW2KB_KEY_PAGEUP);
    expect_key(0x4C, 0, FW2KB_KEY_DEL);
    expect_key(0x4D, 0, FW2KB_KEY_END);
    expect_key(0x4E, 0, FW2KB_KEY_PAGEDOWN);
    expect_key(0x4F, 0, FW2KB_KEY_RIGHT);
    expect_key(0x50, 0, FW2KB_KEY_LEFT);
    expect_key(0x51, 0, FW2KB_KEY_DOWN);
    expect_key(0x52, 0, FW2KB_KEY_UP);
}

static void test_ctrl_chords(void)
{
    expect_key(0x16, 0x01, FW2KB_KEY_SAVE);   /* Ctrl+S */
    expect_key(0x1B, 0x01, FW2KB_KEY_EXIT);   /* Ctrl+X */
    expect_key(0x06, 0x01, FW2KB_KEY_EXIT);   /* Ctrl+C alias */
    expect_key(0x16, 0x10, FW2KB_KEY_SAVE);   /* RCtrl works too */

    fw2kb_t kb;
    fw2kb_init(&kb);
    CHECK(!fw2kb_hid(&kb, 0x04, 0x01));       /* Ctrl+A unmapped -> false */
    CHECK(!fw2kb_hid(&kb, 0x4A, 0x01));       /* Ctrl+Home: ctrl branch wins, unmapped */
}

static void test_shift_variants_and_unmapped(void)
{
    expect_char(0x04, 0x20, 'A');             /* RShift acts as shift */

    fw2kb_t kb;
    fw2kb_init(&kb);
    fw2kb_event ev;
    CHECK(!fw2kb_hid(&kb, 0x2c, S));          /* Shift+Space: no match */
    CHECK(!fw2kb_hid(&kb, 0x65, 0));          /* Application key: unmapped */
    CHECK(!fw2kb_hid(&kb, 0x00, 0));          /* no-event report */
    CHECK(!fw2kb_next_event(&kb, &ev));       /* none of those queued anything */
}

int main(void)
{
    test_letters_and_digits();
    test_punctuation();
    test_nav_and_edit_keys();
    test_ctrl_chords();
    test_shift_variants_and_unmapped();
    if (test_failures() == 0) printf("test_fw2kb_hid: all passed\n");
    return test_failures();
}
