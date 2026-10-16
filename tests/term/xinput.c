/* xterm.js 6.0.0's own InputHandler tests (src/common/InputHandler.test.ts),
 * ported case by case: what they feed is fed, what they poke into the
 * buffer is poked, and the expected lines, cursors, attributes and
 * replies are xterm.js's. */
#include "../../src/term/screen.h"
#include "screen.h"

#include <stdarg.h>

#define YEN "\xEF\xBF\xA5"

/* bufferService.reset() with options.scrollback set. */
static void reset_buffers(tt *x, int scrollback)
{
    sk_term *t = x->t;
    sk_buffer_free(&t->normal);
    sk_buffer_free(&t->alt);
    sk_buffer_init(&t->normal, ++t->buffer_ids, true, scrollback, t->cols, t->rows, &t->listen);
    sk_buffer_fill_viewport(&t->normal, NULL);
    sk_buffer_init(&t->alt, ++t->buffer_ids, false, 0, t->cols, t->rows, &t->listen);
    t->active = &t->normal;
    t->user_scrolling = false;
}

static void open_with(tt *x, int cols, int rows, int scrollback)
{
    tt_open(x, cols, rows);
    reset_buffers(x, scrollback);
}

static void feedf(tt *x, const char *fmt, ...) SK_PRINTF(2, 3);
static void feedf(tt *x, const char *fmt, ...)
{
    sk_buf b = {0};
    va_list ap;
    va_start(ap, fmt);
    sk_buf_vprintf(&b, fmt, ap);
    va_end(ap);
    tt_feedn(x, b.ptr, b.len);
    sk_buf_free(&b);
}

static void repeat(tt *x, const char *s, int n)
{
    for (int i = 0; i < n; i++)
        tt_feed(x, s);
}

/* getLines: the buffer's lines from the top of the scrollback, trimmed,
 * joined by \n; translateToString(trim) of each when not trimmed. */
static char *lines(tt *x, int limit, bool trim)
{
    sk_buf out = {0};
    sk_buffer *b = x->t->active;
    for (int i = 0; i < limit && i < b->lines.len; i++) {
        if (i)
            sk_buf_putc(&out, '\n');
        sk_line *l = sk_screen_line(b, i);
        sk_line_text(l, &out, trim, 0, l->len);
    }
    sk_buf_reserve(&out, 0);
    return out.ptr;
}

#define CHECK_LINES(x, limit, want)                                          \
    do {                                                                     \
        char *l_ = lines(x, limit, true);                                    \
        CHECK_STR(l_, want);                                                 \
        free(l_);                                                            \
    } while (0)

#define CHECK_LINES_FULL(x, limit, want)                                     \
    do {                                                                     \
        char *l_ = lines(x, limit, false);                                   \
        CHECK_STR(l_, want);                                                 \
        free(l_);                                                            \
    } while (0)

#define CHECK_XY(r, cx, cy)                                                  \
    do {                                                                     \
        CHECK_INT((r)->t->active->x, cx);                                    \
        CHECK_INT((r)->t->active->y, cy);                                    \
    } while (0)

static void at(tt *x, int cx, int cy)
{
    x->t->active->x = cx;
    x->t->active->y = cy;
}

static char *spaces(int n, char c)
{
    char *s = sk_xmalloc((size_t)n + 1);
    memset(s, c, (size_t)n);
    s[n] = 0;
    return s;
}

static sk_line *ln(tt *x, int i)
{
    return sk_screen_line(x->t->active, i);
}

static void test_sl_sr_decic_decdc(void)
{
    tt x;
    open_with(&x, 5, 5, 1);
    repeat(&x, "12345", 6);
    tt_feed(&x, "\x1b[ @");
    CHECK_LINES(&x, 6, "12345\n2345\n2345\n2345\n2345\n2345");
    tt_feed(&x, "\x1b[0 @");
    CHECK_LINES(&x, 6, "12345\n345\n345\n345\n345\n345");
    tt_feed(&x, "\x1b[2 @");
    CHECK_LINES(&x, 6, "12345\n5\n5\n5\n5\n5");
    tt_close(&x);

    open_with(&x, 5, 5, 1);
    repeat(&x, "12345", 6);
    tt_feed(&x, "\x1b[ A");
    CHECK_LINES(&x, 6, "12345\n 1234\n 1234\n 1234\n 1234\n 1234");
    tt_feed(&x, "\x1b[0 A");
    CHECK_LINES(&x, 6, "12345\n  123\n  123\n  123\n  123\n  123");
    tt_feed(&x, "\x1b[2 A");
    CHECK_LINES(&x, 6, "12345\n    1\n    1\n    1\n    1\n    1");
    tt_close(&x);

    const char *ic[] = {"\x1b['}", "\x1b[1'}", "\x1b[2'}"};
    const char *ic_want[] = {"12345\n12 34\n12 34\n12 34\n12 34\n12 34", "12345\n12 34\n12 34\n12 34\n12 34\n12 34",
                             "12345\n12  3\n12  3\n12  3\n12  3\n12  3"};
    const char *dc[] = {"\x1b['~", "\x1b[1'~", "\x1b[2'~"};
    const char *dc_want[] = {"12345\n1245\n1245\n1245\n1245\n1245", "12345\n1245\n1245\n1245\n1245\n1245",
                             "12345\n125\n125\n125\n125\n125"};
    for (int i = 0; i < 3; i++) {
        open_with(&x, 5, 5, 1);
        repeat(&x, "12345", 6);
        tt_feed(&x, "\x1b[3;3H");
        tt_feed(&x, ic[i]);
        CHECK_LINES(&x, 6, ic_want[i]);
        tt_close(&x);
        open_with(&x, 5, 5, 1);
        repeat(&x, "12345", 6);
        tt_feed(&x, "\x1b[3;3H");
        tt_feed(&x, dc[i]);
        CHECK_LINES(&x, 6, dc_want[i]);
        tt_close(&x);
    }
}

#define TTY_BS "\x08 \x08"

static void test_bs_margins(void)
{
    tt x;
    open_with(&x, 5, 5, 1);
    tt_feed(&x, "#####abcdefghijklmnopqrstuvwxy");
    CHECK_LINES(&x, 6, "#####\nabcde\nfghij\nklmno\npqrst\nuvwxy");
    CHECK_INT(x.t->active->ydisp, 1);
    CHECK_XY(&x, 5, 4);
    repeat(&x, TTY_BS, 100);
    CHECK_LINES(&x, 6, "#####\nabcde\nfghij\nklmno\npqrst\n    y");
    tt_feed(&x, "\x1b[?45h");
    tt_feed(&x, "uvwxy");
    tt_feed(&x, "\x1b[2;4r");
    at(&x, 5, 4);
    repeat(&x, TTY_BS, 100);
    CHECK_LINES(&x, 6, "#####\nabcde\nfghij\nklmno\npqrst\n     ");
    tt_feed(&x, "uvwxy");
    at(&x, 5, 3);
    repeat(&x, TTY_BS, 100);
    CHECK_LINES(&x, 6, "#####\nabcde\n     \n     \n     \nuvwxy");
    CHECK_INT(x.t->active->x, 0);
    CHECK_INT(x.t->active->y, x.t->active->scroll_top);
    tt_feed(&x, "fghijklmnopqrst");
    at(&x, 5, 0);
    repeat(&x, TTY_BS, 100);
    CHECK_LINES(&x, 6, "#####\n     \nfghij\nklmno\npqrst\nuvwxy");
    tt_close(&x);
}

static void test_save_restore_cursor(void)
{
    tt x;
    tt_open(&x, 80, 30);
    at(&x, 1, 2);
    x.t->cur.fg = 3;
    tt_feed(&x, "\x1b" "7");
    CHECK_XY(&x, 1, 2);
    CHECK_INT(x.t->cur.fg, 3);
    at(&x, 10, 20);
    x.t->cur.fg = 30;
    tt_feed(&x, "\x1b" "8");
    CHECK_XY(&x, 1, 2);
    CHECK_INT(x.t->cur.fg, 3);
    tt_close(&x);
}

