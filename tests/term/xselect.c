/* xterm.js 6.0.0's own SelectionService tests (src/browser/services/
 * SelectionService.test.ts), ported onto the C selection: word
 * selection across wide characters, separators, wrapped lines and
 * emoji, line selection, select all, column selection and the
 * in-selection test. Lines are made as the tests make them, as long as
 * their text, on a 20 by 20 terminal. */
#include "../../src/base/utf8.h"
#include "../../src/term/screen.h"
#include "screen.h"

static void put_line(tt *x, int y, sk_line *l)
{
    sk_lines_set(&x->t->active->lines, y, l);
}

/* stringToRow: a cell of width 1 for each UTF-16 unit; every character
 * here is in the BMP. */
static sk_line *row_of(const char *s)
{
    uint32_t cp[64];
    size_t n = 0, len = strlen(s);
    for (size_t i = 0; i < len && n < SK_LEN(cp);)
        i += sk_utf8_decode(s + i, len - i, &cp[n++], NULL);
    sk_cell_data null = {SK_CELL_NULL, 0, 0, {0, 0}, NULL};
    sk_line *l = sk_line_new((int)n, &null, false);
    for (size_t i = 0; i < n; i++) {
        sk_cell_data c = {cp[i] | 1u << SK_CELL_WIDTH_SHIFT, 0, 0, {0, 0}, NULL};
        sk_line_set(l, (int)i, &c);
    }
    return l;
}

/* stringArrayToRow: a cell of width 1 for each string, combined when it
 * holds more than one character. */
static sk_line *row_cells(const char *const *cells, int n)
{
    sk_cell_data null = {SK_CELL_NULL, 0, 0, {0, 0}, NULL};
    sk_line *l = sk_line_new(n, &null, false);
    for (int i = 0; i < n; i++) {
        uint32_t cp[17];
        uint32_t k = 0;
        size_t len = strlen(cells[i]);
        for (size_t j = 0; j < len && k < 16;)
            j += sk_utf8_decode(cells[i] + j, len - j, &cp[1 + k++], NULL);
        cp[0] = k;
        sk_cell_data c = {cp[1] | 1u << SK_CELL_WIDTH_SHIFT, 0, 0, {0, 0}, NULL};
        if (k > 1)
            c = (sk_cell_data){SK_CELL_COMBINED | 1u << SK_CELL_WIDTH_SHIFT, 0, 0, {0, 0}, cp};
        sk_line_set(l, i, &c);
    }
    return l;
}

static void check_word(tt *x, int cx, int cy, const char *want, int line)
{
    sk_term_select_start(x->t, SK_SELECT_WORD, cx, cy);
    char *s = sk_term_selection_text(x->t);
    if (strcmp(s, want))
        sk_test_fail(__FILE__, line, "word at %d,%d: \"%s\", want \"%s\"", cx, cy, s, want);
    free(s);
}

#define WORD(x, cx, cy, want) check_word(x, cx, cy, want, __LINE__)

static void test_normal_width(void)
{
    tt x;
    tt_open(&x, 20, 20);
    put_line(&x, 0, row_of("foo bar"));
    const char *want[] = {"foo", "foo", "foo", " ", "bar", "bar", "bar"};
    for (int i = 0; i < 7; i++)
        WORD(&x, i, 0, want[i]);
    put_line(&x, 0, row_of("a   b"));
    const char *ws[] = {"a", "   ", "   ", "   ", "b"};
    for (int i = 0; i < 5; i++)
        WORD(&x, i, 0, ws[i]);
    tt_close(&x);
}

static void test_wide(void)
{
    tt x;
    tt_open(&x, 20, 20);
    const struct {
        uint32_t cp;
        int w;
    } data[] = {{0x4E2D, 2}, {0, 0}, {0x6587, 2}, {0, 0}, {' ', 1},   {'a', 1}, {0x4E2D, 2}, {0, 0},
                {0x6587, 2}, {0, 0}, {'b', 1},    {' ', 1}, {'f', 1}, {'o', 1}, {'o', 1}};
    sk_cell_data null = {SK_CELL_NULL, 0, 0, {0, 0}, NULL};
    sk_line *l = sk_line_new((int)SK_LEN(data), &null, false);
    for (size_t i = 0; i < SK_LEN(data); i++) {
        sk_cell_data c = {data[i].cp | (uint32_t)data[i].w << SK_CELL_WIDTH_SHIFT, 0, 0, {0, 0}, NULL};
        sk_line_set(l, (int)i, &c);
    }
    put_line(&x, 0, l);
#define ZW "\xE4\xB8\xAD\xE6\x96\x87"
    const char *want[] = {ZW, ZW, ZW, ZW, " ", "a" ZW "b", "a" ZW "b", "a" ZW "b",
                          "a" ZW "b", "a" ZW "b", "a" ZW "b", " ", "foo", "foo", "foo"};
    for (int i = 0; i < 15; i++)
        WORD(&x, i, 0, want[i]);
    tt_close(&x);
}

