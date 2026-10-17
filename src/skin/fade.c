/* A group that fades above what it covers (viewint.h's sk_vfade).
 *
 * The group qualifies when its animations change its opacity and
 * nothing else of it, it paints as a group of opacity alone (no filter,
 * clip-path, blend or turn), no ancestor up to the stage makes a group,
 * and nothing paints after it where it paints. The stage painted with
 * it hidden is then what lies below it, and its own paint at opacity 1
 * is what the painter's group would composite: the frame there is the
 * one blended at its opacity over the other, with the painter's own
 * arithmetic. What else changes under it is painted into below and
 * blended again, so the two stay what a whole paint would give. */
#include "pinternal.h"
#include "viewint.h"

#include <string.h>

#define NONE SK_PACK_NONE

static bool plain(const sk_css_style *s)
{
    return s->opacity >= 1 && !s->nfilter && !s->nclip && s->mix_blend == SK_MIX_NORMAL && !s->isolate &&
           !s->ntransform;
}

static bool within(const sk_skinview *v, uint32_t n, uint32_t root)
{
    for (; n != NONE && n < v->nnodes; n = v->lnodes[n].parent)
        if (n == root)
            return true;
    return false;
}

static bool qualifies(const sk_skinview *v, uint32_t n, uint32_t at)
{
    const sk_lbox *b = &v->layout.boxes[n];
    const sk_css_style *s = v->lnodes[n].style;
    if (!b->generated || b->svg_content || !s || !sk_motion_fades(v->nodes[n].mo))
        return false;
    if (s->nfilter || s->nclip || s->mix_blend != SK_MIX_NORMAL || s->isolate || s->ntransform)
        return false;
    if (!(b->xform.a > 0) || !(b->xform.d > 0) || b->xform.b != 0 || b->xform.c != 0)
        return false;
    if (b->clipper != NONE && b->clipper != v->stage)
        return false;
    for (uint32_t a = v->lnodes[n].parent; a != NONE && a < v->nnodes; a = v->lnodes[a].parent) {
        if (!v->lnodes[a].style || !plain(v->lnodes[a].style) || v->lnodes[a].kind == SK_EL_SVG)
            return false;
        if (a == v->stage)
            break;
    }
    /* Nothing after it in painting order paints where it does. */
    const sk_frect ink = v->nodes[n].ink;
    for (uint32_t k = at + 1; k < v->norder; k++) {
        uint32_t m = v->order[k];
        if (within(v, m, n))
            continue;
        if (!sk_frect_empty(sk_frect_intersect(ink, v->nodes[m].ink)))
            return false;
    }
    return true;
}

static void drop(sk_vfade *f)
{
    sk_image_free(&f->below);
    sk_image_free(&f->layer);
    f->valid = f->recompose = false;
}

void sk_fade_free(sk_vfade *f)
{
    drop(f);
}

static sk_rect cover_of(const sk_skinview *v, uint32_t n)
{
    sk_frect ink = v->nodes[n].ink;
    double f = v->k * v->scale;
    /* Clamped before they become integers: a hostile pack's ink may
     * reach past any. */
    long x0 = (long)fmax(-1e9, fmin(1e9, floor(ink.x * f))), y0 = (long)fmax(-1e9, fmin(1e9, floor(ink.y * f)));
    long x1 = (long)fmax(-1e9, fmin(1e9, ceil((ink.x + ink.w) * f)));
    long y1 = (long)fmax(-1e9, fmin(1e9, ceil((ink.y + ink.h) * f)));
    /* The stage's pixels, as view.c counts them. */
    long w = lround((v->sx + v->sw) * v->scale) - lround(v->sx * v->scale);
    long h = lround((v->sy + v->sh) * v->scale) - lround(v->sy * v->scale);
    x0 = SK_MAX(x0, 0);
    y0 = SK_MAX(y0, 0);
    x1 = SK_MIN(x1, w);
    y1 = SK_MIN(y1, h);
    if (x1 <= x0 || y1 <= y0)
        return sk_rect_make(0, 0, 0, 0);
    return sk_rect_make((int)x0, (int)y0, (int)(x1 - x0), (int)(y1 - y0));
}

void sk_fade_pick(sk_skinview *v)
{
    sk_vfade *f = &v->fade;
    uint32_t pick = NONE;
    for (uint32_t k = 0; k < v->norder && pick == NONE; k++)
        if (qualifies(v, v->order[k], k))
            pick = v->order[k];
    sk_rect r = pick != NONE ? cover_of(v, pick) : sk_rect_make(0, 0, 0, 0);
    if (pick != f->node || memcmp(&r, &f->r, sizeof r) != 0) {
        drop(f);
        f->node = sk_rect_empty(r) ? NONE : pick;
        f->r = r;
    }
}

static uint32_t alpha_of(const sk_css_style *s)
{
    return (uint32_t)lroundf(fminf(fmaxf(s->opacity, 0), 1) * 255);
}

bool sk_fade_absorbs(sk_skinview *v, uint32_t n, bool was_drawn, sk_frect was_ink, bool drawn, sk_frect ink)
{
    sk_vfade *f = &v->fade;
    if (n != f->node || !f->valid)
        return false;
    bool same = drawn && was_drawn && !memcmp(&ink, &was_ink, sizeof ink) && sk_view_signature(v, n, false) == f->sig;
    if (!same) {
        drop(f);
        return false;
    }
    uint32_t a = alpha_of(v->lnodes[n].style);
    if (a != f->alpha) {
        f->alpha = a;
        f->recompose = true;
    }
    return true;
}

