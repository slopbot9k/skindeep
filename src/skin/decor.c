/* A box's own paint, as CSS Backgrounds 3 and Images 3 describe it:
 * outer box shadows, the background colour and image layers, inset
 * shadows, the border or the border image, the outline.
 *
 * Boxes are snapped to whole pixels as Chromium snaps them, so a 1px
 * border at 2x is two whole pixels; rounded corners are anti-aliased
 * paths. Gradients interpolate premultiplied, as Blink asks Skia to,
 * through a table of at least four entries a pixel of their length.
 * A shadow's blur radius is twice its sigma. */
#include "pinternal.h"

#include <string.h>

/* ---- Images ---- */

void pt_image(painter *P, canvas *cv, uint32_t source, sk_rect dst, const uint8_t *cov, sk_rect cov_r,
              bool pixelated, sk_blend blend, uint32_t opacity)
{
    const sk_pimages *imgs = P->env->images;
    if (!imgs || !imgs->pixels || sk_rect_empty(dst) || !opacity)
        return;
    sk_rect lim = sk_rect_intersect(cv->clip, cov_r);
    sk_rect vis = sk_rect_intersect(sk_rect_intersect(dst, lim), sk_image_bounds(cv->im));
    if (sk_rect_empty(vis))
        return;
    const sk_image *px = imgs->pixels(imgs->data, source, dst.w, dst.h);
    if (!px || px->w <= 0 || px->h <= 0)
        return;
    sk_filter filter = pixelated ? SK_FILTER_NEAREST : SK_FILTER_BILINEAR;
    if (!cov && !cv->mask) {
        sk_blit_scaled(cv->im, dst, px, sk_image_bounds(px), filter, blend, (uint8_t)opacity, &lim);
        return;
    }
    sk_image tmp;
    sk_image_init(&tmp, vis.w, vis.h);
    sk_rect at = sk_rect_make(dst.x - vis.x, dst.y - vis.y, dst.w, dst.h);
    sk_blit_scaled(&tmp, at, px, sk_image_bounds(px), filter, SK_BLEND_SRC, 255, NULL);
    const uint8_t *c = cov ? cov + (size_t)(vis.y - cov_r.y) * (size_t)cov_r.w + (size_t)(vis.x - cov_r.x) : NULL;
    pt_fill_image(cv, vis.x, vis.y, &tmp, c, cov_r.w, blend, opacity);
    sk_image_free(&tmp);
}

/* ---- Gradients ---- */

typedef struct {
    sk_xform inv;          /* canvas pixels to CSS px */
    float ox, oy, tw, th;  /* the tile, CSS px */
    bool rx, ry;           /* repeats across, down */
    bool radial;
    float sx, sy, dx, dy;  /* linear: t = ((u - sx) dx + (v - sy) dy) */
    float cx, cy, irx, iry; /* radial: t = |((u - cx) irx, (v - cy) iry)| */
    sk_px *lut;
    int nlut;
    float t0, t1;
    bool repeating;
    sk_px solid; /* a gradient of no length */
    bool flat;
} grad;

/* The stops' places along the gradient, of length len px: autos
 * shared out, none before the one before it (CSS Images 3, 3.4.3). */
static void stop_places(const sk_css_gradient *g, float len, float *t)
{
    int n = g->nstops;
    for (int k = 0; k < n; k++) {
        float v = sk_layout_len(&g->stops[k].pos, len);
        t[k] = isnan(v) || g->stops[k].pos.kind == SK_LEN_AUTO ? NAN : v / len;
    }
    if (isnan(t[0]))
        t[0] = 0;
    if (isnan(t[n - 1]))
        t[n - 1] = 1;
    float hi = t[0];
    for (int k = 1; k < n; k++)
        if (!isnan(t[k])) {
            if (t[k] < hi)
                t[k] = hi;
            hi = t[k];
        }
    for (int k = 1; k < n;) {
        if (!isnan(t[k])) {
            k++;
            continue;
        }
        int e = k;
        while (isnan(t[e]))
            e++;
        float a = t[k - 1], b = t[e];
        for (int q = k; q < e; q++)
            t[q] = a + (b - a) * (float)(q - k + 1) / (float)(e - k + 1);
        k = e;
    }
}

static void premul_f(sk_css_color c, float out[4])
{
    float a = fminf(fmaxf(c.a, 0), 1);
    out[0] = fminf(fmaxf(c.r, 0), 1) * a;
    out[1] = fminf(fmaxf(c.g, 0), 1) * a;
    out[2] = fminf(fmaxf(c.b, 0), 1) * a;
    out[3] = a;
}

