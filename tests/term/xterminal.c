/* xterm.js 6.0.0's own Terminal tests (src/browser/Terminal.test.ts) that
 * the screen shows: scrolling the viewport, scroll() with and without
 * scrollback and margins, characters past U+FFFF, combining marks and
 * fullwidth runs over the line end, insert mode and LNM, on its 80 by 24
 * terminal. */
#include "../../src/base/utf8.h"
#include "../../src/term/screen.h"
#include "screen.h"

#define COLS 80
#define ROWS 24
#define YEN "\xEF\xBF\xA5"

static sk_line *ln(tt *x, int i)
{
    return sk_screen_line(x->t->active, i);
}

/* A cell's text as getChars gives it, in UTF-8. */
static char *chars(tt *x, int y, int col)
{
    sk_buf b = {0};
    sk_line *l = ln(x, y);
    uint32_t one;
    const uint32_t *cp;
    int n = sk_line_chars(l, col, &one, &cp);
    for (int i = 0; i < n; i++)
        sk_utf8_append(&b, cp[i]);
    sk_buf_reserve(&b, 0);
    return b.ptr;
}

#define CHECK_CHARS(x, y, col, want)                                         \
    do {                                                                     \
        char *c_ = chars(x, y, col);                                         \
        CHECK_STR(c_, want);                                                 \
        free(c_);                                                            \
    } while (0)

static void write_lines(tt *x, int n)
{
    for (int i = 0; i < n; i++)
        tt_feed(x, "test\r\n");
}

static int ydisp(tt *x)
{
    return x->t->active->ydisp;
}

static void test_scroll_lines(void)
{
    tt x;
    tt_open(&x, COLS, ROWS);
    write_lines(&x, ROWS * 2);
    int start = ROWS + 1;
    CHECK_INT(ydisp(&x), start);
    sk_term_scroll(x.t, -1);
    CHECK_INT(ydisp(&x), start - 1);
    sk_term_scroll(x.t, 1);
    CHECK_INT(ydisp(&x), start);
    sk_term_scroll(x.t, -5);
    CHECK_INT(ydisp(&x), start - 5);
    sk_term_scroll(x.t, 5);
    CHECK_INT(ydisp(&x), start);
    sk_term_scroll(x.t, 1);
    CHECK_INT(ydisp(&x), start);
    for (int i = 0; i < start; i++)
        sk_term_scroll(x.t, -1);
    CHECK_INT(ydisp(&x), 0);
    sk_term_scroll(x.t, -1);
    CHECK_INT(ydisp(&x), 0);
    tt_close(&x);

    /* Pages are rows - 1 lines; to the bottom from anywhere. */
    tt_open(&x, COLS, ROWS);
    write_lines(&x, ROWS * 3);
    start = ROWS * 2 + 1;
    CHECK_INT(ydisp(&x), start);
    sk_term_scroll(x.t, -2 * (ROWS - 1));
    CHECK_INT(ydisp(&x), start - (ROWS - 1) * 2);
    sk_term_scroll_to_bottom(x.t);
    CHECK_INT(ydisp(&x), start);
    sk_term_scroll(x.t, -ydisp(&x));
    CHECK_INT(ydisp(&x), 0);
    sk_term_scroll_to_bottom(x.t);
    CHECK_INT(ydisp(&x), start);
    tt_close(&x);
}

static void mark(tt *x, int y, char c)
{
    sk_cell_data d = {(uint32_t)c, 0, 0, {0, 0}, NULL};
    sk_line_set(ln(x, y), 0, &d);
}

static void check_marks(tt *x, const char *want)
{
    for (int i = 0; want[i]; i++) {
        char *c = chars(x, i, 0);
        char w[2] = {want[i] == '.' ? 0 : want[i], 0};
        if (strcmp(c, w))
            sk_test_fail(__FILE__, __LINE__, "line %d: \"%s\", want \"%s\"", i, c, w);
        free(c);
    }
}

/* Term.scroll() with the margins each case sets. */
static void scroll_case(int scrollback, int top, int bottom, const char *marks, int lines, const char *want)
{
    tt x;
    tt_open(&x, COLS, ROWS);
    sk_term *t = x.t;
    sk_buffer_free(&t->normal);
    sk_buffer_init(&t->normal, ++t->buffer_ids, true, scrollback, COLS, ROWS, &t->listen);
    sk_buffer_fill_viewport(&t->normal, NULL);
    t->active = &t->normal;
    for (int i = 0; marks[i]; i++)
        if (marks[i] != '.')
            mark(&x, i, marks[i]);
    t->active->scroll_top = top;
    t->active->scroll_bottom = bottom;
    /* A line feed at the bottom margin is scroll(). */
    t->active->y = bottom;
    tt_feed(&x, "\n");
    CHECK_INT(t->active->lines.len, lines);
    check_marks(&x, want);
    tt_close(&x);
}