static void test_cursor_style(void)
{
    tt x;
    tt_open(&x, 80, 30);
    tt_feed(&x, "\x1b[0 q");
    CHECK_INT(x.t->cursor_style, -1);
    CHECK_INT(x.t->cursor_blink, -1);
    const int style[] = {SK_CURSOR_BLOCK, SK_CURSOR_BLOCK, SK_CURSOR_UNDERLINE, SK_CURSOR_UNDERLINE, SK_CURSOR_BAR,
                         SK_CURSOR_BAR};
    for (int i = 1; i <= 6; i++) {
        feedf(&x, "\x1b[%d q", i);
        CHECK_INT(x.t->cursor_style, style[i - 1]);
        CHECK_INT(x.t->cursor_blink, i % 2);
    }
    tt_feed(&x, "\x1b[?2004h");
    CHECK(x.t->bracketed_paste);
    tt_feed(&x, "\x1b[?2004l");
    CHECK(!x.t->bracketed_paste);
    tt_close(&x);
}

static void test_insert_delete_chars(void)
{
    char *a70 = spaces(70, 'a');
    tt x;
    tt_open(&x, 80, 30);
    for (int i = 0; i < 2; i++) {
        tt_feed(&x, a70);
        tt_feed(&x, "1234567890");
    }
    const char *after[] = {" 123456789", "  12345678", "    123456", "          "};
    const char *ins[] = {"\x1b[0@", "\x1b[1@", "\x1b[2@", "\x1b[10@"};
    for (int k = 0; k < 4; k++) {
        at(&x, 70, 0);
        tt_feed(&x, ins[k]);
        sk_buf want = {0};
        sk_buf_printf(&want, "%s%s", a70, after[k]);
        char *got = lines(&x, 1, false);
        CHECK_STR(got, want.ptr);
        free(got);
        sk_buf_free(&want);
    }
    CHECK_LINES(&x, 1, a70);
    tt_close(&x);

    tt_open(&x, 80, 30);
    for (int i = 0; i < 2; i++) {
        tt_feed(&x, a70);
        tt_feed(&x, "1234567890");
    }
    const char *dafter[] = {"234567890 ", "34567890  ", "567890    ", "          "};
    const char *del[] = {"\x1b[0P", "\x1b[1P", "\x1b[2P", "\x1b[10P"};
    for (int k = 0; k < 4; k++) {
        at(&x, 70, 0);
        tt_feed(&x, del[k]);
        sk_buf want = {0};
        sk_buf_printf(&want, "%s%s", a70, dafter[k]);
        char *got = lines(&x, 1, false);
        CHECK_STR(got, want.ptr);
        free(got);
        sk_buf_free(&want);
    }
    tt_close(&x);
    free(a70);
}

static void test_erase_in_line(void)
{
    char *a80 = spaces(80, 'a'), *a70 = spaces(70, 'a'), *s70 = spaces(70, ' '), *s80 = spaces(80, ' ');
    tt x;
    tt_open(&x, 80, 30);
    repeat(&x, a80, 3);
    at(&x, 70, 0);
    tt_feed(&x, "\x1b[0K");
    sk_buf want = {0};
    sk_buf_printf(&want, "%s          ", a70);
    char *got = lines(&x, 1, false);
    CHECK_STR(got, want.ptr);
    free(got);
    at(&x, 70, 1);
    tt_feed(&x, "\x1b[1K");
    sk_buf_clear(&want);
    sk_buf_printf(&want, "%s aaaaaaaaa", s70);
    sk_buf l1 = {0};
    sk_line_text(ln(&x, 1), &l1, false, 0, 80);
    CHECK_STR(l1.ptr, want.ptr);
    sk_buf_free(&l1);
    at(&x, 70, 2);
    tt_feed(&x, "\x1b[2K");
    sk_buf l2 = {0};
    sk_line_text(ln(&x, 2), &l2, false, 0, 80);
    CHECK_STR(l2.ptr, s80);
    sk_buf_free(&l2);
    sk_buf_free(&want);
    tt_close(&x);
    free(a80);
    free(a70);
    free(s70);
    free(s80);
}

/* A full first line, then one of cols + 9 that wraps, then full lines. */
static void wrapped_base(tt *x)
{
    int cols = x->t->cols;
    char *full = spaces(cols, 'a'), *more = spaces(cols + 9, 'a');
    at(x, 0, 0);
    tt_feed(x, full);
    tt_feed(x, more);
    for (int i = 3; i < x->t->rows; ++i)
        tt_feed(x, full);
    CHECK(ln(x, 2)->wrapped);
    free(full);
    free(more);
}

static void test_erase_in_line_wrap(void)
{
    tt x;
    tt_open(&x, 80, 30);
    wrapped_base(&x);
    at(&x, 40, 2);
    tt_feed(&x, "\x1b[0K");
    CHECK(ln(&x, 2)->wrapped);
    at(&x, 0, 2);
    tt_feed(&x, "\x1b[0K");
    CHECK(!ln(&x, 2)->wrapped);
    wrapped_base(&x);
    at(&x, 40, 2);
    tt_feed(&x, "\x1b[1K");
    CHECK(ln(&x, 2)->wrapped);
    wrapped_base(&x);
    at(&x, 40, 2);
    tt_feed(&x, "\x1b[2K");
    CHECK(!ln(&x, 2)->wrapped);
    tt_close(&x);
}

static void test_erase_in_display(void)
{
    char *a80 = spaces(80, 'a'), *s80 = spaces(80, ' ');
    tt x;
    tt_open(&x, 80, 7);
    repeat(&x, a80, 7);
    at(&x, 40, 5);
    tt_feed(&x, "\x1b[0J");
    sk_buf want = {0};
    for (int i = 0; i < 5; i++)
        sk_buf_printf(&want, "%s\n", a80);
    char *a40 = spaces(40, 'a'), *s40 = spaces(40, ' ');
    sk_buf_printf(&want, "%s%s\n%s", a40, s40, s80);
    char *got = lines(&x, 7, false);
    CHECK_STR(got, want.ptr);
    free(got);
    sk_buf_clear(&want);
    for (int i = 0; i < 5; i++)
        sk_buf_printf(&want, "%s\n", a80);
    sk_buf_printf(&want, "%s\n", a40);
    CHECK_LINES(&x, 7, want.ptr);

    at(&x, 0, 0);
    repeat(&x, a80, 7);
    at(&x, 40, 5);
    tt_feed(&x, "\x1b[1J");
    char *s41 = spaces(41, ' '), *a39 = spaces(39, 'a');
    sk_buf_clear(&want);
    for (int i = 0; i < 5; i++)
        sk_buf_printf(&want, "%s\n", s80);
    sk_buf_printf(&want, "%s%s\n%s", s41, a39, a80);
    got = lines(&x, 7, false);
    CHECK_STR(got, want.ptr);
    free(got);
    sk_buf_clear(&want);
    sk_buf_printf(&want, "\n\n\n\n\n%s%s\n%s", s41, a39, a80);
    CHECK_LINES(&x, 7, want.ptr);

    at(&x, 0, 0);
    repeat(&x, a80, 7);
    at(&x, 40, 5);
    tt_feed(&x, "\x1b[2J");
    CHECK_LINES(&x, 7, "\n\n\n\n\n\n");

    wrapped_base(&x);
    at(&x, 40, 2);
    tt_feed(&x, "\x1b[1J");
    CHECK(!ln(&x, 2)->wrapped);
    wrapped_base(&x);
    at(&x, 90, 1);
    tt_feed(&x, "\x1b[1J");
    CHECK(!ln(&x, 2)->wrapped);
    sk_buf_free(&want);
    tt_close(&x);
    free(a80);
    free(s80);
    free(a40);
    free(s40);
    free(s41);
    free(a39);
}

static void test_print(void)
{
    tt x;
    tt_open(&x, 80, 30);
    /* A lone zero-width character first: no endless loop. */
    tt_feed(&x, "\xE2\x80\x8B");
    tt_close(&x);
    open_with(&x, 5, 5, 1);
    tt_feed(&x, "12345");
    at(&x, 0, 0);
    tt_feed(&x, YEN YEN YEN);
    CHECK_LINES(&x, 2, YEN YEN "\n" YEN);
    tt_close(&x);
}