static sk_px pack_f(const float c[4])
{
    uint32_t a = (uint32_t)lroundf(fminf(fmaxf(c[3], 0), 1) * 255);
    uint32_t r = SK_MIN((uint32_t)lroundf(fminf(fmaxf(c[0], 0), 1) * 255), a);
    uint32_t g = SK_MIN((uint32_t)lroundf(fminf(fmaxf(c[1], 0), 1) * 255), a);
    uint32_t b = SK_MIN((uint32_t)lroundf(fminf(fmaxf(c[2], 0), 1) * 255), a);
    return a << 24 | r << 16 | g << 8 | b;
}

/* The table of colours from t0 to t1, dev_len pixels to a t of 1. */
static void build_lut(grad *gr, const sk_css_gradient *g, float len, float dev_len)
{
    int n = g->nstops;
    if (n <= 0) {
        gr->flat = true;
        gr->solid = 0;
        return;
    }
    float *t = sk_xmalloc((size_t)n * sizeof *t);
    stop_places(g, len, t);
    gr->t0 = t[0];
    gr->t1 = t[n - 1];
    gr->repeating = g->repeating;
    if (!(gr->t1 > gr->t0)) {
        /* No length: the last colour, or a repeating one's average. */
        float c[4];
        premul_f(g->stops[n - 1].color, c);
        gr->flat = true;
        gr->solid = pack_f(c);
        free(t);
        return;
    }
    float span = (gr->t1 - gr->t0) * dev_len * 4;
    int nl = span > 16384 ? 16384 : span < 256 ? 256 : (int)ceilf(span);
    gr->nlut = nl;
    gr->lut = sk_xmalloc((size_t)nl * sizeof *gr->lut);
    int k = 0;
    for (int i = 0; i < nl; i++) {
        float tt = gr->t0 + (gr->t1 - gr->t0) * (float)i / (float)(nl - 1);
        while (k + 1 < n - 1 && t[k + 1] <= tt)
            k++;
        float a[4], b[4], c[4];
        premul_f(g->stops[k].color, a);
        premul_f(g->stops[k + 1 < n ? k + 1 : k].color, b);
        float d = (k + 1 < n ? t[k + 1] : t[k]) - t[k];
        float f = d > 0 ? (tt - t[k]) / d : 1;
        f = fminf(fmaxf(f, 0), 1);
        if (tt >= t[n - 1])
            f = 1, premul_f(g->stops[n - 1].color, b);
        for (int q = 0; q < 4; q++)
            c[q] = a[q] + (b[q] - a[q]) * f;
        gr->lut[i] = pack_f(c);
    }
    free(t);
}

static sk_px lut_at(const grad *g, float t)
{
    if (g->flat)
        return g->solid;
    float span = g->t1 - g->t0;
    if (g->repeating) {
        t = fmodf(t - g->t0, span);
        if (t < 0)
            t += span;
        t += g->t0;
    }
    float f = (t - g->t0) / span * (float)(g->nlut - 1);
    if (!(f > 0))
        return g->lut[0];
    if (f >= (float)(g->nlut - 1))
        return g->lut[g->nlut - 1];
    return g->lut[(int)(f + 0.5f)];
}

static void grad_row(const pt_shader *s, int x, int y, int n, sk_px *out)
{
    const grad *g = s->data;
    float py = (float)y + 0.5f;
    for (int i = 0; i < n; i++) {
        float px = (float)(x + i) + 0.5f;
        float u = g->inv.a * px + g->inv.c * py + g->inv.e - g->ox;
        float v = g->inv.b * px + g->inv.d * py + g->inv.f - g->oy;
        if (g->rx) {
            u = fmodf(u, g->tw);
            if (u < 0)
                u += g->tw;
        } else if (u < 0 || u >= g->tw) {
            out[i] = 0;
            continue;
        }
        if (g->ry) {
            v = fmodf(v, g->th);
            if (v < 0)
                v += g->th;
        } else if (v < 0 || v >= g->th) {
            out[i] = 0;
            continue;
        }
        float t;
        if (g->radial) {
            float a = (u - g->cx) * g->irx, b = (v - g->cy) * g->iry;
            t = sqrtf(a * a + b * b);
        } else {
            t = (u - g->sx) * g->dx + (v - g->sy) * g->dy;
        }
        out[i] = lut_at(g, t);
    }
}

