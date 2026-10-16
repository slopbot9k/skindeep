/* The layout's frame: lengths and box edges, block flow, absolute
 * boxes, and the pass that places every box in the skin area, gives it
 * its transform, the box that clips it and what it may paint. Inline
 * content is inline.c's, flex and grid flex.c's.
 *
 * Two passes, as a browser's. The first lays out the stage's subtree
 * in flow, each box relative to its parent, and notes where each
 * absolute box would have stood (its static position). The second
 * walks the tree in document order: it places each box in the skin
 * area, and on reaching an absolute box sizes it against its
 * containing block, which is an ancestor and so already placed, and
 * lays out its subtree before going on into it. */
#include "internal.h"

#include <string.h>

/* Deeper than this the tree is not laid out further: no skin comes
 * near it, and a pack's tree is checked, but a cycle must not recurse
 * for ever. */
#define MAX_DEPTH 512

/* ---- Geometry ---- */

sk_frect sk_frect_union(sk_frect a, sk_frect b)
{
    if (sk_frect_empty(a))
        return b;
    if (sk_frect_empty(b))
        return a;
    float x0 = fminf(a.x, b.x), y0 = fminf(a.y, b.y);
    float x1 = fmaxf(a.x + a.w, b.x + b.w), y1 = fmaxf(a.y + a.h, b.y + b.h);
    return (sk_frect){x0, y0, x1 - x0, y1 - y0};
}

sk_frect sk_frect_intersect(sk_frect a, sk_frect b)
{
    float x0 = fmaxf(a.x, b.x), y0 = fmaxf(a.y, b.y);
    float x1 = fminf(a.x + a.w, b.x + b.w), y1 = fminf(a.y + a.h, b.y + b.h);
    if (!(x1 > x0) || !(y1 > y0))
        return (sk_frect){x0, y0, 0, 0};
    return (sk_frect){x0, y0, x1 - x0, y1 - y0};
}

sk_frect sk_frect_map(const sk_xform *m, sk_frect r)
{
    if (m->b == 0 && m->c == 0) {
        float x0 = m->a * r.x + m->e, x1 = m->a * (r.x + r.w) + m->e;
        float y0 = m->d * r.y + m->f, y1 = m->d * (r.y + r.h) + m->f;
        return (sk_frect){fminf(x0, x1), fminf(y0, y1), fabsf(x1 - x0), fabsf(y1 - y0)};
    }
    float xs[4] = {r.x, r.x + r.w, r.x, r.x + r.w}, ys[4] = {r.y, r.y, r.y + r.h, r.y + r.h};
    float x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
    for (int i = 0; i < 4; i++) {
        float x = m->a * xs[i] + m->c * ys[i] + m->e, y = m->b * xs[i] + m->d * ys[i] + m->f;
        x0 = fminf(x0, x);
        x1 = fmaxf(x1, x);
        y0 = fminf(y0, y);
        y1 = fmaxf(y1, y);
    }
    return (sk_frect){x0, y0, x1 - x0, y1 - y0};
}

sk_xform sk_xform_mul(const sk_xform *a, const sk_xform *b)
{
    sk_xform r;
    r.a = a->a * b->a + a->c * b->b;
    r.b = a->b * b->a + a->d * b->b;
    r.c = a->a * b->c + a->c * b->d;
    r.d = a->b * b->c + a->d * b->d;
    r.e = a->a * b->e + a->c * b->f + a->e;
    r.f = a->b * b->e + a->d * b->f + a->f;
    return r;
}

bool sk_xform_invert(const sk_xform *m, sk_xform *out)
{
    double det = (double)m->a * m->d - (double)m->b * m->c;
    if (!(fabs(det) > 1e-12) || !isfinite(det))
        return false;
    double ia = m->d / det, ib = -m->b / det, ic = -m->c / det, id = m->a / det;
    out->a = (float)ia;
    out->b = (float)ib;
    out->c = (float)ic;
    out->d = (float)id;
    out->e = (float)(-(ia * m->e + ic * m->f));
    out->f = (float)(-(ib * m->e + id * m->f));
    return true;
}

static sk_xform xf_translate(float x, float y)
{
    return (sk_xform){1, 0, 0, 1, x, y};
}

sk_xform sk_layout_transform(const sk_css_style *s, const sk_css_xf *xf, uint16_t n, sk_frect border)
{
    sk_xform m = SK_XFORM_IDENTITY;
    for (uint16_t i = 0; i < n; i++) {
        const sk_css_xf *f = &xf[i];
        sk_xform t = SK_XFORM_IDENTITY;
        double r;
        switch (f->kind) {
        case SK_XF_TRANSLATE:
            t.e = sk_layout_len(&f->tx, border.w);
            t.f = sk_layout_len(&f->ty, border.h);
            if (isnan(t.e))
                t.e = 0;
            if (isnan(t.f))
                t.f = 0;
            break;
        case SK_XF_ROTATE:
            r = f->v[0] * M_PI / 180;
            t.a = (float)cos(r);
            t.b = (float)sin(r);
            t.c = -t.b;
            t.d = t.a;
            break;
        case SK_XF_SCALE:
            t.a = f->v[0];
            t.d = f->v[1];
            break;
        case SK_XF_SKEW:
            t.c = (float)tan(f->v[0] * M_PI / 180);
            t.b = (float)tan(f->v[1] * M_PI / 180);
            break;
        case SK_XF_MATRIX:
            t = (sk_xform){f->v[0], f->v[1], f->v[2], f->v[3], f->v[4], f->v[5]};
            break;
        }
        m = sk_xform_mul(&m, &t);
    }
    float ox = sk_layout_len(&s->origin[0], border.w), oy = sk_layout_len(&s->origin[1], border.h);
    if (isnan(ox))
        ox = border.w / 2;
    if (isnan(oy))
        oy = border.h / 2;
    ox += border.x;
    oy += border.y;
    sk_xform to = xf_translate(ox, oy), back = xf_translate(-ox, -oy);
    sk_xform r = sk_xform_mul(&to, &m);
    return sk_xform_mul(&r, &back);
}

