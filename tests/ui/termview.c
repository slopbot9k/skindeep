#include "view.h"

#include "../../src/text/text.h"
#include "../../src/ui/internal.h"

#include <math.h>

/* CharSizeService measured DejaVu Sans Mono at 14 px with canvas
 * measureText('W'): the advance, 1233 of 2048 units, and the font
 * bounding box, Blink's ascent and descent each rounded (hhea's 1901
 * and 483). WebglRenderer floors the width times the device pixel
 * ratio and ceils the height. */
static void test_cell_size(void)
{
    const sk_font *f = sk_font_embedded(SK_FONT_MONO);
    double width = sk_font_advance(f, sk_font_glyph(f, 'W'), 14);
    sk_font_vmetrics vm;
    sk_font_vmetrics_at(f, 14, &vm);
    double height = floor(vm.ascent + 0.5) + floor(vm.descent + 0.5);
    CHECK(width == 1233.0 * 14 / 2048);
    CHECK(floor(vm.ascent + 0.5) == 13 && floor(vm.descent + 0.5) == 3);
    const double scales[] = {1, 2, 1.5, 1.25};
    for (size_t i = 0; i < SK_LEN(scales); i++) {
        tv x;
        tv_open(&x, 4, 2, scales[i]);
        CHECK_INT(x.cw, (int)floor(width * scales[i]));
        CHECK_INT(x.ch, (int)ceil(height * scales[i]));
        tv_close(&x);
    }
    tv x;
    tv_open(&x, 4, 2, 1);
    CHECK_INT(x.cw, 8);
    CHECK_INT(x.ch, 16);
    tv_close(&x);
    tv_open(&x, 4, 2, 2);
    CHECK_INT(x.cw, 16);
    CHECK_INT(x.ch, 32);
    tv_close(&x);
}

/* FitAddon: the parent's CSS size, less 14 px of scrollbar across,
 * over the CSS cell, at least 2 by 1. */
static void test_fit(void)
{
    tv x;
    int cols, rows;
    tv_open(&x, 4, 2, 1);
    sk_termview_fit(x.v, 800, 600, &cols, &rows);
    CHECK_INT(cols, 98);
    CHECK_INT(rows, 37);
    sk_termview_fit(x.v, 14 + 8 * 80, 16 * 24, &cols, &rows);
    CHECK_INT(cols, 80);
    CHECK_INT(rows, 24);
    sk_termview_fit(x.v, 14 + 8 * 80 - 1, 16 * 24 - 1, &cols, &rows);
    CHECK_INT(cols, 79);
    CHECK_INT(rows, 23);
    sk_termview_fit(x.v, 0, 0, &cols, &rows);
    CHECK_INT(cols, 2);
    CHECK_INT(rows, 1);
    tv_close(&x);
    tv_open(&x, 4, 2, 2);
    sk_termview_fit(x.v, 1600, 1200, &cols, &rows);
    CHECK_INT(cols, 98);
    CHECK_INT(rows, 37);
    /* 803.5 CSS pixels: parseInt makes them 803. */
    sk_termview_fit(x.v, 1607, 1201, &cols, &rows);
    CHECK_INT(cols, 98);
    CHECK_INT(rows, 37);
    tv_close(&x);
}

static void check_hit(tv *x, int px, int py, int col, int row, int edge)
{
    int c = -1, r = -1, e = -1;
    sk_termview_hit(x->v, x->t, px, py, &c, &r, &e);
    CHECK_INT(c, col);
    CHECK_INT(r, row);
    CHECK_INT(e, edge);
}

static void test_hit(void)
{
    tv x;
    tv_open(&x, 10, 5, 1);
    check_hit(&x, 0, 0, 0, 0, 0);
    check_hit(&x, 4, 5, 0, 0, 0);
    check_hit(&x, 5, 15, 0, 0, 1);
    check_hit(&x, 8, 16, 1, 1, 1);
    check_hit(&x, 13, 31, 1, 1, 2);
    check_hit(&x, 79, 79, 9, 4, 10);
    check_hit(&x, 500, 500, 9, 4, 10);
    check_hit(&x, -5, -5, 0, 0, 0);
    tv_close(&x);
    tv_open(&x, 10, 5, 2);
    check_hit(&x, 8, 31, 0, 0, 0);
    check_hit(&x, 9, 32, 0, 1, 1);
    check_hit(&x, 159, 159, 9, 4, 10);
    tv_close(&x);
}

