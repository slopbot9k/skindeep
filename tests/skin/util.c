/* The skin suites' throwaway skins, their boxes and their paint. */
#include "util.h"

#include "../test.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* The skin's own size, less the identity strip. */
#define AREA_W 1000
#define AREA_H 678

static bool image_size(void *data, uint32_t source, float *w, float *h)
{
    const sk_tskin *s = data;
    (void)source;
    *w = (float)s->image.w;
    *h = (float)s->image.h;
    return s->image.w > 0 && s->image.h > 0;
}

static const sk_image *image_pixels(void *data, uint32_t source, int w, int h)
{
    sk_tskin *s = data;
    (void)source;
    (void)w;
    (void)h;
    if (s->pixels.w != s->image.w || s->pixels.h != s->image.h) {
        sk_image_free(&s->pixels);
        sk_image_init(&s->pixels, s->image.w, s->image.h);
        for (int y = 0; y < s->image.h; y++)
            for (int x = 0; x < s->image.w; x++) {
                bool odd = s->image.cell && ((x / s->image.cell + y / s->image.cell) & 1);
                sk_image_row(&s->pixels, y)[x] = sk_premul(odd ? s->image.b : s->image.a);
            }
    }
    return &s->pixels;
}

void sk_tskin_open(const char *file, int line, sk_tskin *s, const char *panels, const char *widgets, const char *css,
                   const sk_render_state *st)
{
    memset(s, 0, sizeof *s);
    /* a.png for url(a.png): the front end checks that it is there, and
     * the test's own images draw it. */
    const char *dir = sk_test_skin(panels, widgets, css);
    sk_test_write(dir, "a.png", "not decoded");
    s->ok = sk_front_run(&s->f, dir, &s->d);
    if (!s->ok) {
        sk_test_fail(file, line, "the skin did not compile");
        for (size_t i = 0; i < s->d.items.len; i++)
            fprintf(stderr, "      %s\n", s->d.items.ptr[i]);
    }
    s->image = (sk_test_image){8, 8, SK_RGB(255, 0, 0), SK_RGB(0, 0, 255), 0};
    s->images.size = image_size;
    s->images.pixels = image_pixels;
    s->images.data = s;
    if (!s->ok)
        return;
    sk_render_tree_build(&s->t, &s->f, st, AREA_W, AREA_H);
    sk_render_fonts_open(&s->fonts, &s->f);
    s->lfonts.face = sk_render_face;
    s->lfonts.data = &s->fonts;
    sk_layout_run(&s->l, s->t.nodes, s->t.n, s->t.root, AREA_W, AREA_H, &s->lfonts);
    s->glyphs = sk_glyph_cache_new(4u << 20);
}

void sk_tskin_close(sk_tskin *s)
{
    if (s->ok) {
        sk_layout_free(&s->l);
        sk_render_fonts_close(&s->fonts);
        sk_render_tree_free(&s->t);
        sk_glyph_cache_free(s->glyphs);
    }
    sk_image_free(&s->pixels);
    sk_front_free(&s->f);
    sk_diags_free(&s->d);
}

static const sk_lbox *box_of(sk_tskin *s, const sk_el *e, const char *what)
{
    static sk_lbox none;
    if (!s->ok || !e || e->index >= s->l.n || !s->l.boxes[e->index].generated) {
        sk_test_fail(__FILE__, __LINE__, "no box for %s", what);
        return &none;
    }
    return &s->l.boxes[e->index];
}

const sk_lbox *sk_tskin_panel(sk_tskin *s, const char *klass)
{
    return box_of(s, s->ok ? sk_front_panel(&s->f, klass) : NULL, klass);
}

const sk_lbox *sk_tskin_pseudo(sk_tskin *s, const char *klass, bool after)
{
    const sk_el *p = s->ok ? sk_front_panel(&s->f, klass) : NULL;
    return box_of(s, p ? (after ? p->after : p->before) : NULL, after ? "::after" : "::before");
}

const sk_lbox *sk_tskin_part(sk_tskin *s, const char *widget, uint8_t role, int nth)
{
    return box_of(s, s->ok ? sk_front_find(&s->f, widget, role, nth) : NULL, widget);
}

const sk_css_style *sk_tskin_style(const sk_tskin *s, const sk_lbox *b)
{
    return s->ok && b->node < s->t.n ? s->t.nodes[b->node].style : NULL;
}

void sk_tskin_paint(sk_tskin *s, sk_frect r, float scale, sk_image *out)
{
    int w = (int)ceilf(r.w * scale), h = (int)ceilf(r.h * scale);
    sk_image_init(out, w, h);
    if (!s->ok)
        return;
    sk_paint_env env = {&s->l, &s->lfonts, &s->images, s->glyphs, NULL};
    sk_paint(out, &env, s->t.root, scale, r.x, r.y);
}

sk_color sk_tskin_at(const sk_image *im, int x, int y)
{
    if (x < 0 || y < 0 || x >= im->w || y >= im->h)
        return 0;
    return sk_unpremul(sk_image_row(im, y)[x]);
}

void sk_check_rect(const char *file, int line, const char *what, sk_frect r, float x, float y, float w, float h)
{
    if (fabsf(r.x - x) > 0.01f || fabsf(r.y - y) > 0.01f || fabsf(r.w - w) > 0.01f || fabsf(r.h - h) > 0.01f)
        sk_test_fail(file, line, "%s is %.2f,%.2f %.2fx%.2f, not %.2f,%.2f %.2fx%.2f", what, (double)r.x, (double)r.y,
                     (double)r.w, (double)r.h, (double)x, (double)y, (double)w, (double)h);
}

void sk_check_near(const char *file, int line, const char *what, double a, double b, double eps)
{
    if (!(fabs(a - b) <= eps))
        sk_test_fail(file, line, "%s is %.4f, not %.4f", what, a, b);
}

void sk_check_color(const char *file, int line, const char *what, sk_color c, sk_color want, int tol)
{
    for (int sh = 0; sh < 32; sh += 8) {
        int d = abs((int)((c >> sh) & 255) - (int)((want >> sh) & 255));
        if (d > tol) {
            sk_test_fail(file, line, "%s is #%08x, not #%08x", what, (unsigned)c, (unsigned)want);
            return;
        }
    }
}
