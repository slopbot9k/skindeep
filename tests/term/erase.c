/* Erasing and editing as xterm.js does them: ED, EL, ECH, ICH, DCH, IL,
 * DL, SU, SD, SL, SR, DECIC, DECDC, DECALN, and selective erase. */
#include "../../src/term/screen.h"
#include "screen.h"

#define DIGITS "0123456789"

static void test_el(void)
{
    tt x;
    tt_open(&x, 10, 2);
    tt_feed(&x, DIGITS "\x1b[5G\x1b[K");
    CHECK_ROWS(x.t, "0123\n");
    tt_feed(&x, "\r" DIGITS "\x1b[5G\x1b[1K");
    CHECK_ROWS(x.t, "     56789\n");
    tt_feed(&x, "\x1b[2K");
    CHECK_ROWS(x.t, "\n");
    /* From the pending wrap EL 0 erases nothing. */
    tt_feed(&x, "\r" DIGITS "\x1b[K");
    CHECK_ROWS(x.t, DIGITS "\n");
    tt_close(&x);
}

static void test_el_wrap(void)
{
    /* EL 0 from column 0 and EL 2 end a line's wrap; EL 1 does not. */
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, DIGITS DIGITS DIGITS);
    tt_feed(&x, "\x1b[2;1H\x1b[1K");
    CHECK(sk_screen_line(x.t->active, 1)->wrapped);
    tt_feed(&x, "\x1b[2;1H\x1b[K");
    CHECK(!sk_screen_line(x.t->active, 1)->wrapped);
    tt_feed(&x, "\x1b[3;5H\x1b[2K");
    CHECK(!sk_screen_line(x.t->active, 2)->wrapped);
    tt_close(&x);
}

static void test_ed(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, DIGITS DIGITS DIGITS "\x1b[2;5H\x1b[J");
    CHECK_ROWS(x.t, DIGITS "\n0123\n");
    tt_feed(&x, "\x1b[H" DIGITS DIGITS DIGITS "\x1b[2;5H\x1b[1J");
    CHECK_ROWS(x.t, "\n     56789\n" DIGITS);
    tt_feed(&x, "\x1b[2J");
    CHECK_ROWS(x.t, "\n\n");
    /* ED 2 leaves the cursor. */
    CHECK_CURSOR(x.t, 4, 1);
    tt_close(&x);
}

static void test_ed_scrollback(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "1\r\n2\r\n3\r\n4\r\n5");
    int top, base, lines;
    sk_term_viewport(x.t, &top, &base, &lines);
    CHECK_INT(base, 2);
    tt_feed(&x, "\x1b[3J");
    sk_term_viewport(x.t, &top, &base, &lines);
    CHECK_INT(base, 0);
    CHECK_INT(top, 0);
    CHECK_INT(lines, 3);
    CHECK_ROWS(x.t, "3\n4\n5");
    tt_close(&x);
}

static void test_ed1_absolute_quirk(void)
{
    /* ED 1 at the last column unwraps buffer line row + 1, counted from
     * the top of the scrollback, not of the screen. */
    tt x;
    tt_open(&x, 4, 2);
    tt_feed(&x, "abcdefghijklmnopqr");
    sk_buffer *b = x.t->active;
    CHECK_INT(b->ybase, 3);
    CHECK(sk_screen_line(b, 2)->wrapped);
    tt_feed(&x, "\x1b[2;4H\x1b[1J");
    CHECK(!sk_screen_line(b, 2)->wrapped);
    CHECK(sk_screen_line(b, 1)->wrapped);
    tt_close(&x);
}

static void test_bce(void)
{
    tt x;
    tt_open(&x, 10, 2);
    tt_feed(&x, "\x1b[7;44m\x1b[2J");
    sk_term_cell c = tt_cell(x.t, 3, 1);
    CHECK_INT(c.bg, SK_ATTR_CM_P16 | 4);
    CHECK_INT(c.fg, 0);
    tt_feed(&x, "\x1b[48;2;1;2;3m\x1b[K");
    c = tt_cell(x.t, 0, 0);
    CHECK_INT(c.bg, SK_ATTR_CM_RGB | 0x010203);
    tt_close(&x);
}

static void test_ech(void)
{
    tt x;
    tt_open(&x, 10, 2);
    tt_feed(&x, DIGITS "\x1b[3G\x1b[3X");
    CHECK_ROWS(x.t, "01   56789\n");
    tt_feed(&x, "\x1b[2147483647X");
    CHECK_ROWS(x.t, "01\n");
    tt_close(&x);
}

static void test_ich_dch(void)
{
    tt x;
    tt_open(&x, 10, 2);
    tt_feed(&x, DIGITS "\x1b[3G\x1b[2@");
    CHECK_ROWS(x.t, "01  234567\n");
    tt_feed(&x, "\x1b[2P");
    CHECK_ROWS(x.t, "01234567\n");
    tt_feed(&x, "\x1b[99P");
    CHECK_ROWS(x.t, "01\n");
    tt_close(&x);

    /* ICH splits a wide character at the cursor. */
    tt_open(&x, 10, 2);
    tt_feed(&x, "a\xE6\x97\xA5" "b\x1b[3G\x1b[@");
    CHECK_ROWS(x.t, "a   b\n");
    tt_close(&x);
}

