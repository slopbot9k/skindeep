/* Cursor movement as xterm.js moves it: CUP and the relative moves with
 * their margins, origin mode, tabs, save and restore, backspace with
 * reverse wrap, index and reverse index. */
#include "../../src/term/screen.h"
#include "screen.h"

static void test_cup(void)
{
    tt x;
    tt_open(&x, 10, 5);
    tt_feed(&x, "\x1b[2;3Hx");
    CHECK_ROWS(x.t, "\n  x\n\n\n");
    tt_feed(&x, "\x1b[H");
    CHECK_CURSOR(x.t, 0, 0);
    tt_feed(&x, "\x1b[;5H");
    CHECK_CURSOR(x.t, 4, 0);
    /* One parameter: the column is 1. */
    tt_feed(&x, "\x1b[3;7H\x1b[4H");
    CHECK_CURSOR(x.t, 0, 3);
    tt_feed(&x, "\x1b[99;99H");
    CHECK_CURSOR(x.t, 9, 4);
    tt_feed(&x, "\x1b[2;3f");
    CHECK_CURSOR(x.t, 2, 1);
    tt_close(&x);
}

static void test_relative(void)
{
    tt x;
    tt_open(&x, 10, 5);
    tt_feed(&x, "\x1b[3;5H\x1b[A");
    CHECK_CURSOR(x.t, 4, 1);
    tt_feed(&x, "\x1b[2B");
    CHECK_CURSOR(x.t, 4, 3);
    tt_feed(&x, "\x1b[100C");
    CHECK_CURSOR(x.t, 9, 3);
    tt_feed(&x, "\x1b[3D");
    CHECK_CURSOR(x.t, 6, 3);
    tt_feed(&x, "\x1b[E");
    CHECK_CURSOR(x.t, 0, 4);
    tt_feed(&x, "\x1b[5G\x1b[2F");
    CHECK_CURSOR(x.t, 0, 2);
    tt_feed(&x, "\x1b[7`");
    CHECK_CURSOR(x.t, 6, 2);
    tt_feed(&x, "\x1b[2a");
    CHECK_CURSOR(x.t, 8, 2);
    tt_feed(&x, "\x1b[1d");
    CHECK_CURSOR(x.t, 8, 0);
    tt_feed(&x, "\x1b[3e");
    CHECK_CURSOR(x.t, 8, 3);
    /* A huge count stops at the edge. */
    tt_feed(&x, "\x1b[2147483647A\x1b[2147483647D");
    CHECK_CURSOR(x.t, 0, 0);
    tt_close(&x);
}

static void test_margins(void)
{
    tt x;
    tt_open(&x, 10, 5);
    /* CUU and CUD stop at the margins from inside the region... */
    tt_feed(&x, "\x1b[2;4r\x1b[4;1H\x1b[5A");
    CHECK_CURSOR(x.t, 0, 1);
    tt_feed(&x, "\x1b[9B");
    CHECK_CURSOR(x.t, 0, 3);
    /* ...and not from outside it. */
    tt_feed(&x, "\x1b[1;1H\x1b[3B");
    CHECK_CURSOR(x.t, 0, 3);
    tt_feed(&x, "\x1b[5;1H\x1b[A");
    CHECK_CURSOR(x.t, 0, 3);
    tt_feed(&x, "\x1b[5;1H\x1b[B");
    CHECK_CURSOR(x.t, 0, 4);
    tt_close(&x);
}

static void test_scroll_region(void)
{
    tt x;
    tt_open(&x, 10, 5);
    tt_feed(&x, "a\r\nb\r\nc\r\nd\r\ne");
    /* DECSTBM homes the cursor; a bad region is ignored. */
    tt_feed(&x, "\x1b[2;4r");
    CHECK_CURSOR(x.t, 0, 0);
    tt_feed(&x, "\x1b[4;1H\n");
    CHECK_ROWS(x.t, "a\nc\nd\n\ne");
    tt_feed(&x, "\x1b[3;2r");
    CHECK(x.t->active->scroll_top == 1 && x.t->active->scroll_bottom == 3);
    /* The region scrolls nothing into the scrollback. */
    int top, base, lines;
    sk_term_viewport(x.t, &top, &base, &lines);
    CHECK_INT(base, 0);
    /* Reverse index at the top margin scrolls the region down. */
    tt_feed(&x, "\x1b[2;1H\x1bM");
    CHECK_ROWS(x.t, "a\n\nc\nd\ne");
    tt_feed(&x, "\x1b[r");
    CHECK(x.t->active->scroll_top == 0 && x.t->active->scroll_bottom == 4);
    tt_close(&x);
}

static void test_scroll_region_top(void)
{
    /* A region from the top row scrolls into the scrollback, keeping the
     * rows below it in place. */
    tt x;
    tt_open(&x, 10, 4);
    tt_feed(&x, "a\r\nb\r\nc\r\nS\x1b[1;3r\x1b[3;1H\n");
    CHECK_ROWS(x.t, "b\nc\n\nS");
    int top, base, lines;
    sk_term_viewport(x.t, &top, &base, &lines);
    CHECK_INT(base, 1);
    char *l0 = sk_term_line_text(x.t, 0);
    CHECK_STR(l0, "a");
    free(l0);
    tt_close(&x);
}

