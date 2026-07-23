#include <string.h>
#include "uartkbd_parse.h"
#include "test_util.h"

/* Build a valid 23-byte frame with the given payload bytes 2-5. */
static void mk_frame(uint8_t f[UARTKBD_FRAME_LEN],
                     uint8_t b2, uint8_t b3, uint8_t b4, uint8_t b5)
{
    memset(f, 0, UARTKBD_FRAME_LEN);
    f[0] = 0xBD; f[1] = 0x1D;
    f[2] = b2; f[3] = b3; f[4] = b4; f[5] = b5;
    uint8_t sum = 0;
    for (int i = 0; i < UARTKBD_FRAME_LEN - 1; i++) sum = (uint8_t)(sum + f[i]);
    f[UARTKBD_FRAME_LEN - 1] = sum;
}

static void feed(uartkbd_parser_t *p, const uint8_t *d, int n)
{
    for (int i = 0; i < n; i++) uartkbd_parse_byte(p, d[i]);
}

/* Prime the parser with an all-buttons-idle frame (first valid frame only
 * latches the baseline; see uartkbd_parse.h). */
static void prime(uartkbd_parser_t *p)
{
    uint8_t f[UARTKBD_FRAME_LEN];
    mk_frame(f, 0, 0, 0, 0);
    feed(p, f, UARTKBD_FRAME_LEN);
}

static void test_valid_frame_latches_buttons(void)
{
    uartkbd_parser_t p;
    uartkbd_parse_init(&p);
    prime(&p);
    uint8_t f[UARTKBD_FRAME_LEN];
    mk_frame(f, 0x11, 0, 0, 0);            /* GREY (b0) + RED (b4) */
    feed(&p, f, UARTKBD_FRAME_LEN);

    CHECK(uartkbd_parse_frames(&p) == 2);
    CHECK(uartkbd_parse_errors(&p) == 0);
    CHECK(uartkbd_parse_buttons(&p) ==
          ((1u << UARTKBD_BTN_GREY) | (1u << UARTKBD_BTN_RED)));

    uartkbd_event_t ev;
    CHECK(uartkbd_parse_next_event(&p, &ev));
    CHECK(ev.btn == UARTKBD_BTN_GREY && ev.pressed);
    CHECK(uartkbd_parse_next_event(&p, &ev));
    CHECK(ev.btn == UARTKBD_BTN_RED && ev.pressed);
    CHECK(!uartkbd_parse_next_event(&p, &ev));
}

static void test_press_then_release_edges(void)
{
    uartkbd_parser_t p;
    uartkbd_parse_init(&p);
    prime(&p);
    uint8_t f[UARTKBD_FRAME_LEN];
    uartkbd_event_t ev;

    mk_frame(f, 0x01, 0, 0, 0);            /* GREY down */
    feed(&p, f, UARTKBD_FRAME_LEN);
    CHECK(uartkbd_parse_next_event(&p, &ev));
    CHECK(ev.btn == UARTKBD_BTN_GREY && ev.pressed);

    mk_frame(f, 0x01, 0, 0, 0);            /* GREY still down: no new event */
    feed(&p, f, UARTKBD_FRAME_LEN);
    CHECK(!uartkbd_parse_next_event(&p, &ev));

    mk_frame(f, 0x00, 0, 0, 0);            /* GREY up */
    feed(&p, f, UARTKBD_FRAME_LEN);
    CHECK(uartkbd_parse_next_event(&p, &ev));
    CHECK(ev.btn == UARTKBD_BTN_GREY && !ev.pressed);
    CHECK(uartkbd_parse_buttons(&p) == 0);
}

static void test_full_button_mapping(void)
{
    /* one frame with every button down: byte2 b0-b5, byte3 b0/b3/b4/b5,
       byte4 b7, byte5 b0-b2 */
    uartkbd_parser_t p;
    uartkbd_parse_init(&p);
    prime(&p);
    uint8_t f[UARTKBD_FRAME_LEN];
    mk_frame(f, 0x3F, 0x39, 0x80, 0x07);
    feed(&p, f, UARTKBD_FRAME_LEN);
    CHECK(uartkbd_parse_buttons(&p) == 0x3FFF);   /* all 14 bits */
    CHECK(uartkbd_parse_flags(&p) == 0);
}

static void test_checksum_reject(void)
{
    uartkbd_parser_t p;
    uartkbd_parse_init(&p);
    prime(&p);
    uint8_t f[UARTKBD_FRAME_LEN];
    mk_frame(f, 0x01, 0, 0, 0);
    f[UARTKBD_FRAME_LEN - 1] ^= 0xFF;      /* corrupt checksum */
    feed(&p, f, UARTKBD_FRAME_LEN);

    uartkbd_event_t ev;
    CHECK(uartkbd_parse_frames(&p) == 1);
    CHECK(uartkbd_parse_errors(&p) == 1);
    CHECK(uartkbd_parse_buttons(&p) == 0);
    CHECK(!uartkbd_parse_next_event(&p, &ev));

    mk_frame(f, 0x01, 0, 0, 0);            /* recovery: next good frame parses */
    feed(&p, f, UARTKBD_FRAME_LEN);
    CHECK(uartkbd_parse_frames(&p) == 2);
    CHECK(uartkbd_parse_next_event(&p, &ev));
    CHECK(ev.btn == UARTKBD_BTN_GREY && ev.pressed);
}

