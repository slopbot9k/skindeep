/* Screens drawn and compared with golden images at 1x and 2x: what the
 * view draws for each attribute, colour, underline, cursor, the
 * selection, wide and combining characters, and every custom glyph. */
#include "../../src/base/utf8.h"
#include "../golden.h"
#include "view.h"

#include <stdio.h>

typedef void (*screen_fn)(tv *x, sk_termview_state *st);

/* The screen drawn whole at both scales into NAME_1x and NAME_2x. */
static void golden(const char *name, int cols, int rows, screen_fn fn)
{
    for (int s = 1; s <= 2; s++) {
        tv x;
        tv_open(&x, cols, rows, s);
        sk_term_set_focus(x.t, true);
        sk_termview_state st = tv_focused();
        fn(&x, &st);
        tv_draw(&x, &st, true);
        char full[64];
        snprintf(full, sizeof full, "%s_%dx", name, s);
        CHECK_GOLDEN(full, &x.im, 2);
        tv_close(&x);
    }
}

static void plain(tv *x, sk_termview_state *st)
{
    (void)st;
    tv_feed(x, "Hello, world! The quick brown fox jumps over the lazy dog.\r\n"
               "0123456789 ~!@#$%^&*()_+-=[]{};':\",./<>?\\|` \xc2\xa9\xc2\xb5\xc3\x9f\xc3\xa9\r\n"
               "    indented, \ttabbed\x1b[?25l");
}

static void test_plain(void)
{
    golden("termview_plain", 62, 3, plain);
}

static void colors(tv *x, sk_termview_state *st)
{
    (void)st;
    sk_buf b = {0};
    for (int i = 0; i < 8; i++)
        sk_buf_printf(&b, "\x1b[3%dm f%d \x1b[9%dmb%d ", i, i, i, i + 8);
    sk_buf_printf(&b, "\x1b[0m\r\n");
    for (int i = 0; i < 8; i++)
        sk_buf_printf(&b, "\x1b[4%dm g%d \x1b[10%dmb%d ", i, i, i, i + 8);
    sk_buf_printf(&b, "\x1b[0m\r\n");
    for (int i = 16; i < 232; i += 3)
        sk_buf_printf(&b, "\x1b[48;5;%dm ", i);
    sk_buf_printf(&b, "\x1b[0m\r\n");
    for (int i = 232; i < 256; i++)
        sk_buf_printf(&b, "\x1b[48;5;%dm  ", i);
    sk_buf_printf(&b, "\x1b[0m\r\n");
    for (int i = 0; i < 72; i++)
        sk_buf_printf(&b, "\x1b[48;2;%d;%d;%dm\x1b[38;2;%d;%d;%dm%c", i * 255 / 71, 64, 255 - i * 255 / 71, 255 - i * 3,
                      i * 3, 128, 'a' + i % 26);
    sk_buf_printf(&b, "\x1b[0m\r\n\x1b[38;5;208mcolor 208 \x1b[38;5;33;48;5;236m 33 on 236 \x1b[0m\x1b[?25l");
    tv_feed(x, sk_buf_cstr(&b));
    sk_buf_free(&b);
}

static void test_colors(void)
{
    golden("termview_colors", 72, 6, colors);
}

static void attributes(tv *x, sk_termview_state *st)
{
    (void)st;
    tv_feed(x, "normal \x1b[1mbold\x1b[0m \x1b[2mdim\x1b[0m \x1b[3mitalic\x1b[0m \x1b[1;3mbold italic\x1b[0m "
               "\x1b[7minverse\x1b[0m [\x1b[8minvisible\x1b[0m]\r\n"
               "\x1b[31mred \x1b[1mbold red\x1b[0;2;31m dim red\x1b[0;7;31m inverse red \x1b[1minverse bold\x1b[0m "
               "\x1b[7;32;44m green on blue \x1b[0m\r\n"
               "\x1b[3;42mitalic over green\x1b[0;44mWfx\x1b[0m \x1b[1;3;7mbold italic inverse\x1b[0m "
               "\x1b[2;7mdim inv\x1b[0m"
               "\x1b[?25l");
}

static void test_attributes(void)
{
    golden("termview_attributes", 64, 3, attributes);
}

