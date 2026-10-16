#include "shot.h"

#include "../skinc/splits.h"
#include "imgdecode.h"

#include <math.h>
#include <string.h>

static const sk_image *decoded(sk_shot *s, uint32_t i)
{
    if (i >= s->tree.images.len)
        return NULL;
    if (!s->tried[i]) {
        s->tried[i] = 1;
        const char *p = s->tree.images.ptr[i];
        if (p && strncmp(p, "data:", 5) != 0 && sk_tool_decode(p, &s->decoded[i]))
            s->tried[i] = 2;
    }
    return s->tried[i] == 2 ? &s->decoded[i] : NULL;
}

static bool image_size(void *data, uint32_t i, float *w, float *h)
{
    const sk_image *im = decoded(data, i);
    if (!im)
        return false;
    *w = (float)im->w;
    *h = (float)im->h;
    return true;
}

static const sk_image *image_pixels(void *data, uint32_t i, int w, int h)
{
    (void)w;
    (void)h;
    return decoded(data, i);
}

bool sk_shot_load(sk_shot *s, const char *dir, sk_diags *d)
{
    memset(s, 0, sizeof *s);
    if (!sk_front_run(&s->front, dir, d)) {
        sk_front_free(&s->front);
        return false;
    }
    const sk_skin *k = &s->front.skin;
    s->width = k->width;
    s->height = k->height - SK_STRIP;
    /* Every variable 0 but a list's nodata: a list with no items is
     * hidden, as the app hides it. */
    SK_VEC(sk_assign) as = {0};
    for (size_t i = 0; i < s->front.els.len; i++) {
        const sk_el *e = s->front.els.ptr[i];
        if (e->role == SK_ROLE_WIDGET && e->widget >= 0 && k->widgets[e->widget].w.type == SK_WIDGET_LIST) {
            sk_assign a = {e, SK_VAR_NODATA, 1};
            sk_vec_push(&as, a);
        }
    }
    sk_render_state st = {as.ptr, as.len, false, NULL};
    sk_render_tree_build(&s->tree, &s->front, &st, s->width, s->height);
    sk_vec_free(&as);
    sk_render_fonts_open(&s->fonts, &s->front);
    s->lfonts.face = sk_render_face;
    s->lfonts.data = &s->fonts;
    sk_layout_run(&s->layout, s->tree.nodes, s->tree.n, s->tree.root, s->width, s->height, &s->lfonts);
    size_t ni = s->tree.images.len;
    s->decoded = sk_xcalloc(ni ? ni : 1, sizeof *s->decoded);
    s->tried = sk_xcalloc(ni ? ni : 1, 1);
    s->images.size = image_size;
    s->images.pixels = image_pixels;
    s->images.data = s;
    s->glyphs = sk_glyph_cache_new(16u << 20);
    /* The terminal, at its splits' default sizes. */
    double at[64];
    sk_split_sizes(k, NULL, s->width, s->height, at);
    sk_pack_rect t = sk_split_terminal(k, at);
    s->terminal = (sk_frect){t.v[0], t.v[3], s->width - t.v[0] - t.v[1], s->height - t.v[3] - t.v[4]};
    for (int i = 0; sk_theme_keys[i]; i++)
        if (!strcmp(sk_theme_keys[i], "background") && (k->has_theme & (1u << i)))
            s->terminal_bg = k->theme[i];
    return true;
}

void sk_shot_paint(sk_shot *s, float scale, bool terminal, sk_image *out)
{
    int w = (int)ceilf(s->width * scale), h = (int)ceilf(s->height * scale);
    sk_image_init(out, w, h);
    sk_fill_rect(out, sk_image_bounds(out), SK_RGB(0, 0, 0), SK_BLEND_SRC, NULL);
    sk_paint_env env = {&s->layout, &s->lfonts, &s->images, s->glyphs};
    sk_paint(out, &env, s->tree.root, scale, 0, 0);
    if (terminal && s->terminal_bg) {
        sk_frect r = s->terminal;
        sk_rect d = sk_rect_make((int)lroundf(r.x * scale), (int)lroundf(r.y * scale), (int)lroundf(r.w * scale),
                                 (int)lroundf(r.h * scale));
        sk_fill_rect(out, d, s->terminal_bg | 0xff000000u, SK_BLEND_SRC, NULL);
    }
}

void sk_shot_free(sk_shot *s)
{
    for (size_t i = 0; i < s->tree.images.len; i++)
        sk_image_free(&s->decoded[i]);
    free(s->decoded);
    free(s->tried);
    sk_glyph_cache_free(s->glyphs);
    sk_layout_free(&s->layout);
    sk_render_fonts_close(&s->fonts);
    sk_render_tree_free(&s->tree);
    sk_front_free(&s->front);
    memset(s, 0, sizeof *s);
}