static uint32_t fg_color(uint32_t fg)
{
    uint32_t cm = fg & SK_ATTR_CM_MASK;
    return cm == SK_ATTR_CM_P16 || cm == SK_ATTR_CM_P256 ? fg & 0xFF : cm == SK_ATTR_CM_RGB ? fg & 0xFFFFFF : (uint32_t)-1;
}

static void test_alt_screen(void)
{
    const char *modes[] = {"47", "1047"};
    for (int i = 0; i < 2; i++) {
        tt x;
        tt_open(&x, 80, 30);
        feedf(&x, "\x1b[?%sh\r\n\x1b[31mJUNK\x1b[?%slTEST", modes[i], modes[i]);
        CHECK_LINES(&x, 2, "\n    TEST");
        CHECK_INT(fg_color(ln(&x, 1)->cells[4].fg), 1);
        tt_close(&x);
    }
    tt x;
    tt_open(&x, 80, 30);
    tt_feed(&x, "\x1b[?1048h\r\n\x1b[31mJUNK\x1b[?1048lTEST");
    CHECK_LINES(&x, 2, "TEST\nJUNK");
    CHECK_INT(ln(&x, 0)->cells[0].fg, 0);
    CHECK_INT(fg_color(ln(&x, 1)->cells[0].fg), 1);
    tt_close(&x);
    tt_open(&x, 80, 30);
    tt_feed(&x, "\x1b[?1049h\r\n\x1b[31mJUNK\x1b[?1049lTEST");
    CHECK_LINES(&x, 2, "TEST\n");
    CHECK_INT(ln(&x, 0)->cells[0].fg, 0);
    tt_close(&x);
    tt_open(&x, 80, 30);
    tt_feed(&x, "\x1b[?1049h\r\n\x1b[31m\x1b[s\x1b[?1049lTEST");
    CHECK_LINES(&x, 1, "TEST");
    CHECK_INT(ln(&x, 0)->cells[0].fg, 0);
    tt_feed(&x, "\x1b[?1049h\x1b[uTEST");
    CHECK_LINES(&x, 2, "\nTEST");
    CHECK_INT(fg_color(ln(&x, 1)->cells[0].fg), 1);
    tt_close(&x);
    tt_open(&x, 80, 30);
    tt_feed(&x, "\x1b[42m\x1b[?1049h");
    CHECK_INT(fg_color(ln(&x, 20)->cells[10].bg), 2);
    tt_close(&x);
}

static void test_text_attributes(void)
{
    struct {
        const char *on, *off;
        bool bg;
        uint32_t bit;
    } flags[] = {
        {"1", "22", false, SK_FG_BOLD},          {"2", "22", true, SK_BG_DIM},
        {"3", "23", true, SK_BG_ITALIC},         {"4", "24", false, SK_FG_UNDERLINE},
        {"5", "25", false, SK_FG_BLINK},         {"7", "27", false, SK_FG_INVERSE},
        {"8", "28", false, SK_FG_INVISIBLE},     {"9", "29", false, SK_FG_STRIKETHROUGH},
    };
    tt x;
    tt_open(&x, 80, 30);
    for (size_t i = 0; i < SK_LEN(flags); i++) {
        feedf(&x, "\x1b[%sm", flags[i].on);
        CHECK((flags[i].bg ? x.t->cur.bg : x.t->cur.fg) & flags[i].bit);
        feedf(&x, "\x1b[%sm", flags[i].off);
        CHECK(!((flags[i].bg ? x.t->cur.bg : x.t->cur.fg) & flags[i].bit));
    }
    for (int i = 0; i < 8; ++i) {
        feedf(&x, "\x1b[%d;%dm", i + 30, i + 40);
        CHECK_INT(x.t->cur.fg & SK_ATTR_CM_MASK, SK_ATTR_CM_P16);
        CHECK_INT(fg_color(x.t->cur.fg), i);
        CHECK_INT(x.t->cur.bg & SK_ATTR_CM_MASK, SK_ATTR_CM_P16);
        CHECK_INT(fg_color(x.t->cur.bg), i);
    }
    tt_feed(&x, "\x1b[39;49m");
    CHECK_INT(x.t->cur.fg & SK_ATTR_CM_MASK, 0);
    CHECK_INT(x.t->cur.bg & SK_ATTR_CM_MASK, 0);
    for (int i = 0; i < 256; ++i) {
        feedf(&x, "\x1b[38;5;%d;48;5;%dm", i, i);
        CHECK_INT(x.t->cur.fg & SK_ATTR_CM_MASK, SK_ATTR_CM_P256);
        CHECK_INT(fg_color(x.t->cur.fg), i);
        CHECK_INT(fg_color(x.t->cur.bg), i);
    }
    tt_feed(&x, "\x1b[39;49m");
    CHECK_INT(fg_color(x.t->cur.fg), (uint32_t)-1);
    tt_feed(&x, "\x1b[38;2;1;2;3;48;2;4;5;6m");
    CHECK_INT(x.t->cur.fg & SK_ATTR_CM_MASK, SK_ATTR_CM_RGB);
    CHECK_INT(fg_color(x.t->cur.fg), 1 << 16 | 2 << 8 | 3);
    CHECK_INT(fg_color(x.t->cur.bg), 4 << 16 | 5 << 8 | 6);
    tt_feed(&x, "\x1b[39;49m");
    CHECK_INT(fg_color(x.t->cur.bg), (uint32_t)-1);
    /* Transitions between the modes. */
    tt_feed(&x, "\x1b[38;2;1;2;3;48;2;4;5;6m\x1b[38;5;255;48;5;255m");
    CHECK_INT(x.t->cur.fg & SK_ATTR_CM_MASK, SK_ATTR_CM_P256);
    CHECK_INT(fg_color(x.t->cur.fg), 255);
    CHECK_INT(fg_color(x.t->cur.bg), 255);
    tt_feed(&x, "\x1b[38;2;1;2;3;48;2;4;5;6m\x1b[37;47m");
    CHECK_INT(x.t->cur.fg & SK_ATTR_CM_MASK, SK_ATTR_CM_P16);
    CHECK_INT(fg_color(x.t->cur.fg), 7);
    CHECK_INT(fg_color(x.t->cur.bg), 7);
    tt_feed(&x, "\x1b[37;47m\x1b[38;5;255;48;5;255m");
    CHECK_INT(fg_color(x.t->cur.fg), 255);
    tt_feed(&x, "\x1b[38;5;255;48;5;255m\x1b[37;47m");
    CHECK_INT(fg_color(x.t->cur.fg), 7);
    /* Missing RGB values are zero. */
    tt_feed(&x, "\x1b[38;2;1;2;3m\x1b[38;2;5m");
    CHECK_INT(fg_color(x.t->cur.fg), 5 << 16);
    tt_close(&x);
}

