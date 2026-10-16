/* xterm.js 6.0.0's own Buffer tests (src/common/buffer/Buffer.test.ts),
 * ported case by case onto the C buffer: resizing, reflow larger and
 * smaller, wide characters, combined data and the markers' moves. The
 * expected values are xterm.js's. */
#include "../../src/term/buffer.h"
#include "../../src/term/term.h"
#include "../test.h"

#include <stdlib.h>

#define INIT_COLS 80
#define INIT_ROWS 24
#define INIT_SCROLLBACK 1000

/* Markers, as Buffer.addMarker's listeners move them. */
typedef struct {
    int line[8];
    bool dead[8];
    int n;
} markers;

static void on_event(void *ud, int buffer, sk_lines_event ev, int index, int amount)
{
    markers *m = ud;
    (void)buffer;
    for (int i = 0; i < m->n; i++) {
        if (m->dead[i])
            continue;
        if (ev == SK_LINES_TRIM) {
            m->line[i] -= amount;
            if (m->line[i] < 0)
                m->dead[i] = true;
        } else if (ev == SK_LINES_INSERT) {
            if (m->line[i] >= index)
                m->line[i] += amount;
        } else {
            if (m->line[i] >= index && m->line[i] < index + amount)
                m->dead[i] = true;
            if (m->line[i] > index)
                m->line[i] -= amount;
        }
    }
}

typedef struct {
    sk_buffer b;
    markers m;
    sk_lines_listener listen;
} rig;

static void rig_open(rig *r, bool scrollback)
{
    memset(r, 0, sizeof *r);
    r->listen = (sk_lines_listener){on_event, &r->m};
    sk_buffer_init(&r->b, 1, scrollback, INIT_SCROLLBACK, INIT_COLS, INIT_ROWS, &r->listen);
}

static void rig_close(rig *r)
{
    sk_buffer_free(&r->b);
}

static sk_line *line(rig *r, int i)
{
    return sk_lines_get(&r->b.lines, i);
}

static void set(rig *r, int y, int x, uint32_t cp, int width)
{
    sk_cell_data c = {cp | (uint32_t)width << SK_CELL_WIDTH_SHIFT, 0, 0, {0, 0}, NULL};
    sk_line_set(line(r, y), x, &c);
}

static void set_str(rig *r, int y, const char *s)
{
    for (int i = 0; s[i]; i++)
        set(r, y, i, (unsigned char)s[i], 1);
}

static char *text(rig *r, int y, bool trim)
{
    sk_buf b = {0};
    sk_line_text(line(r, y), &b, trim, 0, line(r, y)->len);
    sk_buf_reserve(&b, 0);
    return b.ptr;
}

#define CHECK_LINE(r, y, want)                                               \
    do {                                                                     \
        char *t_ = text(r, y, false);                                        \
        CHECK_STR(t_, want);                                                 \
        free(t_);                                                            \
    } while (0)

#define CHECK_TRIMMED(r, y, want)                                            \
    do {                                                                     \
        char *t_ = text(r, y, true);                                         \
        CHECK_STR(t_, want);                                                 \
        free(t_);                                                            \
    } while (0)

static void check_wrapped(rig *r, const int *wrapped, int n)
{
    for (int i = 0; i < r->b.lines.len; i++) {
        bool want = false;
        for (int k = 0; k < n; k++)
            want |= wrapped[k] == i;
        if (line(r, i)->wrapped != want)
            sk_test_fail(__FILE__, __LINE__, "line %d wrapped %d, want %d", i, line(r, i)->wrapped, want);
    }
}

static void blank_on_top(rig *r, int n)
{
    sk_cell_data null = {SK_CELL_NULL, 0, 0, {0, 0}, NULL};
    for (int i = 0; i < n; i++)
        sk_lines_insert(&r->b.lines, 0, sk_buffer_blank(&r->b, &null, false));
}

static void test_resize_cols(void)
{
    rig r;
    rig_open(&r, true);
    sk_buffer_fill_viewport(&r.b, NULL);
    sk_buffer_resize(&r.b, INIT_COLS / 2, INIT_ROWS);
    CHECK_INT(r.b.lines.len, INIT_ROWS);
    for (int i = 0; i < INIT_ROWS; i++)
        CHECK_INT(line(&r, i)->len, INIT_COLS / 2);
    sk_buffer_resize(&r.b, INIT_COLS + 10, INIT_ROWS);
    for (int i = 0; i < INIT_ROWS; i++)
        CHECK_INT(line(&r, i)->len, INIT_COLS + 10);
    rig_close(&r);
}