/* A gradient's geometry in a tile w by h, k pixels to the px. */
static void grad_setup(grad *gr, const sk_css_gradient *g, float w, float h, float k)
{
    if (g->kind == SK_GRAD_RADIAL) {
        gr->radial = true;
        float cx = sk_layout_len(&g->cx, w), cy = sk_layout_len(&g->cy, h);
        if (isnan(cx))
            cx = w / 2;
        if (isnan(cy))
            cy = h / 2;
        float l = fabsf(cx), r = fabsf(w - cx), t = fabsf(cy), b = fabsf(h - cy);
        float rx, ry;
        switch (g->extent) {
        case SK_EXTENT_CLOSEST_SIDE:
            rx = fminf(l, r);
            ry = fminf(t, b);
            if (g->circle)
                rx = ry = fminf(rx, ry);
            break;
        case SK_EXTENT_FARTHEST_SIDE:
            rx = fmaxf(l, r);
            ry = fmaxf(t, b);
            if (g->circle)
                rx = ry = fmaxf(rx, ry);
            break;
        case SK_EXTENT_CLOSEST_CORNER:
            rx = fminf(l, r);
            ry = fminf(t, b);
            if (g->circle) {
                rx = ry = sqrtf(rx * rx + ry * ry);
            } else {
                rx *= (float)M_SQRT2;
                ry *= (float)M_SQRT2;
            }
            break;
        case SK_EXTENT_EXPLICIT:
            rx = sk_layout_len(&g->rx, w);
            ry = g->circle ? rx : sk_layout_len(&g->ry, h);
            if (isnan(rx))
                rx = 0;
            if (isnan(ry))
                ry = 0;
            break;
        default:
            rx = fmaxf(l, r);
            ry = fmaxf(t, b);
            if (g->circle) {
                rx = ry = sqrtf(rx * rx + ry * ry);
            } else {
                rx *= (float)M_SQRT2;
                ry *= (float)M_SQRT2;
            }
            break;
        }
        rx = fmaxf(rx, 1e-3f);
        ry = fmaxf(ry, 1e-3f);
        gr->cx = cx;
        gr->cy = cy;
        gr->irx = 1 / rx;
        gr->iry = 1 / ry;
        build_lut(gr, g, rx, rx * k);
        return;
    }
    float dx, dy;
    if (g->to_corner & 4) {
        dx = (g->to_corner & 1 ? 1 : -1) * h;
        dy = (g->to_corner & 2 ? 1 : -1) * w;
        float n = sqrtf(dx * dx + dy * dy);
        if (n > 0) {
            dx /= n;
            dy /= n;
        } else {
            dx = 0;
            dy = 1;
        }
    } else {
        double a = g->angle * M_PI / 180;
        dx = (float)sin(a);
        dy = (float)-cos(a);
    }
    float L = fabsf(w * dx) + fabsf(h * dy);
    if (!(L > 1e-4f))
        L = 1e-4f;
    gr->sx = w / 2 - dx * L / 2;
    gr->sy = h / 2 - dy * L / 2;
    gr->dx = dx / L;
    gr->dy = dy / L;
    build_lut(gr, g, L, L * k);
}

/* ---- Backgrounds ---- */

static uint8_t box_of(const uint8_t *list, uint16_t n, uint16_t i, uint8_t def)
{
    return n ? list[i % n] : def;
}

static sk_frect css_box(const sk_lbox *b, uint8_t which)
{
    return which == SK_BOX_BORDER ? b->border : which == SK_BOX_PADDING ? b->padding : b->content;
}

/* Paints one background layer over the clip box clip_r (pixels), its
 * coverage cov when the box is rounded. */