static void test_separators(void)
{
    tt x;
    tt_open(&x, 20, 20);
    put_line(&x, 0, row_of("(cd)[ef]{gh}'ij\""));
    const char *want[] = {"(cd", "cd", "cd", "cd)", "[ef", "ef", "ef", "ef]",
                          "{gh", "gh", "gh", "gh}", "'ij", "ij", "ij", "ij\""};
    for (int i = 0; i < 16; i++)
        WORD(&x, i, 0, want[i]);
    tt_close(&x);
}

static void test_wrapped(void)
{
    tt x;
    tt_open(&x, 20, 20);
    put_line(&x, 0, row_of("                 foo"));
    put_line(&x, 1, row_of("bar                 "));
    sk_screen_line(x.t->active, 1)->wrapped = true;
    WORD(&x, 1, 1, "foobar");
    sk_term_select_clear(x.t);
    WORD(&x, 18, 0, "foobar");
    tt_close(&x);

    tt_open(&x, 20, 20);
    const char *rows[] = {"                 foo", "aaaaaaaaaaaaaaaaaaaa", "bbbbbbbbbbbbbbbbbbbb",
                          "cccccccccccccccccccc", "bar                 "};
    for (int i = 0; i < 5; i++) {
        put_line(&x, i, row_of(rows[i]));
        sk_screen_line(x.t->active, i)->wrapped = i > 0;
    }
    const char *want = "fooaaaaaaaaaaaaaaaaaaaabbbbbbbbbbbbbbbbbbbbccccccccccccccccccccbar";
    WORD(&x, 18, 0, want);
    WORD(&x, 10, 1, want);
    WORD(&x, 10, 2, want);
    WORD(&x, 10, 3, want);
    WORD(&x, 1, 4, want);
    tt_close(&x);
}

#define BALL "\xE2\x9A\xBD"

static void test_emoji(void)
{
    tt x;
    tt_open(&x, 20, 20);
    put_line(&x, 0, row_of(" " BALL " a"));
    WORD(&x, 0, 0, " ");
    WORD(&x, 1, 0, BALL);
    WORD(&x, 2, 0, " ");
    put_line(&x, 0, row_of(" " BALL BALL " a"));
    WORD(&x, 0, 0, " ");
    WORD(&x, 1, 0, BALL BALL);
    WORD(&x, 2, 0, BALL BALL);
    WORD(&x, 3, 0, " ");

#define MAN "\xF0\x9F\x91\xA8\xE2\x80\x8D"
#define WOMAN "\xF0\x9F\x91\xA9\xE2\x80\x8D"
#define GIRL "\xF0\x9F\x91\xA7\xE2\x80\x8D"
#define BOY "\xF0\x9F\x91\xA6"
    const char *family[] = {" ", MAN, WOMAN, GIRL, BOY, " ", "a"};
    put_line(&x, 0, row_cells(family, 7));
    WORD(&x, 0, 0, " ");
    for (int i = 1; i <= 4; i++)
        WORD(&x, i, 0, MAN WOMAN GIRL BOY);
    WORD(&x, 5, 0, " ");

    put_line(&x, 0, row_of(" " BALL "ab cd" BALL " ef" BALL "gh"));
    const char *mixed[] = {" ", BALL "ab", BALL "ab", BALL "ab", " ", "cd" BALL, "cd" BALL,
                           "cd" BALL, " ", "ef" BALL "gh", "ef" BALL "gh", "ef" BALL "gh", "ef" BALL "gh",
                           "ef" BALL "gh"};
    for (int i = 0; i < 14; i++)
        WORD(&x, i, 0, mixed[i]);

#define FLAG "\xF0\x9F\x8F\xB4\xF3\xA0\x81\xA7\xF3\xA0\x81\xA2\xF3\xA0\x81\xA5\xF3\xA0\x81\xAE\xF3\xA0\x81\xA7\xF3\xA0\x81\xBF"
    const char *flags[] = {" ", FLAG, "a", "b", " ", "c", "d", FLAG, " ", "e", "f", FLAG, "g", "h", " ", "a"};
    put_line(&x, 0, row_cells(flags, 16));
    const char *fw[] = {" ", FLAG "ab", FLAG "ab", FLAG "ab", " ", "cd" FLAG, "cd" FLAG, "cd" FLAG, " ",
                        "ef" FLAG "gh", "ef" FLAG "gh", "ef" FLAG "gh", "ef" FLAG "gh", "ef" FLAG "gh"};
    for (int i = 0; i < 14; i++)
        WORD(&x, i, 0, fw[i]);
    tt_close(&x);
}