static void test_resize_rows_reduced(void)
{
    rig r;
    rig_open(&r, true);
    sk_buffer_fill_viewport(&r.b, NULL);
    sk_buffer_resize(&r.b, INIT_COLS, INIT_ROWS - 10);
    CHECK_INT(r.b.lines.len, INIT_ROWS - 10);
    rig_close(&r);

    /* The viewport moves down when the cursor is at the end. */
    rig_open(&r, true);
    sk_buffer_fill_viewport(&r.b, NULL);
    r.b.y = INIT_ROWS - 5 - 1;
    sk_buffer_resize(&r.b, INIT_COLS, INIT_ROWS - 10);
    CHECK_INT(r.b.lines.len, INIT_ROWS - 5);
    CHECK_INT(r.b.ydisp, 5);
    CHECK_INT(r.b.ybase, 5);
    rig_close(&r);

    /* Without scrollback the top is trimmed. */
    rig_open(&r, true);
    sk_buffer_free(&r.b);
    sk_buffer_init(&r.b, 1, true, 0, INIT_COLS, INIT_ROWS, &r.listen);
    CHECK_INT(r.b.lines.max, INIT_ROWS);
    r.b.y = INIT_ROWS - 1;
    sk_buffer_fill_viewport(&r.b, NULL);
    set(&r, 5, 0, 'a', 1);
    set(&r, INIT_ROWS - 1, 0, 'b', 1);
    sk_buffer_resize(&r.b, INIT_COLS, INIT_ROWS - 5);
    CHECK_INT(line(&r, 0)->cells[0].content & SK_CELL_CP_MASK, 'a');
    CHECK_INT(line(&r, INIT_ROWS - 1 - 5)->cells[0].content & SK_CELL_CP_MASK, 'b');
    rig_close(&r);
}

static void test_resize_rows_increased(void)
{
    rig r;
    rig_open(&r, true);
    sk_buffer_fill_viewport(&r.b, NULL);
    sk_buffer_resize(&r.b, INIT_COLS, INIT_ROWS + 10);
    CHECK_INT(r.b.ydisp, 0);
    CHECK_INT(r.b.lines.len, INIT_ROWS + 10);
    rig_close(&r);

    sk_cell_data null = {SK_CELL_NULL, 0, 0, {0, 0}, NULL};
    for (int top = 0; top <= 1; top++) {
        rig_open(&r, true);
        sk_buffer_fill_viewport(&r.b, NULL);
        for (int i = 0; i < 10; i++)
            sk_lines_push(&r.b.lines, sk_buffer_blank(&r.b, &null, false));
        r.b.y = INIT_ROWS - 1;
        r.b.ybase = 10;
        r.b.ydisp = top ? 0 : 10;
        sk_buffer_resize(&r.b, INIT_COLS, INIT_ROWS + 5);
        CHECK_INT(r.b.ydisp, top ? 0 : 5);
        CHECK_INT(r.b.ybase, 5);
        CHECK_INT(r.b.lines.len, INIT_ROWS + 10);
        rig_close(&r);
    }

    rig_open(&r, true);
    sk_buffer_fill_viewport(&r.b, NULL);
    sk_buffer_resize(&r.b, INIT_COLS + 5, INIT_ROWS + 5);
    CHECK_INT(r.b.lines.len, INIT_ROWS + 5);
    for (int i = 0; i < INIT_ROWS + 5; i++)
        CHECK_INT(line(&r, i)->len, INIT_COLS + 5);
    rig_close(&r);
}

static void test_reflow_empty_and_shrink(void)
{
    rig r;
    rig_open(&r, true);
    sk_buffer_fill_viewport(&r.b, NULL);
    sk_buffer_resize(&r.b, INIT_COLS - 5, INIT_ROWS);
    CHECK_INT(r.b.lines.len, INIT_ROWS);
    sk_buffer_resize(&r.b, 5, 10);
    CHECK_INT(r.b.lines.len, 10);
    for (int i = 0; i < 10; i++)
        CHECK_INT(line(&r, i)->len, 5);
    rig_close(&r);
}

static void test_wrap_and_unwrap(void)
{
    rig r;
    rig_open(&r, true);
    sk_buffer_fill_viewport(&r.b, NULL);
    sk_buffer_resize(&r.b, 5, 10);
    set_str(&r, 0, "abcde");
    r.b.y = 1;
    CHECK_LINE(&r, 0, "abcde");
    sk_buffer_resize(&r.b, 1, 10);
    CHECK_INT(r.b.lines.len, 10);
    const char *one[] = {"a", "b", "c", "d", "e", " ", " ", " ", " ", " "};
    for (int i = 0; i < 10; i++)
        CHECK_LINE(&r, i, one[i]);
    sk_buffer_resize(&r.b, 5, 10);
    CHECK_INT(r.b.lines.len, 10);
    CHECK_LINE(&r, 0, "abcde");
    for (int i = 1; i < 10; i++)
        CHECK_LINE(&r, i, "     ");
    rig_close(&r);
}

static void test_discard_past_scrollback(void)
{
    rig r;
    rig_open(&r, true);
    sk_buffer_fill_viewport(&r.b, NULL);
    r.b.scrollback = 1;
    sk_buffer_resize(&r.b, 10, 5);
    set_str(&r, 3, "abcdefghij");
    CHECK_INT(r.b.lines.len, 5);
    r.b.y = 4;
    sk_buffer_resize(&r.b, 2, 5);
    CHECK_INT(r.b.y, 4);
    CHECK_INT(r.b.ybase, 1);
    CHECK_INT(r.b.lines.len, 6);
    const char *two[] = {"ab", "cd", "ef", "gh", "ij", "  "};
    for (int i = 0; i < 6; i++)
        CHECK_LINE(&r, i, two[i]);
    sk_buffer_resize(&r.b, 1, 5);
    CHECK_INT(r.b.y, 4);
    CHECK_INT(r.b.ybase, 1);
    CHECK_INT(r.b.lines.len, 6);
    const char *one[] = {"f", "g", "h", "i", "j", " "};
    for (int i = 0; i < 6; i++)
        CHECK_LINE(&r, i, one[i]);
    sk_buffer_resize(&r.b, 10, 5);
    CHECK_INT(r.b.y, 1);
    CHECK_INT(r.b.ybase, 0);
    CHECK_INT(r.b.lines.len, 5);
    CHECK_LINE(&r, 0, "fghij     ");
    for (int i = 1; i < 5; i++)
        CHECK_LINE(&r, i, "          ");
    rig_close(&r);
}