/* ---- Lengths ---- */

float sk_layout_len(const sk_css_len *l, float base)
{
    if (l->kind != SK_LEN_PX && l->kind != SK_LEN_RECT)
        return NAN;
    if (l->kind == SK_LEN_RECT || l->pct == 0)
        return l->px;
    if (isnan(base))
        return NAN;
    return l->px + l->pct * base / 100;
}

const sk_css_style *lay_style(const lay *L, uint32_t i)
{
    return L->nodes[i].style;
}

float lay_len(const lay *L, uint32_t i, const sk_css_len *v, int key, float base)
{
    const sk_lnode *n = &L->nodes[i];
    switch (v->kind) {
    case SK_LEN_RECT:
        if (key >= 0 && n->rect && (n->rect->has & (1 << key)))
            return n->rect->v[key];
        return v->px;
    case SK_LEN_VALUE:
        return isnan(base) ? NAN : n->value * base / 100;
    case SK_LEN_PX:
        return sk_layout_len(v, base);
    default:
        return NAN;
    }
}

/* A margin: auto is 0 here; flex and absolute boxes look at auto
 * themselves. */
static float margin(const lay *L, uint32_t i, int side, float cbw)
{
    float m = lay_len(L, i, &lay_style(L, i)->margin[side], KEY_NONE, cbw);
    return isnan(m) ? 0 : m;
}

void lay_edges(lay *L, uint32_t i, float cbw)
{
    const sk_css_style *s = lay_style(L, i);
    sk_lbox *b = &L->l->boxes[i];
    for (int k = 0; k < 4; k++) {
        b->margin[k] = margin(L, i, k, cbw);
        float p = lay_len(L, i, &s->padding[k], KEY_NONE, cbw);
        b->pad[k] = isnan(p) || p < 0 ? 0 : p;
        b->bw[k] = s->border_style[k] == SK_BORDER_SOLID && s->border_width[k] > 0 ? s->border_width[k] : 0;
    }
}

float lay_margins_w(const lay *L, uint32_t i)
{
    return margin(L, i, SK_LEFT, NAN) + margin(L, i, SK_RIGHT, NAN);
}

float lay_bp_w(const lay *L, uint32_t i)
{
    const sk_lbox *b = &L->l->boxes[i];
    return b->bw[SK_LEFT] + b->bw[SK_RIGHT] + b->pad[SK_LEFT] + b->pad[SK_RIGHT];
}

float lay_bp_h(const lay *L, uint32_t i)
{
    const sk_lbox *b = &L->l->boxes[i];
    return b->bw[SK_TOP] + b->bw[SK_BOTTOM] + b->pad[SK_TOP] + b->pad[SK_BOTTOM];
}

/* A size property as a border-box size, by box-sizing. */
static float as_border(const lay *L, uint32_t i, float v, float bp)
{
    if (isnan(v))
        return NAN;
    if (v < 0)
        v = 0;
    /* box_sizing: 0 content-box, 1 border-box. */
    return lay_style(L, i)->box_sizing == 1 ? fmaxf(v, bp) : v + bp;
}

float lay_size_w(const lay *L, uint32_t i, const sk_css_len *v, int key, float cbw)
{
    return as_border(L, i, lay_len(L, i, v, key, cbw), lay_bp_w(L, i));
}

float lay_size_h(const lay *L, uint32_t i, const sk_css_len *v, int key, float cbh)
{
    return as_border(L, i, lay_len(L, i, v, key, cbh), lay_bp_h(L, i));
}

float lay_clamp_w(const lay *L, uint32_t i, float w, float cbw)
{
    const sk_css_style *s = lay_style(L, i);
    float max = lay_size_w(L, i, &s->max_width, KEY_NONE, cbw);
    float min = lay_size_w(L, i, &s->min_width, KEY_NONE, cbw);
    if (!isnan(max))
        w = fminf(w, max);
    if (!isnan(min))
        w = fmaxf(w, min);
    return fmaxf(w, lay_bp_w(L, i));
}

float lay_clamp_h(const lay *L, uint32_t i, float h, float cbh)
{
    const sk_css_style *s = lay_style(L, i);
    float max = lay_size_h(L, i, &s->max_height, KEY_NONE, cbh);
    float min = lay_size_h(L, i, &s->min_height, KEY_NONE, cbh);
    if (!isnan(max))
        h = fminf(h, max);
    if (!isnan(min))
        h = fmaxf(h, min);
    return fmaxf(h, lay_bp_h(L, i));
}

/* ---- What a box is ---- */

static bool generated(const lay *L, uint32_t i)
{
    const sk_lnode *n = &L->nodes[i];
    const sk_css_style *s = n->style;
    if (!s || s->display == SK_DISPLAY_NONE)
        return false;
    if ((n->kind == SK_EL_BEFORE || n->kind == SK_EL_AFTER) && s->content_none)
        return false;
    return n->kind != SK_EL_HOST && n->kind != SK_EL_LINK;
}

uint8_t lay_display(const lay *L, uint32_t i)
{
    return generated(L, i) ? lay_style(L, i)->display : SK_DISPLAY_NONE;
}

