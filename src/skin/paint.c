/* The painter's frame: the compositing loop, shapes as coverage, clips,
 * layers and their effects (filters, clip-path, opacity, blend, a turn
 * resampled), the painting order of CSS 2.1's Appendix E, text and the
 * widgets' SVG. Box decorations are decor.c's.
 *
 * Every box is drawn through the mapping of its CSS px to the canvas's
 * pixels, which is axis-aligned for CSS boxes: a subtree that a
 * transform turns or skews is painted into a layer upright and the
 * layer resampled, as a browser composites a turned layer. SVG shapes
 * take any mapping, as paths. */
#include "pinternal.h"

#include <string.h>

/* ---- Pixels ---- */

sk_px pt_color(sk_css_color c, float a)
{
    float al = c.a * a;
    if (!(al > 0))
        return 0;
    if (al > 1)
        al = 1;
    uint32_t A = (uint32_t)lroundf(al * 255);
    uint32_t r = (uint32_t)lroundf(fminf(fmaxf(c.r, 0), 1) * al * 255);
    uint32_t g = (uint32_t)lroundf(fminf(fmaxf(c.g, 0), 1) * al * 255);
    uint32_t b = (uint32_t)lroundf(fminf(fmaxf(c.b, 0), 1) * al * 255);
    return A << 24 | SK_MIN(r, A) << 16 | SK_MIN(g, A) << 8 | SK_MIN(b, A);
}

static uint32_t div255(int32_t t)
{
    t = t < 0 ? 0 : t > 255 * 255 ? 255 * 255 : t;
    uint32_t u = (uint32_t)t + 128;
    return (u + (u >> 8)) >> 8;
}

/* src/gfx/draw.h's formulas, a channel at a time. */
sk_px pt_blend(sk_blend b, sk_px s, sk_px d)
{
    if (b == SK_BLEND_SRC_OVER)
        return pt_over(s, d);
    if (b == SK_BLEND_SRC)
        return s;
    if (b == SK_BLEND_ADD) {
        sk_px out = 0;
        for (int sh = 0; sh < 32; sh += 8)
            out |= SK_MIN(((s >> sh) & 255) + ((d >> sh) & 255), 255u) << sh;
        return out;
    }
    int32_t sa = (int32_t)(s >> 24), da = (int32_t)(d >> 24);
    sk_px out = (uint32_t)(sa + (int32_t)pt_mul((uint32_t)da, (uint32_t)(255 - sa))) << 24;
    for (int sh = 0; sh < 24; sh += 8) {
        int32_t x = (int32_t)((s >> sh) & 255), y = (int32_t)((d >> sh) & 255), t;
        switch (b) {
        case SK_BLEND_MULTIPLY:
            t = x * (255 - da) + y * (255 - sa) + x * y;
            break;
        case SK_BLEND_SCREEN:
            t = 255 * (x + y) - x * y;
            break;
        case SK_BLEND_OVERLAY:
            t = x * (255 - da) + y * (255 - sa) + (2 * y <= da ? 2 * x * y : sa * da - 2 * (da - y) * (sa - x));
            break;
        case SK_BLEND_DARKEN:
            t = 255 * (x + y) - SK_MAX(x * da, y * sa);
            break;
        default:
            t = 255 * (x + y) - SK_MIN(x * da, y * sa);
            break;
        }
        out |= div255(t) << sh;
    }
    return out;
}

sk_blend pt_mix(uint8_t mix)
{
    static const sk_blend MAP[] = {SK_BLEND_SRC_OVER, SK_BLEND_MULTIPLY, SK_BLEND_SCREEN, SK_BLEND_OVERLAY,
                                   SK_BLEND_DARKEN,   SK_BLEND_LIGHTEN,  SK_BLEND_ADD};
    return mix < SK_LEN(MAP) ? MAP[mix] : SK_BLEND_SRC_OVER;
}

/* ---- Filling ---- */

/* One row: s[0..n) scaled by opacity, coverage and the clip's coverage,
 * blended onto d. */
static void comp_row(sk_px *d, const sk_px *s, sk_px solid, int n, const uint8_t *cov, const uint8_t *clip,
                     sk_blend b, uint32_t opacity)
{
    /* The common cases first: source-over at full strength. */
    if (b == SK_BLEND_SRC_OVER && opacity == 255 && !clip) {
        if (!s && !cov) {
            if (solid >> 24 == 255) {
                for (int x = 0; x < n; x++)
                    d[x] = solid;
            } else if (solid) {
                uint32_t k = 255 - (solid >> 24);
                for (int x = 0; x < n; x++)
                    d[x] = solid + pt_scale(d[x], k);
            }
            return;
        }
        if (s && !cov) {
            for (int x = 0; x < n; x++)
                if (s[x])
                    d[x] = pt_over(s[x], d[x]);
            return;
        }
        if (!s) {
            for (int x = 0; x < n; x++)
                if (cov[x])
                    d[x] = pt_over(cov[x] == 255 ? solid : pt_scale(solid, cov[x]), d[x]);
            return;
        }
        for (int x = 0; x < n; x++)
            if (cov[x] && s[x])
                d[x] = pt_over(cov[x] == 255 ? s[x] : pt_scale(s[x], cov[x]), d[x]);
        return;
    }
    for (int x = 0; x < n; x++) {
        uint32_t k = opacity;
        if (cov)
            k = pt_mul(k, cov[x]);
        if (clip)
            k = pt_mul(k, clip[x]);
        if (!k)
            continue;
        sk_px p = s ? s[x] : solid;
        if (k != 255)
            p = pt_scale(p, k);
        if (b == SK_BLEND_SRC_OVER) {
            if (p)
                d[x] = pt_over(p, d[x]);
        } else {
            d[x] = pt_blend(b, p, d[x]);
        }
    }
}

void pt_fill(canvas *cv, sk_rect r, const uint8_t *cov, int cs, const pt_shader *sh, sk_blend blend,
             uint32_t opacity)
{
    sk_rect a = sk_rect_intersect(sk_rect_intersect(r, cv->clip), sk_image_bounds(cv->im));
    if (sk_rect_empty(a) || !opacity)
        return;
    if (!sh->row && !sh->solid && blend == SK_BLEND_SRC_OVER)
        return;
    sk_px *buf = sh->row ? sk_xreallocarray(NULL, (size_t)a.w, sizeof *buf) : NULL;
    for (int y = a.y; y < a.y + a.h; y++) {
        if (buf)
            sh->row(sh, a.x, y, a.w, buf);
        const uint8_t *c = cov ? cov + (size_t)(y - r.y) * (size_t)cs + (size_t)(a.x - r.x) : NULL;
        const uint8_t *m = cv->mask
                               ? cv->mask + (size_t)(y - cv->clip.y) * (size_t)cv->clip.w + (size_t)(a.x - cv->clip.x)
                               : NULL;
        comp_row(sk_image_row(cv->im, y) + a.x, buf, sh->solid, a.w, c, m, blend, opacity);
    }
    free(buf);
}

void pt_fill_image(canvas *cv, int x, int y, const sk_image *src, const uint8_t *cov, int cs, sk_blend blend,
                   uint32_t opacity)
{
    sk_rect r = sk_rect_make(x, y, src->w, src->h);
    sk_rect a = sk_rect_intersect(sk_rect_intersect(r, cv->clip), sk_image_bounds(cv->im));
    if (sk_rect_empty(a) || !opacity)
        return;
    for (int j = a.y; j < a.y + a.h; j++) {
        const sk_px *s = sk_image_row(src, j - y) + (a.x - x);
        const uint8_t *c = cov ? cov + (size_t)(j - r.y) * (size_t)cs + (size_t)(a.x - r.x) : NULL;
        const uint8_t *m = cv->mask
                               ? cv->mask + (size_t)(j - cv->clip.y) * (size_t)cv->clip.w + (size_t)(a.x - cv->clip.x)
                               : NULL;
        comp_row(sk_image_row(cv->im, j) + a.x, s, 0, a.w, c, m, blend, opacity);
    }
}

