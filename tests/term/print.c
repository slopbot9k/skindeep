/* Printing as xterm.js's InputHandler.print does it: wrapping and the
 * pending wrap, wide characters and their placeholders, combining marks
 * joining the cell before, charsets, insert mode and REP. */
#include "../../src/term/screen.h"
#include "screen.h"

static void test_plain(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "hello");
    CHECK_ROWS(x.t, "hello\n\n");
    CHECK_CURSOR(x.t, 5, 0);
    tt_close(&x);
}

static void test_pending_wrap(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "0123456789");
    /* The cursor waits past the last column. */
    CHECK_CURSOR(x.t, 10, 0);
    tt_feed(&x, "a");
    CHECK_ROWS(x.t, "0123456789\na\n");
    CHECK_CURSOR(x.t, 1, 1);
    CHECK(sk_screen_line(x.t->active, 1)->wrapped);
    tt_close(&x);

    tt_open(&x, 10, 3);
    tt_feed(&x, "0123456789\rab");
    CHECK_ROWS(x.t, "ab23456789\n\n");
    tt_close(&x);

    /* LF at the pending wrap keeps the cursor on the last column. */
    tt_open(&x, 10, 3);
    tt_feed(&x, "0123456789\na");
    CHECK_ROWS(x.t, "0123456789\n         a\n");
    CHECK_CURSOR(x.t, 10, 1);
    CHECK(!sk_screen_line(x.t->active, 1)->wrapped);
    tt_close(&x);
}

static void test_scroll(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "1\r\n2\r\n3\r\n4");
    CHECK_ROWS(x.t, "2\n3\n4");
    int top, base, lines;
    sk_term_viewport(x.t, &top, &base, &lines);
    CHECK_INT(top, 1);
    CHECK_INT(base, 1);
    CHECK_INT(lines, 4);
    char *first = sk_term_line_text(x.t, 0);
    CHECK_STR(first, "1");
    free(first);
    tt_close(&x);
}

static void test_line_cells(void)
{
    /* Lines by their index in the buffer, scrollback first, with the
     * flag that says each goes on from the line before. */
    tt x;
    tt_open(&x, 4, 2);
    tt_feed(&x, "ab\r\n\x1b[1mcdefg");
    sk_term_cell cells[4];
    int cols = 0;
    bool wrapped = true;
    CHECK(sk_term_line(x.t, 0, cells, &cols, &wrapped));
    CHECK_INT(cols, 4);
    CHECK(!wrapped);
    CHECK_INT((int)cells[1].cp, 'b');
    CHECK_INT((int)cells[2].cp, 0);
    CHECK(sk_term_line(x.t, 2, cells, &cols, &wrapped));
    CHECK(wrapped);
    CHECK_INT((int)cells[0].cp, 'g');
    CHECK(cells[0].fg & SK_FG_BOLD);
    int top, base, lines;
    sk_term_viewport(x.t, &top, &base, &lines);
    sk_term_cell row[4];
    sk_term_row(x.t, 1, row);
    CHECK(sk_term_line(x.t, top + 1, cells, &cols, &wrapped));
    CHECK(memcmp(row, cells, sizeof row) == 0);
    CHECK(!sk_term_line(x.t, 3, cells, &cols, &wrapped));
    CHECK(!wrapped);
    CHECK(!sk_term_line(x.t, -1, cells, &cols, &wrapped));
    tt_close(&x);
}

static void test_wide(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "\xE6\x97\xA5\xE6\x9C\xAC");
    CHECK_ROWS(x.t, "\xE6\x97\xA5\xE6\x9C\xAC\n\n");
    CHECK_CURSOR(x.t, 4, 0);
    sk_term_cell c = tt_cell(x.t, 0, 0);
    CHECK_INT(c.cp, 0x65E5);
    CHECK_INT(c.width, 2);
    c = tt_cell(x.t, 1, 0);
    CHECK_INT(c.cp, 0);
    CHECK_INT(c.width, 0);
    tt_close(&x);
}

static void test_wide_at_line_end(void)
{
    /* No room for both halves: the last cell is blanked in the current
     * colours and the character wraps. */
    tt x;
    tt_open(&x, 5, 3);
    tt_feed(&x, "\x1b[41mabcd\xE6\x97\xA5");
    CHECK_ROWS(x.t, "abcd\n\xE6\x97\xA5\n");
    CHECK_CURSOR(x.t, 2, 1);
    sk_term_cell c = tt_cell(x.t, 4, 0);
    CHECK_INT(c.cp, 0);
    CHECK_INT(c.width, 1);
    CHECK_INT(c.bg, SK_ATTR_CM_P16 | 1);
    CHECK(sk_screen_line(x.t->active, 1)->wrapped);
    tt_close(&x);

    /* Without wraparound it is dropped. */
    tt_open(&x, 10, 3);
    tt_feed(&x, "\x1b[?7l" "012345678\xE6\x97\xA5");
    CHECK_ROWS(x.t, "012345678\n\n");
    CHECK_CURSOR(x.t, 9, 0);
    tt_close(&x);
}