bool lay_is_svg(const lay *L, uint32_t i)
{
    return L->nodes[i].kind == SK_EL_SVG;
}

bool lay_out_of_flow(const lay *L, uint32_t i)
{
    uint8_t p = lay_style(L, i)->position;
    return p == SK_POS_ABSOLUTE || p == SK_POS_FIXED;
}

static bool floated(const lay *L, uint32_t i)
{
    return lay_style(L, i)->float_ != SK_FLOAT_NONE && !lay_out_of_flow(L, i);
}

bool lay_block_level(const lay *L, uint32_t i)
{
    uint8_t d = lay_display(L, i);
    return (d == SK_DISPLAY_BLOCK || d == SK_DISPLAY_FLEX || d == SK_DISPLAY_GRID) && !floated(L, i) &&
           !lay_out_of_flow(L, i);
}

float lay_svg_ratio(const lay *L, uint32_t i)
{
    const sk_pack_svg *g = L->nodes[i].svg;
    if (g && g->kind == SK_SVG_VIEWBOX && g->v[2] > 0 && g->v[3] > 0)
        return g->v[2] / g->v[3];
    return 2; /* 300 by 150 */
}

/* A box's children in the order inline content meets them: a ::before,
 * the box's own text, then the rest. items holds at most count + 1. */
static size_t flow_items(const lay *L, uint32_t i, uint32_t *items)
{
    size_t n = 0;
    uint32_t c = L->nodes[i].first;
    const sk_lnode *nd = &L->nodes[i];
    bool own = nd->text ? nd->text[0] != 0
                        : (nd->kind == SK_EL_BEFORE || nd->kind == SK_EL_AFTER) && nd->style->ncontent > 0;
    if (c != SK_PACK_NONE && L->nodes[c].kind == SK_EL_BEFORE) {
        items[n++] = c;
        c = L->nodes[c].next;
    }
    if (own)
        items[n++] = LAY_OWN_TEXT;
    for (; c != SK_PACK_NONE; c = L->nodes[c].next)
        items[n++] = c;
    return n;
}

static size_t child_count(const lay *L, uint32_t i)
{
    size_t n = 1;
    for (uint32_t c = L->nodes[i].first; c != SK_PACK_NONE; c = L->nodes[c].next)
        n++;
    return n;
}

/* ---- Block flow ---- */

/* An absolute box's static position in block flow: the content box's
 * left, the flow's place so far. */
static void note_static(lay *L, uint32_t b, uint32_t c, float y)
{
    const sk_lbox *bx = &L->l->boxes[b];
    lstate *s = &L->st[c];
    s->sx = bx->bw[SK_LEFT] + bx->pad[SK_LEFT];
    s->sy = bx->bw[SK_TOP] + bx->pad[SK_TOP] + y;
    s->flex_static = false;
}

/* A block-level child in flow: as wide as the content box less its
 * margins unless it says otherwise, stacked below the flow so far. */
static float block_child(lay *L, uint32_t b, uint32_t c, float cw, float ch, float y)
{
    const sk_css_style *s = lay_style(L, c);
    lay_edges(L, c, cw);
    sk_lbox *cb = &L->l->boxes[c];
    float w = lay_size_w(L, c, &s->width, KEY_WIDTH, cw);
    bool auto_l = s->margin[SK_LEFT].kind == SK_LEN_AUTO, auto_r = s->margin[SK_RIGHT].kind == SK_LEN_AUTO;
    if (isnan(w)) {
        w = lay_clamp_w(L, c, cw - cb->margin[SK_LEFT] - cb->margin[SK_RIGHT], cw);
    } else {
        w = lay_clamp_w(L, c, w, cw);
        /* Auto margins share what the width leaves. */
        float room = cw - w - cb->margin[SK_LEFT] - cb->margin[SK_RIGHT];
        if (auto_l && auto_r)
            cb->margin[SK_LEFT] = cb->margin[SK_RIGHT] = fmaxf(0, room / 2);
        else if (auto_l)
            cb->margin[SK_LEFT] = fmaxf(0, room);
    }
    float h = lay_size_h(L, c, &s->height, KEY_HEIGHT, ch);
    if (!isnan(h))
        h = lay_clamp_h(L, c, h, ch);
    h = lay_contents(L, c, w, h);
    const sk_lbox *bx = &L->l->boxes[b];
    lstate *st = &L->st[c];
    st->x = bx->bw[SK_LEFT] + bx->pad[SK_LEFT] + cb->margin[SK_LEFT];
    st->y = bx->bw[SK_TOP] + bx->pad[SK_TOP] + y + cb->margin[SK_TOP];
    if (isnan(L->st[b].baseline) && !isnan(st->baseline))
        L->st[b].baseline = st->y + st->baseline;
    return cb->margin[SK_TOP] + h + cb->margin[SK_BOTTOM];
}

/* A block container's children: runs of inline content on lines,
 * block-level children stacked between them. The content's height. */
static float block_flow(lay *L, uint32_t b, float cw, float ch)
{
    size_t cap = child_count(L, b);
    uint32_t *items = sk_xreallocarray(NULL, cap, sizeof *items);
    uint32_t *run = sk_xreallocarray(NULL, cap, sizeof *run);
    size_t n = flow_items(L, b, items), nrun = 0;
    float y = 0;
    for (size_t k = 0; k < n; k++) {
        uint32_t c = items[k];
        if (c != LAY_OWN_TEXT) {
            if (!generated(L, c))
                continue;
            if (lay_out_of_flow(L, c)) {
                note_static(L, b, c, y);
                continue;
            }
            if (lay_block_level(L, c) || (lay_is_svg(L, c) && lay_display(L, c) == SK_DISPLAY_BLOCK)) {
                if (nrun)
                    y += lay_inline(L, b, run, nrun, cw, y);
                nrun = 0;
                y += block_child(L, b, c, cw, ch, y);
                continue;
            }
        }
        run[nrun++] = c;
    }
    if (nrun)
        y += lay_inline(L, b, run, nrun, cw, y);
    free(items);
    free(run);
    return y;
}