/* src's pixel at (u, v), bilinear, in 256ths, transparent outside. */
static sk_px sample(const sk_image *src, float u, float v)
{
    u -= 0.5f;
    v -= 0.5f;
    float fu = floorf(u), fv = floorf(v);
    int x0 = (int)fu, y0 = (int)fv;
    uint32_t wx = (uint32_t)((u - fu) * 256), wy = (uint32_t)((v - fv) * 256);
    sk_px p[4] = {0, 0, 0, 0};
    for (int k = 0; k < 4; k++) {
        int x = x0 + (k & 1), y = y0 + (k >> 1);
        if (x >= 0 && y >= 0 && x < src->w && y < src->h)
            p[k] = sk_image_row(src, y)[x];
    }
    sk_px out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        uint32_t a = (p[0] >> sh) & 255, b = (p[1] >> sh) & 255, c = (p[2] >> sh) & 255, d = (p[3] >> sh) & 255;
        uint32_t top = a * (256 - wx) + b * wx, bot = c * (256 - wx) + d * wx;
        uint32_t v8 = (top * (256 - wy) + bot * wy + 32768) >> 16;
        out |= (v8 > 255 ? 255 : v8) << sh;
    }
    return out;
}

void pt_blit_affine(canvas *cv, const sk_image *src, const sk_xform *m, sk_blend blend, uint32_t opacity)
{
    sk_xform inv;
    if (!sk_xform_invert(m, &inv) || !opacity)
        return;
    sk_rect r = pt_round_out(sk_frect_map(m, (sk_frect){0, 0, (float)src->w, (float)src->h}));
    sk_rect a = sk_rect_intersect(sk_rect_intersect(r, cv->clip), sk_image_bounds(cv->im));
    if (sk_rect_empty(a))
        return;
    sk_px *buf = sk_xreallocarray(NULL, (size_t)a.w, sizeof *buf);
    for (int y = a.y; y < a.y + a.h; y++) {
        for (int x = 0; x < a.w; x++) {
            float px = (float)(a.x + x) + 0.5f, py = (float)y + 0.5f;
            buf[x] = sample(src, inv.a * px + inv.c * py + inv.e, inv.b * px + inv.d * py + inv.f);
        }
        const uint8_t *mk = cv->mask
                                ? cv->mask + (size_t)(y - cv->clip.y) * (size_t)cv->clip.w + (size_t)(a.x - cv->clip.x)
                                : NULL;
        comp_row(sk_image_row(cv->im, y) + a.x, buf, 0, a.w, NULL, mk, blend, opacity);
    }
    free(buf);
}

/* ---- Shapes ---- */

bool pt_rrect_square(const pt_rrect *r)
{
    for (int k = 0; k < 4; k++)
        if (r->rx[k] > 0 && r->ry[k] > 0)
            return false;
    return true;
}

/* The quarter ellipse of a corner as a cubic, kappa as usual. */
#define KAPPA 0.5522847498f

void pt_rrect_path(sk_path *p, const pt_rrect *r)
{
    float x0 = r->x, y0 = r->y, x1 = r->x + r->w, y1 = r->y + r->h;
    float rx[4], ry[4];
    for (int k = 0; k < 4; k++) {
        bool on = r->rx[k] > 0 && r->ry[k] > 0;
        rx[k] = on ? r->rx[k] : 0;
        ry[k] = on ? r->ry[k] : 0;
    }
    sk_path_move(p, x0 + rx[SK_TOP_LEFT], y0);
    sk_path_line(p, x1 - rx[SK_TOP_RIGHT], y0);
    if (rx[SK_TOP_RIGHT] > 0)
        sk_path_cubic(p, x1 - rx[SK_TOP_RIGHT] * (1 - KAPPA), y0, x1, y0 + ry[SK_TOP_RIGHT] * (1 - KAPPA), x1,
                      y0 + ry[SK_TOP_RIGHT]);
    sk_path_line(p, x1, y1 - ry[SK_BOTTOM_RIGHT]);
    if (rx[SK_BOTTOM_RIGHT] > 0)
        sk_path_cubic(p, x1, y1 - ry[SK_BOTTOM_RIGHT] * (1 - KAPPA), x1 - rx[SK_BOTTOM_RIGHT] * (1 - KAPPA), y1,
                      x1 - rx[SK_BOTTOM_RIGHT], y1);
    sk_path_line(p, x0 + rx[SK_BOTTOM_LEFT], y1);
    if (rx[SK_BOTTOM_LEFT] > 0)
        sk_path_cubic(p, x0 + rx[SK_BOTTOM_LEFT] * (1 - KAPPA), y1, x0, y1 - ry[SK_BOTTOM_LEFT] * (1 - KAPPA), x0,
                      y1 - ry[SK_BOTTOM_LEFT]);
    sk_path_line(p, x0, y0 + ry[SK_TOP_LEFT]);
    if (rx[SK_TOP_LEFT] > 0)
        sk_path_cubic(p, x0, y0 + ry[SK_TOP_LEFT] * (1 - KAPPA), x0 + rx[SK_TOP_LEFT] * (1 - KAPPA), y0,
                      x0 + rx[SK_TOP_LEFT], y0);
    sk_path_close(p);
}

/* CSS Backgrounds 3's spread of a radius: by d, less where the radius
 * is smaller than d. */
static float spread_radius(float r, float d)
{
    if (r <= 0)
        return 0;
    if (d >= 0 && r < d) {
        float q = r / d - 1;
        return r + d * (1 + q * q * q);
    }
    return fmaxf(0, r + d);
}

pt_rrect pt_rrect_spread(const pt_rrect *r, float dx, float dy)
{
    pt_rrect o = *r;
    o.x -= dx;
    o.y -= dy;
    o.w = fmaxf(0, o.w + 2 * dx);
    o.h = fmaxf(0, o.h + 2 * dy);
    for (int k = 0; k < 4; k++) {
        o.rx[k] = spread_radius(r->rx[k], dx);
        o.ry[k] = spread_radius(r->ry[k], dy);
    }
    return o;
}

sk_rect pt_round_out(sk_frect r)
{
    if (sk_frect_empty(r) || !isfinite(r.x) || !isfinite(r.y) || !isfinite(r.w) || !isfinite(r.h))
        return sk_rect_make(0, 0, 0, 0);
    float x0 = floorf(r.x), y0 = floorf(r.y), x1 = ceilf(r.x + r.w), y1 = ceilf(r.y + r.h);
    x0 = fmaxf(x0, -(1 << 22));
    y0 = fmaxf(y0, -(1 << 22));
    x1 = fminf(x1, 1 << 22);
    y1 = fminf(y1, 1 << 22);
    if (x1 <= x0 || y1 <= y0)
        return sk_rect_make(0, 0, 0, 0);
    return sk_rect_make((int)x0, (int)y0, (int)(x1 - x0), (int)(y1 - y0));
}

uint8_t *pt_cover(const sk_path *p, const sk_xform *m, sk_rect r, sk_fill_rule rule)
{
    sk_xform t = {1, 0, 0, 1, (float)-r.x, (float)-r.y};
    sk_xform mm = m ? sk_xform_mul(&t, m) : t;
    return sk_path_mask(p, &mm, rule, r.w, r.h);
}