static void test_wide_overwritten(void)
{
    /* Writing over the first half blanks the orphaned second half. */
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "\xE6\x97\xA5\xE6\x9C\xAC\ra");
    CHECK_ROWS(x.t, "a \xE6\x9C\xAC\n\n");
    sk_term_cell c = tt_cell(x.t, 1, 0);
    CHECK_INT(c.width, 1);
    tt_close(&x);

    /* Writing over the second half blanks the first. */
    tt_open(&x, 10, 3);
    tt_feed(&x, "\xE6\x97\xA5\xE6\x9C\xAC\x1b[2Gb");
    CHECK_ROWS(x.t, " b\xE6\x9C\xAC\n\n");
    tt_close(&x);
}

static void test_combining(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "e\xCC\x81x");
    CHECK_ROWS(x.t, "e\xCC\x81x\n\n");
    CHECK_CURSOR(x.t, 2, 0);
    sk_term_cell c = tt_cell(x.t, 0, 0);
    CHECK_INT(c.ncomb, 2);
    CHECK_INT(c.comb[0], 'e');
    CHECK_INT(c.comb[1], 0x301);
    CHECK_INT(c.width, 1);
    tt_close(&x);

    /* After a control nothing precedes it: the mark takes a cell of
     * width 0 and moves the cursor, and trimming ignores it. */
    tt_open(&x, 10, 3);
    tt_feed(&x, "a\r\xCC\x81");
    CHECK_ROWS(x.t, "\n\n");
    CHECK_CURSOR(x.t, 1, 0);
    c = tt_cell(x.t, 0, 0);
    CHECK_INT(c.cp, 0x301);
    CHECK_INT(c.width, 0);
    tt_close(&x);

    /* A mark joins a wide character from its placeholder's right. */
    tt_open(&x, 10, 3);
    tt_feed(&x, "\xE6\x97\xA5\xCC\x81z");
    c = tt_cell(x.t, 0, 0);
    CHECK_INT(c.ncomb, 2);
    CHECK_INT(c.width, 2);
    CHECK_CURSOR(x.t, 3, 0);
    tt_close(&x);
}

static void test_emoji(void)
{
    tt x;
    tt_open(&x, 10, 3);
    /* A variation selector joins; the warning sign stays one column. */
    tt_feed(&x, "\xE2\x9A\xA0\xEF\xB8\x8F!");
    sk_term_cell c = tt_cell(x.t, 0, 0);
    CHECK_INT(c.ncomb, 2);
    CHECK_INT(c.width, 1);
    CHECK_CURSOR(x.t, 2, 0);
    tt_feed(&x, "\r\n\xF0\x9F\x98\x80!");
    c = tt_cell(x.t, 0, 1);
    CHECK_INT(c.cp, 0x1F600);
    CHECK_INT(c.width, 2);
    CHECK_CURSOR(x.t, 3, 1);
    tt_close(&x);
}

static void test_charsets(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "\x1b(0lqk\x1b(Bq");
    CHECK_ROWS(x.t, "\xE2\x94\x8C\xE2\x94\x80\xE2\x94\x90q\n\n");
    tt_feed(&x, "\r\n\x1b)0\x0eq\x0fq");
    CHECK_ROWS(x.t, "\xE2\x94\x8C\xE2\x94\x80\xE2\x94\x90q\n\xE2\x94\x80q\n");
    /* UK: # is a pound sign; Dutch [ is the first unit of "ij". */
    tt_feed(&x, "\r\n\x1b(A#\x1b(4[\x1b(B#");
    CHECK_ROWS(x.t, "\xE2\x94\x8C\xE2\x94\x80\xE2\x94\x90q\n\xE2\x94\x80q\n\xC2\xA3i#");
    tt_close(&x);

    /* ESC / designates nothing; ESC % G resets G0 and the level. */
    tt_open(&x, 10, 3);
    tt_feed(&x, "\x1b/0q\x1b(0\x0e\x1b%Gq");
    CHECK_ROWS(x.t, "qq\n\n");
    tt_close(&x);

    /* DECRC restores the charset DECSC saw. */
    tt_open(&x, 10, 3);
    tt_feed(&x, "\x1b(0\x1b" "7\x1b(Bq\x1b" "8q");
    CHECK_ROWS(x.t, "\xE2\x94\x80\n\n");
    tt_close(&x);
}