static void block_widths(lay *L, uint32_t b, float *min_w, float *max_w)
{
    size_t cap = child_count(L, b);
    uint32_t *items = sk_xreallocarray(NULL, cap, sizeof *items);
    uint32_t *run = sk_xreallocarray(NULL, cap, sizeof *run);
    size_t n = flow_items(L, b, items), nrun = 0;
    float lo = 0, hi = 0, a, z;
    for (size_t k = 0; k < n; k++) {
        uint32_t c = items[k];
        if (c != LAY_OWN_TEXT) {
            if (!generated(L, c) || lay_out_of_flow(L, c))
                continue;
            if (lay_block_level(L, c) || (lay_is_svg(L, c) && lay_display(L, c) == SK_DISPLAY_BLOCK)) {
                if (nrun) {
                    lay_inline_widths(L, b, run, nrun, &a, &z);
                    lo = fmaxf(lo, a);
                    hi = fmaxf(hi, z);
                }
                nrun = 0;
                lay_intrinsic(L, c, &a, &z);
                float m = lay_margins_w(L, c);
                lo = fmaxf(lo, a + m);
                hi = fmaxf(hi, z + m);
                continue;
            }
        }
        run[nrun++] = c;
    }
    if (nrun) {
        lay_inline_widths(L, b, run, nrun, &a, &z);
        lo = fmaxf(lo, a);
        hi = fmaxf(hi, z);
    }
    free(items);
    free(run);
    *min_w = lo;
    *max_w = hi;
}

/* ---- A box's contents ---- */

void lay_intrinsic(lay *L, uint32_t i, float *min_w, float *max_w)
{
    lstate *st = &L->st[i];
    if (st->measured) {
        *min_w = st->min_w;
        *max_w = st->max_w;
        return;
    }
    const sk_css_style *s = lay_style(L, i);
    /* Measured with no containing block, its percentages none; the
     * edges it was laid out with come back after. */
    sk_lbox *box = &L->l->boxes[i];
    float keep[12];
    memcpy(keep, box->bw, sizeof box->bw);
    memcpy(keep + 4, box->pad, sizeof box->pad);
    memcpy(keep + 8, box->margin, sizeof box->margin);
    lay_edges(L, i, NAN);
    float bp = lay_bp_w(L, i);
    float w = lay_size_w(L, i, &s->width, KEY_WIDTH, NAN), lo, hi;
    if (!isnan(w)) {
        lo = hi = w;
    } else if (L->depth > MAX_DEPTH) {
        lo = hi = bp;
    } else {
        L->depth++;
        if (lay_is_svg(L, i)) {
            float h = lay_size_h(L, i, &s->height, KEY_HEIGHT, NAN);
            lo = hi = isnan(h) ? 300 + bp : (h - lay_bp_h(L, i)) * lay_svg_ratio(L, i) + bp;
        } else {
            float a = 0, z = 0;
            uint8_t d = lay_display(L, i);
            if (d == SK_DISPLAY_FLEX)
                lay_flex_widths(L, i, &a, &z);
            else if (d == SK_DISPLAY_GRID)
                lay_grid_widths(L, i, &a, &z);
            else
                block_widths(L, i, &a, &z);
            lo = a + bp;
            hi = z + bp;
        }
        L->depth--;
    }
    lo = lay_clamp_w(L, i, lo, NAN);
    hi = lay_clamp_w(L, i, fmaxf(hi, lo), NAN);
    memcpy(box->bw, keep, sizeof box->bw);
    memcpy(box->pad, keep + 4, sizeof box->pad);
    memcpy(box->margin, keep + 8, sizeof box->margin);
    st->min_w = lo;
    st->max_w = hi;
    st->measured = true;
    *min_w = lo;
    *max_w = hi;
}

float lay_fit_w(lay *L, uint32_t i, float avail)
{
    float lo, hi;
    lay_intrinsic(L, i, &lo, &hi);
    if (isnan(avail))
        return hi;
    return fmaxf(lo, fminf(hi, avail));
}

float lay_contents(lay *L, uint32_t i, float w, float h)
{
    lstate *st = &L->st[i];
    st->runs.len = 0;
    st->glyphs.len = 0;
    st->baseline = NAN;
    if (!isfinite(w))
        w = 0;
    float cw = fmaxf(0, w - lay_bp_w(L, i));
    float ch = isnan(h) ? NAN : fmaxf(0, h - lay_bp_h(L, i));
    float used;
    if (L->depth > MAX_DEPTH) {
        used = 0;
    } else {
        L->depth++;
        uint8_t d = lay_display(L, i);
        if (lay_is_svg(L, i))
            used = isnan(ch) ? cw / lay_svg_ratio(L, i) : ch;
        else if (d == SK_DISPLAY_FLEX)
            used = lay_flex(L, i, cw, ch);
        else if (d == SK_DISPLAY_GRID)
            used = lay_grid(L, i, cw, ch);
        else
            used = block_flow(L, i, cw, ch);
        L->depth--;
    }
    if (isnan(h))
        h = lay_clamp_h(L, i, used + lay_bp_h(L, i), NAN);
    st->w = w;
    st->h = h;
    return h;
}