static bool same_image(const sk_image *a, const sk_image *b)
{
    if (a->w != b->w || a->h != b->h)
        return false;
    for (int y = 0; y < a->h; y++)
        if (memcmp(sk_image_row(a, y), sk_image_row(b, y), (size_t)a->w * sizeof(sk_px)))
            return false;
    return true;
}

/* Feeds both, draws a by its dirty rows and b whole, and expects the
 * same picture. */
static void step(tv *a, tv *b, const char *s, const sk_termview_state *st)
{
    tv_feed(a, s);
    tv_feed(b, s);
    tv_draw(a, st, false);
    tv_draw(b, st, true);
    CHECK(same_image(&a->im, &b->im));
}

static void test_incremental_draws_match_whole(void)
{
    const double scales[] = {1, 2};
    for (size_t i = 0; i < SK_LEN(scales); i++) {
        tv a, b;
        tv_open(&a, 20, 8, scales[i]);
        tv_open(&b, 20, 8, scales[i]);
        sk_term_set_focus(a.t, true);
        sk_term_set_focus(b.t, true);
        sk_termview_state st = tv_focused();
        step(&a, &b, "plain \x1b[31mred\x1b[0m \x1b[3;42mitalic\x1b[0m", &st);
        step(&a, &b, "\r\n\x1b[4:2mdouble under\x1b[0m\r\nnext row", &st);
        /* The row under the double underline alone changes. */
        step(&a, &b, "\x1b[3;1Hchanged", &st);
        /* The double underline goes; its second line went into row 3. */
        step(&a, &b, "\x1b[2;1H\x1b[2K", &st);
        step(&a, &b, "\x1b[5;1H\x1b[7mgjpqy_\x1b[0m\x1b[6;1H\x1b[1 q", &st);
        st.cursor_on = false;
        step(&a, &b, "", &st);
        st.focused = false;
        step(&a, &b, "", &st);
        st.focused = true;
        sk_term_select_start(a.t, SK_SELECT_CHAR, 2, 0);
        sk_term_select_move(a.t, 5, 4);
        sk_term_select_start(b.t, SK_SELECT_CHAR, 2, 0);
        sk_term_select_move(b.t, 5, 4);
        step(&a, &b, "", &st);
        st.focused = false;
        step(&a, &b, "", &st);
        sk_term_select_clear(a.t);
        sk_term_select_clear(b.t);
        step(&a, &b, "", &st);
        st.link_row0 = 0;
        st.link_col0 = 3;
        st.link_row1 = 1;
        st.link_col1 = 4;
        step(&a, &b, "", &st);
        st.link_row0 = -1;
        step(&a, &b, "\x1b[8;1H\n\n\nscrolled \x1b[9mout\x1b[0m", &st);
        step(&a, &b, "\x1b[?25l", &st);
        step(&a, &b, "\x1b[?25h\x1b[6 q", &st);
        tv_close(&a);
        tv_close(&b);
    }
}

/* Only what changed is drawn: a row and the neighbours its sprites may
 * reach. */
static void test_dirty_rows(void)
{
    tv x;
    tv_open(&x, 10, 8, 1);
    sk_term_set_focus(x.t, true);
    sk_termview_state st = tv_focused();
    tv_feed(&x, "\x1b[5;3H");
    sk_rect r = tv_draw(&x, &st, false);
    CHECK(r.x == 0 && r.y == 0 && r.w == 80 && r.h == 128);
    r = tv_draw(&x, &st, false);
    CHECK(sk_rect_empty(r));
    tv_feed(&x, "ab");
    r = tv_draw(&x, &st, false);
    CHECK(r.x == 0 && r.y == 3 * 16 && r.w == 80 && r.h == 3 * 16);
    /* all redraws every row. */
    r = tv_draw(&x, &st, true);
    CHECK(r.y == 0 && r.h == 128);
    /* A blink phase off hides a blinking cursor: its rows alone. */
    tv_feed(&x, "\x1b[1 q");
    tv_draw(&x, &st, false);
    CHECK_INT(tv_rgb(&x, 4 * 8 + 3, 4 * 16 + 3), 0xFFFFFF);
    st.cursor_on = false;
    r = tv_draw(&x, &st, false);
    CHECK(r.y == 3 * 16 && r.h == 3 * 16);
    CHECK_INT(tv_rgb(&x, 4 * 8 + 3, 4 * 16 + 3), 0x000000);
    /* A steady cursor shows in either phase. */
    tv_feed(&x, "\x1b[2 q");
    tv_draw(&x, &st, false);
    CHECK_INT(tv_rgb(&x, 4 * 8 + 3, 4 * 16 + 3), 0xFFFFFF);
    /* A new size or style draws everything. */
    sk_termview_set_style(x.v, &x.style);
    r = tv_draw(&x, &st, false);
    CHECK(r.y == 0 && r.h == 128);
    tv_close(&x);
}