static void test_line(void)
{
    tt x;
    tt_open(&x, 20, 20);
    put_line(&x, 0, row_of("foo bar"));
    sk_term_select_start(x.t, SK_SELECT_LINE, 0, 0);
    char *s = sk_term_selection_text(x.t);
    CHECK_STR(s, "foo bar");
    free(s);
    CHECK_INT(x.t->sel.start_len, 20);
    CHECK(!x.t->sel.has_end);
    int x0, y0, x1, y1;
    CHECK(sk_term_selection_range(x.t, &x0, &y0, &x1, &y1, NULL));
    CHECK(x0 == 0 && y0 == 0 && x1 == 20 && y1 == 0);

    put_line(&x, 0, row_of("foo"));
    sk_line *bar = row_of("bar");
    bar->wrapped = true;
    put_line(&x, 1, bar);
    sk_term_select_start(x.t, SK_SELECT_LINE, 0, 0);
    s = sk_term_selection_text(x.t);
    CHECK_STR(s, "foobar");
    free(s);
    CHECK_INT(x.t->sel.start_len, 40);
    CHECK(sk_term_selection_range(x.t, &x0, &y0, &x1, &y1, NULL));
    CHECK(x0 == 0 && y0 == 0 && x1 == 20 && y1 == 1);
    tt_close(&x);
}

static void test_select_all(void)
{
    tt x;
    tt_open(&x, 20, 20);
    sk_term_resize(x.t, 20, 5);
    const char *rows[] = {"1", "2", "3", "4", "5"};
    for (int i = 0; i < 5; i++)
        put_line(&x, i, row_of(rows[i]));
    sk_term_select_all(x.t);
    char *s = sk_term_selection_text(x.t);
    CHECK_STR(s, "1\n2\n3\n4\n5");
    free(s);
    tt_close(&x);
}

static void test_has_selection(void)
{
    tt x;
    tt_open(&x, 20, 20);
    sk_selection *m = &x.t->sel;
    m->has_start = true;
    m->sx = m->sy = 0;
    m->start_len = 0;
    CHECK(!sk_term_has_selection(x.t));
    m->has_end = true;
    const int ends[][3] = {{0, 0, 0}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1}};
    for (int i = 0; i < 4; i++) {
        m->ex = ends[i][0];
        m->ey = ends[i][1];
        CHECK_INT(sk_term_has_selection(x.t), ends[i][2]);
    }
    tt_close(&x);
}

static void column(tt *x, int sx, int sy, int ex, int ey)
{
    sk_selection *m = &x->t->sel;
    m->mode = SK_SEL_COLUMN;
    m->has_start = m->has_end = true;
    m->sx = sx;
    m->sy = sy;
    m->ex = ex;
    m->ey = ey;
}

static void test_column(void)
{
    tt x;
    tt_open(&x, 20, 20);
    put_line(&x, 0, row_of("abcdefghij"));
    put_line(&x, 1, row_of("klmnopqrst"));
    put_line(&x, 2, row_of("uvwxyz"));
    column(&x, 2, 0, 4, 2);
    char *s = sk_term_selection_text(x.t);
    CHECK_STR(s, "cd\nmn\nwx");
    free(s);
    tt_close(&x);

    tt_open(&x, 20, 20);
    put_line(&x, 0, row_of("a"));
    put_line(&x, 1, row_of("\xE8\xAA\x9E"));
    put_line(&x, 2, row_of("b"));
    column(&x, 0, 0, 1, 2);
    s = sk_term_selection_text(x.t);
    CHECK_STR(s, "a\n\xE8\xAA\x9E\nb");
    free(s);
    put_line(&x, 1, row_of("\xE2\x98\x83"));
    s = sk_term_selection_text(x.t);
    CHECK_STR(s, "a\n\xE2\x98\x83\nb");
    free(s);
    put_line(&x, 0, row_of("a "));
    const char *mid[] = {"\xF0\x9F\x98\x81", " "};
    put_line(&x, 1, row_cells(mid, 2));
    put_line(&x, 2, row_of("c "));
    s = sk_term_selection_text(x.t);
    CHECK_STR(s, "a\n\xF0\x9F\x98\x81\nc");
    free(s);
    tt_close(&x);
}

static void test_in_selection(void)
{
    tt x;
    tt_open(&x, 20, 20);
    sk_selection *m = &x.t->sel;
    m->mode = SK_SEL_NORMAL;
    m->has_start = m->has_end = true;
    m->sx = 2;
    m->sy = 0;
    m->ex = 2;
    m->ey = 1;
    const int cases[][3] = {{0, 0, 0}, {1, 0, 0}, {2, 0, 1}, {10, 0, 1}, {0, 1, 1}, {1, 1, 1}, {2, 1, 0}};
    for (int i = 0; i < 7; i++)
        CHECK_INT(sk_term_selected(x.t, cases[i][0], cases[i][1]), cases[i][2]);
    tt_close(&x);
}

const sk_test sk_suite_term_xselect[] = {
    T(test_normal_width),
    T(test_wide),
    T(test_separators),
    T(test_wrapped),
    T(test_emoji),
    T(test_line),
    T(test_select_all),
    T(test_has_selection),
    T(test_column),
    T(test_in_selection),
    {0},
};