static void test_remove_right_rows(void)
{
    rig r;
    rig_open(&r, true);
    sk_buffer_fill_viewport(&r.b, NULL);
    sk_buffer_resize(&r.b, 10, 10);
    r.b.y = 2;
    set_str(&r, 0, "abcdefghij");
    set_str(&r, 1, "0123456789");
    sk_buffer_resize(&r.b, 2, 10);
    CHECK_INT(r.b.ybase, 1);
    CHECK_INT(r.b.lines.len, 11);
    const char *two[] = {"ab", "cd", "ef", "gh", "ij", "01", "23", "45", "67", "89", "  "};
    for (int i = 0; i < 11; i++)
        CHECK_LINE(&r, i, two[i]);
    sk_buffer_resize(&r.b, 10, 10);
    CHECK_INT(r.b.ybase, 0);
    CHECK_INT(r.b.lines.len, 10);
    CHECK_LINE(&r, 0, "abcdefghij");
    CHECK_LINE(&r, 1, "0123456789");
    for (int i = 2; i < 10; i++)
        CHECK_LINE(&r, i, "          ");
    rig_close(&r);
}

static void test_combined_data_moves(void)
{
    rig r;
    rig_open(&r, true);
    sk_buffer_fill_viewport(&r.b, NULL);
    sk_buffer_resize(&r.b, 4, 3);
    r.b.y = 2;
    set_str(&r, 0, "abc");
    /* BufferLine.set stores a two-unit string as combined data. */
    static const uint32_t emoji[] = {1, 0x1F601};
    sk_cell_data c = {SK_CELL_COMBINED | 1u << SK_CELL_WIDTH_SHIFT, 0, 0, {0, 0}, emoji};
    sk_line_set(line(&r, 0), 3, &c);
    CHECK_INT(r.b.lines.len, 3);
    CHECK_LINE(&r, 0, "abc\xF0\x9F\x98\x81");
    CHECK_LINE(&r, 1, "    ");
    sk_buffer_resize(&r.b, 2, 3);
    CHECK_LINE(&r, 0, "ab");
    CHECK_LINE(&r, 1, "c\xF0\x9F\x98\x81");
    rig_close(&r);
}

static void fill_three(rig *r)
{
    set_str(r, 0, "abcdefghij");
    set_str(r, 1, "0123456789");
    set_str(r, 2, "klmnopqrst");
}

static void test_markers_reflow(void)
{
    rig r;
    rig_open(&r, true);
    sk_buffer_fill_viewport(&r.b, NULL);
    sk_buffer_resize(&r.b, 10, 16);
    fill_three(&r);
    r.b.y = 3;
    r.m = (markers){{0, 1, 2}, {false}, 3};
    sk_buffer_resize(&r.b, 2, 16);
    const char *two[] = {"ab", "cd", "ef", "gh", "ij", "01", "23", "45", "67", "89", "kl", "mn", "op", "qr", "st"};
    for (int i = 0; i < 15; i++)
        CHECK_LINE(&r, i, two[i]);
    CHECK_INT(r.m.line[0], 0);
    CHECK_INT(r.m.line[1], 5);
    CHECK_INT(r.m.line[2], 10);
    sk_buffer_resize(&r.b, 10, 16);
    CHECK_LINE(&r, 0, "abcdefghij");
    CHECK_LINE(&r, 1, "0123456789");
    CHECK_LINE(&r, 2, "klmnopqrst");
    CHECK_INT(r.m.line[0], 0);
    CHECK_INT(r.m.line[1], 1);
    CHECK_INT(r.m.line[2], 2);
    CHECK(!r.m.dead[0] && !r.m.dead[1] && !r.m.dead[2]);
    rig_close(&r);
}

static void test_markers_trimmed_in_reflow(void)
{
    rig r;
    rig_open(&r, true);
    sk_buffer_fill_viewport(&r.b, NULL);
    r.b.scrollback = 1;
    sk_buffer_resize(&r.b, 10, 11);
    fill_three(&r);
    r.b.y = 10;
    r.m = (markers){{0, 1, 2}, {false}, 3};
    r.b.y = 3;
    sk_buffer_resize(&r.b, 2, 11);
    const char *two[] = {"ij", "01", "23", "45", "67", "89", "kl", "mn", "op", "qr", "st"};
    for (int i = 0; i < 11; i++)
        CHECK_LINE(&r, i, two[i]);
    CHECK_INT(r.m.line[1], 1);
    CHECK_INT(r.m.line[2], 6);
    CHECK(r.m.dead[0] && !r.m.dead[1] && !r.m.dead[2]);
    sk_buffer_resize(&r.b, 10, 11);
    CHECK_LINE(&r, 0, "ij        ");
    CHECK_LINE(&r, 1, "0123456789");
    CHECK_LINE(&r, 2, "klmnopqrst");
    CHECK_INT(r.m.line[1], 1);
    CHECK_INT(r.m.line[2], 2);
    rig_close(&r);
}