static void test_colon_notation(void)
{
    struct {
        const char *semi, *colon;
    } same[] = {
        {"\x1b[38;2;50;100;150m", "\x1b[38:2::50:100:150m"}, {"\x1b[38;2;50;100;m", "\x1b[38:2::50:100:m"},
        {"\x1b[38;2;50;;m", "\x1b[38:2::50::m"},              {"\x1b[38;2;;;m", "\x1b[38:2::::m"},
        {"\x1b[38;2;50;100;150m", "\x1b[38;2::50:100:150m"}, {"\x1b[38;2;50;100;150m", "\x1b[38;2;50:100:150m"},
        {"\x1b[38;2;50;100;150m", "\x1b[38;2;50;100:150m"},  {"\x1b[38;5;50m", "\x1b[38:5:50m"},
        {"\x1b[38;5;m", "\x1b[38:5:m"},                      {"\x1b[38;5;50m", "\x1b[38;5:50m"},
        {"\x1b[38;2m", "\x1b[38:2m"},                        {"\x1b[38;5m", "\x1b[38:5m"},
    };
    const uint32_t rgb[] = {50 << 16 | 100 << 8 | 150, 50 << 16 | 100 << 8, 50 << 16, 0, 50 << 16 | 100 << 8 | 150,
                            50 << 16 | 100 << 8 | 150, 50 << 16 | 100 << 8 | 150};
    for (size_t i = 0; i < SK_LEN(same); i++) {
        tt a, b;
        tt_open(&a, 80, 30);
        tt_open(&b, 80, 30);
        a.t->cur.fg = 0xFFFFFFFF;
        b.t->cur.fg = 0xFFFFFFFF;
        tt_feed(&b, same[i].semi);
        tt_feed(&a, same[i].colon);
        if (i < SK_LEN(rgb))
            CHECK_INT(b.t->cur.fg & 0xFFFFFF, rgb[i]);
        CHECK_INT(a.t->cur.fg, b.t->cur.fg);
        tt_close(&a);
        tt_close(&b);
    }
    struct {
        const char *semi, *colon;
        uint32_t rgb;
    } around[] = {
        {"\x1b[1;38;2;50;100;150;4m", "\x1b[1;38:2::50:100:150;4m", 50 << 16 | 100 << 8 | 150},
        {"\x1b[1;38;2;50;100;;4m", "\x1b[1;38:2::50:100:;4m", 50 << 16 | 100 << 8},
        {"\x1b[1;38;2;50;100;;4m", "\x1b[1;38:2::50:100;4m", 50 << 16 | 100 << 8},
        {"\x1b[1;38;2;;;;4m", "\x1b[1;38:2::;4m", 0},
        {"\x1b[1;38;2;;;;4m", "\x1b[1;38;2::;4m", 0},
    };
    for (size_t i = 0; i < SK_LEN(around); i++) {
        tt a, b;
        tt_open(&a, 80, 30);
        tt_open(&b, 80, 30);
        tt_feed(&b, around[i].semi);
        tt_feed(&a, around[i].colon);
        CHECK(b.t->cur.fg & SK_FG_BOLD);
        CHECK(b.t->cur.fg & SK_FG_UNDERLINE);
        CHECK_INT(b.t->cur.fg & 0xFFFFFF, around[i].rgb);
        CHECK_INT(a.t->cur.fg, b.t->cur.fg);
        tt_close(&a);
        tt_close(&b);
    }
}

typedef struct {
    const char *seq;
    int x, y;
} move;

static void run_moves(const move *m, size_t n)
{
    tt x;
    tt_open(&x, 10, 10);
    for (size_t i = 0; i < n; i++) {
        if (m[i].seq[0] == '@') {
            at(&x, m[i].x, m[i].y);
            continue;
        }
        tt_feed(&x, m[i].seq);
        if (x.t->active->x != m[i].x || x.t->active->y != m[i].y)
            sk_test_fail(__FILE__, __LINE__, "step %zu: cursor %d,%d, want %d,%d", i, x.t->active->x,
                         x.t->active->y, m[i].x, m[i].y);
    }
    tt_close(&x);
}

#define MOVES(...)                                                           \
    do {                                                                     \
        const move m_[] = {__VA_ARGS__};                                     \
        run_moves(m_, SK_LEN(m_));                                           \
    } while (0)

static void test_cursor_positioning(void)
{
    MOVES({"\x1b[C", 1, 0}, {"\x1b[1C", 2, 0}, {"\x1b[4C", 6, 0}, {"\x1b[100C", 9, 0}, {"@", 8, 4}, {"\x1b[C", 9, 4});
    MOVES({"\x1b[D", 0, 0}, {"\x1b[1D", 0, 0}, {"\x1b[100C", 9, 0}, {"\x1b[D", 8, 0}, {"\x1b[1D", 7, 0},
          {"\x1b[4D", 3, 0}, {"\x1b[100D", 0, 0}, {"@", 4, 4}, {"\x1b[D", 3, 4});
    MOVES({"\x1b[B", 0, 1}, {"\x1b[1B", 0, 2}, {"\x1b[4B", 0, 6}, {"\x1b[100B", 0, 9}, {"@", 8, 0}, {"\x1b[B", 8, 1});
    MOVES({"\x1b[A", 0, 0}, {"\x1b[1A", 0, 0}, {"\x1b[100B", 0, 9}, {"\x1b[A", 0, 8}, {"\x1b[1A", 0, 7},
          {"\x1b[4A", 0, 3}, {"\x1b[100A", 0, 0}, {"@", 8, 9}, {"\x1b[A", 8, 8});
    MOVES({"\x1b[E", 0, 1}, {"\x1b[1E", 0, 2}, {"\x1b[4E", 0, 6}, {"\x1b[100E", 0, 9}, {"@", 8, 0}, {"\x1b[E", 0, 1});
    MOVES({"\x1b[F", 0, 0}, {"\x1b[1F", 0, 0}, {"\x1b[100E", 0, 9}, {"\x1b[F", 0, 8}, {"\x1b[1F", 0, 7},
          {"\x1b[4F", 0, 3}, {"\x1b[100F", 0, 0}, {"@", 8, 9}, {"\x1b[F", 0, 8});
    MOVES({"\x1b[G", 0, 0}, {"\x1b[1G", 0, 0}, {"\x1b[2G", 1, 0}, {"\x1b[5G", 4, 0}, {"\x1b[100G", 9, 0});
    MOVES({"@", 5, 5}, {"\x1b[H", 0, 0}, {"@", 5, 5}, {"\x1b[1H", 0, 0}, {"@", 5, 5}, {"\x1b[1;1H", 0, 0},
          {"@", 5, 5}, {"\x1b[8H", 0, 7}, {"@", 5, 5}, {"\x1b[;8H", 7, 0}, {"@", 5, 5}, {"\x1b[100;100H", 9, 9});
    MOVES({"\x1b[`", 0, 0}, {"\x1b[1`", 0, 0}, {"\x1b[2`", 1, 0}, {"\x1b[5`", 4, 0}, {"\x1b[100`", 9, 0});
    MOVES({"\x1b[a", 1, 0}, {"\x1b[1a", 2, 0}, {"\x1b[4a", 6, 0}, {"\x1b[100a", 9, 0}, {"@", 8, 4}, {"\x1b[a", 9, 4});
    MOVES({"\x1b[d", 0, 0}, {"\x1b[1d", 0, 0}, {"\x1b[2d", 0, 1}, {"\x1b[5d", 0, 4}, {"\x1b[100d", 0, 9},
          {"@", 8, 4}, {"\x1b[d", 8, 0});
    MOVES({"\x1b[e", 0, 1}, {"\x1b[1e", 0, 2}, {"\x1b[4e", 0, 6}, {"\x1b[100e", 0, 9}, {"@", 8, 4}, {"\x1b[e", 8, 5});
}

static void test_cursor_clamped(void)
{
    struct {
        const char *seq;
        int hx, hy, lx, ly;
    } c[] = {
        {"\x1b[C", 9, 9, 1, 0},  {"\x1b[D", 8, 9, 0, 0},  {"\x1b[B", 9, 9, 0, 1},   {"\x1b[A", 9, 8, 0, 0},
        {"\x1b[E", 0, 9, 0, 1},  {"\x1b[F", 0, 8, 0, 0},  {"\x1b[5G", 4, 9, 4, 0},  {"\x1b[5;5H", 4, 4, 4, 4},
        {"\x1b[5`", 4, 9, 4, 0}, {"\x1b[a", 9, 9, 1, 0},  {"\x1b[5d", 9, 4, 0, 4},  {"\x1b[e", 9, 9, 0, 1},
        {"\x1b[P", 9, 9, 0, 0},  {"\x1b[X", 9, 9, 0, 0},  {"\x1b[@", 9, 9, 0, 0},
    };
    for (size_t i = 0; i < SK_LEN(c); i++) {
        tt x;
        tt_open(&x, 10, 10);
        at(&x, 10000, 10000);
        tt_feed(&x, c[i].seq);
        CHECK_XY(&x, c[i].hx, c[i].hy);
        at(&x, -10000, -10000);
        tt_feed(&x, c[i].seq);
        CHECK_XY(&x, c[i].lx, c[i].ly);
        tt_close(&x);
    }
    const char *last[] = {"\x1b[P", "\x1b[X", "\x1b[@"};
    for (int i = 0; i < 3; i++) {
        tt x;
        tt_open(&x, 10, 10);
        tt_feed(&x, "0123456789");
        tt_feed(&x, last[i]);
        CHECK_LINES_FULL(&x, 1, "012345678 ");
        tt_close(&x);
    }
}