/* A cell's background: palette, 256 colours, RGB, inverse. */
static void test_backgrounds(void)
{
    tv x;
    tv_open(&x, 10, 2, 1);
    sk_termview_state st = tv_focused();
    tv_feed(&x, "\x1b[41m \x1b[48;5;196m \x1b[48;2;1;2;3m \x1b[0;7m \x1b[1;7;31m \x1b[0;7;32;45m \x1b[0m");
    tv_draw(&x, &st, true);
    CHECK_INT(tv_rgb(&x, 4, 4), 0xCC0000);
    CHECK_INT(tv_rgb(&x, 12, 4), 0xFF0000);
    CHECK_INT(tv_rgb(&x, 20, 4), 0x010203);
    /* Inverse: the foreground's colour, never made bright. */
    CHECK_INT(tv_rgb(&x, 28, 4), 0xFFFFFF);
    CHECK_INT(tv_rgb(&x, 36, 4), 0xCC0000);
    CHECK_INT(tv_rgb(&x, 44, 4), 0x4E9A06);
    CHECK_INT(tv_rgb(&x, 52, 4), 0x000000);
    tv_close(&x);
}

/* A full block takes its cell in the foreground: bold makes palette 0
 * to 7 bright, dim draws at half opacity, invisible draws nothing. */
static void test_foregrounds(void)
{
    tv x;
    tv_open(&x, 12, 2, 1);
    sk_termview_state st = tv_focused();
    tv_feed(&x, "\xe2\x96\x88\x1b[31m\xe2\x96\x88\x1b[1m\xe2\x96\x88\x1b[22;38;5;1;1m\xe2\x96\x88"
                "\x1b[0;38;5;9;1m\xe2\x96\x88\x1b[0;2m\xe2\x96\x88\x1b[0;8m\xe2\x96\x88"
                "\x1b[0;7;34;43m\xe2\x96\x88\x1b[0;38;2;10;20;30;1m\xe2\x96\x88\x1b[0m");
    tv_draw(&x, &st, true);
    CHECK_INT(tv_rgb(&x, 4, 8), 0xFFFFFF);
    CHECK_INT(tv_rgb(&x, 12, 8), 0xCC0000);
    CHECK_INT(tv_rgb(&x, 20, 8), 0xEF2929);
    /* 38;5;1 is still below 8. */
    CHECK_INT(tv_rgb(&x, 28, 8), 0xEF2929);
    CHECK_INT(tv_rgb(&x, 36, 8), 0xEF2929);
    CHECK_INT(tv_rgb(&x, 44, 8), 0x808080);
    CHECK_INT(tv_rgb(&x, 52, 8), 0x000000);
    /* Inverse swaps them: the block in the background's yellow. */
    CHECK_INT(tv_rgb(&x, 60, 8), 0xC4A000);
    CHECK_INT(tv_rgb(&x, 68, 8), 0x0A141E);
    tv_close(&x);
}

/* Underline 1 device pixel at both scales on the cell's last row;
 * double's second line 2 rows lower, in the row below; overline on the
 * first row; strikethrough at half the character's height. */