static void tab_gap(rig *r)
{
    sk_buffer_fill_viewport(&r->b, NULL);
    sk_buffer_resize(&r->b, 4, 10);
    r->b.y = 2;
    set_str(r, 0, "ab");
    set_str(r, 1, "cd");
    line(r, 1)->wrapped = true;
}

static void test_zero_space_end_larger(void)
{
    rig r;
    rig_open(&r, true);
    tab_gap(&r);
    sk_buffer_resize(&r.b, 5, 10);
    CHECK_INT(r.b.ybase, 0);
    CHECK_INT(r.b.lines.len, 10);
    CHECK_TRIMMED(&r, 0, "ab  c");
    CHECK_LINE(&r, 1, "d    ");
    sk_buffer_resize(&r.b, 6, 10);
    CHECK_INT(r.b.ybase, 0);
    CHECK_INT(r.b.lines.len, 10);
    CHECK_TRIMMED(&r, 0, "ab  cd");
    CHECK_LINE(&r, 1, "      ");
    rig_close(&r);
}

static void test_zero_space_end_smaller(void)
{
    rig r;
    rig_open(&r, true);
    tab_gap(&r);
    sk_buffer_resize(&r.b, 3, 10);
    CHECK_INT(r.b.y, 2);
    CHECK_INT(r.b.ybase, 0);
    CHECK_INT(r.b.lines.len, 10);
    CHECK_LINE(&r, 0, "ab ");
    CHECK_LINE(&r, 1, " cd");
    sk_buffer_resize(&r.b, 2, 10);
    CHECK_INT(r.b.y, 3);
    CHECK_INT(r.b.ybase, 0);
    CHECK_INT(r.b.lines.len, 10);
    CHECK_LINE(&r, 0, "ab");
    CHECK_LINE(&r, 1, "  ");
    CHECK_LINE(&r, 2, "cd");
    rig_close(&r);
}

#define HAN "\xE6\xB1\x89"
#define YU "\xE8\xAF\xAD"

static void han_lines(rig *r)
{
    sk_buffer_fill_viewport(&r->b, NULL);
    sk_buffer_resize(&r->b, 12, 10);
    r->b.y = 2;
    for (int y = 0; y < 2; y++) {
        for (int i = 0; i < 12; i += 4)
            set(r, y, i, 0x6C49, 2);
        for (int i = 2; i < 12; i += 4)
            set(r, y, i, 0x8BED, 2);
        for (int i = 1; i < 12; i += 2)
            set(r, y, i, 0, 0);
    }
    line(r, 1)->wrapped = true;
    CHECK_TRIMMED(r, 0, HAN YU HAN YU HAN YU);
    CHECK_TRIMMED(r, 1, HAN YU HAN YU HAN YU);
}

static void test_wide_larger(void)
{
    rig r;
    rig_open(&r, true);
    han_lines(&r);
    sk_buffer_resize(&r.b, 13, 10);
    CHECK_INT(r.b.ybase, 0);
    CHECK_INT(r.b.lines.len, 10);
    CHECK_TRIMMED(&r, 0, HAN YU HAN YU HAN YU);
    CHECK_LINE(&r, 0, HAN YU HAN YU HAN YU " ");
    CHECK_TRIMMED(&r, 1, HAN YU HAN YU HAN YU);
    CHECK_LINE(&r, 1, HAN YU HAN YU HAN YU " ");
    sk_buffer_resize(&r.b, 14, 10);
    CHECK_TRIMMED(&r, 0, HAN YU HAN YU HAN YU HAN);
    CHECK_LINE(&r, 0, HAN YU HAN YU HAN YU HAN);
    CHECK_TRIMMED(&r, 1, YU HAN YU HAN YU);
    CHECK_LINE(&r, 1, YU HAN YU HAN YU "    ");
    rig_close(&r);
}