static void bg_layer(painter *P, canvas *cv, uint32_t i, uint16_t li, const sk_xform *m, sk_rect clip_r,
                     const uint8_t *cov, sk_blend blend)
{
    const sk_css_style *s = P->nodes[i].style;
    const sk_lbox *b = &P->l->boxes[i];
    const sk_css_image *img = &s->bg_image[li];
    if (img->kind == SK_IMAGE_NONE)
        return;
    sk_frect area = css_box(b, box_of(s->bg_origin, s->nbg_origin, li, SK_BOX_PADDING));
    float iw = 0, ih = 0;
    bool has_size = false;
    if (img->kind == SK_IMAGE_URL) {
        const sk_pimages *imgs = P->env->images;
        if (!imgs || !imgs->size || !imgs->size(imgs->data, img->source, &iw, &ih) || !(iw > 0) || !(ih > 0))
            return;
        has_size = true;
    } else if (!img->gradient) {
        return;
    }
    /* The tile's size. */
    float tw, th;
    const sk_css_bgsize *sz = s->nbg_size ? &s->bg_size[li % s->nbg_size] : NULL;
    if (sz && (sz->kind == SK_BGSIZE_COVER || sz->kind == SK_BGSIZE_CONTAIN)) {
        if (has_size) {
            float f = sz->kind == SK_BGSIZE_COVER ? fmaxf(area.w / iw, area.h / ih) : fminf(area.w / iw, area.h / ih);
            tw = iw * f;
            th = ih * f;
        } else {
            tw = area.w;
            th = area.h;
        }
    } else {
        float w = sz ? sk_layout_len(&sz->w, area.w) : NAN, h = sz ? sk_layout_len(&sz->h, area.h) : NAN;
        if (sz && sz->w.kind == SK_LEN_VALUE)
            w = P->nodes[i].value * area.w / 100;
        if (sz && sz->h.kind == SK_LEN_VALUE)
            h = P->nodes[i].value * area.h / 100;
        if (isnan(w) && isnan(h)) {
            w = has_size ? iw : area.w;
            h = has_size ? ih : area.h;
        } else if (isnan(w)) {
            w = has_size ? h * iw / ih : area.w;
        } else if (isnan(h)) {
            h = has_size ? w * ih / iw : area.h;
        }
        tw = w;
        th = h;
    }
    if (!(tw > 0) || !(th > 0))
        return;
    /* Where the first tile sits. */
    const sk_css_position *pos = s->nbg_position ? &s->bg_position[li % s->nbg_position] : NULL;
    float px = 0, py = 0;
    if (pos) {
        px = pos->x.kind == SK_LEN_VALUE ? P->nodes[i].value * (area.w - tw) / 100 : sk_layout_len(&pos->x, area.w - tw);
        py = pos->y.kind == SK_LEN_VALUE ? P->nodes[i].value * (area.h - th) / 100 : sk_layout_len(&pos->y, area.h - th);
        if (isnan(px))
            px = 0;
        if (isnan(py))
            py = 0;
        if (pos->x_edge == SK_EDGE_END)
            px = area.w - tw - px;
        if (pos->y_edge == SK_EDGE_END)
            py = area.h - th - py;
    }
    float ox = area.x + px, oy = area.y + py;
    uint8_t rep_x = s->nbg_repeat ? s->bg_repeat[2 * (li % s->nbg_repeat)] : SK_REPEAT;
    uint8_t rep_y = s->nbg_repeat ? s->bg_repeat[2 * (li % s->nbg_repeat) + 1] : SK_REPEAT;
    bool rx = rep_x != SK_NO_REPEAT, ry = rep_y != SK_NO_REPEAT;
    sk_rect vis = sk_rect_intersect(clip_r, cv->clip);
    if (sk_rect_empty(vis))
        return;
    if (img->kind == SK_IMAGE_GRADIENT) {
        grad g;
        memset(&g, 0, sizeof g);
        if (!sk_xform_invert(m, &g.inv))
            return;
        g.ox = ox;
        g.oy = oy;
        g.tw = tw;
        g.th = th;
        g.rx = rx;
        g.ry = ry;
        grad_setup(&g, img->gradient, tw, th, fabsf(m->a));
        pt_shader sh = {0, grad_row, &g};
        pt_fill(cv, clip_r, cov, clip_r.w, &sh, blend, 255);
        free(g.lut);
        return;
    }
    /* Image tiles, each snapped to whole pixels so they meet. */
    sk_xform inv;
    if (!sk_xform_invert(m, &inv))
        return;
    sk_frect cvis = sk_frect_map(&inv, (sk_frect){(float)vis.x, (float)vis.y, (float)vis.w, (float)vis.h});
    long k0 = 0, k1 = 1, j0 = 0, j1 = 1;
    if (rx) {
        k0 = (long)floorf((cvis.x - ox) / tw);
        k1 = (long)ceilf((cvis.x + cvis.w - ox) / tw);
    }
    if (ry) {
        j0 = (long)floorf((cvis.y - oy) / th);
        j1 = (long)ceilf((cvis.y + cvis.h - oy) / th);
    }
    if ((k1 - k0) * (j1 - j0) > 200000)
        return;
    bool pixelated = s->image_rendering == SK_RENDER_PIXELATED;
    for (long j = j0; j < j1; j++)
        for (long k = k0; k < k1; k++) {
            float x0 = m->a * (ox + (float)k * tw) + m->e, x1 = m->a * (ox + (float)(k + 1) * tw) + m->e;
            float y0 = m->d * (oy + (float)j * th) + m->f, y1 = m->d * (oy + (float)(j + 1) * th) + m->f;
            sk_rect d = sk_rect_make((int)lroundf(x0), (int)lroundf(y0), (int)lroundf(x1) - (int)lroundf(x0),
                                     (int)lroundf(y1) - (int)lroundf(y0));
            if (sk_rect_empty(sk_rect_intersect(d, vis)))
                continue;
            pt_image(P, cv, img->source, d, cov, clip_r, pixelated, blend, 255);
        }
}