/* ---- Absolute boxes ---- */

/* Where a flex or grid parent would put an absolute child alone, along
 * one axis: room is what its margin box leaves. */
static float align_alone(uint8_t a, bool reverse, float room)
{
    switch (a) {
    case SK_ALIGN_CENTER:
        return room / 2;
    case SK_ALIGN_FLEX_END:
        return reverse ? 0 : room;
    case SK_ALIGN_END:
        return room;
    case SK_ALIGN_SPACE_AROUND:
    case SK_ALIGN_SPACE_EVENLY:
        return room / 2;
    case SK_ALIGN_FLEX_START:
    case SK_ALIGN_NORMAL:
    case SK_ALIGN_STRETCH:
    case SK_ALIGN_SPACE_BETWEEN:
        return reverse ? room : 0;
    default:
        return 0;
    }
}

/* The containing block of an absolute or fixed box: the nearest
 * ancestor positioned, transformed or filtered (only those last two
 * for a fixed box), else the skin area (SK_PACK_NONE). */
static uint32_t containing_block(const lay *L, uint32_t i)
{
    bool fixed = lay_style(L, i)->position == SK_POS_FIXED;
    for (uint32_t p = L->nodes[i].parent; p != SK_PACK_NONE && p != L->l->root; p = L->nodes[p].parent) {
        const sk_css_style *s = lay_style(L, p);
        if (!s)
            break;
        if (s->ntransform || s->nfilter || (!fixed && s->position != SK_POS_STATIC))
            return p;
    }
    if (fixed)
        return SK_PACK_NONE;
    return L->l->root;
}

/* Sizes absolute box i against its containing block's padding box and
 * lays out its contents; its border box's place in the skin area. */
static void absolute(lay *L, uint32_t i, sk_frect cb, float static_x, float static_y, float *ax, float *ay)
{
    const sk_css_style *s = lay_style(L, i);
    lstate *st = &L->st[i];
    lay_edges(L, i, cb.w);
    sk_lbox *b = &L->l->boxes[i];
    float left = lay_len(L, i, &s->inset[SK_LEFT], KEY_LEFT, cb.w);
    float right = lay_len(L, i, &s->inset[SK_RIGHT], KEY_RIGHT, cb.w);
    float top = lay_len(L, i, &s->inset[SK_TOP], KEY_TOP, cb.h);
    float bottom = lay_len(L, i, &s->inset[SK_BOTTOM], KEY_BOTTOM, cb.h);
    float ml = b->margin[SK_LEFT], mr = b->margin[SK_RIGHT], mt = b->margin[SK_TOP], mb = b->margin[SK_BOTTOM];
    float w = lay_size_w(L, i, &s->width, KEY_WIDTH, cb.w);
    if (isnan(w)) {
        if (!isnan(left) && !isnan(right))
            w = cb.w - left - right - ml - mr;
        else
            w = lay_fit_w(L, i, cb.w - (isnan(left) ? 0 : left) - (isnan(right) ? 0 : right) - ml - mr);
    }
    w = lay_clamp_w(L, i, w, cb.w);
    float h = lay_size_h(L, i, &s->height, KEY_HEIGHT, cb.h);
    if (isnan(h) && !isnan(top) && !isnan(bottom))
        h = cb.h - top - bottom - mt - mb;
    if (!isnan(h))
        h = lay_clamp_h(L, i, h, cb.h);
    else if (lay_is_svg(L, i))
        h = lay_clamp_h(L, i, (w - lay_bp_w(L, i)) / lay_svg_ratio(L, i) + lay_bp_h(L, i), cb.h);
    h = lay_contents(L, i, w, h);
    /* The static position, where a flex parent would align it alone. */
    if (st->flex_static && ((isnan(left) && isnan(right)) || (isnan(top) && isnan(bottom)))) {
        float main_free = (st->row ? st->cw - w - ml - mr : st->ch - h - mt - mb);
        float cross_free = (st->row ? st->ch - h - mt - mb : st->cw - w - ml - mr);
        float mo = align_alone(st->justify, st->reverse, main_free);
        float co = align_alone(st->align == SK_ALIGN_BASELINE ? SK_ALIGN_FLEX_START : st->align, false, cross_free);
        if (!isfinite(co))
            co = 0;
        if (!isfinite(mo))
            mo = 0;
        static_x += st->cx + (st->row ? mo : co) - st->sx;
        static_y += st->cy + (st->row ? co : mo) - st->sy;
    }
    if (!isnan(left))
        *ax = cb.x + left + ml;
    else if (!isnan(right))
        *ax = cb.x + cb.w - right - mr - w;
    else
        *ax = static_x + ml;
    if (!isnan(top))
        *ay = cb.y + top + mt;
    else if (!isnan(bottom))
        *ay = cb.y + cb.h - bottom - mb - h;
    else
        *ay = static_y + mt;
}

/* ---- Placing ---- */

static void set_rects(sk_lbox *b, float x, float y, float w, float h)
{
    b->border = (sk_frect){x, y, w, h};
    b->padding = (sk_frect){x + b->bw[SK_LEFT], y + b->bw[SK_TOP], fmaxf(0, w - b->bw[SK_LEFT] - b->bw[SK_RIGHT]),
                            fmaxf(0, h - b->bw[SK_TOP] - b->bw[SK_BOTTOM])};
    b->content = (sk_frect){b->padding.x + b->pad[SK_LEFT], b->padding.y + b->pad[SK_TOP],
                            fmaxf(0, b->padding.w - b->pad[SK_LEFT] - b->pad[SK_RIGHT]),
                            fmaxf(0, b->padding.h - b->pad[SK_TOP] - b->pad[SK_BOTTOM])};
}