static void test_lines(void)
{
    tv x;
    tv_open(&x, 8, 3, 1);
    sk_termview_state st = tv_focused();
    tv_feed(&x, "\x1b[4m \x1b[4:2m \x1b[0;53m \x1b[0;9m \x1b[0m");
    tv_draw(&x, &st, true);
    for (int y = 0; y < 16; y++) {
        CHECK_INT(tv_count_not(&x, sk_rect_make(0, y, 8, 1), 0), y == 15 ? 8 : 0);
        CHECK_INT(tv_count_not(&x, sk_rect_make(16, y, 8, 1), 0), y == 0 ? 8 : 0);
        CHECK_INT(tv_count_not(&x, sk_rect_make(24, y, 8, 1), 0), y == 7 ? 8 : 0);
    }
    CHECK_INT(tv_count_not(&x, sk_rect_make(8, 15, 8, 1), 0), 8);
    CHECK_INT(tv_count_not(&x, sk_rect_make(8, 16, 8, 1), 0), 0);
    CHECK_INT(tv_count_not(&x, sk_rect_make(8, 17, 8, 1), 0), 8);
    tv_close(&x);

    tv_open(&x, 8, 3, 2);
    tv_feed(&x, "\x1b[4m \x1b[0;9m \x1b[9m\xe2\x94\x80\x1b[0m");
    tv_draw(&x, &st, true);
    CHECK_INT(tv_count_not(&x, sk_rect_make(0, 0, 16, 31), 0), 0);
    CHECK_INT(tv_count_not(&x, sk_rect_make(0, 31, 16, 1), 0xFFFFFF), 0);
    /* Strikethrough 2 pixels about 15.5: half, whole, half. */
    CHECK_INT(tv_rgb(&x, 20, 14), 0x808080);
    CHECK_INT(tv_rgb(&x, 20, 15), 0xFFFFFF);
    CHECK_INT(tv_rgb(&x, 20, 16), 0x808080);
    CHECK_INT(tv_count_not(&x, sk_rect_make(16, 17, 16, 15), 0), 0);
    /* After box drawing's 2 pixel line the offset is 0: rows 15 and 16. */
    CHECK_INT(tv_rgb(&x, 36, 15), 0xFFFFFF);
    CHECK_INT(tv_rgb(&x, 36, 16), 0xFFFFFF);
    CHECK_INT(tv_rgb(&x, 36, 17), 0x808080);
    tv_close(&x);
}

/* SGR 58 colours an underline, a palette colour made bright when bold;
 * without it the underline is in the text's colour, dim included. SGR
 * 59 sets every colour bit, which xterm.js reads as RGB white. */
static void test_underline_colors(void)
{
    tv x;
    tv_open(&x, 8, 2, 1);
    sk_termview_state st = tv_focused();
    tv_feed(&x, "\x1b[4;58;2;0;0;255m \x1b[0;4;58;5;2m \x1b[0;4;31;1m \x1b[0;4;2m \x1b[0;4;31;2m "
                "\x1b[0;31;4;58:5:2;59m \x1b[0;1;4;58;5;2m \x1b[0m");
    tv_draw(&x, &st, true);
    CHECK_INT(tv_rgb(&x, 3, 15), 0x0000FF);
    CHECK_INT(tv_rgb(&x, 11, 15), 0x4E9A06);
    CHECK_INT(tv_rgb(&x, 19, 15), 0xEF2929);
    /* Default: in the foreground, dim included. */
    CHECK_INT(tv_rgb(&x, 27, 15), 0x808080);
    /* SGR 4 always sets the extended word, so its default colour is the
     * dim red foreground. */
    CHECK_INT(tv_rgb(&x, 35, 15), 0x660000);
    CHECK_INT(tv_rgb(&x, 43, 15), 0xFFFFFF);
    CHECK_INT(tv_rgb(&x, 51, 15), 0x8AE234);
    tv_close(&x);
}

/* The cursor: a block in the cursor colour with its glyph in the accent,
 * an outline when not focused, a bar and an underline one device pixel a
 * scale. */
