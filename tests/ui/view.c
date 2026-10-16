#include "view.h"

void tv_open(tv *x, int cols, int rows, double scale)
{
    memset(x, 0, sizeof *x);
    static const char *const families[] = {"DejaVu Sans Mono", "monospace", NULL};
    x->fonts = sk_fontset_new(NULL, families, true);
    x->cache = sk_glyph_cache_new((size_t)8 << 20);
    sk_termview_default_style(&x->style);
    x->style.fonts = x->fonts;
    x->style.cache = x->cache;
    x->style.scale = scale;
    x->v = sk_termview_new(&x->style);
    x->t = sk_term_new(cols, rows, NULL);
    x->cols = cols;
    x->rows = rows;
    sk_termview_cell(x->v, &x->cw, &x->ch);
    sk_image_init(&x->im, cols * x->cw, rows * x->ch);
}

void tv_close(tv *x)
{
    sk_image_free(&x->im);
    sk_termview_free(x->v);
    sk_term_free(x->t);
    sk_glyph_cache_free(x->cache);
    sk_fontset_free(x->fonts);
}

void tv_feed(tv *x, const char *s)
{
    sk_term_feed(x->t, s, strlen(s));
}

sk_rect tv_draw(tv *x, const sk_termview_state *st, bool all)
{
    sk_term_lock(x->t);
    sk_rect r = sk_termview_draw(x->v, x->t, &x->im, st, all);
    sk_term_unlock(x->t);
    return r;
}

sk_termview_state tv_focused(void)
{
    sk_termview_state st = {true, true, 0, -1, 0, -1, 0};
    return st;
}

uint32_t tv_rgb(const tv *x, int px, int py)
{
    return sk_unpremul(sk_image_row(&x->im, py)[px]) & 0xFFFFFF;
}

int tv_count_not(const tv *x, sk_rect r, uint32_t rgb)
{
    int n = 0;
    for (int y = r.y; y < r.y + r.h; y++)
        for (int i = r.x; i < r.x + r.w; i++)
            n += tv_rgb(x, i, y) != rgb;
    return n;
}