static void test_wide_smaller(void)
{
    rig r;
    rig_open(&r, true);
    han_lines(&r);
    sk_buffer_resize(&r.b, 11, 10);
    CHECK_INT(r.b.ybase, 0);
    CHECK_INT(r.b.lines.len, 10);
    CHECK_TRIMMED(&r, 0, HAN YU HAN YU HAN);
    CHECK_TRIMMED(&r, 1, YU HAN YU HAN YU);
    CHECK_TRIMMED(&r, 2, HAN YU);
    sk_buffer_resize(&r.b, 10, 10);
    CHECK_TRIMMED(&r, 0, HAN YU HAN YU HAN);
    CHECK_TRIMMED(&r, 1, YU HAN YU HAN YU);
    CHECK_TRIMMED(&r, 2, HAN YU);
    sk_buffer_resize(&r.b, 9, 10);
    CHECK_TRIMMED(&r, 0, HAN YU HAN YU);
    CHECK_TRIMMED(&r, 1, HAN YU HAN YU);
    CHECK_TRIMMED(&r, 2, HAN YU HAN YU);
    sk_buffer_resize(&r.b, 8, 10);
    CHECK_TRIMMED(&r, 0, HAN YU HAN YU);
    CHECK_TRIMMED(&r, 1, HAN YU HAN YU);
    CHECK_TRIMMED(&r, 2, HAN YU HAN YU);
    sk_buffer_resize(&r.b, 7, 10);
    CHECK_TRIMMED(&r, 0, HAN YU HAN);
    CHECK_TRIMMED(&r, 1, YU HAN YU);
    CHECK_TRIMMED(&r, 2, HAN YU HAN);
    CHECK_TRIMMED(&r, 3, YU HAN YU);
    sk_buffer_resize(&r.b, 6, 10);
    CHECK_TRIMMED(&r, 0, HAN YU HAN);
    CHECK_TRIMMED(&r, 1, YU HAN YU);
    CHECK_TRIMMED(&r, 2, HAN YU HAN);
    CHECK_TRIMMED(&r, 3, YU HAN YU);
    rig_close(&r);
}

/* 'ab' 'cd'(w) 'ef' 'gh'(w) 'ij' 'kl'(w) and four blank lines. */
static void larger_setup(rig *r)
{
    sk_buffer_fill_viewport(&r->b, NULL);
    sk_buffer_resize(&r->b, 2, 10);
    const char *s[] = {"ab", "cd", "ef", "gh", "ij", "kl"};
    for (int i = 0; i < 6; i++) {
        set_str(r, i, s[i]);
        line(r, i)->wrapped = i % 2 == 1;
    }
}

static void check_larger_text(rig *r, int first)
{
    for (int i = 0; i < first; i++)
        CHECK_LINE(r, i, "    ");
    CHECK_LINE(r, first, "abcd");
    CHECK_LINE(r, first + 1, "efgh");
    CHECK_LINE(r, first + 2, "ijkl");
    for (int i = first + 3; i < r->b.lines.len; i++)
        CHECK_LINE(r, i, "    ");
    check_wrapped(r, NULL, 0);
}

static void test_larger_not_filled(void)
{
    rig r;
    rig_open(&r, true);
    larger_setup(&r);
    r.b.y = 6;
    sk_buffer_resize(&r.b, 4, 10);
    CHECK_INT(r.b.y, 3);
    CHECK_INT(r.b.ydisp, 0);
    CHECK_INT(r.b.ybase, 0);
    CHECK_INT(r.b.lines.len, 10);
    check_larger_text(&r, 0);
    rig_close(&r);
}

static void test_larger_filled(void)
{
    rig r;
    rig_open(&r, true);
    larger_setup(&r);
    r.b.y = 9;
    sk_buffer_resize(&r.b, 4, 10);
    CHECK_INT(r.b.y, 6);
    CHECK_INT(r.b.ydisp, 0);
    CHECK_INT(r.b.ybase, 0);
    CHECK_INT(r.b.lines.len, 10);
    check_larger_text(&r, 0);
    rig_close(&r);
    /* ybase above 0, with scrollback left or none, ydisp at it or not. */
    for (int full = 0; full <= 1; full++) {
        for (int at_base = 0; at_base <= 1; at_base++) {
            rig_open(&r, true);
            larger_setup(&r);
            r.b.y = 9;
            if (full)
                r.b.scrollback = 10;
            blank_on_top(&r, 10);
            r.b.ybase = 10;
            r.b.ydisp = at_base ? 10 : 5;
            sk_buffer_resize(&r.b, 4, 10);
            CHECK_INT(r.b.y, 9);
            CHECK_INT(r.b.ydisp, at_base ? 7 : 5);
            CHECK_INT(r.b.ybase, 7);
            CHECK_INT(r.b.lines.len, 17);
            check_larger_text(&r, 10);
            rig_close(&r);
        }
    }
}

/* 'abcd' 'efgh'(w) 'ijkl' and seven blank lines. */
static void smaller_setup(rig *r)
{
    sk_buffer_fill_viewport(&r->b, NULL);
    sk_buffer_resize(&r->b, 4, 10);
    set_str(r, 0, "abcd");
    set_str(r, 1, "efgh");
    set_str(r, 2, "ijkl");
}

static void check_smaller_text(rig *r, int first)
{
    for (int i = 0; i < first; i++)
        CHECK_LINE(r, i, "  ");
    const char *two[] = {"ab", "cd", "ef", "gh", "ij", "kl"};
    for (int i = 0; i < 6; i++)
        CHECK_LINE(r, first + i, two[i]);
    for (int i = first + 6; i < r->b.lines.len; i++)
        CHECK_LINE(r, i, "  ");
    int wrapped[] = {first + 1, first + 3, first + 5};
    check_wrapped(r, wrapped, 3);
}

static void test_smaller_not_filled(void)
{
    rig r;
    rig_open(&r, true);
    smaller_setup(&r);
    r.b.y = 3;
    sk_buffer_resize(&r.b, 2, 10);
    CHECK_INT(r.b.y, 6);
    CHECK_INT(r.b.ydisp, 0);
    CHECK_INT(r.b.ybase, 0);
    CHECK_INT(r.b.lines.len, 10);
    check_smaller_text(&r, 0);
    rig_close(&r);
}