uint8_t *pt_cover_rrect(const pt_rrect *rr, sk_rect r)
{
    if (sk_rect_empty(r))
        return NULL;
    if (pt_rrect_square(rr) && rr->x == floorf(rr->x) && rr->y == floorf(rr->y) && rr->w == floorf(rr->w) &&
        rr->h == floorf(rr->h)) {
        uint8_t *m = sk_xcalloc((size_t)r.w, (size_t)r.h);
        int x0 = SK_MAX((int)rr->x, r.x), y0 = SK_MAX((int)rr->y, r.y);
        int x1 = SK_MIN((int)(rr->x + rr->w), r.x + r.w), y1 = SK_MIN((int)(rr->y + rr->h), r.y + r.h);
        for (int y = y0; y < y1; y++)
            memset(m + (size_t)(y - r.y) * (size_t)r.w + (size_t)(x0 - r.x), 255, (size_t)SK_MAX(0, x1 - x0));
        return m;
    }
    sk_path *p = sk_path_new();
    pt_rrect_path(p, rr);
    uint8_t *m = pt_cover(p, NULL, r, SK_FILL_NONZERO);
    sk_path_free(p);
    return m;
}

/* ---- Boxes ---- */

sk_xform pt_map(const painter *P, const canvas *cv, uint32_t i)
{
    return sk_xform_mul(&cv->base, &P->l->boxes[i].xform);
}

sk_frect pt_snap(const sk_xform *m, sk_frect r)
{
    float x0 = roundf(m->a * r.x + m->e), x1 = roundf(m->a * (r.x + r.w) + m->e);
    float y0 = roundf(m->d * r.y + m->f), y1 = roundf(m->d * (r.y + r.h) + m->f);
    return (sk_frect){fminf(x0, x1), fminf(y0, y1), fabsf(x1 - x0), fabsf(y1 - y0)};
}

pt_rrect pt_box_rrect(const painter *P, uint32_t i, const sk_xform *m, uint8_t which)
{
    const sk_lbox *b = &P->l->boxes[i];
    const sk_css_style *s = P->nodes[i].style;
    sk_frect bb = b->border;
    /* The border box's radii, reduced where they would overlap. */
    float rx[4], ry[4], f = 1;
    for (int k = 0; k < 4; k++) {
        rx[k] = sk_layout_len(&s->radius[k][0], bb.w);
        ry[k] = sk_layout_len(&s->radius[k][1], bb.h);
        if (!(rx[k] > 0) || !(ry[k] > 0))
            rx[k] = ry[k] = 0;
    }
    float sums[4] = {rx[SK_TOP_LEFT] + rx[SK_TOP_RIGHT], ry[SK_TOP_RIGHT] + ry[SK_BOTTOM_RIGHT],
                     rx[SK_BOTTOM_RIGHT] + rx[SK_BOTTOM_LEFT], ry[SK_BOTTOM_LEFT] + ry[SK_TOP_LEFT]};
    float sides[4] = {bb.w, bb.h, bb.w, bb.h};
    for (int k = 0; k < 4; k++)
        if (sums[k] > sides[k] && sums[k] > 0)
            f = fminf(f, sides[k] / sums[k]);
    /* What lies inside the border box: borders, then padding. */
    float in[4] = {0, 0, 0, 0};
    if (which != SK_BOX_BORDER)
        for (int k = 0; k < 4; k++)
            in[k] = b->bw[k] + (which == SK_BOX_CONTENT ? b->pad[k] : 0);
    sk_frect box = which == SK_BOX_BORDER ? bb : which == SK_BOX_PADDING ? b->padding : b->content;
    pt_rrect r;
    sk_frect d = pt_snap(m, box);
    r.x = d.x;
    r.y = d.y;
    r.w = d.w;
    r.h = d.h;
    float sx = fabsf(m->a), sy = fabsf(m->d);
    static const int HSIDE[4] = {SK_LEFT, SK_RIGHT, SK_RIGHT, SK_LEFT};
    static const int VSIDE[4] = {SK_TOP, SK_TOP, SK_BOTTOM, SK_BOTTOM};
    for (int k = 0; k < 4; k++) {
        r.rx[k] = fmaxf(0, rx[k] * f - in[HSIDE[k]]) * sx;
        r.ry[k] = fmaxf(0, ry[k] * f - in[VSIDE[k]]) * sy;
    }
    return r;
}

/* ---- Clips ---- */

typedef struct {
    sk_rect clip;
    uint8_t *mask;
} clip_state;

static clip_state clip_save(const canvas *cv)
{
    clip_state s = {cv->clip, cv->mask};
    return s;
}

static void clip_restore(canvas *cv, clip_state s)
{
    cv->clip = s.clip;
    cv->mask = s.mask;
}

/* The canvas's clip narrowed to rr: a rectangle while it is one, else
 * coverage too. */
static void clip_to(painter *P, canvas *cv, const pt_rrect *rr)
{
    sk_rect r = pt_round_out((sk_frect){rr->x, rr->y, rr->w, rr->h});
    sk_rect nc = sk_rect_intersect(cv->clip, r);
    bool square = pt_rrect_square(rr) && rr->x == floorf(rr->x) && rr->y == floorf(rr->y) &&
                  rr->w == floorf(rr->w) && rr->h == floorf(rr->h);
    if (sk_rect_empty(nc)) {
        cv->clip = sk_rect_make(nc.x, nc.y, 0, 0);
        cv->mask = NULL;
        return;
    }
    if (square && !cv->mask) {
        cv->clip = nc;
        return;
    }
    uint8_t *m = square ? NULL : pt_cover_rrect(rr, nc);
    if (!m) {
        m = sk_xmalloc((size_t)nc.w * (size_t)nc.h);
        memset(m, 255, (size_t)nc.w * (size_t)nc.h);
    }
    if (cv->mask)
        for (int y = 0; y < nc.h; y++)
            for (int x = 0; x < nc.w; x++) {
                uint8_t *o = &m[(size_t)y * (size_t)nc.w + (size_t)x];
                *o = (uint8_t)pt_mul(*o, cv->mask[(size_t)(y + nc.y - cv->clip.y) * (size_t)cv->clip.w +
                                                  (size_t)(x + nc.x - cv->clip.x)]);
            }
    sk_vec_push(&P->masks, m);
    cv->clip = nc;
    cv->mask = m;
}

static bool clips_overflow(const sk_css_style *s)
{
    return s->overflow_x != SK_OVERFLOW_VISIBLE || s->overflow_y != SK_OVERFLOW_VISIBLE;
}

/* Box i's overflow clip: its padding box, rounded; an svg's viewport. */
static void clip_box(painter *P, canvas *cv, uint32_t i)
{
    sk_xform m = pt_map(P, cv, i);
    if (!pt_square_map(&m)) {
        /* A turned clip: its bounds, as near as a rectangle comes. */
        sk_rect r = pt_round_out(sk_frect_map(&m, P->l->boxes[i].padding));
        cv->clip = sk_rect_intersect(cv->clip, r);
        return;
    }
    pt_rrect rr = pt_box_rrect(P, i, &m, P->nodes[i].kind == SK_EL_SVG ? SK_BOX_CONTENT : SK_BOX_PADDING);
    if (P->nodes[i].kind == SK_EL_SVG)
        memset(rr.rx, 0, sizeof rr.rx);
    clip_to(P, cv, &rr);
}

/* ---- Filters ---- */

static float clampf(float v)
{
    return v < 0 ? 0 : v > 1 ? 1 : v;
}

/* A colour matrix over every pixel, unpremultiplied as Skia applies it:
 * rows of r, g, b, a, each four factors and an offset. */