static void underlines(tv *x, sk_termview_state *st)
{
    (void)st;
    tv_feed(x, "\x1b[4msingle gjpqy_\x1b[0m \x1b[4:2mdouble gjpqy\x1b[0m \x1b[4:3mcurly gjpqy\x1b[0m"
               "\r\n\r\n\x1b[4:4mdotted gjpqy\x1b[0m \x1b[4:5mdashed gjpqy\x1b[0m \x1b[2;4mdim\x1b[0m "
               "\x1b[3;4mitalic\x1b[0m\r\n\r\n"
               "\x1b[4;58;2;255;64;0mrgb colour\x1b[0m \x1b[4:3;58;5;12mcurly blue\x1b[0m \x1b[31;4mred fg\x1b[0m "
               "\x1b[1;34;4mbold blue\x1b[0m \x1b[4;58;5;3;59mSGR 59\x1b[0m\r\n\r\n"
               "\x1b[4:4;58;5;2mdots\xe4\xb8\xad\x1b[0m \x1b[4:3m\xe4\xb8\xad\xe6\x96\x87\x1b[0m "
               "\x1b[7;4:2mdouble inv\x1b[0m"
               "\x1b[?25l");
}

static void test_underlines(void)
{
    golden("termview_underlines", 64, 8, underlines);
}

static void strikes(tv *x, sk_termview_state *st)
{
    (void)st;
    tv_feed(x, "\x1b[9mstrikethrough\x1b[0m \x1b[53moverline\x1b[0m \x1b[9;53mboth\x1b[0m "
               "\x1b[4;9;53mall three\x1b[0m\r\n"
               "\x1b[9;31mred strike\x1b[0m \x1b[2;9mdim strike\x1b[0m \x1b[9m\xe2\x94\x80\xe2\x94\x80\x1b[0m "
               "\x1b[9m\xe2\x96\x92\xe2\x96\x92\x1b[0m \x1b[53;4m\xe2\x96\x91\xe2\x96\x91\x1b[0m "
               "\x1b[9m\xe4\xb8\xad\x1b[0m"
               "\x1b[?25l");
}

static void test_strikes(void)
{
    golden("termview_strikes", 48, 3, strikes);
}

static void wide(tv *x, sk_termview_state *st)
{
    (void)st;
    /* The embedded faces have no CJK: its characters draw DejaVu's box,
     * placed in the first of their two cells. */
    tv_feed(x, "CJK: \xe4\xb8\xad\xe6\x96\x87\xe5\xad\x97 | \x1b[44m\xe4\xb8\xad\x1b[0m| "
               "\xe2\x9a\xa1\xe2\x98\x94 \x1b[41m\xe2\x9a\xa1\x1b[0m|\r\n"
               "combining: e\xcc\x81 a\xcc\xa3 o\xcc\x88\xcc\x81 n\xcc\x83 \x1b[1me\xcc\x81\x1b[0m "
               "\x1b[3me\xcc\x80\x1b[0m "
               "x\xe2\x83\x9d\r\n"
               "end\xe4\xb8\xad");
}

static void test_wide_and_combining(void)
{
    golden("termview_wide", 40, 3, wide);
}