static void paint_background(painter *P, canvas *cv, uint32_t i, const sk_xform *m)
{
    const sk_css_style *s = P->nodes[i].style;
    uint16_t n = s->nbg_image;
    /* The colour, under the bottom layer's clip. */
    uint8_t cbox = box_of(s->bg_clip, s->nbg_clip, n ? (uint16_t)(n - 1) : 0, SK_BOX_BORDER);
    sk_px color = pt_color(s->bg_color, 1);
    if (color) {
        pt_rrect rr = pt_box_rrect(P, i, m, cbox);
        sk_rect r = sk_rect_intersect(pt_round_out((sk_frect){rr.x, rr.y, rr.w, rr.h}), cv->clip);
        if (!sk_rect_empty(r)) {
            uint8_t *cov = pt_rrect_square(&rr) ? NULL : pt_cover_rrect(&rr, r);
            pt_shader sh = {color, NULL, NULL};
            pt_fill(cv, r, cov, r.w, &sh, SK_BLEND_SRC_OVER, 255);
            free(cov);
        }
    }
    for (int li = (int)n - 1; li >= 0; li--) {
        uint8_t clip = box_of(s->bg_clip, s->nbg_clip, (uint16_t)li, SK_BOX_BORDER);
        pt_rrect rr = pt_box_rrect(P, i, m, clip);
        sk_rect r = pt_round_out((sk_frect){rr.x, rr.y, rr.w, rr.h});
        sk_rect vis = sk_rect_intersect(r, cv->clip);
        if (sk_rect_empty(vis))
            continue;
        uint8_t *cov = pt_rrect_square(&rr) ? NULL : pt_cover_rrect(&rr, vis);
        sk_blend blend = pt_mix(box_of(s->bg_blend, s->nbg_blend, (uint16_t)li, SK_MIX_NORMAL));
        bg_layer(P, cv, i, (uint16_t)li, m, vis, cov, blend);
        free(cov);
    }
}

void pt_background(painter *P, canvas *cv, uint32_t i)
{
    const sk_css_style *s = P->nodes[i].style;
    bool any = s->bg_color.a > 0;
    bool blends = false;
    for (uint16_t k = 0; k < s->nbg_image; k++) {
        any = any || s->bg_image[k].kind != SK_IMAGE_NONE;
        blends = blends || box_of(s->bg_blend, s->nbg_blend, k, SK_MIX_NORMAL) != SK_MIX_NORMAL;
    }
    if (!any)
        return;
    sk_xform m = pt_map(P, cv, i);
    if (!pt_square_map(&m))
        return;
    if (!blends) {
        paint_background(P, cv, i, &m);
        return;
    }
    /* Layers that blend do so with the layers below them alone: an
     * isolated group, composited normally. */
    sk_rect at = sk_rect_intersect(pt_round_out(sk_frect_map(&m, P->l->boxes[i].border)), cv->clip);
    if (sk_rect_empty(at))
        return;
    sk_image im;
    sk_image_init(&im, at.w, at.h);
    sk_xform t = {1, 0, 0, 1, (float)-at.x, (float)-at.y};
    canvas lc = {&im, sk_xform_mul(&t, &cv->base), sk_image_bounds(&im), NULL};
    sk_xform lm = pt_map(P, &lc, i);
    paint_background(P, &lc, i, &lm);
    pt_fill_image(cv, at.x, at.y, &im, NULL, 0, SK_BLEND_SRC_OVER, 255);
    sk_image_free(&im);
}

/* ---- Shadows ---- */

static sk_rect grow(sk_rect r, int d)
{
    return sk_rect_make(r.x - d, r.y - d, r.w + 2 * d, r.h + 2 * d);
}