static void color_matrix(sk_image *im, const float m[20])
{
    for (int y = 0; y < im->h; y++) {
        sk_px *row = sk_image_row(im, y);
        for (int x = 0; x < im->w; x++) {
            sk_px p = row[x];
            uint32_t a8 = p >> 24;
            if (!a8 && m[19] <= 0)
                continue;
            float a = (float)a8 / 255;
            float r = a8 ? (float)((p >> 16) & 255) / (float)a8 : 0;
            float g = a8 ? (float)((p >> 8) & 255) / (float)a8 : 0;
            float b = a8 ? (float)(p & 255) / (float)a8 : 0;
            float o[4];
            for (int k = 0; k < 4; k++)
                o[k] = clampf(m[k * 5] * r + m[k * 5 + 1] * g + m[k * 5 + 2] * b + m[k * 5 + 3] * a + m[k * 5 + 4]);
            sk_css_color c = {o[0], o[1], o[2], 1};
            row[x] = pt_color(c, o[3]);
        }
    }
}

static void matrix_of(const sk_css_filter *f, float m[20])
{
    float v = f->amount;
    memset(m, 0, 20 * sizeof *m);
    m[0] = m[6] = m[12] = m[18] = 1;
    float s, c;
    double r;
    switch (f->kind) {
    case SK_FX_BRIGHTNESS:
        m[0] = m[6] = m[12] = v;
        break;
    case SK_FX_CONTRAST:
        m[0] = m[6] = m[12] = v;
        m[4] = m[9] = m[14] = (1 - v) / 2;
        break;
    case SK_FX_OPACITY:
        m[18] = clampf(v);
        break;
    case SK_FX_INVERT:
        v = clampf(v);
        m[0] = m[6] = m[12] = 1 - 2 * v;
        m[4] = m[9] = m[14] = v;
        break;
    case SK_FX_SATURATE:
    case SK_FX_GRAYSCALE:
        s = f->kind == SK_FX_SATURATE ? v : 1 - clampf(v);
        m[0] = 0.213f + 0.787f * s;
        m[1] = 0.715f - 0.715f * s;
        m[2] = 0.072f - 0.072f * s;
        m[5] = 0.213f - 0.213f * s;
        m[6] = 0.715f + 0.285f * s;
        m[7] = 0.072f - 0.072f * s;
        m[10] = 0.213f - 0.213f * s;
        m[11] = 0.715f - 0.715f * s;
        m[12] = 0.072f + 0.928f * s;
        break;
    case SK_FX_SEPIA:
        s = clampf(v);
        m[0] = 0.393f + 0.607f * (1 - s);
        m[1] = 0.769f - 0.769f * (1 - s);
        m[2] = 0.189f - 0.189f * (1 - s);
        m[5] = 0.349f - 0.349f * (1 - s);
        m[6] = 0.686f + 0.314f * (1 - s);
        m[7] = 0.168f - 0.168f * (1 - s);
        m[10] = 0.272f - 0.272f * (1 - s);
        m[11] = 0.534f - 0.534f * (1 - s);
        m[12] = 0.131f + 0.869f * (1 - s);
        break;
    case SK_FX_HUE_ROTATE:
        r = v * M_PI / 180;
        c = (float)cos(r);
        s = (float)sin(r);
        m[0] = 0.213f + c * 0.787f - s * 0.213f;
        m[1] = 0.715f - c * 0.715f - s * 0.715f;
        m[2] = 0.072f - c * 0.072f + s * 0.928f;
        m[5] = 0.213f - c * 0.213f + s * 0.143f;
        m[6] = 0.715f + c * 0.285f + s * 0.140f;
        m[7] = 0.072f - c * 0.072f - s * 0.283f;
        m[10] = 0.213f - c * 0.213f - s * 0.787f;
        m[11] = 0.715f - c * 0.715f + s * 0.715f;
        m[12] = 0.072f + c * 0.928f + s * 0.072f;
        break;
    default:
        break;
    }
}

/* The shadow of im's alpha under it: offset, blurred, in colour. */
static void drop_shadow(sk_image *im, const sk_css_shadow *sh, float k)
{
    sk_px color = pt_color(sh->color, 1);
    if (!color)
        return;
    int w = im->w, h = im->h;
    uint8_t *a = sk_xcalloc((size_t)w * (size_t)h, 1);
    int dx = (int)lroundf(sh->x * k), dy = (int)lroundf(sh->y * k);
    for (int y = 0; y < h; y++) {
        int sy = y - dy;
        if (sy < 0 || sy >= h)
            continue;
        const sk_px *row = sk_image_row(im, sy);
        for (int x = 0; x < w; x++) {
            int sx = x - dx;
            if (sx >= 0 && sx < w)
                a[(size_t)y * (size_t)w + (size_t)x] = (uint8_t)(row[sx] >> 24);
        }
    }
    sk_blur_a8(a, w, h, w, fmaxf(0, sh->blur) / 2 * k);
    for (int y = 0; y < h; y++) {
        sk_px *row = sk_image_row(im, y);
        for (int x = 0; x < w; x++) {
            uint8_t c = a[(size_t)y * (size_t)w + (size_t)x];
            sk_px s = c ? pt_scale(color, c) : 0;
            /* The element over its shadow. */
            row[x] = pt_over(row[x], s);
        }
    }
    free(a);
}

static void apply_filters(sk_image *im, const sk_css_style *s, float k)
{
    for (uint16_t i = 0; i < s->nfilter; i++) {
        const sk_css_filter *f = &s->filter[i];
        if (f->kind == SK_FX_BLUR) {
            sk_blur_image(im, fmaxf(0, f->amount) * k);
        } else if (f->kind == SK_FX_DROP_SHADOW) {
            drop_shadow(im, &f->shadow, k);
        } else {
            float m[20];
            matrix_of(f, m);
            color_matrix(im, m);
        }
    }
}

/* A filter that changes nothing: a transparent drop shadow, a factor
 * of 1, a blur of 0. */
static bool filter_idle(const sk_css_filter *f)
{
    switch (f->kind) {
    case SK_FX_DROP_SHADOW:
        return f->shadow.color.a <= 0;
    case SK_FX_BLUR:
    case SK_FX_HUE_ROTATE:
    case SK_FX_GRAYSCALE:
    case SK_FX_SEPIA:
    case SK_FX_INVERT:
        return f->amount == 0;
    default:
        return f->amount == 1;
    }
}

static bool filters_idle(const sk_css_style *s)
{
    for (uint16_t i = 0; i < s->nfilter; i++)
        if (!filter_idle(&s->filter[i]))
            return false;
    return true;
}

/* ---- Text ---- */

/* Coverage of a glyph mask at (x, y) added into a w by h buffer. */
static void add_mask(uint8_t *buf, int w, int h, int x, int y, const sk_mask *m)
{
    for (int j = 0; j < m->h; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= h)
            continue;
        for (int i = 0; i < m->w; i++) {
            int xx = x + i;
            if (xx < 0 || xx >= w)
                continue;
            uint8_t *o = &buf[(size_t)yy * (size_t)w + (size_t)xx];
            uint32_t c = m->a[(size_t)j * (size_t)m->stride + (size_t)i];
            *o = (uint8_t)(*o + c - pt_mul(*o, c));
        }
    }
}

typedef struct {
    const sk_glyph *g;
    int x, y;
} placed;

/* A run's glyphs drawn into coverage over r (a pixel rectangle), each
 * at its pen offset by (dx, dy) pixels. */