static void test_decstbm(void)
{
    tt x;
    tt_open(&x, 10, 10);
    tt_feed(&x, "\x1b[r");
    CHECK(x.t->active->scroll_top == 0 && x.t->active->scroll_bottom == 9);
    tt_feed(&x, "\x1b[3;7r");
    CHECK(x.t->active->scroll_top == 2 && x.t->active->scroll_bottom == 6);
    tt_feed(&x, "\x1b[0;0r");
    CHECK(x.t->active->scroll_top == 0 && x.t->active->scroll_bottom == 9);
    tt_feed(&x, "\x1b[3;1000r");
    CHECK(x.t->active->scroll_top == 2 && x.t->active->scroll_bottom == 9);
    tt_feed(&x, "\x1b[r\x1b[7;2r");
    CHECK(x.t->active->scroll_top == 0 && x.t->active->scroll_bottom == 9);
    at(&x, 10000, 10000);
    tt_feed(&x, "\x1b[2;7r");
    CHECK_XY(&x, 0, 0);
    tt_close(&x);
}

#define DIGITS10 "0\r\n1\r\n2\r\n3\r\n4\r\n5\r\n6\r\n7\r\n8\r\n9"

static void test_scroll_margins(void)
{
    tt x;
    tt_open(&x, 10, 10);
    tt_feed(&x, DIGITS10 "\x1b[2;4r\x1b[2Sm");
    CHECK_LINES(&x, 10, "m\n3\n\n\n4\n5\n6\n7\n8\n9");
    tt_close(&x);
    tt_open(&x, 10, 10);
    tt_feed(&x, DIGITS10 "\x1b[2;4r\x1b[2Tm");
    CHECK_LINES(&x, 10, "m\n\n\n1\n4\n5\n6\n7\n8\n9");
    tt_close(&x);
    for (int del = 0; del <= 1; del++) {
        const char op = del ? 'M' : 'L';
        tt_open(&x, 10, 10);
        tt_feed(&x, DIGITS10 "\x1b[3;6r");
        CHECK(x.t->active->scroll_top == 2 && x.t->active->scroll_bottom == 5);
        feedf(&x, "\x1b[2%cm", op);
        CHECK_LINES(&x, 10, "m\n1\n2\n3\n4\n5\n6\n7\n8\n9");
        feedf(&x, "\x1b[2H\x1b[2%cn", op);
        CHECK_LINES(&x, 10, "m\nn\n2\n3\n4\n5\n6\n7\n8\n9");
        feedf(&x, "\x1b[7H\x1b[2%co", op);
        CHECK_LINES(&x, 10, "m\nn\n2\n3\n4\n5\no\n7\n8\n9");
        feedf(&x, "\x1b[8H\x1b[2%cp", op);
        CHECK_LINES(&x, 10, "m\nn\n2\n3\n4\n5\no\np\n8\n9");
        feedf(&x, "\x1b[100H\x1b[2%cq", op);
        CHECK_LINES(&x, 10, "m\nn\n2\n3\n4\n5\no\np\n8\nq");
        tt_close(&x);
    }
    tt_open(&x, 10, 10);
    tt_feed(&x, DIGITS10 "\x1b[3;6r\x1b[3H\x1b[2Lm");
    CHECK_LINES(&x, 10, "0\n1\nm\n\n2\n3\n6\n7\n8\n9");
    tt_feed(&x, "\x1b[6H\x1b[2Ln");
    CHECK_LINES(&x, 10, "0\n1\nm\n\n2\nn\n6\n7\n8\n9");
    tt_close(&x);
    tt_open(&x, 10, 10);
    tt_feed(&x, DIGITS10 "\x1b[3;6r\x1b[6H\x1b[2Mm");
    CHECK_LINES(&x, 10, "0\n1\n2\n3\n4\nm\n6\n7\n8\n9");
    tt_feed(&x, "\x1b[3H\x1b[2Mn");
    CHECK_LINES(&x, 10, "0\n1\nn\nm\n\n\n6\n7\n8\n9");
    tt_close(&x);
}

static void wide_setup(tt *x)
{
    open_with(x, 10, 5, 1);
    repeat(x, YEN, 20);
}

static void test_wide_resets(void)
{
    tt x;
    wide_setup(&x);
    tt_feed(&x, "\x1b[H#");
    CHECK_LINES(&x, 5, "# " YEN YEN YEN YEN "\n" YEN YEN YEN YEN YEN "\n" YEN YEN YEN YEN YEN "\n" YEN YEN YEN YEN YEN "\n");
    tt_feed(&x, "\x1b[1;6H######");
    CHECK_LINES(&x, 5, "# " YEN " #####\n# " YEN YEN YEN YEN "\n" YEN YEN YEN YEN YEN "\n" YEN YEN YEN YEN YEN "\n");
    tt_feed(&x, "#");
    CHECK_LINES(&x, 5, "# " YEN " #####\n##" YEN YEN YEN YEN "\n" YEN YEN YEN YEN YEN "\n" YEN YEN YEN YEN YEN "\n");
    tt_feed(&x, "#");
    CHECK_LINES(&x, 5, "# " YEN " #####\n### " YEN YEN YEN "\n" YEN YEN YEN YEN YEN "\n" YEN YEN YEN YEN YEN "\n");
    tt_feed(&x, "\x1b[3;9H#");
    CHECK_LINES(&x, 5, "# " YEN " #####\n### " YEN YEN YEN "\n" YEN YEN YEN YEN "#\n" YEN YEN YEN YEN YEN "\n");
    tt_feed(&x, "#");
    CHECK_LINES(&x, 5, "# " YEN " #####\n### " YEN YEN YEN "\n" YEN YEN YEN YEN "##\n" YEN YEN YEN YEN YEN "\n");
    tt_feed(&x, "#");
    CHECK_LINES(&x, 5, "# " YEN " #####\n### " YEN YEN YEN "\n" YEN YEN YEN YEN "##\n# " YEN YEN YEN YEN "\n");
    tt_feed(&x, "\x1b[4;10H#");
    CHECK_LINES(&x, 5, "# " YEN " #####\n### " YEN YEN YEN "\n" YEN YEN YEN YEN "##\n# " YEN YEN YEN " #\n");
    tt_close(&x);

    wide_setup(&x);
    tt_feed(&x, "\x1b[1;6H\x1b[K#");
    CHECK_LINES(&x, 5, YEN YEN " #\n" YEN YEN YEN YEN YEN "\n" YEN YEN YEN YEN YEN "\n" YEN YEN YEN YEN YEN "\n");
    tt_feed(&x, "\x1b[2;5H\x1b[1K");
    CHECK_LINES(&x, 5, YEN YEN " #\n      " YEN YEN "\n" YEN YEN YEN YEN YEN "\n" YEN YEN YEN YEN YEN "\n");
    tt_feed(&x, "\x1b[3;6H\x1b[1K");
    CHECK_LINES(&x, 5, YEN YEN " #\n      " YEN YEN "\n      " YEN YEN "\n" YEN YEN YEN YEN YEN "\n");
    tt_close(&x);

    wide_setup(&x);
    tt_feed(&x, "\x1b[1;6H\x1b[@");
    CHECK_LINES(&x, 5, YEN YEN "   " YEN "\n" YEN YEN YEN YEN YEN "\n" YEN YEN YEN YEN YEN "\n" YEN YEN YEN YEN YEN "\n");
    tt_feed(&x, "\x1b[2;4H\x1b[2@");
    CHECK_LINES(&x, 5, YEN YEN "   " YEN "\n" YEN "    " YEN YEN "\n" YEN YEN YEN YEN YEN "\n" YEN YEN YEN YEN YEN "\n");
    tt_feed(&x, "\x1b[3;4H\x1b[3@");
    CHECK_LINES(&x, 5, YEN YEN "   " YEN "\n" YEN "    " YEN YEN "\n" YEN "     " YEN "\n" YEN YEN YEN YEN YEN "\n");
    tt_feed(&x, "\x1b[4;4H\x1b[4@");
    CHECK_LINES(&x, 5, YEN YEN "   " YEN "\n" YEN "    " YEN YEN "\n" YEN "     " YEN "\n" YEN "      " YEN "\n");
    tt_close(&x);

    wide_setup(&x);
    tt_feed(&x, "\x1b[1;6H\x1b[P");
    CHECK_LINES(&x, 5, YEN YEN " " YEN YEN "\n" YEN YEN YEN YEN YEN "\n" YEN YEN YEN YEN YEN "\n" YEN YEN YEN YEN YEN "\n");
    tt_feed(&x, "\x1b[2;6H\x1b[2P");
    CHECK_LINES(&x, 5, YEN YEN " " YEN YEN "\n" YEN YEN "  " YEN "\n" YEN YEN YEN YEN YEN "\n" YEN YEN YEN YEN YEN "\n");
    tt_feed(&x, "\x1b[3;6H\x1b[3P");
    CHECK_LINES(&x, 5, YEN YEN " " YEN YEN "\n" YEN YEN "  " YEN "\n" YEN YEN " " YEN "\n" YEN YEN YEN YEN YEN "\n");
    tt_close(&x);

    wide_setup(&x);
    tt_feed(&x, "\x1b[1;6H\x1b[X");
    CHECK_LINES(&x, 5, YEN YEN "  " YEN YEN "\n" YEN YEN YEN YEN YEN "\n" YEN YEN YEN YEN YEN "\n" YEN YEN YEN YEN YEN "\n");
    tt_feed(&x, "\x1b[2;6H\x1b[2X");
    CHECK_LINES(&x, 5, YEN YEN "  " YEN YEN "\n" YEN YEN "    " YEN "\n" YEN YEN YEN YEN YEN "\n" YEN YEN YEN YEN YEN "\n");
    tt_feed(&x, "\x1b[3;6H\x1b[3X");
    CHECK_LINES(&x, 5, YEN YEN "  " YEN YEN "\n" YEN YEN "    " YEN "\n" YEN YEN "    " YEN "\n" YEN YEN YEN YEN YEN "\n");
    tt_close(&x);
}