void pt_shadows(painter *P, canvas *cv, uint32_t i, bool inset)
{
    const sk_css_style *s = P->nodes[i].style;
    if (!s->nbox_shadow)
        return;
    sk_xform m = pt_map(P, cv, i);
    if (!pt_square_map(&m))
        return;
    float kx = fabsf(m.a), ky = fabsf(m.d);
    pt_rrect outer = pt_box_rrect(P, i, &m, SK_BOX_BORDER);
    pt_rrect pad = pt_box_rrect(P, i, &m, SK_BOX_PADDING);
    for (int k = (int)s->nbox_shadow - 1; k >= 0; k--) {
        const sk_css_shadow *sh = &s->box_shadow[k];
        if (sh->inset != inset)
            continue;
        sk_px c = pt_color(sh->color, 1);
        if (!c)
            continue;
        float sigma = fmaxf(0, sh->blur) / 2 * kx;
        int reach = sk_blur_reach(sigma);
        if (!inset) {
            pt_rrect S = pt_rrect_spread(&outer, sh->spread * kx, sh->spread * ky);
            S.x += sh->x * kx;
            S.y += sh->y * ky;
            if (!(S.w > 0) || !(S.h > 0))
                continue;
            sk_rect e = grow(pt_round_out((sk_frect){S.x, S.y, S.w, S.h}), reach + 1);
            /* What can show: the clip, and as much around it as blurs in. */
            e = sk_rect_intersect(e, grow(cv->clip, reach + 1));
            if (sk_rect_empty(sk_rect_intersect(e, cv->clip)))
                continue;
            uint8_t *cov = pt_cover_rrect(&S, e);
            sk_blur_a8(cov, e.w, e.h, e.w, sigma);
            uint8_t *hole = pt_cover_rrect(&outer, e);
            for (size_t q = 0; q < (size_t)e.w * (size_t)e.h; q++)
                cov[q] = (uint8_t)pt_mul(cov[q], 255u - hole[q]);
            pt_shader shd = {c, NULL, NULL};
            pt_fill(cv, e, cov, e.w, &shd, SK_BLEND_SRC_OVER, 255);
            free(cov);
            free(hole);
        } else {
            if (!(pad.w > 0) || !(pad.h > 0))
                continue;
            sk_rect a = sk_rect_intersect(pt_round_out((sk_frect){pad.x, pad.y, pad.w, pad.h}), cv->clip);
            if (sk_rect_empty(a))
                continue;
            pt_rrect I = pt_rrect_spread(&pad, -sh->spread * kx, -sh->spread * ky);
            I.x += sh->x * kx;
            I.y += sh->y * ky;
            sk_rect e = grow(a, reach + 1);
            uint8_t *cov = (I.w > 0 && I.h > 0) ? pt_cover_rrect(&I, e) : sk_xcalloc((size_t)e.w, (size_t)e.h);
            for (size_t q = 0; q < (size_t)e.w * (size_t)e.h; q++)
                cov[q] = (uint8_t)(255 - cov[q]);
            sk_blur_a8(cov, e.w, e.h, e.w, sigma);
            uint8_t *inside = pt_cover_rrect(&pad, e);
            for (size_t q = 0; q < (size_t)e.w * (size_t)e.h; q++)
                cov[q] = (uint8_t)pt_mul(cov[q], inside[q]);
            pt_shader shd = {c, NULL, NULL};
            pt_fill(cv, e, cov, e.w, &shd, SK_BLEND_SRC_OVER, 255);
            free(cov);
            free(inside);
        }
    }
}

/* ---- Borders ---- */

/* The ring between two rounded rectangles over r. */
static uint8_t *ring(const pt_rrect *outer, const pt_rrect *inner, sk_rect r)
{
    sk_path *p = sk_path_new();
    pt_rrect_path(p, outer);
    if (inner->w > 0 && inner->h > 0)
        pt_rrect_path(p, inner);
    uint8_t *m = pt_cover(p, NULL, r, SK_FILL_EVENODD);
    sk_path_free(p);
    return m;
}

static void fill_ring(canvas *cv, const pt_rrect *outer, const pt_rrect *inner, sk_px c)
{
    sk_rect r = sk_rect_intersect(pt_round_out((sk_frect){outer->x, outer->y, outer->w, outer->h}), cv->clip);
    if (sk_rect_empty(r) || !c)
        return;
    uint8_t *cov = ring(outer, inner, r);
    pt_shader sh = {c, NULL, NULL};
    pt_fill(cv, r, cov, r.w, &sh, SK_BLEND_SRC_OVER, 255);
    free(cov);
}