static uint8_t *run_cover(painter *P, const sk_lrun *run, const sk_xform *m, float dx, float dy, sk_rect r)
{
    uint8_t *buf = sk_xcalloc((size_t)r.w, (size_t)r.h);
    float px = run->px * sqrtf(fabsf(m->a * m->d - m->b * m->c));
    float by = m->b * run->x + m->d * run->y + m->f + dy;
    int base = (int)floorf(by + 0.5f);
    for (uint32_t k = 0; k < run->n; k++) {
        const sk_lglyph *g = &P->l->glyphs[run->first + k];
        float x = run->x + g->x;
        float gx = m->a * x + m->c * run->y + m->e + dx;
        float q = floorf(gx * 4 + 0.5f), whole = floorf(q / 4);
        sk_glyph_ref ref = {g->font, g->glyph, g->synth};
        const sk_glyph *gl = sk_glyph_get(P->env->glyphs, ref, px, (int)(q - whole * 4));
        if (gl->mask.w > 0)
            add_mask(buf, r.w, r.h, (int)whole + gl->left - r.x, base + gl->top - r.y, &gl->mask);
    }
    return buf;
}

/* The pixels a run may cover, under m, offset. */
static sk_rect run_rect(const sk_lrun *run, const sk_xform *m, float dx, float dy, int reach)
{
    float pad = run->px * 0.6f + 2;
    sk_frect f = {run->x - pad, run->y - run->ascent - pad, run->width + 2 * pad, run->ascent + run->descent + 2 * pad};
    sk_frect d = sk_frect_map(m, f);
    d.x += dx - (float)reach;
    d.y += dy - (float)reach;
    d.w += 2 * (float)reach;
    d.h += 2 * (float)reach;
    return pt_round_out(d);
}

/* The opacity of the inline boxes between a run's element and its
 * block, which group with nothing else on the line. */
static float inline_opacity(const painter *P, uint32_t node, uint32_t block)
{
    float a = 1;
    for (uint32_t n = node; n != SK_PACK_NONE && n != block; n = P->nodes[n].parent)
        a *= P->nodes[n].style->opacity;
    return a;
}

static void draw_run(painter *P, canvas *cv, const sk_lrun *run, const sk_xform *m, bool svg)
{
    const sk_css_style *s = P->nodes[run->node].style;
    if (s->visibility != SK_VIS_VISIBLE)
        return;
    float alpha = svg ? 1 : inline_opacity(P, run->node, run->block);
    sk_css_color color = s->color;
    if (svg) {
        if (s->fill_kind != SK_PAINT_COLOR)
            return;
        color = s->fill;
    }
    float k = sqrtf(fabsf(m->a * m->d - m->b * m->c));
    /* Shadows, the first on top. */
    for (int i = (int)s->ntext_shadow - 1; i >= 0; i--) {
        const sk_css_shadow *sh = &s->text_shadow[i];
        sk_px c = pt_color(sh->color, alpha);
        if (!c)
            continue;
        float sigma = fmaxf(0, sh->blur) / 2 * k;
        int reach = sk_blur_reach(sigma);
        float dx = sh->x * k, dy = sh->y * k;
        /* As much as can blur into the clip, no more. */
        sk_rect r = run_rect(run, m, dx, dy, reach);
        sk_rect vis = sk_rect_intersect(r, cv->clip);
        if (sk_rect_empty(vis))
            continue;
        r = sk_rect_intersect(r, sk_rect_make(cv->clip.x - reach, cv->clip.y - reach, cv->clip.w + 2 * reach,
                                              cv->clip.h + 2 * reach));
        uint8_t *cov = run_cover(P, run, m, dx, dy, r);
        sk_blur_a8(cov, r.w, r.h, r.w, sigma);
        pt_shader sh_ = {c, NULL, NULL};
        pt_fill(cv, r, cov, r.w, &sh_, SK_BLEND_SRC_OVER, 255);
        free(cov);
    }
    sk_px c = pt_color(color, alpha);
    if (!c)
        return;
    sk_rect r = sk_rect_intersect(run_rect(run, m, 0, 0, 0), cv->clip);
    if (sk_rect_empty(r))
        return;
    uint8_t *cov = run_cover(P, run, m, 0, 0, r);
    pt_shader sh_ = {c, NULL, NULL};
    pt_fill(cv, r, cov, r.w, &sh_, SK_BLEND_SRC_OVER, 255);
    free(cov);
    /* Decorations: a line under, over or through, in their colour. */
    if (run->deco && !svg) {
        sk_px dc = pt_color(run->deco_color, alpha);
        float t = fmaxf(1, roundf(run->px / 14 * k));
        float x0 = m->a * run->x + m->e, x1 = m->a * (run->x + run->width) + m->e;
        float base = m->d * run->y + m->f;
        float ys[3] = {base + fmaxf(1, roundf(run->descent * k / 3)), base - roundf(run->ascent * k) + t,
                       base - roundf(run->px * 0.3f * k)};
        static const uint8_t BITS[3] = {SK_DECO_UNDERLINE, SK_DECO_OVERLINE, SK_DECO_LINE_THROUGH};
        for (int i = 0; i < 3; i++)
            if (run->deco & BITS[i]) {
                sk_rect lr = pt_round_out((sk_frect){fminf(x0, x1), roundf(ys[i] - t / 2), fabsf(x1 - x0), t});
                pt_shader ds = {dc, NULL, NULL};
                pt_fill(cv, lr, NULL, 0, &ds, SK_BLEND_SRC_OVER, 255);
            }
    }
}

static void paint_runs(painter *P, canvas *cv, uint32_t i)
{
    const sk_lbox *b = &P->l->boxes[i];
    if (!b->nruns)
        return;
    bool svg = b->svg_content;
    /* A block's runs are in the skin area's px, an SVG text's in its
     * user units, under the box's mapping either way. */
    sk_xform m = pt_map(P, cv, i);
    for (uint32_t k = 0; k < b->nruns; k++)
        draw_run(P, cv, &P->l->runs[b->runs + k], &m, svg);
}

/* ---- SVG ---- */

static sk_cap cap_of(uint8_t c)
{
    return c == SK_LINECAP_ROUND ? SK_CAP_ROUND : c == SK_LINECAP_SQUARE ? SK_CAP_SQUARE : SK_CAP_BUTT;
}

static sk_join join_of(uint8_t j)
{
    return j == SK_LINEJOIN_ROUND ? SK_JOIN_ROUND : j == SK_LINEJOIN_BEVEL ? SK_JOIN_BEVEL : SK_JOIN_MITER;
}

/* A point of the arc at angle deg, clockwise from 12 o'clock. */
static void polar(const sk_pack_svg *g, double deg, float *x, float *y)
{
    double r = deg * M_PI / 180;
    *x = (float)(g->v[0] + g->v[2] * sin(r));
    *y = (float)(g->v[1] - g->v[2] * cos(r));
}

/* The arc from a0 to a1 degrees as cubics of at most 90 degrees. */
static void arc_path(sk_path *p, const sk_pack_svg *g, double a0, double a1)
{
    float x, y;
    polar(g, a0, &x, &y);
    sk_path_move(p, x, y);
    if (a1 <= a0) {
        sk_path_line(p, x, y);
        return;
    }
    int n = (int)ceil((a1 - a0) / 90);
    double step = (a1 - a0) / n, k = 4.0 / 3.0 * tan(step * M_PI / 180 / 4);
    for (int i = 0; i < n; i++) {
        double s = (a0 + step * i) * M_PI / 180, e = (a0 + step * (i + 1)) * M_PI / 180;
        double cx = g->v[0], cy = g->v[1], r = g->v[2];
        double x0 = cx + r * sin(s), y0 = cy - r * cos(s), x1 = cx + r * sin(e), y1 = cy - r * cos(e);
        /* The tangent at angle t runs (cos t, sin t), clockwise. */
        sk_path_cubic(p, (float)(x0 + k * r * cos(s)), (float)(y0 + k * r * sin(s)), (float)(x1 - k * r * cos(e)),
                      (float)(y1 - k * r * sin(e)), (float)x1, (float)y1);
    }
}

/* Dashes along [0, len) of a dash list in its own units scaled by
 * unit: the on intervals, each given to emit. */