static void test_il_dl(void)
{
    tt x;
    tt_open(&x, 10, 5);
    tt_feed(&x, "a\r\nb\r\nc\r\nd\r\ne");
    tt_feed(&x, "\x1b[2;3H\x1b[L");
    CHECK_ROWS(x.t, "a\n\nb\nc\nd");
    CHECK_CURSOR(x.t, 0, 1);
    tt_feed(&x, "\x1b[2M");
    CHECK_ROWS(x.t, "a\nc\nd\n\n");
    /* Inside a region only the region moves. */
    tt_feed(&x, "\x1b[H\x1b[2J" "a\r\nb\r\nc\r\nd\r\ne\x1b[2;4r\x1b[3;1H\x1b[9L");
    CHECK_ROWS(x.t, "a\nb\n\n\ne");
    /* Outside it IL does nothing. */
    tt_feed(&x, "\x1b[5;3H\x1b[L");
    CHECK_ROWS(x.t, "a\nb\n\n\ne");
    CHECK_CURSOR(x.t, 2, 4);
    tt_close(&x);
}

static void test_su_sd(void)
{
    tt x;
    tt_open(&x, 10, 4);
    tt_feed(&x, "a\r\nb\r\nc\r\nd\x1b[42m\x1b[S");
    CHECK_ROWS(x.t, "b\nc\nd\n");
    /* SU fills from the bottom in the erase colour... */
    CHECK_INT(tt_cell(x.t, 0, 3).bg, SK_ATTR_CM_P16 | 2);
    tt_feed(&x, "\x1b[2T");
    CHECK_ROWS(x.t, "\n\nb\nc");
    /* ...and SD from the top in the default one. */
    CHECK_INT(tt_cell(x.t, 0, 0).bg, 0);
    /* Nothing goes to the scrollback. */
    int top, base, lines;
    sk_term_viewport(x.t, &top, &base, &lines);
    CHECK_INT(base, 0);
    tt_close(&x);
}

static void test_sl_sr(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, DIGITS "\r\n" DIGITS "\r\n" DIGITS "\x1b[2;2r\x1b[H\x1b[2 @");
    /* DECSTBM 2;2 is ignored: the region stays the whole screen. */
    CHECK_ROWS(x.t, "23456789\n23456789\n23456789");
    tt_feed(&x, "\x1b[3 A");
    CHECK_ROWS(x.t, "   2345678\n   2345678\n   2345678");
    tt_close(&x);
}

static void test_decic_decdc(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, DIGITS "\r\n" DIGITS "\r\n" DIGITS "\x1b[1;2r\x1b[1;3H\x1b[2'}");
    CHECK_ROWS(x.t, "01  234567\n01  234567\n" DIGITS);
    tt_feed(&x, "\x1b[3'~");
    CHECK_ROWS(x.t, "0134567\n0134567\n" DIGITS);
    tt_close(&x);

    /* From the pending wrap the column wraps to 0, as xterm.js's
     * pos %= length makes it. */
    tt_open(&x, 10, 2);
    tt_feed(&x, DIGITS "\x1b['}");
    CHECK_ROWS(x.t, " 012345678\n");
    tt_close(&x);
}

static void test_decaln(void)
{
    tt x;
    tt_open(&x, 4, 2);
    tt_feed(&x, "ab\x1b[2;3r\x1b#8");
    CHECK_ROWS(x.t, "EEEE\nEEEE");
    CHECK_CURSOR(x.t, 0, 0);
    tt_close(&x);
}

static void test_selective_erase(void)
{
    tt x;
    tt_open(&x, 10, 2);
    tt_feed(&x, "\x1b[1\"qAB\x1b[0\"qCD\x1b[?2J");
    CHECK_ROWS(x.t, "AB\n");
    CHECK(tt_cell(x.t, 0, 0).bg & SK_BG_PROTECTED);
    tt_feed(&x, "EF\x1b[?2K");
    CHECK_ROWS(x.t, "AB\n");
    tt_feed(&x, "\x1b[2K");
    CHECK_ROWS(x.t, "\n");
    tt_close(&x);
}

const sk_test sk_suite_term_erase[] = {
    T(test_el),
    T(test_el_wrap),
    T(test_ed),
    T(test_ed_scrollback),
    T(test_ed1_absolute_quirk),
    T(test_bce),
    T(test_ech),
    T(test_ich_dch),
    T(test_il_dl),
    T(test_su_sd),
    T(test_sl_sr),
    T(test_decic_decdc),
    T(test_decaln),
    T(test_selective_erase),
    {0},
};