/* U+2500 to U+259F, 16 to a row, every cell touching its neighbours. */
static void box_table(tv *x, sk_termview_state *st)
{
    (void)st;
    sk_buf b = {0};
    for (uint32_t row = 0x2500; row < 0x25A0; row += 16) {
        for (uint32_t cp = row; cp < row + 16; cp++) {
            char u[4];
            sk_buf_append(&b, u, sk_utf8_encode(cp, u));
        }
        sk_buf_printf(&b, " ");
        for (uint32_t cp = row; cp < row + 16; cp++) {
            char u[4];
            sk_buf_printf(&b, "\x1b[%dm", 31 + (int)(cp % 7));
            sk_buf_append(&b, u, sk_utf8_encode(cp, u));
            sk_buf_append(&b, u, sk_utf8_encode(cp, u));
        }
        sk_buf_printf(&b, "\x1b[0m\r\n");
    }
    /* Dim, inverse and on a light background; Powerline; the legacy
     * computing eighths and checkers. */
    sk_buf_append_c(&b, "\x1b[2m\xe2\x94\x80\xe2\x94\xbc\xe2\x96\x92\xe2\x96\x88"
                        "\x1b[0;7m\xe2\x94\x80\xe2\x94\xbc\xe2\x96\x92"
                        "\x1b[0;47;30m\xe2\x95\xad\xe2\x95\xae\x1b[0m \xee\x82\xa0\xee\x82\xa1\xee\x82\xa2 "
                        "\x1b[44m\xee\x82\xb0\x1b[0;34m\xee\x82\xb0\xee\x82\xb1 "
                        "\xee\x82\xb2\x1b[44;37m\xee\x82\xb2\x1b[0m "
                        "\xee\x82\xb4\xee\x82\xb5\xee\x82\xb6\xee\x82\xb7 "
                        "\xee\x82\xb8\xee\x82\xb9\xee\x82\xba\xee\x82\xbb"
                        "\xee\x82\xbc\xee\x82\xbd\xee\x82\xbe\xee\x82\xbf "
                        "\xf0\x9f\xad\xb0\xf0\x9f\xad\xb5\xf0\x9f\xad\xbc\xf0\x9f\xae\x81"
                        "\xf0\x9f\xae\x95\xf0\x9f\xae\x96\xf0\x9f\xae\x97\x1b[?25l");
    tv_feed(x, sk_buf_cstr(&b));
    sk_buf_free(&b);
}

static void test_box_table(void)
{
    golden("termview_box_table", 49, 11, box_table);
}

/* The alignment test of CustomGlyphs.ts's own comment. */
static void box_align(tv *x, sk_termview_state *st)
{
    (void)st;
    static const char *const lines[] = {
        "                                                                      █",
        "                                                                      ▉",
        "  ╔══╦══╗  ┌──┬──┐  ╭──┬──╮  ╭──┬──╮  ┏━━┳━━┓  ┎┒┏┑   ╷  ╻ ┏┯┓ ┌┰┐    ▊ ╱╲╱╲╳╳╳",
        "  ║┌─╨─┐║  │╔═╧═╗│  │╒═╪═╕│  │╓─╁─╖│  ┃┌─╂─┐┃  ┗╃╄┙  ╶┼╴╺╋╸┠┼┨ ┝╋┥    ▋ ╲╱╲╱╳╳╳",
        "  ║│╲ ╱│║  │║   ║│  ││ │ ││  │║ ┃ ║│  ┃│ ╿ │┃  ┍╅╆┓   ╵  ╹ ┗┷┛ └┸┘    ▌ ╱╲╱╲╳╳╳",
        "  ╠╡ ╳ ╞╣  ├╢   ╟┤  ├┼─┼─┼┤  ├╫─╂─╫┤  ┣┿╾┼╼┿┫  ┕┛┖┚     ┌┄┄┐ ╎ ┏┅┅┓ ┋ ▍ ╲╱╲╱╳╳╳",
        "  ║│╱ ╲│║  │║   ║│  ││ │ ││  │║ ┃ ║│  ┃│ ╽ │┃  ░░▒▒▓▓██ ┊  ┆ ╎ ╏  ┇ ┋ ▎",
        "  ║└─╥─┘║  │╚═╤═╝│  │╘═╪═╛│  │╙─╀─╜│  ┃└─╂─┘┃  ░░▒▒▓▓██ ┊  ┆ ╎ ╏  ┇ ┋ ▏",
        "  ╚══╩══╝  └──┴──┘  ╰──┴──╯  ╰──┴──╯  ┗━━┻━━┛           └╌╌┘ ╎ ┗╍╍┛ ┋  ▁▂▃▄▅▆▇█",
    };
    for (size_t i = 0; i < SK_LEN(lines); i++) {
        tv_feed(x, lines[i]);
        tv_feed(x, i + 1 < SK_LEN(lines) ? "\r\n" : "\x1b[?25l");
    }
}

static void test_box_align(void)
{
    golden("termview_box_align", 82, 9, box_align);
}

/* Each cursor style, focused and not, side by side: a 6 by 2 terminal
 * each, the cursor on the b of "ab", or on a wide character. */
