/* Selection as xterm.js's SelectionService makes it and the text its
 * getSelection gives: by cell, by word with its separators, by line,
 * across wrapped lines and the scrollback; and renderer.js's screenText. */
#include "../../src/term/screen.h"
#include "screen.h"

#define CHECK_SELECTION(t, want)                                             \
    do {                                                                     \
        char *s_ = sk_term_selection_text(t);                                \
        CHECK_STR(s_, want);                                                 \
        free(s_);                                                            \
    } while (0)

static void test_cells(void)
{
    tt x;
    tt_open(&x, 20, 4);
    tt_feed(&x, "hello world\r\nsecond line");
    sk_term_select_start(x.t, SK_SELECT_CHAR, 0, 0);
    CHECK(!sk_term_has_selection(x.t));
    sk_term_select_move(x.t, 5, 0);
    CHECK(sk_term_has_selection(x.t));
    CHECK_SELECTION(x.t, "hello");
    CHECK(sk_term_selected(x.t, 4, 0));
    CHECK(!sk_term_selected(x.t, 5, 0));
    sk_term_select_move(x.t, 6, 1);
    CHECK_SELECTION(x.t, "hello world\nsecond");
    /* Backwards. */
    sk_term_select_start(x.t, SK_SELECT_CHAR, 3, 1);
    sk_term_select_move(x.t, 6, 0);
    CHECK_SELECTION(x.t, "world\nsec");
    /* Past the text, trailing blanks are trimmed. */
    sk_term_select_start(x.t, SK_SELECT_CHAR, 0, 0);
    sk_term_select_move(x.t, 20, 2);
    CHECK_SELECTION(x.t, "hello world\nsecond line\n");
    sk_term_select_clear(x.t);
    CHECK(!sk_term_has_selection(x.t));
    CHECK_SELECTION(x.t, "");
    tt_close(&x);
}

static void test_wrapped_join(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "0123456789abcde\r\nnext");
    sk_term_select_start(x.t, SK_SELECT_CHAR, 0, 0);
    sk_term_select_move(x.t, 4, 2);
    CHECK_SELECTION(x.t, "0123456789abcde\nnext");
    tt_close(&x);
}

static void test_wide(void)
{
    /* A press on a wide character's second half starts after it. */
    tt x;
    tt_open(&x, 10, 2);
    tt_feed(&x, "a\xE6\x97\xA5" "b");
    sk_term_select_start(x.t, SK_SELECT_CHAR, 2, 0);
    sk_term_select_move(x.t, 4, 0);
    CHECK_SELECTION(x.t, "b");
    /* A drag onto one takes it whole. */
    sk_term_select_start(x.t, SK_SELECT_CHAR, 0, 0);
    sk_term_select_move(x.t, 2, 0);
    CHECK_SELECTION(x.t, "a\xE6\x97\xA5");
    tt_close(&x);
}

static void test_word(void)
{
    tt x;
    tt_open(&x, 20, 3);
    tt_feed(&x, "foo bar(baz) qux");
    sk_term_select_start(x.t, SK_SELECT_CHAR, 5, 0);
    sk_term_select_start(x.t, SK_SELECT_WORD, 5, 0);
    CHECK_SELECTION(x.t, "bar");
    sk_term_select_start(x.t, SK_SELECT_WORD, 9, 0);
    CHECK_SELECTION(x.t, "baz");
    /* On blanks the run of blanks. */
    tt_feed(&x, "\r\na    b");
    sk_term_select_start(x.t, SK_SELECT_WORD, 3, 1);
    CHECK_SELECTION(x.t, "    ");
    /* Dragging extends by words. */
    sk_term_select_start(x.t, SK_SELECT_WORD, 1, 0);
    sk_term_select_move(x.t, 14, 0);
    CHECK_SELECTION(x.t, "foo bar(baz) qux");
    tt_close(&x);
}

static void test_word_wrapped(void)
{
    tt x;
    tt_open(&x, 5, 3);
    tt_feed(&x, "abcdefgh");
    sk_term_select_start(x.t, SK_SELECT_WORD, 1, 1);
    CHECK_SELECTION(x.t, "abcdefgh");
    sk_term_select_start(x.t, SK_SELECT_WORD, 2, 0);
    CHECK_SELECTION(x.t, "abcdefgh");
    tt_close(&x);
}