static bool clips(const lay *L, uint32_t i)
{
    const sk_css_style *s = lay_style(L, i);
    return s->overflow_x != SK_OVERFLOW_VISIBLE || s->overflow_y != SK_OVERFLOW_VISIBLE;
}

/* A relative box's offset: left before right, top before bottom. */
static void relative_offset(const lay *L, uint32_t i, uint32_t parent, float *dx, float *dy)
{
    const sk_css_style *s = lay_style(L, i);
    *dx = *dy = 0;
    if (s->position != SK_POS_RELATIVE)
        return;
    const sk_lbox *p = &L->l->boxes[parent];
    float cw = p->content.w, ch = p->content.h;
    float l = lay_len(L, i, &s->inset[SK_LEFT], KEY_LEFT, cw), r = lay_len(L, i, &s->inset[SK_RIGHT], KEY_RIGHT, cw);
    float t = lay_len(L, i, &s->inset[SK_TOP], KEY_TOP, ch), b = lay_len(L, i, &s->inset[SK_BOTTOM], KEY_BOTTOM, ch);
    *dx = !isnan(l) ? l : !isnan(r) ? -r : 0;
    *dy = !isnan(t) ? t : !isnan(b) ? -b : 0;
}

/* The runs box i laid out, moved into the skin area. */
static void keep_runs(lay *L, uint32_t i, float ax, float ay)
{
    lstate *st = &L->st[i];
    sk_layout *l = L->l;
    sk_lbox *b = &l->boxes[i];
    b->runs = (uint32_t)l->nruns;
    b->nruns = (uint32_t)st->runs.len;
    if (!st->runs.len)
        return;
    l->runs = sk_xreallocarray(l->runs, l->nruns + st->runs.len, sizeof *l->runs);
    l->glyphs = sk_xreallocarray(l->glyphs, l->nglyphs + st->glyphs.len, sizeof *l->glyphs);
    memcpy(l->glyphs + l->nglyphs, st->glyphs.ptr, st->glyphs.len * sizeof *l->glyphs);
    for (size_t k = 0; k < st->runs.len; k++) {
        sk_lrun r = st->runs.ptr[k];
        r.first += (uint32_t)l->nglyphs;
        if (!b->svg_content) {
            r.x += ax;
            r.y += ay;
        }
        l->runs[l->nruns++] = r;
    }
    l->nglyphs += st->glyphs.len;
}

/* The mapping of an svg's viewBox into its content box: xMidYMid meet,
 * or none. */
static sk_xform viewbox(const lay *L, uint32_t i, sk_frect c)
{
    const sk_pack_svg *g = L->nodes[i].svg;
    float vx = 0, vy = 0, vw = 300, vh = 150;
    bool none = false;
    if (g && g->kind == SK_SVG_VIEWBOX && g->v[2] > 0 && g->v[3] > 0) {
        vx = g->v[0];
        vy = g->v[1];
        vw = g->v[2];
        vh = g->v[3];
        none = g->flag == 1;
    } else {
        return xf_translate(c.x, c.y);
    }
    float sx = c.w / vw, sy = c.h / vh;
    if (!none)
        sx = sy = fminf(sx, sy);
    float ox = c.x + (none ? 0 : (c.w - vw * sx) / 2) - vx * sx;
    float oy = c.y + (none ? 0 : (c.h - vh * sy) / 2) - vy * sy;
    return (sk_xform){sx, 0, 0, sy, ox, oy};
}

static void place(lay *L, uint32_t i, float ax, float ay, const sk_xform *parent_xf, uint32_t clipper);

/* An svg's shapes: no boxes, its geometry in user units, the groups
 * turned about their centres. */
static void place_svg(lay *L, uint32_t i, const sk_xform *xf, uint32_t clipper)
{
    for (uint32_t c = L->nodes[i].first; c != SK_PACK_NONE; c = L->nodes[c].next) {
        sk_lbox *b = &L->l->boxes[c];
        b->node = c;
        b->svg_content = true;
        b->generated = generated(L, c) || (L->nodes[c].style && L->nodes[c].style->display != SK_DISPLAY_NONE &&
                                            L->nodes[c].kind > SK_EL_SVG && L->nodes[c].kind < SK_EL_BEFORE);
        if (!b->generated)
            continue;
        b->cb = i;
        b->clipper = clipper;
        b->baseline = NAN;
        b->local = SK_XFORM_IDENTITY;
        const sk_pack_svg *g = L->nodes[c].svg;
        if (g && g->kind == SK_SVG_TURN && L->nodes[c].turn != 0) {
            double r = L->nodes[c].turn * M_PI / 180;
            float cs = (float)cos(r), sn = (float)sin(r);
            sk_xform rot = {cs, sn, -sn, cs, 0, 0};
            sk_xform to = xf_translate(g->v[0], g->v[1]), back = xf_translate(-g->v[0], -g->v[1]);
            sk_xform t = sk_xform_mul(&to, &rot);
            b->local = sk_xform_mul(&t, &back);
            b->has_xform = true;
        }
        if (L->nodes[c].style->ntransform) {
            sk_frect box = {0, 0, 0, 0};
            if (g && g->kind == SK_SVG_IMAGE)
                box = (sk_frect){g->v[0], g->v[1], g->v[2], g->v[3]};
            sk_xform t = sk_layout_transform(L->nodes[c].style, L->nodes[c].style->transform,
                                             L->nodes[c].style->ntransform, box);
            b->local = sk_xform_mul(&b->local, &t);
            b->has_xform = true;
        }
        b->xform = sk_xform_mul(xf, &b->local);
        if (L->nodes[c].kind == SK_EL_TEXT) {
            lay_svg_text(L, c);
            keep_runs(L, c, 0, 0);
        }
        place_svg(L, c, &b->xform, clipper);
    }
}