static void test_scroll_function(void)
{
    /* 'a' at 0 and 'b' at the last row; a new line below. */
    char marks[ROWS + 1];
    memset(marks, '.', ROWS);
    marks[ROWS] = 0;
    marks[0] = 'a';
    marks[ROWS - 1] = 'b';
    char want[ROWS + 2];
    memcpy(want, marks, ROWS);
    want[ROWS] = '.';
    want[ROWS + 1] = 0;
    scroll_case(1000, 0, ROWS - 1, marks, ROWS + 1, want);
    scroll_case(1000, 1, ROWS - 1, "abc", ROWS, "ac");
    scroll_case(1000, 0, 3, "abcde", ROWS + 1, "abcd.e");
    scroll_case(1000, 1, 3, "abcde", ROWS, "acd.e");
    /* Without scrollback the top line goes. */
    memset(marks, '.', ROWS);
    marks[0] = 'a';
    marks[1] = 'b';
    marks[ROWS - 1] = 'c';
    memset(want, '.', ROWS);
    want[ROWS] = 0;
    want[0] = 'b';
    want[ROWS - 2] = 'c';
    scroll_case(0, 0, ROWS - 1, marks, ROWS, want);
    scroll_case(0, 1, ROWS - 1, "abc", ROWS, "ac");
    scroll_case(0, 0, 3, "abcde", ROWS, "bcd.e");
    scroll_case(0, 1, 3, "abcde", ROWS, "acd.e");
}

static void utf8_of(uint32_t cp, char out[5])
{
    size_t n = sk_utf8_encode(cp, out);
    out[n] = 0;
}

static void test_surrogates(void)
{
    for (uint32_t cp = 0x10000; cp <= 0x100FF; cp++) {
        char c[5];
        utf8_of(cp, c);
        tt x;
        tt_open(&x, COLS, ROWS);
        tt_feed(&x, c);
        CHECK_CHARS(&x, 0, 0, c);
        CHECK_INT(sk_line_width(ln(&x, 0), 0), 1);
        CHECK_CHARS(&x, 0, 1, "");
        tt_close(&x);

        tt_open(&x, COLS, ROWS);
        x.t->active->x = COLS - 1;
        tt_feed(&x, c);
        CHECK_CHARS(&x, 0, x.t->active->x - 1, c);
        CHECK_CHARS(&x, 1, 0, "");
        tt_close(&x);

        tt_open(&x, COLS, ROWS);
        x.t->active->x = COLS - 1;
        tt_feed(&x, "a");
        tt_feed(&x, c);
        CHECK_CHARS(&x, 0, COLS - 1, "a");
        CHECK_CHARS(&x, 1, 0, c);
        CHECK_CHARS(&x, 1, 1, "");
        tt_close(&x);

        tt_open(&x, COLS, ROWS);
        x.t->active->x = COLS - 1;
        tt_feed(&x, "\x1b[?7la");
        tt_feed(&x, c);
        CHECK_CHARS(&x, 0, COLS - 1, c);
        CHECK_CHARS(&x, 1, 1, "");
        tt_close(&x);
    }
}

static void test_combining(void)
{
    tt x;
    tt_open(&x, COLS, ROWS);
    tt_feed(&x, "cafe\xCC\x81");
    CHECK_CHARS(&x, 0, 3, "e\xCC\x81");
    CHECK_INT(sk_line_width(ln(&x, 0), 3), 1);
    tt_close(&x);

    tt_open(&x, COLS, ROWS);
    x.t->active->x = COLS - 1 - 3;
    tt_feed(&x, "cafe\xCC\x81");
    CHECK_CHARS(&x, 0, COLS - 1, "e\xCC\x81");
    CHECK_CHARS(&x, 0, 1, "");
    CHECK_INT(sk_line_width(ln(&x, 0), 1), 1);
    tt_close(&x);

    const char *runs[] = {"e\xCC\x81", "\xF0\x90\x80\x80\xCC\x81"};
    for (int r = 0; r < 2; r++) {
        tt_open(&x, COLS, ROWS);
        for (int i = 0; i < 99; i++)
            tt_feed(&x, runs[r]);
        for (int i = 0; i < COLS; i++) {
            CHECK_CHARS(&x, 0, i, runs[r]);
            CHECK_INT(sk_line_width(ln(&x, 0), i), 1);
        }
        CHECK_CHARS(&x, 1, 0, runs[r]);
        tt_close(&x);
    }
}

