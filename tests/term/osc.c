/* OSC titles and OSC 8 links as xterm.js handles them: a link is an id on
 * every cell printed while it is open, kept while a line it was printed
 * on lives. */
#include "../../src/term/screen.h"
#include "screen.h"

static void test_titles(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "\x1b]0;hello\x07");
    CHECK_STR(sk_buf_cstr(&x.title), "hello");
    CHECK_STR(x.t->icon, "hello");
    tt_feed(&x, "\x1b]2;w\xC3\xB6rld\x1b\\");
    CHECK_STR(sk_buf_cstr(&x.title), "w\xC3\xB6rld");
    tt_feed(&x, "\x1b]1;icon\x07");
    CHECK_STR(sk_buf_cstr(&x.title), "w\xC3\xB6rld");
    CHECK_STR(x.t->icon, "icon");
    /* OSC 7 and others are ignored, and print nothing. */
    tt_feed(&x, "\x1b]7;file://host/tmp\x07\x1b]52;c;aGk=\x07\x1b]133;A\x07");
    CHECK_ROWS(x.t, "\n\n");
    tt_close(&x);
}

static void test_bell(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "a\x07" "b\x07");
    CHECK_INT(x.bells, 2);
    CHECK_ROWS(x.t, "ab\n\n");
    tt_close(&x);
}

static void test_link(void)
{
    tt x;
    tt_open(&x, 20, 3);
    tt_feed(&x, "\x1b]8;;https://a.example/\x07link\x1b]8;;\x07 plain");
    sk_term_cell c = tt_cell(x.t, 0, 0);
    CHECK(c.bg & SK_BG_HAS_EXTENDED);
    CHECK(c.link != 0);
    CHECK_STR(sk_term_link_uri(x.t, c.link), "https://a.example/");
    CHECK(sk_term_link_id(x.t, c.link) == NULL);
    CHECK_INT(tt_cell(x.t, 3, 0).link, c.link);
    sk_term_cell after = tt_cell(x.t, 5, 0);
    CHECK_INT(after.bg & SK_BG_HAS_EXTENDED, 0);
    /* A link draws a dashed underline only when underlined. */
    CHECK_INT(sk_term_cell_underline(&c), SK_UNDERLINE_NONE);
    tt_close(&x);
}

static void test_link_ids(void)
{
    tt x;
    tt_open(&x, 20, 3);
    tt_feed(&x, "\x1b]8;id=x;https://b/\x07" "ab\x1b]8;;\x07 \x1b]8;id=x;https://b/\x07" "cd\x1b]8;;\x07");
    uint32_t a = tt_cell(x.t, 0, 0).link, b = tt_cell(x.t, 3, 0).link;
    CHECK(a != 0);
    CHECK_INT(a, b);
    CHECK_STR(sk_term_link_id(x.t, a), "x");
    /* Without an id each opening is a new link. */
    tt_feed(&x, "\r\n\x1b]8;;https://b/\x07" "e\x1b]8;;\x07\x1b]8;;https://b/\x07" "f");
    CHECK(tt_cell(x.t, 0, 1).link != tt_cell(x.t, 1, 1).link);
    /* Opening a link closes the last one. */
    tt_feed(&x, "\x1b]8;foo=1:id=y;https://c/\x07g");
    uint32_t g = tt_cell(x.t, 2, 1).link;
    CHECK_STR(sk_term_link_uri(x.t, g), "https://c/");
    CHECK_STR(sk_term_link_id(x.t, g), "y");
    tt_close(&x);
}

/* A link to every cell, each its own id: they are looked up by a map,
 * not each against every other, and only the newest 16384 or so are
 * kept, so the oldest's cells link to nothing. */