static sk_paint_env env_of(sk_skinview *v)
{
    return sk_view_env(v);
}

bool sk_fade_prepare(sk_skinview *v)
{
    sk_vfade *f = &v->fade;
    if (f->node == NONE || f->valid || !v->laid)
        return false;
    drop(f);
    sk_image_init(&f->below, f->r.w, f->r.h);
    sk_image_init(&f->layer, f->r.w, f->r.h);
    /* The group alone, as its group layer holds it before its opacity. */
    sk_lnode *l = &v->lnodes[f->node];
    const sk_css_style *was = l->style;
    sk_css_style one = *was;
    one.opacity = 1;
    l->style = &one;
    sk_paint_env env = env_of(v);
    sk_paint_px(&f->layer, &env, f->node, (float)(v->k * v->scale), f->r.x, f->r.y);
    l->style = was;
    f->alpha = alpha_of(was);
    f->sig = sk_view_signature(v, f->node, false);
    f->valid = true;
    f->recompose = false;
    return true;
}

/* below over i (stage pixels inside the cover): the stage with the
 * group hidden. */
static void paint_below(sk_skinview *v, sk_rect i)
{
    sk_vfade *f = &v->fade;
    sk_image view = sk_image_view(&f->below, sk_rect_make(i.x - f->r.x, i.y - f->r.y, i.w, i.h));
    sk_fill_rect(&view, sk_image_bounds(&view), SK_RGB(0, 0, 0), SK_BLEND_SRC, NULL);
    sk_lnode *l = &v->lnodes[f->node];
    const sk_css_style *was = l->style;
    sk_css_style none = *was;
    none.opacity = 0;
    l->style = &none;
    sk_paint_env env = env_of(v);
    sk_paint_px(&view, &env, v->stage, (float)(v->k * v->scale), i.x, i.y);
    l->style = was;
}

/* The frame over i (stage pixels inside the cover): below, the group
 * over it at its opacity, as the painter composites a group. */
static void blend(sk_skinview *v, sk_image *frame, sk_rect st, sk_rect i)
{
    sk_vfade *f = &v->fade;
    uint32_t a = f->alpha;
    for (int y = 0; y < i.h; y++) {
        const sk_px *b = sk_image_row(&f->below, i.y - f->r.y + y) + (i.x - f->r.x);
        const sk_px *g = sk_image_row(&f->layer, i.y - f->r.y + y) + (i.x - f->r.x);
        sk_px *d = sk_image_row(frame, st.y + i.y + y) + st.x + i.x;
        for (int x = 0; x < i.w; x++) {
            sk_px p = g[x], out = b[x];
            if (a == 255) {
                if (p)
                    out = pt_over(p, out);
            } else if (a && p) {
                p = pt_scale(p, a);
                if (p)
                    out = pt_over(p, out);
            }
            d[x] = out;
        }
    }
}

/* The parts of r outside c: above, below, left, right of it. */
static int outside(sk_rect r, sk_rect c, sk_rect out[4])
{
    sk_rect t = sk_rect_intersect(r, c);
    if (sk_rect_empty(t)) {
        out[0] = r;
        return 1;
    }
    out[0] = sk_rect_make(r.x, r.y, r.w, t.y - r.y);
    out[1] = sk_rect_make(r.x, t.y + t.h, r.w, r.y + r.h - t.y - t.h);
    out[2] = sk_rect_make(r.x, t.y, t.x - r.x, t.h);
    out[3] = sk_rect_make(t.x + t.w, t.y, r.x + r.w - t.x - t.w, t.h);
    return 4;
}

void sk_fade_paint(sk_skinview *v, sk_image *frame, sk_rect st, sk_rect r)
{
    sk_vfade *f = &v->fade;
    sk_rect cover = sk_rect_make(f->r.x + st.x, f->r.y + st.y, f->r.w, f->r.h);
    if (f->node == NONE || !f->valid || sk_rect_empty(sk_rect_intersect(r, cover))) {
        sk_view_paint_skin(v, frame, st, r);
        return;
    }
    sk_rect parts[4];
    int n = outside(r, cover, parts);
    for (int k = 0; k < n; k++)
        if (!sk_rect_empty(parts[k]))
            sk_view_paint_skin(v, frame, st, parts[k]);
    sk_rect in = sk_rect_intersect(r, cover);
    in.x -= st.x;
    in.y -= st.y;
    paint_below(v, in);
    blend(v, frame, st, in);
}

size_t sk_fade_recompose(sk_skinview *v, sk_image *frame, sk_rect st, sk_rect term, sk_rect *out, size_t cap)
{
    sk_vfade *f = &v->fade;
    if (f->node == NONE || !f->valid || !f->recompose)
        return 0;
    f->recompose = false;
    sk_rect cover = sk_rect_intersect(sk_rect_make(f->r.x + st.x, f->r.y + st.y, f->r.w, f->r.h),
                                      sk_image_bounds(frame));
    sk_rect parts[4];
    int n = outside(cover, term, parts);
    size_t made = 0;
    for (int k = 0; k < n; k++) {
        if (sk_rect_empty(parts[k]))
            continue;
        sk_rect in = parts[k];
        in.x -= st.x;
        in.y -= st.y;
        blend(v, frame, st, in);
        if (made < cap)
            out[made++] = parts[k];
    }
    return made;
}