static void test_smaller_filled(void)
{
    rig r;
    rig_open(&r, true);
    smaller_setup(&r);
    r.b.y = 9;
    sk_buffer_resize(&r.b, 2, 10);
    CHECK_INT(r.b.y, 9);
    CHECK_INT(r.b.ydisp, 3);
    CHECK_INT(r.b.ybase, 3);
    CHECK_INT(r.b.lines.len, 13);
    check_smaller_text(&r, 0);
    rig_close(&r);
    for (int at_base = 0; at_base <= 1; at_base++) {
        rig_open(&r, true);
        smaller_setup(&r);
        r.b.y = 9;
        blank_on_top(&r, 10);
        r.b.ybase = 10;
        r.b.ydisp = at_base ? 10 : 5;
        sk_buffer_resize(&r.b, 2, 10);
        CHECK_INT(r.b.ydisp, at_base ? 13 : 5);
        CHECK_INT(r.b.ybase, 13);
        CHECK_INT(r.b.lines.len, 23);
        check_smaller_text(&r, 10);
        rig_close(&r);
    }
    /* No scrollback left: the top is trimmed. */
    for (int at_base = 0; at_base <= 1; at_base++) {
        rig_open(&r, true);
        smaller_setup(&r);
        r.b.y = 9;
        r.b.scrollback = 10;
        blank_on_top(&r, 10);
        r.b.ybase = 10;
        r.b.ydisp = at_base ? 10 : 5;
        r.b.y = 13;
        sk_buffer_resize(&r.b, 2, 10);
        CHECK_INT(r.b.ydisp, at_base ? 10 : 5);
        CHECK_INT(r.b.ybase, 10);
        CHECK_INT(r.b.lines.len, 20);
        check_smaller_text(&r, 7);
        rig_close(&r);
    }
}

static void test_no_scrollback(void)
{
    rig r;
    rig_open(&r, false);
    sk_buffer_fill_viewport(&r.b, NULL);
    CHECK_INT(r.b.lines.max, INIT_ROWS);
    sk_buffer_resize(&r.b, INIT_COLS, INIT_ROWS * 2);
    CHECK_INT(r.b.lines.max, INIT_ROWS * 2);
    sk_buffer_resize(&r.b, INIT_COLS, INIT_ROWS / 2);
    CHECK_INT(r.b.lines.max, INIT_ROWS / 2);
    rig_close(&r);
}

/* BufferLine.test.ts: lines made alone. */

static sk_cell_data cell_of(uint32_t cp, int width)
{
    return (sk_cell_data){cp | (uint32_t)width << SK_CELL_WIDTH_SHIFT, 1, 0, {0, 0}, NULL};
}

static sk_line *line_of(int cols, int fill_width)
{
    sk_cell_data fill = {(uint32_t)fill_width << SK_CELL_WIDTH_SHIFT, 0, 0, {0, 0}, NULL};
    return sk_line_new(cols, &fill, false);
}

static char *str_of(const sk_line *l, bool trim, int start, int end)
{
    sk_buf b = {0};
    sk_line_text(l, &b, trim, start, end);
    sk_buf_reserve(&b, 0);
    return b.ptr;
}

#define CHECK_TEXT(l, trim, start, end, want)                                \
    do {                                                                     \
        char *s_ = str_of(l, trim, start, end);                              \
        CHECK_STR(s_, want);                                                 \
        free(s_);                                                            \
    } while (0)

#define FW "\xEF\xBC\x91"
#define YEN "\xEF\xBF\xA5"

static void test_line_trimmed_length(void)
{
    sk_line *l = line_of(10, 1);
    CHECK_INT(sk_line_trimmed_length(l), 0);
    sk_cell_data a = cell_of('a', 1);
    sk_line_set(l, 0, &a);
    sk_line_set(l, 2, &a);
    CHECK_INT(sk_line_trimmed_length(l), 3);
    sk_cell_data clef = cell_of(0x1D11E, 1);
    sk_line_set(l, 2, &clef);
    CHECK_INT(sk_line_trimmed_length(l), 3);
    static const uint32_t e_acute[] = {2, 'e', 0x301};
    sk_cell_data comb = {SK_CELL_COMBINED | 1u << SK_CELL_WIDTH_SHIFT, 1, 0, {0, 0}, e_acute};
    sk_line_set(l, 2, &comb);
    CHECK_INT(sk_line_trimmed_length(l), 3);
    sk_cell_data one = cell_of(0xFF11, 2), ph = {0, 0, 0, {0, 0}, NULL};
    sk_line_set(l, 2, &one);
    sk_line_set(l, 3, &ph);
    CHECK_INT(sk_line_trimmed_length(l), 4);
    sk_line_free(l);
}