static void test_bs_reverse_wrap(void)
{
    tt x;
    open_with(&x, 5, 5, 1);
    tt_feed(&x, "12345" TTY_BS);
    CHECK_LINES(&x, 1, "123 5");
    repeat(&x, TTY_BS, 10);
    CHECK_LINES(&x, 1, "    5");
    tt_close(&x);
    open_with(&x, 5, 5, 1);
    tt_feed(&x, "1234512345" TTY_BS);
    CHECK_LINES(&x, 2, "12345\n123 5");
    repeat(&x, TTY_BS, 10);
    CHECK_LINES(&x, 2, "12345\n    5");
    tt_close(&x);

    /* The spaces BS SP BS types are content, which trimming keeps. */
    open_with(&x, 5, 5, 1);
    tt_feed(&x, "\x1b[?45h" "12345" TTY_BS);
    CHECK_LINES(&x, 1, "1234 ");
    repeat(&x, TTY_BS, 7);
    CHECK_LINES(&x, 1, "     ");
    tt_close(&x);
    open_with(&x, 5, 5, 1);
    tt_feed(&x, "\x1b[?45h" "1234512345" TTY_BS);
    CHECK_LINES(&x, 2, "12345\n1234 ");
    repeat(&x, TTY_BS, 7);
    CHECK_LINES(&x, 2, "12   \n     ");
    tt_close(&x);
    open_with(&x, 5, 5, 1);
    tt_feed(&x, "\x1b[?45h" "1234512345");
    CHECK(ln(&x, 1)->wrapped);
    repeat(&x, TTY_BS, 7);
    CHECK(!ln(&x, 1)->wrapped);
    tt_close(&x);
    open_with(&x, 5, 5, 1);
    tt_feed(&x, "\x1b[?45h" "12345\r\n" "1234512345");
    repeat(&x, TTY_BS, 50);
    CHECK_LINES(&x, 3, "12345\n     \n     ");
    CHECK_XY(&x, 0, 1);
    tt_close(&x);

    open_with(&x, 5, 5, 1);
    tt_feed(&x, "\x1b[?45h" YEN YEN YEN);
    CHECK_LINES(&x, 2, YEN YEN "\n" YEN);
    int want_x[] = {1, 0, 3, 2, 1, 0};
    const char *want[] = {YEN YEN "\n  ", YEN YEN "\n  ", YEN "  \n  ", YEN "  \n  ", "    \n  ", "    \n  "};
    for (int i = 0; i < 6; i++) {
        tt_feed(&x, TTY_BS);
        CHECK_LINES(&x, 2, want[i]);
        CHECK_INT(x.t->active->x, want_x[i]);
    }
    tt_close(&x);
}

static void test_sgr0(void)
{
    tt x;
    tt_open(&x, 80, 30);
    tt_feed(&x, "\x1b[30m\x1b[40m\x1b[4m");
    CHECK(x.t->cur.fg != 0 && x.t->cur.bg != 0);
    CHECK(x.t->cur.x.ext & SK_EXT_UNDERLINE_STYLE);
    tt_feed(&x, "\x1b[m");
    CHECK_INT(x.t->cur.fg, 0);
    CHECK_INT(x.t->cur.bg, 0);
    CHECK_INT(x.t->cur.x.ext, 0);
    CHECK_INT(x.t->cur.x.url, 0);
    tt_feed(&x, "\x1b[30m\x1b[40m\x1b[4m\x1b]8;;http://example.com\x1b\\");
    uint32_t url = x.t->cur.x.url;
    CHECK(url != 0);
    tt_feed(&x, "\x1b[m");
    CHECK_INT(x.t->cur.fg, 0);
    CHECK_INT(x.t->cur.bg, SK_BG_HAS_EXTENDED);
    CHECK_INT(x.t->cur.x.ext, 0);
    CHECK_INT(x.t->cur.x.url, url);
    tt_close(&x);
}

static sk_underline cur_underline(sk_term *t)
{
    sk_term_cell c = {0};
    c.fg = t->cur.fg;
    c.bg = t->cur.bg;
    c.ext = t->cur.x.ext;
    c.link = t->cur.x.url;
    return sk_term_cell_underline(&c);
}

static void test_underline_styles(void)
{
    tt x;
    tt_open(&x, 10, 5);
    struct {
        const char *on;
        sk_underline style;
    } s[] = {{"\x1b[4m", SK_UNDERLINE_SINGLE},   {"\x1b[21m", SK_UNDERLINE_DOUBLE}, {"\x1b[4:1m", SK_UNDERLINE_SINGLE},
             {"\x1b[4:2m", SK_UNDERLINE_DOUBLE}, {"\x1b[4:3m", SK_UNDERLINE_CURLY}, {"\x1b[4:4m", SK_UNDERLINE_DOTTED},
             {"\x1b[4:5m", SK_UNDERLINE_DASHED}};
    for (size_t i = 0; i < SK_LEN(s); i++) {
        tt_feed(&x, s[i].on);
        CHECK_INT(cur_underline(x.t), s[i].style);
        tt_feed(&x, "\x1b[24m");
        CHECK_INT(cur_underline(x.t), SK_UNDERLINE_NONE);
        if (i >= 2) {
            tt_feed(&x, s[i].on);
            tt_feed(&x, "\x1b[4:0m");
            CHECK_INT(cur_underline(x.t), SK_UNDERLINE_NONE);
        }
    }
    tt_feed(&x, "\x1b[4:5m\x1b[4m");
    CHECK_INT(cur_underline(x.t), SK_UNDERLINE_SINGLE);
    tt_close(&x);
}

/* AttributeData.getUnderlineColor and its mode. */
static uint32_t ul_color(uint32_t fg, uint32_t bg, uint32_t ext)
{
    if (bg & SK_BG_HAS_EXTENDED) {
        uint32_t c = ext & SK_EXT_UNDERLINE_COLOR;
        if (c & SK_ATTR_CM_MASK)
            return fg_color(c);
    }
    return fg_color(fg);
}

