/* Resizing as xterm.js's Buffer.resize does it: wrapped lines rejoin and
 * split again, wide characters move whole to the next line, the
 * cursor's line is left alone, rows come from and go to the scrollback,
 * and the scrollback keeps its 5000 lines. */
#include "../../src/term/screen.h"
#include "screen.h"

static bool wrapped(tt *x, int line)
{
    return sk_screen_line(x->t->active, line)->wrapped;
}

static void test_narrow_and_back(void)
{
    tt x;
    tt_open(&x, 10, 4);
    tt_feed(&x, "0123456789abcde\r\n");
    sk_term_resize(x.t, 5, 4);
    CHECK_ROWS(x.t, "01234\n56789\nabcde\n");
    CHECK_CURSOR(x.t, 0, 3);
    CHECK(!wrapped(&x, 0) && wrapped(&x, 1) && wrapped(&x, 2) && !wrapped(&x, 3));
    sk_term_resize(x.t, 10, 4);
    CHECK_ROWS(x.t, "0123456789\nabcde\n\n");
    CHECK_CURSOR(x.t, 0, 2);
    CHECK(wrapped(&x, 1) && !wrapped(&x, 2));
    tt_close(&x);
}

static void test_cursor_line_untouched(void)
{
    /* The cursor's wrapped line is the program's to redraw: narrowing
     * cuts it rather than reflow it. */
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "0123456789abcde");
    sk_term_resize(x.t, 5, 3);
    CHECK_ROWS(x.t, "01234\nabcde\n");
    CHECK_CURSOR(x.t, 4, 1);
    tt_close(&x);
}

static void test_wide_at_boundary(void)
{
    tt x;
    tt_open(&x, 6, 3);
    tt_feed(&x, "abcde\xE6\x97\xA5\r\n");
    CHECK_ROWS(x.t, "abcde\n\xE6\x97\xA5\n");
    sk_term_resize(x.t, 8, 3);
    CHECK_ROWS(x.t, "abcde\xE6\x97\xA5\n\n");
    sk_term_resize(x.t, 6, 3);
    CHECK_ROWS(x.t, "abcde\n\xE6\x97\xA5\n");
    CHECK(wrapped(&x, 1));
    tt_close(&x);
}

static void test_rows_grow_from_scrollback(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "1\r\n2\r\n3\r\n4\r\n5");
    CHECK_ROWS(x.t, "3\n4\n5");
    sk_term_resize(x.t, 10, 5);
    CHECK_ROWS(x.t, "1\n2\n3\n4\n5");
    CHECK_CURSOR(x.t, 1, 4);
    int top, base, lines;
    sk_term_viewport(x.t, &top, &base, &lines);
    CHECK_INT(base, 0);
    /* Shrinking drops the rows below the cursor first, blank or not. */
    tt_feed(&x, "\x1b[2;1H");
    sk_term_resize(x.t, 10, 3);
    CHECK_ROWS(x.t, "1\n2\n3");
    CHECK_CURSOR(x.t, 0, 1);
    tt_close(&x);
}

static void test_rows_shrink_blank_below(void)
{
    tt x;
    tt_open(&x, 10, 5);
    tt_feed(&x, "a\r\nb");
    sk_term_resize(x.t, 10, 2);
    CHECK_ROWS(x.t, "a\nb");
    int top, base, lines;
    sk_term_viewport(x.t, &top, &base, &lines);
    CHECK_INT(base, 0);
    CHECK_INT(lines, 2);
    tt_close(&x);
}

static void test_scrollback_limit(void)
{
    tt x;
    tt_open(&x, 10, 4);
    sk_buf s = {0};
    for (int i = 0; i < 5100; i++)
        sk_buf_printf(&s, "%d\r\n", i);
    tt_feedn(&x, s.ptr, s.len);
    sk_buf_free(&s);
    int top, base, lines;
    sk_term_viewport(x.t, &top, &base, &lines);
    CHECK_INT(lines, 5004);
    CHECK_INT(base, 5000);
    char *first = sk_term_line_text(x.t, 0);
    CHECK_STR(first, "97");
    free(first);
    CHECK_ROWS(x.t, "5097\n5098\n5099\n");
    tt_close(&x);
}