static void test_link_many(void)
{
    tt x;
    tt_open(&x, 20, 3);
    sk_buf b = {0};
    for (int i = 0; i < 40000; i++)
        sk_buf_printf(&b, "\x1b]8;id=%d;https://x/\x07" "a", i);
    tt_feed(&x, sk_buf_cstr(&b));
    uint32_t last = tt_cell(x.t, 19, 2).link;
    CHECK(last != 0);
    CHECK_STR(sk_term_link_id(x.t, last), "39999");
    CHECK(sk_term_link_uri(x.t, 1) == NULL);
    /* The newest one's id again is the same link. */
    tt_feed(&x, "\r\n\x1b]8;id=39999;https://x/\x07" "b");
    CHECK_INT(tt_cell(x.t, 0, 2).link, last);
    sk_buf_free(&b);
    tt_close(&x);
}

static void test_link_edges(void)
{
    tt x;
    tt_open(&x, 20, 3);
    /* No ; at all: nothing. */
    tt_feed(&x, "\x1b]8;x\x07" "a");
    CHECK_INT(tt_cell(x.t, 0, 0).link, 0);
    /* Params but no URI: nothing either, so an open link stays open. */
    tt_feed(&x, "\x1b]8;;https://d/\x07" "b\x1b]8;id=a;\x07" "c");
    CHECK(tt_cell(x.t, 2, 0).link != 0);
    CHECK_INT(tt_cell(x.t, 2, 0).link, tt_cell(x.t, 1, 0).link);
    /* Blank params close it. */
    tt_feed(&x, "\x1b]8; \xC2\xA0;\x07" "d");
    CHECK_INT(tt_cell(x.t, 3, 0).link, 0);
    /* SGR 0 keeps a link open. */
    tt_feed(&x, "\x1b]8;;https://e/\x07\x1b[0m" "e");
    CHECK(tt_cell(x.t, 4, 0).link != 0);
    tt_close(&x);
}

static void test_link_lifetime(void)
{
    tt x;
    tt_open(&x, 20, 3);
    tt_feed(&x, "\x1b]8;;https://f/\x07" "f\x1b]8;;\x07");
    uint32_t id = tt_cell(x.t, 0, 0).link;
    CHECK(sk_term_link_uri(x.t, id) != NULL);
    /* Erasing the screen resets its lines and their markers. */
    tt_feed(&x, "\x1b[2J");
    CHECK(sk_term_link_uri(x.t, id) == NULL);
    /* Leaving the alternate screen drops the links printed there. */
    tt_feed(&x, "\x1b[?1049h\x1b[H\x1b]8;;https://g/\x07" "g\x1b]8;;\x07");
    id = tt_cell(x.t, 0, 0).link;
    CHECK(sk_term_link_uri(x.t, id) != NULL);
    tt_feed(&x, "\x1b[?1049l");
    CHECK(sk_term_link_uri(x.t, id) == NULL);
    tt_close(&x);
}

static void test_link_moves(void)
{
    /* A link wrapped across lines marks each line, and lives while one
     * of them does. */
    tt x;
    tt_open(&x, 4, 2);
    tt_feed(&x, "\x1b]8;;https://h/\x07" "abcdef\x1b]8;;\x07");
    uint32_t id = tt_cell(x.t, 0, 0).link;
    CHECK_INT(tt_cell(x.t, 1, 1).link, id);
    /* ED 1 resets the first line, dropping its marker, and only erases
     * cells on the second, whose marker keeps the link. */
    tt_feed(&x, "\x1b[2;1H\x1b[1J");
    CHECK(sk_term_link_uri(x.t, id) != NULL);
    tt_feed(&x, "\x1b[1;1H\x1b[J");
    CHECK(sk_term_link_uri(x.t, id) == NULL);
    tt_close(&x);
}

const sk_test sk_suite_term_osc[] = {
    T(test_titles),
    T(test_bell),
    T(test_link),
    T(test_link_ids),
    T(test_link_edges),
    T(test_link_many),
    T(test_link_lifetime),
    T(test_link_moves),
    {0},
};