static void test_resync_through_garbage(void)
{
    uartkbd_parser_t p;
    uartkbd_parse_init(&p);
    prime(&p);
    const uint8_t junk[] = { 0x00, 0xFF, 0xBD, 0x77, 0x1D, 0xBD };
    feed(&p, junk, sizeof junk);           /* includes a false 0xBD start */

    uint8_t f[UARTKBD_FRAME_LEN];
    mk_frame(f, 0x02, 0, 0, 0);            /* YELLOW */
    /* NOTE: last junk byte was 0xBD, so the parser may be in expect-0x1D
       state; a real frame starts 0xBD 0x1D — feeding it works either way
       because its first byte re-arms the hunt. */
    feed(&p, f, UARTKBD_FRAME_LEN);
    /* Depending on junk alignment the first frame may be consumed by a
       false sync; feed a second identical frame to prove steady-state. */
    mk_frame(f, 0x02, 0, 0, 0);
    feed(&p, f, UARTKBD_FRAME_LEN);
    CHECK(uartkbd_parse_frames(&p) >= 2);
    CHECK(uartkbd_parse_buttons(&p) == (1u << UARTKBD_BTN_YELLOW));
}

static void test_reserved_bits_masked(void)
{
    uartkbd_parser_t p;
    uartkbd_parse_init(&p);
    prime(&p);
    uint8_t f[UARTKBD_FRAME_LEN];
    /* only reserved bits set: byte2 b6-7, byte3 b6-7, byte4 all but b2/b7,
       byte5 b3-7 */
    mk_frame(f, 0xC0, 0xC0, 0x79, 0xF8);
    feed(&p, f, UARTKBD_FRAME_LEN);
    uartkbd_event_t ev;
    CHECK(uartkbd_parse_frames(&p) == 2);
    CHECK(uartkbd_parse_buttons(&p) == 0);
    CHECK(uartkbd_parse_flags(&p) == 0);
    CHECK(!uartkbd_parse_next_event(&p, &ev));
}

static void test_flags_update_without_events(void)
{
    uartkbd_parser_t p;
    uartkbd_parse_init(&p);
    prime(&p);
    uint8_t f[UARTKBD_FRAME_LEN];
    mk_frame(f, 0, 0x06, 0x04, 0);   /* AUDIO_DET + HOTPLUG_DET + USB_DET */
    feed(&p, f, UARTKBD_FRAME_LEN);
    uartkbd_event_t ev;
    CHECK(uartkbd_parse_flags(&p) ==
          (UARTKBD_FLAG_AUDIO | UARTKBD_FLAG_HOTPLUG | UARTKBD_FLAG_USB));
    CHECK(uartkbd_parse_buttons(&p) == 0);
    CHECK(!uartkbd_parse_next_event(&p, &ev));
}

static void test_ring_overflow_drops_oldest(void)
{
    uartkbd_parser_t p;
    uartkbd_parse_init(&p);
    prime(&p);
    uint8_t f[UARTKBD_FRAME_LEN];
    mk_frame(f, 0x3F, 0x39, 0x80, 0x07);   /* 14 press edges -> 8-slot ring */
    feed(&p, f, UARTKBD_FRAME_LEN);

    uartkbd_event_t ev;
    int n = 0;
    uartkbd_btn_t first = UARTKBD_BTN_COUNT;
    while (uartkbd_parse_next_event(&p, &ev)) {
        if (n == 0) first = ev.btn;
        n++;
    }
    CHECK(n == 8);                          /* 6 oldest dropped */
    CHECK(first == UARTKBD_BTN_NAV_UP);     /* bits 0-5 were dropped */
}

static void test_split_delivery(void)
{
    uartkbd_parser_t p;
    uartkbd_parse_init(&p);
    prime(&p);
    uint8_t f[UARTKBD_FRAME_LEN];
    mk_frame(f, 0x04, 0, 0, 0);            /* GREEN */
    feed(&p, f, 7);                        /* arrives in two chunks */
    feed(&p, f + 7, UARTKBD_FRAME_LEN - 7);
    CHECK(uartkbd_parse_frames(&p) == 2);
    CHECK(uartkbd_parse_buttons(&p) == (1u << UARTKBD_BTN_GREEN));
}

static void test_first_frame_primes_without_events(void)
{
    uartkbd_parser_t p;
    uartkbd_parse_init(&p);
    uint8_t f[UARTKBD_FRAME_LEN];
    /* garbage boot frame: every button bit set (observed on hardware) */
    mk_frame(f, 0x3F, 0x39, 0x80, 0x07);
    feed(&p, f, UARTKBD_FRAME_LEN);
    uartkbd_event_t ev;
    CHECK(!uartkbd_parse_next_event(&p, &ev));          /* no phantom presses */
    CHECK(uartkbd_parse_buttons(&p) == 0x3FFF);         /* but state latched */
    mk_frame(f, 0, 0, 0, 0);                            /* buttons clear */
    feed(&p, f, UARTKBD_FRAME_LEN);
    int releases = 0;
    while (uartkbd_parse_next_event(&p, &ev)) { CHECK(!ev.pressed); releases++; }
    CHECK(releases == 8);                               /* ring holds last 8 */
}

int main(void)
{
    test_valid_frame_latches_buttons();
    test_press_then_release_edges();
    test_full_button_mapping();
    test_checksum_reject();
    test_resync_through_garbage();
    test_reserved_bits_masked();
    test_flags_update_without_events();
    test_ring_overflow_drops_oldest();
    test_split_delivery();
    test_first_frame_primes_without_events();
    if (g_failures == 0) printf("test_uartkbd_parse: all passed\n");
    TEST_RETURN();
}