static void check_fullwidth_line(tt *x, int odd, const char *ch)
{
    for (int i = odd; i < COLS - odd; ++i) {
        bool placeholder = (i % 2) != odd;
        char *c = chars(x, 0, i);
        if (strcmp(c, placeholder ? "" : ch) || sk_line_width(ln(x, 0), i) != (placeholder ? 0 : 2))
            sk_test_fail(__FILE__, __LINE__, "cell %d: \"%s\" width %d", i, c, sk_line_width(ln(x, 0), i));
        free(c);
    }
    if (odd) {
        CHECK_CHARS(x, 0, COLS - 1, "");
        CHECK_INT(sk_line_width(ln(x, 0), COLS - 1), 1);
    }
    CHECK_CHARS(x, 1, 0, ch);
    CHECK_INT(sk_line_width(ln(x, 1), 0), 2);
}

static void test_fullwidth(void)
{
    tt x;
    tt_open(&x, COLS, ROWS);
    tt_feed(&x, YEN);
    CHECK_INT(x.t->active->x, 2);
    tt_close(&x);
    tt_open(&x, COLS, ROWS);
    x.t->active->x = 1;
    tt_feed(&x, YEN);
    CHECK_INT(x.t->active->x, 3);
    tt_close(&x);

    const char *runs[] = {YEN, YEN "\xCC\x81", "\xF0\xA0\xB9\xAD\xCC\x81"};
    for (int r = 0; r < 3; r++) {
        for (int odd = 0; odd <= 1; odd++) {
            tt_open(&x, COLS, ROWS);
            x.t->active->x = odd;
            for (int i = 0; i < 49; i++)
                tt_feed(&x, runs[r]);
            check_fullwidth_line(&x, odd, runs[r]);
            tt_close(&x);
        }
    }
}

static void test_insert_mode(void)
{
    tt x;
    tt_open(&x, COLS, ROWS);
    for (int i = 0; i < 8; i++)
        tt_feed(&x, "0123456789");
    x.t->active->x = 10;
    x.t->active->y = 0;
    tt_feed(&x, "\x1b[4habcde");
    CHECK_INT(ln(&x, 0)->len, COLS);
    CHECK_CHARS(&x, 0, 10, "a");
    CHECK_CHARS(&x, 0, 14, "e");
    CHECK_CHARS(&x, 0, 15, "0");
    CHECK_CHARS(&x, 0, 79, "4");
    tt_close(&x);

    tt_open(&x, COLS, ROWS);
    for (int i = 0; i < 8; i++)
        tt_feed(&x, "0123456789");
    x.t->active->x = 10;
    x.t->active->y = 0;
    tt_feed(&x, "\x1b[4h" YEN YEN YEN);
    CHECK_CHARS(&x, 0, 10, YEN);
    CHECK_CHARS(&x, 0, 11, "");
    CHECK_CHARS(&x, 0, 14, YEN);
    CHECK_CHARS(&x, 0, 15, "");
    CHECK_CHARS(&x, 0, 79, "3");
    tt_close(&x);

    tt_open(&x, COLS, ROWS);
    for (int i = 0; i < 40; i++)
        tt_feed(&x, YEN);
    x.t->active->x = 10;
    x.t->active->y = 0;
    tt_feed(&x, "\x1b[4ha");
    CHECK_CHARS(&x, 0, 10, "a");
    CHECK_CHARS(&x, 0, 11, YEN);
    CHECK_CHARS(&x, 0, 79, "");
    tt_feed(&x, "b");
    CHECK_CHARS(&x, 0, 11, "b");
    CHECK_CHARS(&x, 0, 12, YEN);
    CHECK_CHARS(&x, 0, 79, "");
    tt_close(&x);
}

static void test_convert_eol(void)
{
    tt x;
    tt_open(&x, 15, 10);
    tt_feed(&x, "Hello\nWorld");
    CHECK_ROWS(x.t, "Hello\n     World\n\n\n\n\n\n\n\n");
    tt_close(&x);
    tt_open(&x, 15, 10);
    tt_feed(&x, "\x1b[20hHello\nWorld");
    CHECK_ROWS(x.t, "Hello\nWorld\n\n\n\n\n\n\n\n");
    tt_close(&x);
    /* Without Windows heuristics no line is marked wrapped. */
    tt_open(&x, 10, 5);
    tt_feed(&x, "aaaaaaaaaa\n\raaaaaaaaa\n\raaaaaaaaa");
    CHECK(!ln(&x, 0)->wrapped && !ln(&x, 1)->wrapped && !ln(&x, 2)->wrapped);
    tt_close(&x);
}

const sk_test sk_suite_term_xterminal[] = {
    T(test_scroll_lines),
    T(test_scroll_function),
    T(test_surrogates),
    T(test_combining),
    T(test_fullwidth),
    T(test_insert_mode),
    T(test_convert_eol),
    {0},
};