static void test_cursor(void)
{
    const double scales[] = {1, 2};
    for (size_t i = 0; i < SK_LEN(scales); i++) {
        int s = (int)scales[i];
        tv x;
        tv_open(&x, 6, 2, scales[i]);
        sk_term_set_focus(x.t, true);
        x.style.cursor_accent = SK_RGB(0, 0, 255);
        sk_termview_set_style(x.v, &x.style);
        sk_termview_state st = tv_focused();
        tv_feed(&x, "\xe2\x96\x88\x1b[D");
        tv_draw(&x, &st, true);
        /* The block: the full block in the accent over the cursor. */
        CHECK_INT(tv_count_not(&x, sk_rect_make(0, 0, x.cw, x.ch), 0x0000FF), 0);
        tv_feed(&x, " \x1b[D");
        tv_draw(&x, &st, true);
        CHECK_INT(tv_count_not(&x, sk_rect_make(0, 0, x.cw, x.ch), 0xFFFFFF), 0);
        st.focused = false;
        tv_draw(&x, &st, true);
        int border = 0;
        for (int y = 0; y < x.ch; y++)
            for (int px = 0; px < x.cw; px++) {
                bool edge = px < s || px >= x.cw - s || y < s || y >= x.ch - s;
                border += tv_rgb(&x, px, y) == (edge ? 0xFFFFFFu : 0u);
            }
        CHECK_INT(border, x.cw * x.ch);
        st.focused = true;
        tv_feed(&x, "\x1b[6 q");
        tv_draw(&x, &st, true);
        CHECK_INT(tv_count_not(&x, sk_rect_make(0, 0, s, x.ch), 0xFFFFFF), 0);
        CHECK_INT(tv_count_not(&x, sk_rect_make(s, 0, x.cw - s, x.ch), 0), 0);
        tv_feed(&x, "\x1b[4 q");
        tv_draw(&x, &st, true);
        CHECK_INT(tv_count_not(&x, sk_rect_make(0, x.ch - s, x.cw, s), 0xFFFFFF), 0);
        CHECK_INT(tv_count_not(&x, sk_rect_make(0, 0, x.cw, x.ch - s), 0), 0);
        /* Hidden. */
        tv_feed(&x, "\x1b[?25l");
        tv_draw(&x, &st, true);
        CHECK_INT(tv_count_not(&x, sk_rect_make(0, 0, x.cw, x.ch), 0), 0);
        tv_close(&x);
    }
}

/* A wide character's cursor covers both its cells. */
static void test_cursor_on_wide(void)
{
    tv x;
    tv_open(&x, 6, 2, 1);
    sk_term_set_focus(x.t, true);
    sk_termview_state st = tv_focused();
    tv_feed(&x, "\xe2\x9a\xa1\x1b[1;1H");
    st.focused = false;
    tv_draw(&x, &st, true);
    CHECK_INT(tv_rgb(&x, 15, 8), 0xFFFFFF);
    CHECK_INT(tv_rgb(&x, 7, 0), 0xFFFFFF);
    CHECK_INT(tv_rgb(&x, 12, 8), 0x000000);
    tv_close(&x);
}

/* The selection: its colour over the background, focused or not; over
 * a coloured cell, blended half with it. */
static void test_selection(void)
{
    tv x;
    tv_open(&x, 8, 3, 1);
    x.style.selection_inactive_bg = SK_RGB(0x20, 0x40, 0x60);
    sk_termview_set_style(x.v, &x.style);
    sk_termview_state st = tv_focused();
    tv_feed(&x, "ab\x1b[41m  \x1b[0m");
    sk_term_select_start(x.t, SK_SELECT_CHAR, 1, 0);
    sk_term_select_move(x.t, 3, 0);
    tv_draw(&x, &st, false);
    CHECK_INT(tv_rgb(&x, 4, 1), 0x000000);
    /* rgba(255, 255, 255, 0.3) over black. */
    CHECK_INT(tv_rgb(&x, 12, 1), 0x4D4D4D);
    CHECK_INT(tv_rgb(&x, 20, 1), 0x8C2727);
    CHECK_INT(tv_rgb(&x, 28, 1), 0xCC0000);
    st.focused = false;
    sk_rect r = tv_draw(&x, &st, false);
    CHECK(!sk_rect_empty(r));
    CHECK_INT(tv_rgb(&x, 12, 1), 0x204060);
    tv_close(&x);
}