static void cursors(tv *x, sk_termview_state *st)
{
    (void)st;
    static const char *const styles[] = {"\x1b[2 q", "\x1b[4 q", "\x1b[6 q", "\x1b[2 q\x1b[2;1H", "\x1b[6 q\x1b[2;1H"};
    int n = (int)SK_LEN(styles);
    for (int f = 0; f < 2; f++)
        for (int i = 0; i < n; i++) {
            tv c;
            tv_open(&c, 6, 2, x->style.scale);
            sk_term_set_focus(c.t, true);
            sk_termview_state cs = tv_focused();
            cs.focused = f == 0;
            tv_feed(&c, "ab\x1b[D\r\n\xe4\xb8\xadx\x1b[1;2H");
            tv_feed(&c, styles[i]);
            tv_draw(&c, &cs, true);
            sk_blit(&x->im, i * 7 * x->cw, f * 3 * x->ch, &c.im, sk_image_bounds(&c.im), SK_BLEND_SRC, 255, NULL);
            tv_close(&c);
        }
}

static void test_cursors(void)
{
    /* The panels are blitted over the frame after it is drawn. */
    for (int s = 1; s <= 2; s++) {
        tv x;
        tv_open(&x, 34, 5, s);
        sk_termview_state st = tv_focused();
        tv_draw(&x, &st, true);
        cursors(&x, &st);
        char name[64];
        snprintf(name, sizeof name, "termview_cursors_%dx", s);
        CHECK_GOLDEN(name, &x.im, 2);
        tv_close(&x);
    }
}

/* A selection across lines over coloured and inverse cells, focused;
 * and a column selection unfocused, with a selection foreground. */
static void selection(tv *x, sk_termview_state *st)
{
    (void)st;
    tv_feed(x, "plain \x1b[41mred bg\x1b[0m \x1b[7minverse\x1b[0m \x1b[2mdim\x1b[0m \x1b[34mblue\x1b[0m\r\n"
               "second line \xe2\x94\x80\xe2\x94\xbc\xe2\x94\x80 \xee\x82\xb0 \x1b[4munder\x1b[0m\r\n"
               "third line ends here\x1b[?25l");
    sk_term_select_start(x->t, SK_SELECT_CHAR, 3, 0);
    sk_term_select_move(x->t, 9, 2);
}

static void test_selection(void)
{
    golden("termview_selection", 36, 3, selection);
    for (int s = 1; s <= 2; s++) {
        tv x;
        tv_open(&x, 36, 3, s);
        x.style.selection_inactive_bg = SK_RGBA(0, 128, 255, 0x60);
        x.style.selection_fg = SK_RGB(255, 255, 0);
        sk_termview_set_style(x.v, &x.style);
        sk_termview_state st = tv_focused();
        selection(&x, &st);
        sk_term_select_start(x.t, SK_SELECT_COLUMN, 2, 0);
        sk_term_select_move(x.t, 14, 2);
        st.focused = false;
        tv_draw(&x, &st, true);
        char name[64];
        snprintf(name, sizeof name, "termview_selection_inactive_%dx", s);
        CHECK_GOLDEN(name, &x.im, 2);
        tv_close(&x);
    }
}

/* The hovered links: a detected one over two rows, an OSC 8 one, whose
 * cells always carry a dashed underline. */
static void links(tv *x, sk_termview_state *st)
{
    tv_feed(x, "see https://example.com/a/long/path and \x1b]8;;https://example.org\x1b\\this link\x1b]8;;\x1b\\.\r\n"
               "continued/here ok\x1b[?25l");
    st->link_row0 = 0;
    st->link_col0 = 4;
    st->link_row1 = 1;
    st->link_col1 = 14;
    sk_term_cell cells[56];
    sk_term_row(x->t, 0, cells);
    st->link = cells[41].link;
}

static void test_links(void)
{
    golden("termview_links", 56, 2, links);
}

const sk_test sk_suite_termview_golden[] = {
    T(test_plain),
    T(test_colors),
    T(test_attributes),
    T(test_underlines),
    T(test_strikes),
    T(test_wide_and_combining),
    T(test_box_table),
    T(test_box_align),
    T(test_cursors),
    T(test_selection),
    T(test_links),
    {0},
};