static void place_children(lay *L, uint32_t i, uint32_t clipper)
{
    sk_lbox *b = &L->l->boxes[i];
    uint32_t mine = clips(L, i) ? i : clipper;
    for (uint32_t c = L->nodes[i].first; c != SK_PACK_NONE; c = L->nodes[c].next) {
        if (!generated(L, c))
            continue;
        if (lay_out_of_flow(L, c)) {
            uint32_t cb = containing_block(L, c);
            sk_frect area = cb == SK_PACK_NONE ? (sk_frect){0, 0, L->l->width, L->l->height}
                                               : L->l->boxes[cb].padding;
            float ax, ay;
            absolute(L, c, area, b->border.x + L->st[c].sx, b->border.y + L->st[c].sy, &ax, &ay);
            L->l->boxes[c].cb = cb;
            /* An absolute box escapes the clips between it and its
             * containing block. */
            uint32_t clip = SK_PACK_NONE;
            if (cb != SK_PACK_NONE)
                clip = clips(L, cb) ? cb : L->l->boxes[cb].clipper;
            place(L, c, ax, ay, &b->xform, clip);
        } else {
            float dx, dy;
            relative_offset(L, c, i, &dx, &dy);
            L->l->boxes[c].cb = i;
            place(L, c, b->border.x + L->st[c].x + dx, b->border.y + L->st[c].y + dy, &b->xform, mine);
        }
    }
}

static void place(lay *L, uint32_t i, float ax, float ay, const sk_xform *parent_xf, uint32_t clipper)
{
    lstate *st = &L->st[i];
    sk_lbox *b = &L->l->boxes[i];
    b->generated = true;
    b->node = i;
    b->clipper = clipper;
    set_rects(b, ax, ay, st->w, st->h);
    b->baseline = isnan(st->baseline) ? NAN : ay + st->baseline;
    const sk_css_style *s = lay_style(L, i);
    b->local = SK_XFORM_IDENTITY;
    b->has_xform = s->ntransform > 0;
    if (b->has_xform)
        b->local = sk_layout_transform(s, s->transform, s->ntransform, b->border);
    b->xform = sk_xform_mul(parent_xf, &b->local);
    keep_runs(L, i, ax, ay);
    if (lay_is_svg(L, i)) {
        b->viewbox = viewbox(L, i, b->content);
        sk_xform xf = sk_xform_mul(&b->xform, &b->viewbox);
        place_svg(L, i, &xf, clips(L, i) ? i : clipper);
        return;
    }
    place_children(L, i, clipper);
}

/* ---- What each box may paint ---- */

float sk_layout_shadow_reach(const sk_css_shadow *s, uint16_t n, bool box)
{
    float r = 0;
    for (uint16_t i = 0; i < n; i++) {
        if (box && s[i].inset)
            continue;
        float d = fmaxf(fabsf(s[i].x), fabsf(s[i].y)) + 1.5f * fmaxf(0, s[i].blur) + 1;
        if (box)
            d += fmaxf(0, s[i].spread);
        r = fmaxf(r, d);
    }
    return r;
}

sk_frect sk_layout_filter_reach(const sk_css_style *s, sk_frect ink)
{
    for (uint16_t i = 0; i < s->nfilter; i++) {
        const sk_css_filter *f = &s->filter[i];
        float d = 0;
        if (f->kind == SK_FX_BLUR)
            d = 3 * fmaxf(0, f->amount) + 1;
        else if (f->kind == SK_FX_DROP_SHADOW)
            d = fmaxf(fabsf(f->shadow.x), fabsf(f->shadow.y)) + 1.5f * fmaxf(0, f->shadow.blur) + 1;
        if (d > 0 && !sk_frect_empty(ink))
            ink = (sk_frect){ink.x - d, ink.y - d, ink.w + 2 * d, ink.h + 2 * d};
    }
    return ink;
}

static sk_frect inflate(sk_frect r, float d)
{
    return (sk_frect){r.x - d, r.y - d, r.w + 2 * d, r.h + 2 * d};
}

/* What runs paint: their advance and the font's height, and the room
 * a glyph's ink or a shadow takes beyond them. */
static sk_frect runs_ink(const lay *L, const sk_lbox *b)
{
    sk_frect r = {0, 0, 0, 0};
    for (uint32_t k = 0; k < b->nruns; k++) {
        const sk_lrun *run = &L->l->runs[b->runs + k];
        const sk_css_style *s = L->nodes[run->node].style;
        float reach = run->px * 0.5f + sk_layout_shadow_reach(s->text_shadow, s->ntext_shadow, false);
        sk_frect g = {run->x, run->y - run->ascent, run->width, run->ascent + run->descent};
        r = sk_frect_union(r, inflate(g, reach));
    }
    return r;
}