static void test_line_translate(void)
{
    sk_line *l = line_of(10, 1);
    CHECK_TEXT(l, false, 0, 10, "          ");
    CHECK_TEXT(l, true, 0, 10, "");
    sk_cell_data a = cell_of('a', 1);
    sk_line_set(l, 0, &a);
    sk_line_set(l, 2, &a);
    sk_line_set(l, 4, &a);
    sk_line_set(l, 5, &a);
    CHECK_TEXT(l, false, 0, 10, "a a aa    ");
    CHECK_TEXT(l, true, 0, 10, "a a aa");
    for (int trim = 0; trim <= 1; trim++) {
        CHECK_TEXT(l, trim, 0, 5, "a a a");
        CHECK_TEXT(l, trim, 0, 4, "a a ");
        CHECK_TEXT(l, trim, 0, 3, "a a");
    }
    sk_cell_data sp = cell_of(' ', 1);
    sk_line_set(l, 6, &sp);
    CHECK_TEXT(l, true, 0, 10, "a a aa ");
    sk_line_free(l);

    l = line_of(10, 1);
    sk_cell_data one = cell_of(0xFF11, 2), ph = {0, 0, 0, {0, 0}, NULL};
    sk_line_set(l, 0, &a);
    for (int i = 2; i <= 7; i += i == 2 ? 3 : 2) {
        sk_line_set(l, i, &one);
        sk_line_set(l, i + 1, &ph);
    }
    CHECK_TEXT(l, false, 0, 10, "a " FW " " FW FW " ");
    CHECK_TEXT(l, true, 0, 10, "a " FW " " FW FW);
    for (int trim = 0; trim <= 1; trim++) {
        CHECK_TEXT(l, trim, 0, 7, "a " FW " " FW);
        CHECK_TEXT(l, trim, 0, 6, "a " FW " " FW);
        CHECK_TEXT(l, trim, 0, 5, "a " FW " ");
        CHECK_TEXT(l, trim, 0, 4, "a " FW);
        CHECK_TEXT(l, trim, 0, 3, "a " FW);
        CHECK_TEXT(l, trim, 0, 2, "a ");
    }
    sk_line_free(l);

    /* Cells of width 0 everywhere still advance. */
    l = line_of(10, 0);
    CHECK_TEXT(l, false, 0, 10, "          ");
    CHECK_TEXT(l, true, 0, 10, "");
    sk_line_set(l, 0, &a);
    CHECK_TEXT(l, true, 0, 0, "");
    sk_line_free(l);
}

static void test_line_add_codepoint(void)
{
    sk_line *l = line_of(3, 1);
    sk_line_add_cp(l, 0, 0x301, 0);
    CHECK_INT(l->cells[0].content, 0x301 | 1u << SK_CELL_WIDTH_SHIFT);
    sk_line_free(l);

    l = line_of(3, 1);
    static const uint32_t e_acute[] = {2, 'e', 0x301};
    sk_cell_data comb = {SK_CELL_COMBINED | 1u << SK_CELL_WIDTH_SHIFT, 123, 0, {0, 0}, e_acute};
    sk_line_set(l, 0, &comb);
    sk_line_add_cp(l, 0, 0x301, 0);
    CHECK_TEXT(l, false, 0, 1, "e\xCC\x81\xCC\x81");
    CHECK(l->cells[0].content & SK_CELL_COMBINED);
    CHECK_INT(sk_line_width(l, 0), 1);
    sk_line_free(l);

    l = line_of(3, 1);
    sk_cell_data e = {'e' | 1u << SK_CELL_WIDTH_SHIFT, 123, 0, {0, 0}, NULL};
    sk_line_set(l, 0, &e);
    sk_line_add_cp(l, 0, 0x301, 0);
    CHECK_TEXT(l, false, 0, 1, "e\xCC\x81");
    CHECK(l->cells[0].content & SK_CELL_COMBINED);
    CHECK_INT(sk_line_codepoint(l, 0), 0x301);
    sk_line_free(l);
}

static sk_line *yens(void)
{
    sk_line *l = line_of(10, 0);
    sk_cell_data y = cell_of(0xFFE5, 2);
    for (int i = 0; i < 10; i += 2)
        sk_line_set(l, i, &y);
    return l;
}

static void test_line_fullwidth(void)
{
    sk_cell_data a = cell_of('a', 1);
    sk_line *l = yens();
    sk_line_insert_cells(l, 9, 1, &a);
    CHECK_TEXT(l, false, 0, 10, YEN YEN YEN YEN " a");
    sk_line_insert_cells(l, 8, 1, &a);
    CHECK_TEXT(l, false, 0, 10, YEN YEN YEN YEN "a ");
    sk_line_insert_cells(l, 1, 1, &a);
    CHECK_TEXT(l, false, 0, 10, " a " YEN YEN YEN "a");
    sk_line_free(l);

    l = yens();
    sk_line_insert_cells(l, 0, 3, &a);
    CHECK_TEXT(l, false, 0, 10, "aaa" YEN YEN YEN " ");
    sk_line_insert_cells(l, 4, 1, &a);
    CHECK_TEXT(l, false, 0, 10, "aaa a " YEN YEN);
    sk_line_insert_cells(l, 4, 1, &a);
    CHECK_TEXT(l, false, 0, 10, "aaa aa " YEN " ");
    sk_line_free(l);

    l = yens();
    sk_line_delete_cells(l, 0, 1, &a);
    CHECK_TEXT(l, false, 0, 10, " " YEN YEN YEN YEN "a");
    sk_line_delete_cells(l, 5, 2, &a);
    CHECK_TEXT(l, false, 0, 10, " " YEN YEN YEN "aaa");
    sk_line_delete_cells(l, 0, 2, &a);
    CHECK_TEXT(l, false, 0, 10, " " YEN YEN "aaaaa");
    sk_line_free(l);

    const struct {
        int start, end;
        const char *want;
    } rep[] = {
        {0, 1, "a " YEN YEN YEN YEN}, {0, 2, "aa" YEN YEN YEN YEN}, {0, 3, "aaa " YEN YEN YEN},
        {0, 8, "aaaaaaaa" YEN},       {0, 9, "aaaaaaaaa "},          {0, 10, "aaaaaaaaaa"},
        {1, 2, " a" YEN YEN YEN YEN}, {1, 3, " aa " YEN YEN YEN},    {1, 4, " aaa" YEN YEN YEN},
        {1, 8, " aaaaaaa" YEN},       {1, 9, " aaaaaaaa "},          {1, 10, " aaaaaaaaa"},
    };
    for (size_t i = 0; i < SK_LEN(rep); i++) {
        l = yens();
        sk_line_replace_cells(l, rep[i].start, rep[i].end, &a, false);
        CHECK_TEXT(l, false, 0, 10, rep[i].want);
        sk_line_free(l);
    }
}