static uint32_t ul_mode(uint32_t fg, uint32_t bg, uint32_t ext)
{
    return (bg & SK_BG_HAS_EXTENDED) ? ext & SK_ATTR_CM_MASK : fg & SK_ATTR_CM_MASK;
}

static void test_underline_colors(void)
{
    tt x;
    tt_open(&x, 10, 5);
    const char *seqs[] = {"", "\x1b[30m", "\x1b[38;510m", "\x1b[38;2;1;2;3m"};
    for (int i = 0; i < 4; i++) {
        tt_feed(&x, seqs[i]);
        sk_cell_data *c = &x.t->cur;
        CHECK_INT(ul_color(c->fg, c->bg, c->x.ext), fg_color(c->fg));
    }
    tt_close(&x);

    tt_open(&x, 10, 5);
    tt_feed(&x, "\x1b[4m\x1b[58;5;123m");
    sk_cell_data *c = &x.t->cur;
    CHECK_INT(ul_color(c->fg, c->bg, c->x.ext), 123);
    CHECK_INT(ul_mode(c->fg, c->bg, c->x.ext), SK_ATTR_CM_P256);
    tt_feed(&x, "\x1b[58;2::1:2:3m");
    CHECK_INT(ul_color(c->fg, c->bg, c->x.ext), 1 << 16 | 2 << 8 | 3);
    CHECK_INT(ul_mode(c->fg, c->bg, c->x.ext), SK_ATTR_CM_RGB);
    tt_close(&x);

    /* Persisted in the cells. */
    tt_open(&x, 10, 5);
    tt_feed(&x, "\x1b[4m\x1b[58;5;123mab");
    sk_term_cell cell = tt_cell(x.t, 1, 0);
    CHECK_INT(ul_color(cell.fg, cell.bg, cell.ext), 123);
    CHECK_INT(ul_mode(cell.fg, cell.bg, cell.ext), SK_ATTR_CM_P256);
    tt_feed(&x, "\x1b[4:0m");
    c = &x.t->cur;
    CHECK_INT(ul_color(c->fg, c->bg, c->x.ext), fg_color(c->fg));
    tt_feed(&x, "a");
    cell = tt_cell(x.t, 1, 0);
    CHECK_INT(ul_color(cell.fg, cell.bg, cell.ext), 123);
    cell = tt_cell(x.t, 2, 0);
    CHECK_INT(ul_color(cell.fg, cell.bg, cell.ext), fg_color(x.t->cur.fg));
    CHECK_INT(cell.bg & SK_BG_HAS_EXTENDED, 0);
    tt_feed(&x, "\x1b[4m\x1b[58;2::1:2:3ma\x1b[24m");
    cell = tt_cell(x.t, 1, 0);
    CHECK_INT(ul_color(cell.fg, cell.bg, cell.ext), 123);
    cell = tt_cell(x.t, 3, 0);
    CHECK_INT(ul_color(cell.fg, cell.bg, cell.ext), 1 << 16 | 2 << 8 | 3);
    CHECK_INT(ul_mode(cell.fg, cell.bg, cell.ext), SK_ATTR_CM_RGB);
    tt_close(&x);
}

static void test_decstr(void)
{
    tt x;
    open_with(&x, 10, 5, 1);
    tt_feed(&x, "01234567890123");
    tt_feed(&x, "\x1b[4h");
    CHECK(x.t->insert_mode);
    tt_feed(&x, "\x1b[!p");
    CHECK(!x.t->insert_mode);
    tt_feed(&x, "\x1b[?25l");
    CHECK(x.t->cursor_hidden);
    tt_feed(&x, "\x1b[!p");
    CHECK(!x.t->cursor_hidden);
    tt_feed(&x, "\x1b[2;4r");
    CHECK(x.t->active->scroll_top == 1 && x.t->active->scroll_bottom == 3);
    tt_feed(&x, "\x1b[!p");
    CHECK(x.t->active->scroll_top == 0 && x.t->active->scroll_bottom == 4);
    tt_feed(&x, "\x1b[1;2;32;43m");
    CHECK(x.t->cur.fg & SK_FG_BOLD);
    tt_feed(&x, "\x1b[!p");
    CHECK_INT(x.t->cur.fg, 0);
    CHECK_INT(x.t->cur.bg, 0);
    tt_close(&x);

    open_with(&x, 10, 5, 1);
    tt_feed(&x, "01234567890123\x1b" "7");
    CHECK_INT(x.t->active->saved_x, 4);
    CHECK_INT(x.t->active->saved_y, 1);
    tt_feed(&x, "\x1b[!p");
    CHECK_INT(x.t->active->saved_x, 0);
    CHECK_INT(x.t->active->saved_y, 0);
    tt_feed(&x, "\x1b[?6h");
    CHECK(x.t->origin);
    tt_feed(&x, "\x1b[!p");
    CHECK(!x.t->origin);
    tt_close(&x);
}

static void test_osc(void)
{
    tt x;
    tt_open(&x, 80, 30);
    tt_feed(&x, "\x1b]4;0;rgb:01/02/03\x07");
    CHECK_INT(x.t->pal.ansi[0], 0x010203);
    tt_feed(&x, "\x1b]4;123;#aabbcc\x07");
    CHECK_INT(x.t->pal.ansi[123], 0xaabbcc);
    tt_feed(&x, "\x1b]4;0;rgb:aa/bb/cc;123;#001122\x07");
    CHECK_INT(x.t->pal.ansi[0], 0xaabbcc);
    CHECK_INT(x.t->pal.ansi[123], 0x001122);
    /* An invalid value leaves its index alone. */
    tt_feed(&x, "\x1b]4;0;rgb:01/02/03;45;rgb:1/22/333;123;#334455\x07");
    CHECK_INT(x.t->pal.ansi[0], 0x010203);
    CHECK_INT(x.t->pal.ansi[45], 0x00d7ff);
    CHECK_INT(x.t->pal.ansi[123], 0x334455);
    tt_feed(&x, "\x1b]4;0;?;123;?\x07");
    CHECK_REPLY(&x, "\x1b]4;0;rgb:0101/0202/0303\x1b\\\x1b]4;123;rgb:3333/4444/5555\x1b\\");
    tt_feed(&x, "\x1b]104;0;123\x07");
    CHECK_INT(x.t->pal.ansi[0], 0x2e3436);
    CHECK_INT(x.t->pal.ansi[123], 0x87ffff);

    tt_feed(&x, "\x1b]10;?;?;?;?\x07");
    CHECK_REPLY(&x, "\x1b]10;rgb:ffff/ffff/ffff\x1b\\\x1b]11;rgb:0000/0000/0000\x1b\\\x1b]12;rgb:ffff/ffff/ffff\x1b\\");
    tt_feed(&x, "\x1b]11;?;?;?;?\x07");
    CHECK_REPLY(&x, "\x1b]11;rgb:0000/0000/0000\x1b\\\x1b]12;rgb:ffff/ffff/ffff\x1b\\");
    tt_feed(&x, "\x1b]10;rgb:aa/bb/cc;#001122;rgb:12/34/56\x07");
    CHECK_INT(x.t->pal.fg, 0xaabbcc);
    CHECK_INT(x.t->pal.bg, 0x001122);
    CHECK_INT(x.t->pal.cursor, 0x123456);
    tt_feed(&x, "\x1b]110\x07\x1b]111\x07\x1b]112\x07");
    CHECK_INT(x.t->pal.fg, 0xffffff);
    CHECK_INT(x.t->pal.bg, 0);
    CHECK_INT(x.t->pal.cursor, 0xffffff);

    tt_feed(&x, "\x1b]8;id=100;http://localhost:3000\x07");
    uint32_t id = x.t->cur.x.url;
    CHECK(id != 0);
    CHECK_STR(sk_term_link_id(x.t, id), "100");
    CHECK_STR(sk_term_link_uri(x.t, id), "http://localhost:3000");
    tt_feed(&x, "\x1b]8;;\x07");
    CHECK_INT(x.t->cur.x.url, 0);
    tt_feed(&x, "\x1b]8;;http://localhost:3000;abc=def\x07");
    id = x.t->cur.x.url;
    CHECK(sk_term_link_id(x.t, id) == NULL);
    CHECK_STR(sk_term_link_uri(x.t, id), "http://localhost:3000;abc=def");
    tt_feed(&x, "\x1b]8;;\x07");
    CHECK_INT(x.t->cur.x.url, 0);
    tt_close(&x);
}