/* An SVG shape's bounds in user units, its stroke included. */
static sk_frect svg_ink(const lay *L, uint32_t i)
{
    const sk_pack_svg *g = L->nodes[i].svg;
    const sk_css_style *s = L->nodes[i].style;
    float sw = s->stroke_kind == SK_PAINT_COLOR ? sk_layout_len(&s->stroke_width, 100) : 0;
    if (isnan(sw))
        sw = 1;
    sk_frect r = {0, 0, 0, 0};
    if (!g)
        return r;
    switch (g->kind) {
    case SK_SVG_ARC:
    case SK_SVG_CIRCLE:
        r = (sk_frect){g->v[0] - g->v[2], g->v[1] - g->v[2], 2 * g->v[2], 2 * g->v[2]};
        break;
    case SK_SVG_LINE:
        r = (sk_frect){fminf(g->v[0], g->v[2]), fminf(g->v[1], g->v[3]), fabsf(g->v[2] - g->v[0]) + 0.01f,
                       fabsf(g->v[3] - g->v[1]) + 0.01f};
        break;
    case SK_SVG_IMAGE:
        r = (sk_frect){g->v[0], g->v[1], g->v[2], g->v[3]};
        break;
    default:
        break;
    }
    if (!sk_frect_empty(r))
        r = inflate(r, sw / 2 + 1);
    return r;
}

static sk_frect ink_of(lay *L, uint32_t i);

static sk_frect children_ink(lay *L, uint32_t i, sk_frect clip, bool clipped)
{
    sk_frect r = {0, 0, 0, 0};
    for (uint32_t c = L->nodes[i].first; c != SK_PACK_NONE; c = L->nodes[c].next) {
        sk_lbox *cb = &L->l->boxes[c];
        if (!cb->generated)
            continue;
        sk_frect k = ink_of(L, c);
        if (cb->has_xform)
            k = sk_frect_map(&cb->local, k);
        /* An absolute child escapes a clip it is not inside. */
        if (clipped && !(lay_style(L, c) && lay_out_of_flow(L, c) && cb->clipper != i && !cb->svg_content))
            k = sk_frect_intersect(k, clip);
        r = sk_frect_union(r, k);
    }
    return r;
}

static sk_frect ink_of(lay *L, uint32_t i)
{
    sk_lbox *b = &L->l->boxes[i];
    const sk_css_style *s = lay_style(L, i);
    sk_frect r;
    if (b->svg_content) {
        r = sk_frect_union(svg_ink(L, i), runs_ink(L, b));
        r = sk_frect_union(r, children_ink(L, i, r, false));
    } else {
        r = inflate(b->border, sk_layout_shadow_reach(s->box_shadow, s->nbox_shadow, true));
        if (s->outline_style == SK_BORDER_SOLID && s->outline_width > 0)
            r = sk_frect_union(r, inflate(b->border, s->outline_width + fmaxf(0, s->outline_offset)));
        if (s->bi_source.kind != SK_IMAGE_NONE)
            for (int k = 0; k < 4; k++)
                r = inflate(r, fmaxf(0, s->bi_outset[k]));
        r = sk_frect_union(r, runs_ink(L, b));
        bool clipped = clips(L, i);
        sk_frect clip = b->padding;
        if (lay_is_svg(L, i)) {
            sk_frect inner = children_ink(L, i, clip, false);
            inner = sk_frect_map(&b->viewbox, inner);
            r = sk_frect_union(r, clipped ? sk_frect_intersect(inner, b->content) : inner);
        } else {
            r = sk_frect_union(r, children_ink(L, i, clip, clipped));
        }
    }
    r = sk_layout_filter_reach(s, r);
    b->ink = r;
    return r;
}

/* ---- The run ---- */

const sk_font *sk_layout_face(const sk_lfonts *fonts, const sk_css_style *s)
{
    const sk_font *f = fonts && fonts->face ? fonts->face(fonts->data, s->font) : NULL;
    return f ? f : sk_font_embedded(SK_FONT_SANS);
}

void sk_layout_run(sk_layout *l, const sk_lnode *nodes, uint32_t n, uint32_t root, float w, float h,
                   const sk_lfonts *fonts)
{
    memset(l, 0, sizeof *l);
    sk_arena_init(&l->arena, 0);
    l->nodes = nodes;
    l->n = n;
    l->root = root;
    l->width = w;
    l->height = h;
    l->boxes = sk_xcalloc(n ? n : 1, sizeof *l->boxes);
    for (uint32_t i = 0; i < n; i++) {
        l->boxes[i].node = i;
        l->boxes[i].cb = l->boxes[i].clipper = SK_PACK_NONE;
        l->boxes[i].baseline = NAN;
        l->boxes[i].local = l->boxes[i].xform = l->boxes[i].viewbox = SK_XFORM_IDENTITY;
    }
    if (root >= n)
        return;
    lay L = {l, nodes, fonts, sk_xcalloc(n, sizeof(lstate)), 0};
    for (uint32_t i = 0; i < n; i++)
        L.st[i].baseline = NAN;
    if (generated(&L, root)) {
        /* The stage is an absolute box against the skin area. */
        sk_frect area = {0, 0, w, h};
        float ax, ay;
        L.st[root].sx = L.st[root].sy = 0;
        absolute(&L, root, area, 0, 0, &ax, &ay);
        sk_xform id = SK_XFORM_IDENTITY;
        place(&L, root, ax, ay, &id, SK_PACK_NONE);
        ink_of(&L, root);
    }
    for (uint32_t i = 0; i < n; i++) {
        sk_vec_free(&L.st[i].runs);
        sk_vec_free(&L.st[i].glyphs);
    }
    free(L.st);
}

void sk_layout_free(sk_layout *l)
{
    free(l->boxes);
    free(l->runs);
    free(l->glyphs);
    sk_arena_free(&l->arena);
    memset(l, 0, sizeof *l);
}