static void test_line_resize_drops_combined(void)
{
    sk_cell_data a = cell_of('a', 0);
    a.fg = 1;
    sk_line *l = sk_line_new(10, &a, false);
    static const uint32_t grin[] = {1, 0x1F601};
    sk_cell_data g = {SK_CELL_COMBINED | 1u << SK_CELL_WIDTH_SHIFT, 0, 0, {0, 0}, grin};
    sk_line_set(l, 2, &g);
    sk_line_set(l, 9, &g);
    CHECK_TEXT(l, false, 0, 10, "aa\xF0\x9F\x98\x81" "aaaaaa\xF0\x9F\x98\x81");
    sk_line_resize(l, 5, &a);
    CHECK_TEXT(l, false, 0, 5, "aa\xF0\x9F\x98\x81" "aa");
    sk_line_resize(l, 10, &a);
    CHECK_TEXT(l, false, 0, 10, "aa\xF0\x9F\x98\x81" "aaaaaaa");
    CHECK(l->comb[2] != NULL && l->comb[9] == NULL);
    sk_line_free(l);
}

static void test_translate_buffer_line(void)
{
    sk_line *l = line_of(4, 1);
    const char *abcd = "abcd";
    for (int i = 0; i < 4; i++) {
        sk_cell_data c = cell_of((uint32_t)abcd[i], 1);
        sk_line_set(l, i, &c);
    }
    CHECK_TEXT(l, true, 0, 2, "ab");
    sk_line_free(l);
    l = line_of(3, 1);
    sk_cell_data yu = cell_of(0x8A9E, 2), ph = {0, 0, 0, {0, 0}, NULL}, a = cell_of('a', 1);
    sk_line_set(l, 0, &yu);
    sk_line_set(l, 1, &ph);
    sk_line_set(l, 2, &a);
    CHECK_TEXT(l, true, 0, 1, "\xE8\xAA\x9E");
    CHECK_TEXT(l, true, 0, 2, "\xE8\xAA\x9E");
    CHECK_TEXT(l, true, 0, 3, "\xE8\xAA\x9E" "a");
    sk_line_free(l);
    l = line_of(2, 1);
    sk_cell_data grin = cell_of(0x1F601, 1);
    sk_line_set(l, 0, &grin);
    sk_line_set(l, 1, &a);
    CHECK_TEXT(l, true, 0, 1, "\xF0\x9F\x98\x81");
    CHECK_TEXT(l, true, 0, 2, "\xF0\x9F\x98\x81" "a");
    sk_line_free(l);
    l = line_of(3, 1);
    grin = cell_of(0x1F601, 2);
    sk_line_set(l, 0, &grin);
    sk_line_set(l, 1, &ph);
    sk_line_set(l, 2, &a);
    CHECK_TEXT(l, true, 0, 1, "\xF0\x9F\x98\x81");
    CHECK_TEXT(l, true, 0, 2, "\xF0\x9F\x98\x81");
    CHECK_TEXT(l, true, 0, 3, "\xF0\x9F\x98\x81" "a");
    sk_line_free(l);
}

const sk_test sk_suite_term_xbuffer[] = {
    T(test_line_trimmed_length),
    T(test_line_translate),
    T(test_line_add_codepoint),
    T(test_line_fullwidth),
    T(test_line_resize_drops_combined),
    T(test_translate_buffer_line),
    T(test_resize_cols),
    T(test_resize_rows_reduced),
    T(test_resize_rows_increased),
    T(test_reflow_empty_and_shrink),
    T(test_wrap_and_unwrap),
    T(test_discard_past_scrollback),
    T(test_remove_right_rows),
    T(test_combined_data_moves),
    T(test_markers_reflow),
    T(test_markers_trimmed_in_reflow),
    T(test_zero_space_end_larger),
    T(test_zero_space_end_smaller),
    T(test_wide_larger),
    T(test_wide_smaller),
    T(test_larger_not_filled),
    T(test_larger_filled),
    T(test_smaller_not_filled),
    T(test_smaller_filled),
    T(test_no_scrollback),
    {0},
};