/* The nine slices of a border image. */
static void border_image(painter *P, canvas *cv, uint32_t i, const sk_xform *m)
{
    const sk_css_style *s = P->nodes[i].style;
    const sk_lbox *b = &P->l->boxes[i];
    const sk_pimages *imgs = P->env->images;
    float iw, ih;
    if (!imgs || !imgs->size || !imgs->pixels || s->bi_source.kind != SK_IMAGE_URL ||
        !imgs->size(imgs->data, s->bi_source.source, &iw, &ih) || !(iw > 0) || !(ih > 0))
        return;
    float kx = fabsf(m->a), ky = fabsf(m->d);
    const sk_image *px = imgs->pixels(imgs->data, s->bi_source.source, (int)lroundf(iw * kx), (int)lroundf(ih * ky));
    if (!px || px->w <= 0 || px->h <= 0)
        return;
    float fx = (float)px->w / iw, fy = (float)px->h / ih;
    /* Slices in the image's pixels. */
    float sl[4];
    for (int k = 0; k < 4; k++) {
        float dim = (k == SK_TOP || k == SK_BOTTOM) ? ih : iw;
        float v = s->bi_slice[k];
        if (s->bi_slice_pct & (1 << k))
            v = v / 100 * dim;
        sl[k] = fminf(fmaxf(v, 0), dim) * ((k == SK_TOP || k == SK_BOTTOM) ? fy : fx);
    }
    /* The area, the border box grown by the outsets, and the widths. */
    sk_frect area = b->border;
    area.x -= s->bi_outset[SK_LEFT];
    area.y -= s->bi_outset[SK_TOP];
    area.w += s->bi_outset[SK_LEFT] + s->bi_outset[SK_RIGHT];
    area.h += s->bi_outset[SK_TOP] + s->bi_outset[SK_BOTTOM];
    float w[4];
    for (int k = 0; k < 4; k++) {
        const sk_css_len *l = &s->bi_width[k];
        bool vert = k == SK_TOP || k == SK_BOTTOM;
        if (s->bi_width_num & (1 << k))
            w[k] = l->px * b->bw[k];
        else if (l->kind == SK_LEN_AUTO)
            w[k] = sl[k] / (vert ? fy : fx);
        else
            w[k] = sk_layout_len(l, vert ? area.h : area.w);
        if (isnan(w[k]) || w[k] < 0)
            w[k] = 0;
    }
    float f = 1;
    if (w[SK_LEFT] + w[SK_RIGHT] > area.w)
        f = fminf(f, area.w / (w[SK_LEFT] + w[SK_RIGHT]));
    if (w[SK_TOP] + w[SK_BOTTOM] > area.h)
        f = fminf(f, area.h / (w[SK_TOP] + w[SK_BOTTOM]));
    sk_frect d = pt_snap(m, area);
    float dl = roundf(w[SK_LEFT] * f * kx), dr = roundf(w[SK_RIGHT] * f * kx);
    float dt = roundf(w[SK_TOP] * f * ky), db = roundf(w[SK_BOTTOM] * f * ky);
    float sx[4] = {0, sl[SK_LEFT], (float)px->w - sl[SK_RIGHT], (float)px->w};
    float sy[4] = {0, sl[SK_TOP], (float)px->h - sl[SK_BOTTOM], (float)px->h};
    float dx[4] = {d.x, d.x + dl, d.x + d.w - dr, d.x + d.w};
    float dy[4] = {d.y, d.y + dt, d.y + d.h - db, d.y + d.h};
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            if (r == 1 && c == 1 && !s->bi_fill)
                continue;
            sk_rect src = sk_rect_make((int)lroundf(sx[c]), (int)lroundf(sy[r]),
                                       (int)lroundf(sx[c + 1]) - (int)lroundf(sx[c]),
                                       (int)lroundf(sy[r + 1]) - (int)lroundf(sy[r]));
            sk_rect dst = sk_rect_make((int)dx[c], (int)dy[r], (int)dx[c + 1] - (int)dx[c], (int)dy[r + 1] - (int)dy[r]);
            if (sk_rect_empty(src) || sk_rect_empty(dst))
                continue;
            sk_blit_scaled(cv->im, dst, px, src, SK_FILTER_BILINEAR, SK_BLEND_SRC_OVER, 255, &cv->clip);
        }
}