static void test_el_ed_at_cols(void)
{
    struct {
        const char *seq, *want;
    } c[] = {
        {"\x1b[0K", "##########\n\n\n\n"}, {"\x1b[1K", "\n\n\n\n"}, {"\x1b[2K", "\n\n\n\n"},
        {"\x1b[0J", "##########\n\n\n\n"}, {"\x1b[1J", "\n\n\n\n"}, {"\x1b[2J", "\n\n\n\n"},
        {"\x1b[3J", "##########\n\n\n\n"},
    };
    for (size_t i = 0; i < SK_LEN(c); i++) {
        tt x;
        tt_open(&x, 10, 5);
        tt_feed(&x, "##########");
        tt_feed(&x, c[i].seq);
        CHECK_INT(x.t->active->x, 10);
        CHECK_LINES(&x, 5, c[i].want);
        tt_close(&x);
    }
    const char *seq[] = {"\x1b[10@", "\x1b[10 @", "\x1b[10A", "\x1b[10 A", "\x1b[10B", "\x1b[10C", "\x1b[10D",
                         "\x1b[10E", "\x1b[10F", "\x1b[10G", "\x1b[10;10H", "\x1b[10I", "\x1b[10L", "\x1b[10M",
                         "\x1b[10P", "\x1b[10S", "\x1b[10T", "\x1b[10X", "\x1b[10Z", "\x1b[10`", "\x1b[10a",
                         "\x1b[10b", "\x1b[10d", "\x1b[10e", "\x1b[10;10f", "\x1b[0g", "\x1b[s", "\x1b[10'}",
                         "\x1b[10'~"};
    tt x;
    tt_open(&x, 10, 5);
    for (size_t i = 0; i < SK_LEN(seq); i++) {
        tt_feed(&x, "##########\x1b[2J");
        tt_feed(&x, seq[i]);
        CHECK(x.t->active->x <= x.t->cols);
        x.t->cur = (sk_cell_data){SK_CELL_NULL, 0, 0, {0, 0}, NULL};
        reset_buffers(&x, SK_TERM_SCROLLBACK);
    }
    tt_close(&x);
}

static void test_decsca(void)
{
    tt x;
    tt_open(&x, 80, 30);
    tt_feed(&x, "some text\x1b[?2K");
    CHECK_LINES(&x, 2, "\n");
    tt_feed(&x, "some text\x1b[?2J");
    CHECK_LINES(&x, 2, "\n");
    tt_close(&x);
    tt_open(&x, 80, 30);
    tt_feed(&x, "###\x1b[1\"qlineerase\x1b[0\"q***\x1b[?2K");
    CHECK_LINES(&x, 2, "   lineerase\n");
    tt_feed(&x, "\x1b[2K");
    CHECK_LINES(&x, 2, "\n");
    tt_close(&x);
    tt_open(&x, 80, 30);
    tt_feed(&x, "###\x1b[1\"qdisplayerase\x1b[0\"q***\x1b[?2J");
    CHECK_LINES(&x, 2, "   displayerase\n");
    tt_feed(&x, "\x1b[2J");
    CHECK_LINES(&x, 2, "\n");
    tt_close(&x);
    tt_open(&x, 80, 30);
    tt_feed(&x, "\x1bP$q\"q\x1b\\");
    CHECK_REPLY(&x, "\x1bP1$r0\"q\x1b\\");
    tt_feed(&x, "###\x1b[1\"q\x1bP$q\"q\x1b\\");
    CHECK_REPLY(&x, "\x1bP1$r1\"q\x1b\\");
    tt_feed(&x, "###\x1b[2\"q\x1bP$q\"q\x1b\\");
    CHECK_REPLY(&x, "\x1bP1$r0\"q\x1b\\");
    tt_close(&x);
}

static void test_decrqm(void)
{
    tt x;
    tt_open(&x, 80, 30);
    tt_feed(&x, "\x1b[2$p");
    CHECK_REPLY(&x, "\x1b[2;4$y");
    tt_feed(&x, "\x1b[4$p\x1b[4h\x1b[4$p\x1b[4l\x1b[4$p");
    CHECK_REPLY(&x, "\x1b[4;2$y\x1b[4;1$y\x1b[4;2$y");
    tt_feed(&x, "\x1b[12$p");
    CHECK_REPLY(&x, "\x1b[12;3$y");
    tt_feed(&x, "\x1b[20$p\x1b[20h\x1b[20$p\x1b[20l\x1b[20$p");
    CHECK_REPLY(&x, "\x1b[20;2$y\x1b[20;1$y\x1b[20;2$y");
    tt_feed(&x, "\x1b[1234$p");
    CHECK_REPLY(&x, "\x1b[1234;0$y");
    const int reset[] = {1, 6, 9, 12, 45, 66, 1000, 1002, 1003, 1004, 1006, 1016, 47, 1047, 1049, 2004, 2026};
    for (size_t i = 0; i < SK_LEN(reset); i++) {
        int m = reset[i];
        feedf(&x, "\x1b[?%d$p", m);
        char *r = tt_take(&x);
        sk_buf want = {0};
        sk_buf_printf(&want, "\x1b[?%d;2$y", m);
        /* 1004 answers the focus report too. */
        CHECK(sk_str_suffix(sk_str_c(r), sk_buf_str(&want)));
        free(r);
        feedf(&x, "\x1b[?%dh\x1b[?%d$p", m, m);
        r = tt_take(&x);
        sk_buf_clear(&want);
        sk_buf_printf(&want, "\x1b[?%d;1$y", m);
        CHECK(sk_str_suffix(sk_str_c(r), sk_buf_str(&want)));
        free(r);
        feedf(&x, "\x1b[?%dl\x1b[?%d$p", m, m);
        r = tt_take(&x);
        sk_buf_clear(&want);
        sk_buf_printf(&want, "\x1b[?%d;2$y", m);
        CHECK_STR(r, want.ptr);
        free(r);
        sk_buf_free(&want);
    }
    const int set[] = {7, 25};
    for (size_t i = 0; i < SK_LEN(set); i++) {
        int m = set[i];
        feedf(&x, "\x1b[?%d$p\x1b[?%dl\x1b[?%d$p\x1b[?%dh\x1b[?%d$p", m, m, m, m, m);
        sk_buf want = {0};
        sk_buf_printf(&want, "\x1b[?%d;1$y\x1b[?%d;2$y\x1b[?%d;1$y", m, m, m);
        CHECK_REPLY(&x, want.ptr);
        sk_buf_free(&want);
    }
    const int perma[][2] = {{3, 0}, {8, 3}, {67, 4}, {1005, 4}, {1015, 4}, {1048, 1}};
    for (size_t i = 0; i < SK_LEN(perma); i++) {
        feedf(&x, "\x1b[?%d$p", perma[i][0]);
        sk_buf want = {0};
        sk_buf_printf(&want, "\x1b[?%d;%d$y", perma[i][0], perma[i][1]);
        CHECK_REPLY(&x, want.ptr);
        sk_buf_free(&want);
    }
    tt_close(&x);
}

const sk_test sk_suite_term_xinput[] = {
    T(test_sl_sr_decic_decdc),
    T(test_bs_margins),
    T(test_save_restore_cursor),
    T(test_cursor_style),
    T(test_insert_delete_chars),
    T(test_erase_in_line),
    T(test_erase_in_line_wrap),
    T(test_erase_in_display),
    T(test_print),
    T(test_alt_screen),
    T(test_text_attributes),
    T(test_colon_notation),
    T(test_cursor_positioning),
    T(test_cursor_clamped),
    T(test_decstbm),
    T(test_scroll_margins),
    T(test_wide_resets),
    T(test_bs_reverse_wrap),
    T(test_sgr0),
    T(test_underline_styles),
    T(test_underline_colors),
    T(test_decstr),
    T(test_osc),
    T(test_el_ed_at_cols),
    T(test_decsca),
    T(test_decrqm),
    {0},
};