static void dashes(const sk_css_style *s, double len, double unit, void (*emit)(void *, double, double), void *data)
{
    uint16_t n = s->ndasharray;
    double total = 0;
    double d[64];
    int nd = 0;
    for (int rep = 0; rep < (n & 1 ? 2 : 1); rep++)
        for (uint16_t i = 0; i < n && nd < 64; i++) {
            float v = sk_layout_len(&s->dasharray[i], 100);
            d[nd] = isnan(v) || v < 0 ? 0 : v * unit;
            total += d[nd++];
        }
    if (!(total > 0) || nd == 0) {
        emit(data, 0, len);
        return;
    }
    double at = 0;
    for (int i = 0, guard = 0; at < len && guard < 100000; i = (i + 1) % nd, guard++) {
        if (!(i & 1))
            emit(data, at, fmin(len, at + d[i]));
        at += d[i];
    }
}

typedef struct {
    sk_path *p;
    const sk_pack_svg *g;
    double a0, deg_per_len; /* an arc */
} dash_ctx;

static void emit_arc(void *data, double s, double e)
{
    dash_ctx *c = data;
    arc_path(c->p, c->g, c->a0 + s * c->deg_per_len, c->a0 + e * c->deg_per_len);
}

static void emit_line(void *data, double s, double e)
{
    dash_ctx *c = data;
    const float *v = c->g->v;
    double dx = v[2] - v[0], dy = v[3] - v[1], len = sqrt(dx * dx + dy * dy);
    double fs = len > 0 ? s / len : 0, fe = len > 0 ? e / len : 0;
    sk_path_move(c->p, (float)(v[0] + dx * fs), (float)(v[1] + dy * fs));
    sk_path_line(c->p, (float)(v[0] + dx * fe), (float)(v[1] + dy * fe));
}

/* A path's coverage filled under m, in colour. */
static void fill_path(painter *P, canvas *cv, const sk_path *p, const sk_xform *m, sk_frect user_bounds, sk_px c)
{
    (void)P;
    if (!c)
        return;
    sk_rect r = sk_rect_intersect(pt_round_out(sk_frect_map(m, user_bounds)), cv->clip);
    if (sk_rect_empty(r))
        return;
    uint8_t *cov = pt_cover(p, m, r, SK_FILL_NONZERO);
    pt_shader sh = {c, NULL, NULL};
    pt_fill(cv, r, cov, r.w, &sh, SK_BLEND_SRC_OVER, 255);
    free(cov);
}

static void svg_image(painter *P, canvas *cv, uint32_t i, const sk_xform *m, float alpha);

/* An SVG shape's fill and stroke, alpha its opacity when nothing else
 * groups it. */
static void svg_shape(painter *P, canvas *cv, uint32_t i, float alpha)
{
    const sk_css_style *s = P->nodes[i].style;
    const sk_pack_svg *g = P->nodes[i].svg;
    if (!g || s->visibility != SK_VIS_VISIBLE)
        return;
    sk_xform m = pt_map(P, cv, i);
    if (g->kind == SK_SVG_IMAGE) {
        svg_image(P, cv, i, &m, alpha);
        return;
    }
    if (g->kind != SK_SVG_ARC && g->kind != SK_SVG_LINE && g->kind != SK_SVG_CIRCLE)
        return;
    sk_path *p = sk_path_new();
    sk_frect bounds;
    double len = 0;
    if (g->kind == SK_SVG_ARC) {
        arc_path(p, g, g->v[3], g->v[4]);
        len = g->v[2] * fabs((double)g->v[4] - g->v[3]) * M_PI / 180;
        bounds = (sk_frect){g->v[0] - g->v[2], g->v[1] - g->v[2], 2 * g->v[2], 2 * g->v[2]};
    } else if (g->kind == SK_SVG_LINE) {
        sk_path_move(p, g->v[0], g->v[1]);
        sk_path_line(p, g->v[2], g->v[3]);
        double dx = g->v[2] - g->v[0], dy = g->v[3] - g->v[1];
        len = sqrt(dx * dx + dy * dy);
        bounds = (sk_frect){fminf(g->v[0], g->v[2]), fminf(g->v[1], g->v[3]), fabsf(g->v[2] - g->v[0]),
                            fabsf(g->v[3] - g->v[1])};
    } else {
        sk_path_ellipse(p, g->v[0] - g->v[2], g->v[1] - g->v[2], 2 * g->v[2], 2 * g->v[2]);
        bounds = (sk_frect){g->v[0] - g->v[2], g->v[1] - g->v[2], 2 * g->v[2], 2 * g->v[2]};
    }
    if (s->fill_kind == SK_PAINT_COLOR && g->kind != SK_SVG_LINE)
        fill_path(P, cv, p, &m, bounds, pt_color(s->fill, alpha));
    float sw = sk_layout_len(&s->stroke_width, 100);
    if (s->stroke_kind == SK_PAINT_COLOR && sw > 0) {
        sk_path *src = p;
        sk_path *dashed = NULL;
        if (s->ndasharray && (g->kind == SK_SVG_ARC || g->kind == SK_SVG_LINE)) {
            dashed = sk_path_new();
            /* pathLength 100: the dashes in hundredths of the length. */
            double unit = g->flag && len > 0 ? len / 100 : 1;
            dash_ctx dc = {dashed, g, g->v[3], len > 0 ? (g->v[4] - g->v[3]) / len : 0};
            dashes(s, len, unit, g->kind == SK_SVG_ARC ? emit_arc : emit_line, &dc);
            src = dashed;
        }
        sk_path *out = sk_path_new();
        sk_path_stroke(out, src, sw, cap_of(s->linecap), join_of(s->linejoin), 4);
        float pad = sw * 2 + 1;
        sk_frect sb = {bounds.x - pad, bounds.y - pad, bounds.w + 2 * pad, bounds.h + 2 * pad};
        fill_path(P, cv, out, &m, sb, pt_color(s->stroke, alpha));
        sk_path_free(out);
        if (dashed)
            sk_path_free(dashed);
    }
    sk_path_free(p);
}

/* ---- Painting order ---- */

static bool positioned(const sk_css_style *s)
{
    return s->position != SK_POS_STATIC;
}

static bool is_inline(const painter *P, uint32_t i)
{
    return P->nodes[i].style->display == SK_DISPLAY_INLINE && P->nodes[i].kind != SK_EL_SVG;
}

/* Whether box i starts a stacking context of its own. */
static bool makes_context(const painter *P, uint32_t i, uint32_t root)
{
    const sk_css_style *s = P->nodes[i].style;
    if (i == root)
        return true;
    if (is_inline(P, i))
        return false;
    if (positioned(s) && (!s->z_auto || s->position == SK_POS_FIXED))
        return true;
    uint32_t parent = P->nodes[i].parent;
    if (!s->z_auto && parent != SK_PACK_NONE &&
        (P->nodes[parent].style->display == SK_DISPLAY_FLEX || P->nodes[parent].style->display == SK_DISPLAY_GRID))
        return true;
    return s->opacity < 1 || s->ntransform || s->nfilter || s->nclip || s->mix_blend != SK_MIX_NORMAL || s->isolate;
}

typedef struct {
    uint32_t box;
    int32_t z;
    uint32_t order;
} entry;

typedef SK_VEC(entry) entries;

/* The boxes of root's stacking context painted apart from its flow:
 * contexts within it, and positioned boxes, in tree order. */