static void test_insert_mode(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "abc\r\x1b[4hX");
    CHECK_ROWS(x.t, "Xabc\n\n");
    tt_feed(&x, "\x1b[4lY");
    CHECK_ROWS(x.t, "XYbc\n\n");
    tt_close(&x);

    /* A wide character pushed to the last column is lost. */
    tt_open(&x, 5, 3);
    tt_feed(&x, "abc\xE6\x97\xA5\r\x1b[4hX");
    CHECK_ROWS(x.t, "Xabc\n\n");
    tt_close(&x);
}

static void test_rep(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "a\x1b[3b");
    CHECK_ROWS(x.t, "aaaa\n\n");
    /* After any sequence there is nothing to repeat. */
    tt_feed(&x, "\r\x1b[3b");
    CHECK_ROWS(x.t, "aaaa\n\n");
    tt_feed(&x, "\r\n\xE6\x97\xA5\x1b[2b");
    CHECK_ROWS(x.t, "aaaa\n\xE6\x97\xA5\xE6\x97\xA5\xE6\x97\xA5\n");
    /* It wraps like printing. */
    tt_feed(&x, "\r\nz\x1b[11b");
    CHECK_ROWS(x.t, "\xE6\x97\xA5\xE6\x97\xA5\xE6\x97\xA5\nzzzzzzzzzz\nzz");
    tt_close(&x);
}

static void test_rep_huge(void)
{
    /* A count far past the scrollback ends where printing it would. */
    tt x, y;
    tt_open(&x, 7, 3);
    tt_open(&y, 7, 3);
    tt_feed(&x, "q\x1b[2147483647b");
    int64_t count = 2147483647, per = 7, settled = (int64_t)(5003 + 3 + 2) * per;
    int64_t same = settled + (count - settled) % per;
    sk_buf s = {0};
    sk_buf_printf(&s, "q\x1b[%lldb", (long long)same);
    tt_feed(&y, s.ptr);
    sk_buf_free(&s);
    char *a = tt_dump(x.t), *b = tt_dump(y.t);
    CHECK_STR(a, b);
    CHECK_CURSOR(x.t, (int)((count + 1) % 7 ? (count + 1) % 7 : 7), 2);
    free(a);
    free(b);
    tt_close(&x);
    tt_close(&y);
}

static void test_zero_width_at_pending_wrap(void)
{
    /* A mark with nothing to join, at the pending wrap, is written past
     * the line, where JavaScript drops it, and still moves the cursor. */
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "0123456789\x1b[m\xCC\x81");
    CHECK_CURSOR(x.t, 11, 0);
    CHECK_ROWS(x.t, "0123456789\n\n");
    tt_feed(&x, "\x1b[6n");
    CHECK_REPLY(&x, "\x1b[1;12R");
    tt_feed(&x, "a");
    CHECK_ROWS(x.t, "0123456789\na\n");
    tt_close(&x);
}

static void test_wraparound_off(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "\x1b[?7l" "0123456789AB");
    CHECK_ROWS(x.t, "012345678B\n\n");
    CHECK_CURSOR(x.t, 10, 0);
    tt_close(&x);
}

static void test_split_utf8(void)
{
    const char *s = "\xE2\x94\x80\xE6\x97\xA5\xF0\x9F\x98\x80" "e\xCC\x81";
    tt whole;
    tt_open(&whole, 10, 3);
    tt_feed(&whole, s);
    char *want = tt_dump(whole.t);
    for (size_t cut = 1; cut < strlen(s); cut++) {
        tt x;
        tt_open(&x, 10, 3);
        tt_feedn(&x, s, cut);
        tt_feed(&x, s + cut);
        char *got = tt_dump(x.t);
        CHECK_STR(got, want);
        free(got);
        tt_close(&x);
    }
    free(want);
    tt_close(&whole);
}

const sk_test sk_suite_term_print[] = {
    T(test_plain),
    T(test_pending_wrap),
    T(test_scroll),
    T(test_line_cells),
    T(test_wide),
    T(test_wide_at_line_end),
    T(test_wide_overwritten),
    T(test_combining),
    T(test_emoji),
    T(test_charsets),
    T(test_insert_mode),
    T(test_rep),
    T(test_rep_huge),
    T(test_zero_width_at_pending_wrap),
    T(test_wraparound_off),
    T(test_split_utf8),
    {0},
};