static void test_origin(void)
{
    tt x;
    tt_open(&x, 10, 6);
    tt_feed(&x, "\x1b[2;5r\x1b[?6h");
    CHECK_CURSOR(x.t, 0, 1);
    tt_feed(&x, "\x1b[1;1Hx");
    CHECK_ROWS(x.t, "\nx\n\n\n\n");
    tt_feed(&x, "\x1b[10;1H");
    CHECK_CURSOR(x.t, 0, 4);
    /* CPR reports the row from the top of the screen. */
    tt_feed(&x, "\x1b[6n");
    CHECK_REPLY(&x, "\x1b[5;1R");
    /* CHA keeps the row but adds the margin again in origin mode. */
    tt_feed(&x, "\x1b[2;1H\x1b[5G");
    CHECK_CURSOR(x.t, 4, 3);
    tt_feed(&x, "\x1b[?6l");
    CHECK_CURSOR(x.t, 0, 0);
    tt_close(&x);
}

static void test_tabs(void)
{
    tt x;
    tt_open(&x, 20, 3);
    tt_feed(&x, "a\tb\tc");
    CHECK_ROWS(x.t, "a       b       c\n\n");
    tt_feed(&x, "\r\n\x1b[5G\x1bH\r\tx");
    CHECK_CURSOR(x.t, 5, 1);
    /* TBC 0 clears the stop under the cursor. */
    tt_feed(&x, "\x1b[9G\x1b[g\r\x1b[2Iy");
    CHECK_CURSOR(x.t, 17, 1);
    tt_feed(&x, "\x1b[3g\r\t");
    CHECK_CURSOR(x.t, 19, 1);
    tt_feed(&x, "\x1b[5G\x1bH\x1b[18G\x1b[Z");
    CHECK_CURSOR(x.t, 4, 1);
    tt_feed(&x, "\x1b[Z");
    CHECK_CURSOR(x.t, 0, 1);
    /* C1 HTS. */
    tt_feed(&x, "\x1b[11G\xC2\x88\r\t");
    CHECK_CURSOR(x.t, 4, 1);
    tt_feed(&x, "\t");
    CHECK_CURSOR(x.t, 10, 1);
    tt_close(&x);

    /* No tab from the pending wrap. */
    tt_open(&x, 10, 3);
    tt_feed(&x, "0123456789\t");
    CHECK_CURSOR(x.t, 10, 0);
    tt_close(&x);
}

static void test_tabs_resize(void)
{
    /* Widening adds stops from the last one before the old width. */
    tt x;
    tt_open(&x, 10, 3);
    sk_term_resize(x.t, 30, 3);
    tt_feed(&x, "\t\t\t");
    CHECK_CURSOR(x.t, 24, 0);
    tt_close(&x);
}

static void test_save_restore(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "\x1b[2;3H\x1b" "7\x1b[H\x1b" "8x");
    CHECK_ROWS(x.t, "\n  x\n");
    tt_feed(&x, "\x1b[31m\x1b[s\x1b[0m\x1b[3;1H\x1b[uy");
    sk_term_cell c = tt_cell(x.t, 3, 1);
    CHECK_INT(c.cp, 'y');
    CHECK_INT(c.fg, SK_ATTR_CM_P16 | 1);
    tt_close(&x);
}

static void test_backspace(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "ab\bX");
    CHECK_ROWS(x.t, "aX\n\n");
    tt_feed(&x, "\r\b\b");
    CHECK_CURSOR(x.t, 0, 0);
    /* From the pending wrap BS lands on the last but one column. */
    tt_feed(&x, "\r0123456789\bX");
    CHECK_ROWS(x.t, "01234567X9\n\n");
    tt_close(&x);
}

static void test_reverse_wrap(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "\x1b[?45h" "0123456789a\b\bZ");
    CHECK_ROWS(x.t, "012345678Z\na\n");
    CHECK(!sk_screen_line(x.t->active, 1)->wrapped);
    CHECK_CURSOR(x.t, 10, 0);
    tt_close(&x);
}

static void test_index(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "a\x1b" "Db\x1b" "Ec");
    CHECK_ROWS(x.t, "a\n b\nc");
    tt_feed(&x, "\xC2\x84" "d\xC2\x85" "e");
    CHECK_ROWS(x.t, "c\n d\ne");
    tt_feed(&x, "\x1b[H\x1bMz");
    CHECK_ROWS(x.t, "z\nc\n d");
    tt_close(&x);
}

static void test_line_feed(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "a\nb\x0b" "c\x0c" "d");
    CHECK_ROWS(x.t, " b\n  c\n   d");
    tt_close(&x);
    /* LNM: a line feed returns too. */
    tt_open(&x, 10, 3);
    tt_feed(&x, "\x1b[20ha\nb\x1b[20l\nc");
    CHECK_ROWS(x.t, "a\nb\n c");
    tt_close(&x);
    /* A line feed ends the wrap of the line it reaches. */
    tt_open(&x, 10, 3);
    tt_feed(&x, "0123456789a\x1b[H\n");
    CHECK(!sk_screen_line(x.t->active, 1)->wrapped);
    tt_close(&x);
}

const sk_test sk_suite_term_cursor[] = {
    T(test_cup),
    T(test_relative),
    T(test_margins),
    T(test_scroll_region),
    T(test_scroll_region_top),
    T(test_origin),
    T(test_tabs),
    T(test_tabs_resize),
    T(test_save_restore),
    T(test_backspace),
    T(test_reverse_wrap),
    T(test_index),
    T(test_line_feed),
    {0},
};