static void collect(const painter *P, uint32_t root, uint32_t x, entries *out)
{
    for (uint32_t c = P->nodes[x].first; c != SK_PACK_NONE; c = P->nodes[c].next) {
        const sk_lbox *b = &P->l->boxes[c];
        if (!b->generated || b->svg_content)
            continue;
        const sk_css_style *s = P->nodes[c].style;
        if (makes_context(P, c, root)) {
            entry e = {c, s->z_auto ? 0 : s->z_index, (uint32_t)out->len};
            sk_vec_push(out, e);
            continue;
        }
        if (positioned(s) && !is_inline(P, c)) {
            entry e = {c, 0, (uint32_t)out->len};
            sk_vec_push(out, e);
        }
        if (P->nodes[c].kind != SK_EL_SVG)
            collect(P, root, c, out);
    }
}

static int by_z(const void *a, const void *b)
{
    const entry *x = a, *y = b;
    if (x->z != y->z)
        return x->z < y->z ? -1 : 1;
    return x->order < y->order ? -1 : x->order > y->order;
}

static bool within(const painter *P, uint32_t c, uint32_t root)
{
    for (; c != SK_PACK_NONE; c = P->nodes[c].parent)
        if (c == root)
            return true;
    return false;
}

/* A box's own paint: shadows, background, inset shadows, border. */
static void paint_own(painter *P, canvas *cv, uint32_t i)
{
    if (P->nodes[i].style->visibility != SK_VIS_VISIBLE)
        return;
    pt_shadows(P, cv, i, false);
    pt_background(P, cv, i);
    pt_shadows(P, cv, i, true);
    pt_border(P, cv, i);
}

static void paint_context(painter *P, canvas *cv, uint32_t e, uint32_t root);
static void paint_svg_node(painter *P, canvas *cv, uint32_t i);

static void paint_flow(painter *P, canvas *cv, uint32_t x, uint32_t root)
{
    clip_state saved = clip_save(cv);
    if (clips_overflow(P->nodes[x].style))
        clip_box(P, cv, x);
    if (P->nodes[x].kind == SK_EL_SVG) {
        for (uint32_t c = P->nodes[x].first; c != SK_PACK_NONE; c = P->nodes[c].next)
            paint_svg_node(P, cv, c);
        clip_restore(cv, saved);
        return;
    }
    for (uint32_t c = P->nodes[x].first; c != SK_PACK_NONE; c = P->nodes[c].next) {
        const sk_lbox *b = &P->l->boxes[c];
        if (!b->generated || b->svg_content)
            continue;
        if (makes_context(P, c, root) || (positioned(P->nodes[c].style) && !is_inline(P, c)))
            continue;
        if (!is_inline(P, c))
            paint_own(P, cv, c);
        paint_flow(P, cv, c, root);
        if (!is_inline(P, c))
            pt_outline(P, cv, c);
    }
    paint_runs(P, cv, x);
    clip_restore(cv, saved);
}

/* An entry of root's context, under the clips between it and root. */
static void paint_entry(painter *P, canvas *cv, uint32_t root, uint32_t e)
{
    clip_state saved = clip_save(cv);
    uint32_t chain[64];
    int n = 0;
    for (uint32_t c = P->l->boxes[e].clipper; c != SK_PACK_NONE && n < 64 && within(P, c, root);
         c = P->l->boxes[c].clipper)
        chain[n++] = c;
    while (n > 0)
        clip_box(P, cv, chain[--n]);
    if (!sk_rect_empty(cv->clip)) {
        if (makes_context(P, e, root)) {
            paint_context(P, cv, e, root);
        } else {
            paint_own(P, cv, e);
            paint_flow(P, cv, e, root);
            pt_outline(P, cv, e);
        }
    }
    clip_restore(cv, saved);
}

/* A stacking context's paint, in Appendix E's order. */
static void paint_inner(painter *P, canvas *cv, uint32_t r)
{
    entries list = {0};
    collect(P, r, r, &list);
    if (list.len > 1)
        qsort(list.ptr, list.len, sizeof *list.ptr, by_z);
    paint_own(P, cv, r);
    size_t k = 0;
    for (; k < list.len && list.ptr[k].z < 0; k++)
        paint_entry(P, cv, r, list.ptr[k].box);
    paint_flow(P, cv, r, r);
    for (; k < list.len; k++)
        paint_entry(P, cv, r, list.ptr[k].box);
    pt_outline(P, cv, r);
    sk_vec_free(&list);
}

/* ---- Layers ---- */

/* Whether a subtree of SVG holds what a turned mapping cannot draw
 * directly: text, images. */
static bool svg_needs_upright(const painter *P, uint32_t i)
{
    if (P->nodes[i].kind == SK_EL_TEXT || P->nodes[i].kind == SK_EL_IMAGE)
        return true;
    for (uint32_t c = P->nodes[i].first; c != SK_PACK_NONE; c = P->nodes[c].next)
        if (P->l->boxes[c].generated && svg_needs_upright(P, c))
            return true;
    return false;
}

static void svg_inner(painter *P, canvas *cv, uint32_t i);

/* Draws box e's content, group effects applied: inner paints it. */
static void with_effects(painter *P, canvas *cv, uint32_t e, void (*inner)(painter *, canvas *, uint32_t))
{
    const sk_lbox *b = &P->l->boxes[e];
    const sk_css_style *s = P->nodes[e].style;
    if (!(s->opacity > 0))
        return;
    sk_xform m = pt_map(P, cv, e);
    bool turned = !pt_square_map(&m) && (!b->svg_content || svg_needs_upright(P, e));
    bool filtered = s->nfilter && !filters_idle(s);
    bool group = s->opacity < 1 || filtered || s->nclip || s->mix_blend != SK_MIX_NORMAL || turned;
    if (!group) {
        inner(P, cv, e);
        return;
    }
    uint32_t opacity = (uint32_t)lroundf(clampf(s->opacity) * 255);
    sk_blend blend = pt_mix(s->mix_blend);
    float k = sqrtf(fabsf(m.a * m.d - m.b * m.c));
    if (!(k > 0))
        return;
    /* The layer: upright at k pixels to the box's px when turned, else
     * the canvas's own pixels over what the box may paint. */
    sk_xform base, to_canvas;
    sk_rect at;
    if (turned) {
        sk_frect lf = b->ink;
        lf = (sk_frect){lf.x * k, lf.y * k, lf.w * k, lf.h * k};
        /* No more of it than the clip, mapped back upright, shows. */
        sk_xform minv;
        if (!sk_xform_invert(&m, &minv))
            return;
        sk_xform up = {k, 0, 0, k, 0, 0};
        up = sk_xform_mul(&up, &minv);
        sk_frect seen = sk_frect_map(&up, (sk_frect){(float)cv->clip.x, (float)cv->clip.y, (float)cv->clip.w,
                                                     (float)cv->clip.h});
        float pad = 3 * k * (b->ink.w > 0 ? 1 : 0) + (float)SK_MIN(sk_blur_reach(SK_BLUR_MAX_SIGMA), 64);
        seen = (sk_frect){seen.x - pad, seen.y - pad, seen.w + 2 * pad, seen.h + 2 * pad};
        at = pt_round_out(sk_frect_intersect(lf, seen));
        sk_xform inv;
        if (sk_rect_empty(at) || !sk_xform_invert(&b->xform, &inv) || (double)at.w * at.h > (double)(1 << 24))
            return;
        sk_xform st = {k, 0, 0, k, (float)-at.x, (float)-at.y};
        base = sk_xform_mul(&st, &inv);
        sk_xform un = {1 / k, 0, 0, 1 / k, (float)at.x / k, (float)at.y / k};
        to_canvas = sk_xform_mul(&m, &un);
    } else {
        sk_frect dev = sk_frect_map(&m, b->ink);
        /* What lies outside the clip yet blurs or casts a shadow into
         * it, each blur as far as its capped sigma spreads it. */
        int reach = 0;
        for (uint16_t f = 0; f < s->nfilter; f++) {
            const sk_css_filter *fx = &s->filter[f];
            if (fx->kind == SK_FX_BLUR)
                reach += sk_blur_reach(fmaxf(0, fx->amount) * k);
            else if (fx->kind == SK_FX_DROP_SHADOW)
                reach += sk_blur_reach(fmaxf(0, fx->shadow.blur) / 2 * k) +
                         (int)fminf(4096, ceilf((fabsf(fx->shadow.x) + fabsf(fx->shadow.y)) * k));
        }
        reach = SK_MIN(reach, 8192);
        sk_rect lim = cv->clip;
        lim = sk_rect_make(lim.x - reach - 1, lim.y - reach - 1, lim.w + 2 * reach + 2, lim.h + 2 * reach + 2);
        at = sk_rect_intersect(pt_round_out(dev), lim);
        if (sk_rect_empty(at))
            return;
        sk_xform t = {1, 0, 0, 1, (float)-at.x, (float)-at.y};
        base = sk_xform_mul(&t, &cv->base);
        to_canvas = (sk_xform){1, 0, 0, 1, (float)at.x, (float)at.y};
    }
    sk_image im;
    sk_image_init(&im, at.w, at.h);
    canvas lc = {&im, base, sk_image_bounds(&im), NULL};
    inner(P, &lc, e);
    if (filtered)
        apply_filters(&im, s, k);
    if (s->nclip) {
        /* clip-path: the polygon in the border box. */
        sk_path *p = sk_path_new();
        sk_frect bb = b->border;
        for (uint16_t q = 0; q < s->nclip; q++) {
            float x = sk_layout_len(&s->clip[2 * q], bb.w), y = sk_layout_len(&s->clip[2 * q + 1], bb.h);
            x = (isnan(x) ? 0 : x) + bb.x;
            y = (isnan(y) ? 0 : y) + bb.y;
            if (q == 0)
                sk_path_move(p, x, y);
            else
                sk_path_line(p, x, y);
        }
        sk_path_close(p);
        sk_xform pm = sk_xform_mul(&base, &b->xform);
        uint8_t *cov = pt_cover(p, &pm, sk_image_bounds(&im), s->clip_evenodd ? SK_FILL_EVENODD : SK_FILL_NONZERO);
        for (int y = 0; y < im.h; y++) {
            sk_px *row = sk_image_row(&im, y);
            for (int x = 0; x < im.w; x++)
                row[x] = pt_scale(row[x], cov[(size_t)y * (size_t)im.w + (size_t)x]);
        }
        free(cov);
        sk_path_free(p);
    }
    if (turned)
        pt_blit_affine(cv, &im, &to_canvas, blend, opacity);
    else
        pt_fill_image(cv, at.x, at.y, &im, NULL, 0, blend, opacity);
    sk_image_free(&im);
}