/* The hovered link's underline: dpr high, a pixel above the bottom. */
static void test_link_underline(void)
{
    const double scales[] = {1, 2};
    for (size_t i = 0; i < SK_LEN(scales); i++) {
        int s = (int)scales[i];
        tv x;
        tv_open(&x, 10, 3, scales[i]);
        tv_feed(&x, "\x1b]8;;https://example.com\x1b\\link\x1b]8;;\x1b\\ text");
        sk_termview_state st = tv_focused();
        tv_draw(&x, &st, true);
        sk_term_cell cells[10];
        sk_term_row(x.t, 0, cells);
        CHECK(cells[0].link != 0 && cells[3].link == cells[0].link && cells[4].link == 0);
        int y = x.ch - s - 1;
        CHECK_INT(tv_count_not(&x, sk_rect_make(5 * x.cw, y, 4 * x.cw, s), 0), 0);
        st.link_row0 = 0;
        st.link_col0 = 5;
        st.link_row1 = 0;
        st.link_col1 = 9;
        sk_rect r = tv_draw(&x, &st, false);
        CHECK(r.y == 0 && r.h >= x.ch);
        CHECK_INT(tv_count_not(&x, sk_rect_make(5 * x.cw, y, 4 * x.cw, s), 0xFFFFFF), 0);
        CHECK_INT(tv_count_not(&x, sk_rect_make(9 * x.cw, y, x.cw, s), 0), 0);
        st.link_row0 = -1;
        st.link = cells[0].link;
        tv_draw(&x, &st, false);
        CHECK_INT(tv_count_not(&x, sk_rect_make(5 * x.cw, y, 4 * x.cw, s), 0), 0);
        CHECK_INT(tv_count_not(&x, sk_rect_make(0, y, 4 * x.cw, s), 0xFFFFFF), 0);
        tv_close(&x);
    }
}

/* The atlas draws a cell once and keeps it. */
static void test_atlas(void)
{
    tv x;
    tv_open(&x, 4, 2, 1);
    sk_ui_atlas *a = sk_ui_atlas_new();
    sk_ui_atlas_config cfg = {x.fonts, x.cache, 14, 1, 8, 16, 8, 16, {{0}, 0, 0, 0}};
    sk_term_default_palette(&cfg.pal);
    sk_ui_atlas_configure(a, &cfg);
    uint32_t cp = 'A', space = ' ', comb[2] = {'e', 0x301};
    const sk_ui_sprite *s = sk_ui_atlas_get(a, &cp, 1, 1, 0, 0, 0);
    CHECK(s && s->w > 0 && s->h > 0);
    CHECK(sk_ui_atlas_get(a, &cp, 1, 1, 0, 0, 0) == s);
    CHECK(sk_ui_atlas_get(a, &cp, 1, 1, SK_ATTR_CM_P16 | 1, 0, 0) != s);
    CHECK(sk_ui_atlas_get(a, &space, 1, 1, 0, 0, 0) == NULL);
    CHECK(sk_ui_atlas_get(a, &cp, 1, 1, SK_FG_INVISIBLE, 0, 0) == NULL);
    CHECK(sk_ui_atlas_get(a, &space, 1, 1, SK_FG_UNDERLINE, 0, 0) != NULL);
    const sk_ui_sprite *e = sk_ui_atlas_get(a, comb, 2, 1, 0, 0, 0);
    uint32_t plain = 'e';
    const sk_ui_sprite *ep = sk_ui_atlas_get(a, &plain, 1, 1, 0, 0, 0);
    /* The accent over the e reaches higher. */
    CHECK(e && ep && e->y < ep->y);
    CHECK_INT(sk_ui_atlas_count(a), 7);
    sk_ui_atlas_configure(a, &cfg);
    CHECK_INT(sk_ui_atlas_count(a), 0);
    sk_ui_atlas_free(a);
    tv_close(&x);
}

const sk_test sk_suite_termview[] = {
    T(test_cell_size),
    T(test_fit),
    T(test_hit),
    T(test_incremental_draws_match_whole),
    T(test_dirty_rows),
    T(test_backgrounds),
    T(test_foregrounds),
    T(test_lines),
    T(test_underline_colors),
    T(test_cursor),
    T(test_cursor_on_wide),
    T(test_selection),
    T(test_link_underline),
    T(test_atlas),
    {0},
};