void pt_border(painter *P, canvas *cv, uint32_t i)
{
    const sk_lbox *b = &P->l->boxes[i];
    const sk_css_style *s = P->nodes[i].style;
    if (!(b->bw[0] > 0 || b->bw[1] > 0 || b->bw[2] > 0 || b->bw[3] > 0))
        return;
    sk_xform m = pt_map(P, cv, i);
    if (!pt_square_map(&m))
        return;
    if (s->bi_source.kind != SK_IMAGE_NONE) {
        border_image(P, cv, i, &m);
        return;
    }
    pt_rrect outer = pt_box_rrect(P, i, &m, SK_BOX_BORDER);
    pt_rrect inner = pt_box_rrect(P, i, &m, SK_BOX_PADDING);
    sk_px c[4];
    bool same = true;
    int first = -1;
    for (int k = 0; k < 4; k++) {
        c[k] = b->bw[k] > 0 ? pt_color(s->border_color[k], 1) : 0;
        if (b->bw[k] > 0) {
            if (first < 0)
                first = k;
            else if (c[k] != c[first])
                same = false;
        }
    }
    if (same) {
        fill_ring(cv, &outer, &inner, c[first]);
        return;
    }
    /* Sides of their own colours meet on the corners' diagonals. */
    float ox0 = outer.x, oy0 = outer.y, ox1 = outer.x + outer.w, oy1 = outer.y + outer.h;
    float ix0 = inner.x, iy0 = inner.y, ix1 = inner.x + inner.w, iy1 = inner.y + inner.h;
    const float poly[4][8] = {
        {ox0, oy0, ox1, oy0, ix1, iy0, ix0, iy0},
        {ox1, oy0, ox1, oy1, ix1, iy1, ix1, iy0},
        {ox1, oy1, ox0, oy1, ix0, iy1, ix1, iy1},
        {ox0, oy1, ox0, oy0, ix0, iy0, ix0, iy1},
    };
    for (int k = 0; k < 4; k++) {
        if (!c[k])
            continue;
        float x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
        sk_path *p = sk_path_new();
        for (int q = 0; q < 4; q++) {
            float x = poly[k][2 * q], y = poly[k][2 * q + 1];
            x0 = fminf(x0, x);
            y0 = fminf(y0, y);
            x1 = fmaxf(x1, x);
            y1 = fmaxf(y1, y);
            if (q == 0)
                sk_path_move(p, x, y);
            else
                sk_path_line(p, x, y);
        }
        sk_path_close(p);
        sk_rect r = sk_rect_intersect(pt_round_out((sk_frect){x0, y0, x1 - x0, y1 - y0}), cv->clip);
        if (!sk_rect_empty(r)) {
            uint8_t *side = pt_cover(p, NULL, r, SK_FILL_NONZERO);
            uint8_t *rg = ring(&outer, &inner, r);
            for (size_t q = 0; q < (size_t)r.w * (size_t)r.h; q++)
                side[q] = (uint8_t)pt_mul(side[q], rg[q]);
            pt_shader sh = {c[k], NULL, NULL};
            pt_fill(cv, r, side, r.w, &sh, SK_BLEND_SRC_OVER, 255);
            free(side);
            free(rg);
        }
        sk_path_free(p);
    }
}

void pt_outline(painter *P, canvas *cv, uint32_t i)
{
    const sk_css_style *s = P->nodes[i].style;
    if (s->outline_style != SK_BORDER_SOLID || !(s->outline_width > 0) || s->visibility != SK_VIS_VISIBLE)
        return;
    sk_xform m = pt_map(P, cv, i);
    if (!pt_square_map(&m))
        return;
    sk_frect bb = P->l->boxes[i].border;
    float o = s->outline_offset, w = s->outline_width;
    sk_frect in = {bb.x - o, bb.y - o, bb.w + 2 * o, bb.h + 2 * o};
    sk_frect out = {in.x - w, in.y - w, in.w + 2 * w, in.h + 2 * w};
    sk_frect di = pt_snap(&m, in), dout = pt_snap(&m, out);
    pt_rrect ri = {di.x, di.y, di.w, di.h, {0, 0, 0, 0}, {0, 0, 0, 0}};
    pt_rrect ro = {dout.x, dout.y, dout.w, dout.h, {0, 0, 0, 0}, {0, 0, 0, 0}};
    fill_ring(cv, &ro, &ri, pt_color(s->outline_color, 1));
}
