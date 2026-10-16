/* A stand-in for the terminal view while xterm.js's renderer is ported
 * (src/ui/termview.h): plain characters in their colours and a block
 * cursor, enough to run the window. It goes when termview.c comes. */
#include "termview.h"

#include "../base/utf8.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

struct sk_termview {
    sk_termview_style s;
    int cw, ch, ascent;
};

void sk_termview_default_style(sk_termview_style *out)
{
    memset(out, 0, sizeof *out);
    out->font_px = 14;
    out->scale = 1;
    out->selection_bg = SK_RGBA(255, 255, 255, 77);
    out->selection_inactive_bg = SK_RGBA(255, 255, 255, 38);
}

static void measure(sk_termview *v)
{
    float px = (float)(v->s.font_px * v->s.scale);
    const sk_font *f = sk_fontset_primary(v->s.fonts, 0);
    sk_font_vmetrics m;
    sk_font_vmetrics_at(f, px, &m);
    v->cw = (int)floorf(sk_font_advance(f, sk_font_glyph(f, 'W'), px));
    v->ch = (int)ceilf(m.ascent + m.descent);
    v->ascent = (int)ceilf(m.ascent);
}

sk_termview *sk_termview_new(const sk_termview_style *s)
{
    sk_termview *v = sk_xcalloc(1, sizeof *v);
    v->s = *s;
    measure(v);
    return v;
}

void sk_termview_free(sk_termview *v)
{
    free(v);
}

void sk_termview_set_style(sk_termview *v, const sk_termview_style *s)
{
    v->s = *s;
    measure(v);
}

void sk_termview_cell(const sk_termview *v, int *w, int *h)
{
    *w = v->cw;
    *h = v->ch;
}

void sk_termview_fit(const sk_termview *v, int w, int h, int *cols, int *rows)
{
    *cols = SK_MAX(2, w / SK_MAX(1, v->cw));
    *rows = SK_MAX(1, h / SK_MAX(1, v->ch));
}

void sk_termview_hit(const sk_termview *v, const sk_term *t, int x, int y, int *col, int *row, int *edge)
{
    int cols, rows;
    sk_term_size((sk_term *)t, &cols, &rows);
    *col = SK_MIN(SK_MAX(x / v->cw, 0), cols - 1);
    *row = SK_MIN(SK_MAX(y / v->ch, 0), rows - 1);
    *edge = SK_MIN(SK_MAX((x + v->cw / 2) / v->cw, 0), cols);
}

static sk_color colour(uint32_t word, const sk_term_palette *p, uint32_t dflt)
{
    switch (word & SK_ATTR_CM_MASK) {
    case SK_ATTR_CM_P16:
    case SK_ATTR_CM_P256:
        return 0xff000000u | p->ansi[word & SK_ATTR_PCOLOR_MASK];
    case SK_ATTR_CM_RGB:
        return 0xff000000u | (word & SK_ATTR_RGB_MASK);
    default:
        return 0xff000000u | dflt;
    }
}

sk_rect sk_termview_draw(sk_termview *v, sk_term *t, sk_image *dst, const sk_termview_state *st, bool all)
{
    int cols, rows;
    sk_term_size(t, &cols, &rows);
    sk_term_palette p;
    sk_term_palette_of(t, &p);
    bool *dirty = sk_xcalloc((size_t)rows, sizeof *dirty);
    bool any = sk_term_take_dirty(t, dirty);
    sk_term_cell *cells = sk_xcalloc((size_t)cols, sizeof *cells);
    sk_term_cursor cur;
    sk_term_cursor_of(t, &cur);
    float px = (float)(v->s.font_px * v->s.scale);
    sk_rect drawn = {0};
    for (int y = 0; y < rows && (all || any); y++) {
        if (!all && !dirty[y])
            continue;
        sk_term_row(t, y, cells);
        sk_rect row = sk_rect_make(0, y * v->ch, cols * v->cw, v->ch);
        sk_fill_rect(dst, row, 0xff000000u | p.bg, SK_BLEND_SRC, NULL);
        for (int x = 0; x < cols; x++) {
            sk_term_cell *c = &cells[x];
            sk_color fg = colour(c->fg, &p, p.fg), bg = colour(c->bg, &p, p.bg);
            if (c->fg & SK_FG_INVERSE) {
                sk_color s = fg;
                fg = bg;
                bg = s;
            }
            bool cursor = st->cursor_on && !cur.hidden && cur.x == x && cur.y == y;
            if (cursor || sk_term_selected(t, x, y)) {
                bg = cursor ? 0xff000000u | p.cursor : SK_RGB(80, 80, 80);
                if (cursor)
                    fg = 0xff000000u | p.bg;
            }
            sk_rect cell = sk_rect_make(x * v->cw, y * v->ch, v->cw * (c->width == 2 ? 2 : 1), v->ch);
            if (bg != (0xff000000u | p.bg) || cursor)
                sk_fill_rect(dst, cell, bg, SK_BLEND_SRC, NULL);
            if (!c->cp || c->fg & SK_FG_INVISIBLE)
                continue;
            char utf8[8];
            size_t n = sk_utf8_encode(c->cp, utf8);
            int style = (c->fg & SK_FG_BOLD ? SK_STYLE_BOLD : 0) | (c->bg & SK_BG_ITALIC ? SK_STYLE_ITALIC : 0);
            sk_draw_text(dst, v->s.cache, v->s.fonts, style, px, (float)cell.x, (float)(cell.y + v->ascent), utf8, n,
                         fg, &cell);
        }
        drawn = sk_rect_union(drawn, row);
    }
    free(cells);
    free(dirty);
    return drawn;
}

bool sk_termview_custom_glyph(sk_image *dst, sk_rect r, uint32_t cp, sk_color c, double scale)
{
    (void)dst, (void)r, (void)cp, (void)c, (void)scale;
    return false;
}