static void test_word_wide_and_emoji(void)
{
    tt x;
    tt_open(&x, 20, 2);
    tt_feed(&x, "x \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E y \xF0\x9F\x98\x80z w");
    sk_term_select_start(x.t, SK_SELECT_WORD, 4, 0);
    CHECK_SELECTION(x.t, "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E");
    sk_term_select_start(x.t, SK_SELECT_WORD, 11, 0);
    CHECK_SELECTION(x.t, "\xF0\x9F\x98\x80z");
    tt_close(&x);
}

static void test_line(void)
{
    tt x;
    tt_open(&x, 5, 4);
    tt_feed(&x, "one\r\nabcdefgh\r\nlast");
    sk_term_select_start(x.t, SK_SELECT_LINE, 2, 2);
    CHECK_SELECTION(x.t, "abcdefgh");
    sk_term_select_start(x.t, SK_SELECT_LINE, 0, 0);
    sk_term_select_move(x.t, 0, 3);
    CHECK_SELECTION(x.t, "one\nabcdefgh\nlast");
    tt_close(&x);
}

static void test_all(void)
{
    tt x;
    tt_open(&x, 10, 2);
    tt_feed(&x, "1\r\n2\r\n3\xC2\xA0x");
    sk_term_select_all(x.t);
    CHECK_SELECTION(x.t, "1\n2\n3 x");
    tt_close(&x);
}

static void test_scrollback(void)
{
    tt x;
    tt_open(&x, 10, 2);
    tt_feed(&x, "1\r\n2\r\n3\r\n4");
    sk_term_scroll(x.t, -2);
    sk_term_select_start(x.t, SK_SELECT_CHAR, 0, 0);
    sk_term_select_move(x.t, 1, 1);
    CHECK_SELECTION(x.t, "1\n2");
    sk_term_select_drag_scroll(x.t, 1);
    CHECK_SELECTION(x.t, "1\n2\n3\n4");
    /* Trimming the scrollback moves it up, and drops what it lost. */
    tt_feed(&x, "\x1b[3J");
    CHECK_SELECTION(x.t, "3\n4");
    tt_close(&x);
}

static void test_cleared(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "hello");
    sk_term_select_start(x.t, SK_SELECT_CHAR, 0, 0);
    sk_term_select_move(x.t, 3, 0);
    tt_feed(&x, "\x1b[?1049h");
    CHECK(!sk_term_has_selection(x.t));
    tt_feed(&x, "\x1b[?1049l");
    sk_term_select_start(x.t, SK_SELECT_CHAR, 0, 0);
    sk_term_select_move(x.t, 3, 0);
    /* A change of width keeps it; of height drops it. */
    sk_term_resize(x.t, 12, 3);
    CHECK(sk_term_has_selection(x.t));
    sk_term_resize(x.t, 12, 4);
    CHECK(!sk_term_has_selection(x.t));
    tt_close(&x);
}

static void test_extend(void)
{
    tt x;
    tt_open(&x, 20, 2);
    tt_feed(&x, "abcdefghij");
    sk_term_select_start(x.t, SK_SELECT_CHAR, 2, 0);
    sk_term_select_extend(x.t, 6, 0);
    CHECK_SELECTION(x.t, "cdef");
    /* A column selection takes the same columns of each line. */
    tt_feed(&x, "\r\nklmnopqrst");
    sk_term_select_start(x.t, SK_SELECT_COLUMN, 1, 0);
    sk_term_select_move(x.t, 3, 1);
    CHECK_SELECTION(x.t, "bc\nlm");
    tt_close(&x);
}

static void test_screen_text(void)
{
    tt x;
    tt_open(&x, 10, 6);
    tt_feed(&x, "  \r\nhello  \r\n\r\nworld\xC2\xA0\r\n");
    char *s = sk_term_screen_text(x.t);
    CHECK_STR(s, "hello  \n\nworld\xC2\xA0");
    free(s);
    /* The screen is the bottom of the buffer, wherever the view is. */
    tt_feed(&x, "1\r\n2\r\n3\r\n4\r\n5\r\n6");
    sk_term_scroll(x.t, -3);
    s = sk_term_screen_text(x.t);
    CHECK_STR(s, "1\n2\n3\n4\n5\n6");
    free(s);
    tt_close(&x);
}

const sk_test sk_suite_term_select[] = {
    T(test_cells),
    T(test_wrapped_join),
    T(test_wide),
    T(test_word),
    T(test_word_wrapped),
    T(test_word_wide_and_emoji),
    T(test_line),
    T(test_all),
    T(test_scrollback),
    T(test_cleared),
    T(test_extend),
    T(test_screen_text),
    {0},
};