static void paint_context(painter *P, canvas *cv, uint32_t e, uint32_t root)
{
    (void)root;
    with_effects(P, cv, e, paint_inner);
}

/* An SVG element and its children, in document order. */
static void svg_inner(painter *P, canvas *cv, uint32_t i)
{
    svg_shape(P, cv, i, 1);
    paint_runs(P, cv, i);
    for (uint32_t c = P->nodes[i].first; c != SK_PACK_NONE; c = P->nodes[c].next)
        paint_svg_node(P, cv, c);
}

static void paint_svg_node(painter *P, canvas *cv, uint32_t i)
{
    const sk_lbox *b = &P->l->boxes[i];
    if (!b->generated)
        return;
    const sk_css_style *s = P->nodes[i].style;
    /* A lone shape's opacity goes to its paint; anything more groups. */
    bool one_paint = !P->nodes[i].first && !(s->fill_kind == SK_PAINT_COLOR && s->stroke_kind == SK_PAINT_COLOR &&
                                              P->nodes[i].kind != SK_EL_LINE);
    sk_xform m = pt_map(P, cv, i);
    bool plain = (!s->nfilter || filters_idle(s)) && !s->nclip && s->mix_blend == SK_MIX_NORMAL &&
                 (pt_square_map(&m) || !svg_needs_upright(P, i));
    if (plain && s->opacity < 1 && one_paint && P->nodes[i].kind != SK_EL_TEXT) {
        if (s->opacity > 0)
            svg_shape(P, cv, i, s->opacity);
        return;
    }
    with_effects(P, cv, i, svg_inner);
}

static void svg_image(painter *P, canvas *cv, uint32_t i, const sk_xform *m, float alpha)
{
    const sk_pack_svg *g = P->nodes[i].svg;
    const sk_pimages *imgs = P->env->images;
    if (!imgs || !imgs->size || g->image == SK_PACK_NONE)
        return;
    float iw, ih;
    if (!imgs->size(imgs->data, g->image, &iw, &ih) || !(iw > 0) || !(ih > 0))
        return;
    sk_frect r = {g->v[0], g->v[1], g->v[2], g->v[3]};
    if (g->flag != 1) {
        /* xMidYMid meet of the image's own ratio. */
        float s = fminf(r.w / iw, r.h / ih);
        float w = iw * s, h = ih * s;
        r = (sk_frect){r.x + (r.w - w) / 2, r.y + (r.h - h) / 2, w, h};
    }
    uint32_t op = (uint32_t)lroundf(clampf(alpha) * 255);
    bool pix = P->nodes[i].style->image_rendering == SK_RENDER_PIXELATED;
    if (pt_square_map(m)) {
        sk_frect d = sk_frect_map(m, r);
        sk_rect dst = sk_rect_make((int)lroundf(d.x), (int)lroundf(d.y), (int)lroundf(d.x + d.w) - (int)lroundf(d.x),
                                   (int)lroundf(d.y + d.h) - (int)lroundf(d.y));
        pt_image(P, cv, g->image, dst, NULL, dst, pix, SK_BLEND_SRC_OVER, op);
        return;
    }
    /* Turned with no layer about it: resampled from its own size. */
    float k = sqrtf(fabsf(m->a * m->d - m->b * m->c));
    int w = (int)ceilf(r.w * k), h = (int)ceilf(r.h * k);
    if (w <= 0 || h <= 0 || imgs->pixels == NULL)
        return;
    const sk_image *px = imgs->pixels(imgs->data, g->image, w, h);
    if (!px)
        return;
    sk_image tmp;
    sk_image_init(&tmp, w, h);
    sk_blit_scaled(&tmp, sk_image_bounds(&tmp), px, sk_image_bounds(px), SK_FILTER_BILINEAR, SK_BLEND_SRC, 255, NULL);
    sk_xform to = {r.w / (float)w, 0, 0, r.h / (float)h, r.x, r.y};
    sk_xform mm = sk_xform_mul(m, &to);
    pt_blit_affine(cv, &tmp, &mm, SK_BLEND_SRC_OVER, op);
    sk_image_free(&tmp);
}

/* ---- The paint ---- */

void sk_paint(sk_image *dst, const sk_paint_env *env, uint32_t root, float scale, float ox, float oy)
{
    const sk_layout *l = env->layout;
    if (root >= l->n || !l->boxes[root].generated || !(scale > 0))
        return;
    painter P;
    memset(&P, 0, sizeof P);
    P.env = env;
    P.l = l;
    P.nodes = l->nodes;
    canvas cv = {dst, {scale, 0, 0, scale, -ox * scale, -oy * scale}, sk_image_bounds(dst), NULL};
    if (l->boxes[root].svg_content)
        paint_svg_node(&P, &cv, root);
    else
        with_effects(&P, &cv, root, paint_inner);
    for (size_t i = 0; i < P.masks.len; i++)
        free(P.masks.ptr[i]);
    sk_vec_free(&P.masks);
}