static void test_scrolled_up_stays(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "1\r\n2\r\n3\r\n4\r\n5");
    sk_term_scroll(x.t, -1);
    CHECK_ROWS(x.t, "2\n3\n4");
    tt_feed(&x, "\r\n6\r\n7");
    CHECK_ROWS(x.t, "2\n3\n4");
    int top, base, lines;
    sk_term_viewport(x.t, &top, &base, &lines);
    CHECK_INT(top, 1);
    CHECK_INT(base, 4);
    sk_term_scroll_to_bottom(x.t);
    CHECK_ROWS(x.t, "5\n6\n7");
    /* At the bottom again, output follows. */
    tt_feed(&x, "\r\n8");
    CHECK_ROWS(x.t, "6\n7\n8");
    tt_close(&x);
}

static void test_scrolled_up_full_buffer(void)
{
    /* With the scrollback full, a scrolled-up view keeps its text by
     * moving up as the top line goes. */
    tt x;
    tt_open(&x, 10, 2);
    sk_buf s = {0};
    for (int i = 0; i < 5002; i++)
        sk_buf_printf(&s, "%d\r\n", i);
    tt_feedn(&x, s.ptr, s.len - 2);
    sk_buf_free(&s);
    sk_term_scroll(x.t, -10);
    CHECK_ROWS(x.t, "4990\n4991");
    tt_feed(&x, "\r\nx");
    CHECK_ROWS(x.t, "4990\n4991");
    tt_close(&x);
}

static void test_region_scroll_full(void)
{
    /* A region from the top, scrolling with the scrollback full, inserts
     * below itself and drops the oldest line. */
    tt x;
    tt_open(&x, 10, 3);
    sk_buf s = {0};
    for (int i = 0; i < 5003; i++)
        sk_buf_printf(&s, i < 5002 ? "%d\r\n" : "%d", i);
    tt_feedn(&x, s.ptr, s.len);
    sk_buf_free(&s);
    tt_feed(&x, "\x1b[1;2r\x1b[3;1HS\x1b[2;1H\n");
    CHECK_ROWS(x.t, "5001\n\nS002");
    int top, base, lines;
    sk_term_viewport(x.t, &top, &base, &lines);
    CHECK_INT(base, 5000);
    CHECK_INT(lines, 5003);
    char *first = sk_term_line_text(x.t, 0);
    CHECK_STR(first, "1");
    free(first);
    tt_close(&x);
}

static void test_alt_resize(void)
{
    /* The alternate buffer is not reflowed; the normal one is, under it. */
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "0123456789abc\r\n\x1b[?1049h" "0123456789xy");
    sk_term_resize(x.t, 5, 3);
    /* Its lines keep their cells past the new width. */
    CHECK_ROWS(x.t, "\n0123456789\nxy");
    tt_feed(&x, "\x1b[?1049l");
    CHECK_ROWS(x.t, "56789\nabc\n");
    CHECK_CURSOR(x.t, 0, 2);
    tt_close(&x);
}

static void test_minimum_size(void)
{
    tt x;
    tt_open(&x, 1, 0);
    int cols, rows;
    sk_term_size(x.t, &cols, &rows);
    CHECK_INT(cols, 2);
    CHECK_INT(rows, 1);
    tt_feed(&x, "abc");
    CHECK_ROWS(x.t, "c");
    sk_term_resize(x.t, 0, 0);
    sk_term_size(x.t, &cols, &rows);
    CHECK_INT(cols, 2);
    CHECK_INT(rows, 1);
    tt_close(&x);
}

const sk_test sk_suite_term_reflow[] = {
    T(test_narrow_and_back),
    T(test_cursor_line_untouched),
    T(test_wide_at_boundary),
    T(test_rows_grow_from_scrollback),
    T(test_rows_shrink_blank_below),
    T(test_scrollback_limit),
    T(test_scrolled_up_stays),
    T(test_scrolled_up_full_buffer),
    T(test_region_scroll_full),
    T(test_alt_resize),
    T(test_minimum_size),
    {0},
};
