/* The visualizer's Canvas2D (canvas.h). Paths become lines in pixels as
 * they are built and go to gfx's rasterizer and stroker. A fill that
 * gfx draws as it is, a colour source-over with no clip mask, goes
 * there whole; anything else is rasterized to a coverage mask over its
 * box and painted here, pixel by pixel, with gfx's arithmetic. */
#include "canvas.h"

#include "../base/utf8.h"

#include <math.h>
#include <string.h>

/* How far a chord may stray from its curve or arc, in pixels. */
#define TOLERANCE 0.2
#define MAX_STEPS 1024

/* ---- Pixels: draw.h's arithmetic ---- */

static inline uint32_t mul8(uint32_t x, uint32_t y)
{
    uint32_t t = x * y + 128;
    return (t + (t >> 8)) >> 8;
}

static inline sk_px scale_px(sk_px p, uint32_t k)
{
    uint32_t rb = (p & 0x00ff00ffu) * k + 0x00800080u;
    uint32_t ag = ((p >> 8) & 0x00ff00ffu) * k + 0x00800080u;
    rb = ((rb + ((rb >> 8) & 0x00ff00ffu)) >> 8) & 0x00ff00ffu;
    ag = (ag + ((ag >> 8) & 0x00ff00ffu)) & 0xff00ff00u;
    return rb | ag;
}

static inline sk_px over_px(sk_px s, sk_px d)
{
    return s + scale_px(d, 255 - (s >> 24));
}

static inline sk_px add_px(sk_px s, sk_px d)
{
    uint32_t rb = (s & 0x00ff00ffu) + (d & 0x00ff00ffu);
    uint32_t ag = ((s >> 8) & 0x00ff00ffu) + ((d >> 8) & 0x00ff00ffu);
    /* A lane past 255 saturates. */
    rb |= ((rb & 0x01000100u) >> 8) * 255;
    ag |= ((ag & 0x01000100u) >> 8) * 255;
    return (rb & 0x00ff00ffu) | ((ag & 0x00ff00ffu) << 8);
}

static inline sk_px blend(sk_blend op, sk_px s, sk_px d)
{
    return op == SK_BLEND_ADD ? add_px(s, d) : over_px(s, d);
}

/* ---- Colours ---- */

static uint32_t byte(double v)
{
    return (uint32_t)floor(sk_vm_clamp(v, 0, 255) + 0.5);
}

sk_color sk_vc_rgba(double r, double g, double b, double a)
{
    return SK_RGBA(byte(r), byte(g), byte(b), byte(sk_vm_clamp(a, 0, 1) * 255));
}

void sk_vc_hsl_rgb(double h, double s, double l, double out[3])
{
    h = fmod(fmod(h, 360) + 360, 360);
    if (isnan(h))
        h = 0;
    static const int ns[3] = {0, 8, 4};
    for (int i = 0; i < 3; i++) {
        double k = fmod(ns[i] + h / 30, 12);
        double m = SK_MIN(SK_MIN(k - 3, 9 - k), 1.0);
        out[i] = l - s * SK_MIN(l, 1 - l) * SK_MAX(-1.0, m);
    }
}

sk_color sk_vc_hsla(double h, double s, double l, double a)
{
    double c[3];
    sk_vc_hsl_rgb(h, sk_vm_clamp(s / 100, 0, 1), sk_vm_clamp(l / 100, 0, 1), c);
    return sk_vc_rgba(c[0] * 255, c[1] * 255, c[2] * 255, a);
}

/* ---- Paints ---- */

sk_vc_paint sk_vc_linear(double x0, double y0, double x1, double y1)
{
    sk_vc_paint p = {0};
    p.kind = SK_VC_LINEAR;
    p.x0 = x0;
    p.y0 = y0;
    p.x1 = x1;
    p.y1 = y1;
    return p;
}

sk_vc_paint sk_vc_radial(double x0, double y0, double r0, double x1, double y1, double r1)
{
    sk_vc_paint p = {0};
    p.kind = SK_VC_RADIAL;
    p.x0 = x0;
    p.y0 = y0;
    p.r0 = r0;
    p.x1 = x1;
    p.y1 = y1;
    p.r1 = r1;
    return p;
}

void sk_vc_add_stop(sk_vc_paint *p, double off, sk_color c)
{
    if (p->n_stops >= SK_VC_STOPS)
        return;
    p->off[p->n_stops] = sk_vm_clamp(off, 0, 1);
    p->stop[p->n_stops++] = c;
}

/* A gradient's colours at 256 steps of t, premultiplied, its stops
 * interpolated unpremultiplied. */
static void gradient_lut(const sk_vc_paint *p, sk_px lut[256])
{
    for (int i = 0; i < 256; i++) {
        double t = i / 255.0;
        if (!p->n_stops) {
            lut[i] = 0;
            continue;
        }
        int k = 0;
        while (k + 1 < p->n_stops && p->off[k + 1] < t)
            k++;
        sk_color a = p->stop[k], b = p->stop[SK_MIN(k + 1, p->n_stops - 1)];
        double span = k + 1 < p->n_stops ? p->off[k + 1] - p->off[k] : 0;
        double f = t <= p->off[0] ? 0 : span > 0 ? sk_vm_clamp((t - p->off[k]) / span, 0, 1) : 1;
        if (k + 1 >= p->n_stops || t <= p->off[0])
            b = a;
        uint32_t out = 0;
        for (int sh = 0; sh < 32; sh += 8) {
            double va = (a >> sh) & 255, vb = (b >> sh) & 255;
            out |= byte(va + (vb - va) * f) << sh;
        }
        lut[i] = sk_premul(out);
    }
}

/* The inverse of m, false when it has none. */
static bool invert(const sk_vc_xform *m, sk_vc_xform *inv)
{
    double det = m->a * m->d - m->b * m->c;
    if (!(fabs(det) > 1e-12))
        return false;
    inv->a = m->d / det;
    inv->b = -m->b / det;
    inv->c = -m->c / det;
    inv->d = m->a / det;
    inv->e = -(inv->a * m->e + inv->c * m->f);
    inv->f = -(inv->b * m->e + inv->d * m->f);
    return true;
}

/* A paint set up for one fill, in pixels: a colour, or a gradient's
 * LUT and how t follows the pixel. A linear gradient's t is linear in
 * the pixel, so it is stepped along a row. A radial one with one centre
 * under a transform that keeps circles round is a distance from a point
 * in pixels, its colour flat inside the radius where the LUT still
 * holds its first entry. Any other radial gradient solves canvas's two
 * circles per pixel. */
enum { SHADE_SOLID, SHADE_LINEAR, SHADE_ROUND, SHADE_CONE };

typedef struct {
    int kind;
    bool ok;
    sk_px solid;
    sk_px lut[256];
    /* linear: t = a x + b y + c at a pixel's centre */
    double a, b, c;
    /* round: the centre, the inner radius and 255 / the span, the
     * squared radius within which the colour is lut[0] and past which
     * lut[255], in pixels */
    double cx, cy, r0, k, flat2, out2;
    /* cone: the points of each pixel, and the quadratic's constants */
    const sk_vc_paint *p;
    sk_vc_xform inv;
    double cdx, cdy, dr, qa;
} shader;

static void shader_init(shader *sh, const sk_vc_paint *p, const sk_vc_xform *m)
{
    memset(sh, 0, sizeof *sh);
    sh->p = p;
    sh->ok = true;
    if (p->kind == SK_VC_SOLID) {
        sh->kind = SHADE_SOLID;
        sh->solid = sk_premul(p->color);
        return;
    }
    gradient_lut(p, sh->lut);
    if (!invert(m, &sh->inv)) {
        sh->ok = false;
        return;
    }
    double cdx = p->x1 - p->x0, cdy = p->y1 - p->y0, dr = p->r1 - p->r0;
    if (p->kind == SK_VC_LINEAR) {
        double len2 = cdx * cdx + cdy * cdy;
        if (!(len2 > 0)) {
            sh->ok = false;
            return;
        }
        sh->kind = SHADE_LINEAR;
        const sk_vc_xform *v = &sh->inv;
        sh->a = (v->a * cdx + v->b * cdy) / len2;
        sh->b = (v->c * cdx + v->d * cdy) / len2;
        sh->c = ((v->e - p->x0) * cdx + (v->f - p->y0) * cdy) / len2;
        return;
    }
    bool round = m->a == m->d && m->b == -m->c;
    if (cdx == 0 && cdy == 0 && round) {
        /* Two circles about one centre, one inside the other. */
        if (!(dr != 0) || p->r0 < 0 || p->r1 < 0) {
            sh->ok = false;
            return;
        }
        double k = sqrt(fabs(m->a * m->d - m->b * m->c));
        sh->kind = SHADE_ROUND;
        sh->cx = m->a * p->x0 + m->c * p->y0 + m->e;
        sh->cy = m->b * p->x0 + m->d * p->y0 + m->f;
        double r0 = p->r0 * k, r1 = p->r1 * k;
        sh->r0 = r0;
        sh->k = 255 / (r1 - r0);
        /* t below j's half step rounds to entry j: entries 0..j alike are
         * flat. With r1 < r0, t falls outward and nothing is flat. */
        int j = 0;
        while (j < 255 && sh->lut[j + 1] == sh->lut[0])
            j++;
        double flat = r1 > r0 ? r0 + (j + 0.5) / 255 * (r1 - r0) : 0;
        sh->flat2 = flat > 0 ? flat * flat : -1;
        sh->out2 = r1 > r0 ? r1 * r1 : INFINITY;
        return;
    }
    sh->kind = SHADE_CONE;
    sh->cdx = cdx;
    sh->cdy = cdy;
    sh->dr = dr;
    sh->qa = cdx * cdx + cdy * cdy - dr * dr;
}

/* A two-circle gradient's colour at pixel (x, y), or 0 where it paints
 * nothing: the largest w with |q - c(w)| = r(w) >= 0, c(w) = c0 + w (c1
 * - c0), r(w) = r0 + w (r1 - r0), from a w^2 - 2 b w + c = 0. */
static sk_px shade_cone(const shader *sh, int x, int y)
{
    const sk_vc_paint *p = sh->p;
    double px = x + 0.5, py = y + 0.5;
    double qx = sh->inv.a * px + sh->inv.c * py + sh->inv.e - p->x0;
    double qy = sh->inv.b * px + sh->inv.d * py + sh->inv.f - p->y0;
    double b = qx * sh->cdx + qy * sh->cdy + p->r0 * sh->dr;
    double c = qx * qx + qy * qy - p->r0 * p->r0;
    double t;
    if (fabs(sh->qa) < 1e-12) {
        if (fabs(b) < 1e-12)
            return 0;
        t = c / (2 * b);
        if (p->r0 + t * sh->dr < 0)
            return 0;
    } else {
        double disc = b * b - sh->qa * c;
        if (disc < 0)
            return 0;
        double s = sqrt(disc);
        double t1 = (b + s) / sh->qa, t2 = (b - s) / sh->qa;
        if (t1 < t2) {
            double tmp = t1;
            t1 = t2;
            t2 = tmp;
        }
        if (p->r0 + t1 * sh->dr >= 0)
            t = t1;
        else if (p->r0 + t2 * sh->dr >= 0)
            t = t2;
        else
            return 0;
    }
    return sh->lut[(int)floor(sk_vm_clamp(t, 0, 1) * 255 + 0.5)];
}

/* A row of a paint's colours: n pixels from (x, y) into out. */
/* The most pixels a row is shaded in at once. */
#define SHADE_RUN 256

/* A run of LUT indices, t in 255ths rounded, from t at the first pixel
 * and its step: in floats with no branch, so the loop vectorizes. */
static void linear_index(float t, float step, int n, int32_t *idx)
{
    for (int i = 0; i < n; i++) {
        float v = (t + step * (float)i) * 255.0f + 0.5f;
        v = v < 0 ? 0 : v > 255 ? 255 : v;
        idx[i] = (int32_t)v;
    }
}

/* The same for a round gradient: t from the distance to the centre. */
static void round_index(float dx, float dy2, float r0, float k, int n, int32_t *idx)
{
    for (int i = 0; i < n; i++) {
        float x = dx + (float)i;
        float v = (sqrtf(x * x + dy2) - r0) * k + 0.5f;
        v = v < 0 ? 0 : v > 255 ? 255 : v;
        idx[i] = (int32_t)v;
    }
}

static void shade_row(const shader *sh, int x, int y, int n, sk_px *out)
{
    int32_t idx[SHADE_RUN];
    switch (sh->kind) {
    case SHADE_SOLID:
        for (int i = 0; i < n; i++)
            out[i] = sh->solid;
        break;
    case SHADE_LINEAR:
        for (int at = 0; at < n; at += SHADE_RUN) {
            int m = SK_MIN(SHADE_RUN, n - at);
            double t = sh->a * (x + at + 0.5) + sh->b * (y + 0.5) + sh->c;
            linear_index((float)t, (float)sh->a, m, idx);
            for (int i = 0; i < m; i++)
                out[at + i] = sh->lut[idx[i]];
        }
        break;
    case SHADE_ROUND: {
        double dy = y + 0.5 - sh->cy;
        for (int at = 0; at < n; at += SHADE_RUN) {
            int m = SK_MIN(SHADE_RUN, n - at);
            round_index((float)(x + at + 0.5 - sh->cx), (float)(dy * dy), (float)sh->r0, (float)sh->k, m, idx);
            for (int i = 0; i < m; i++)
                out[at + i] = sh->lut[idx[i]];
        }
        break;
    }
    default:
        for (int i = 0; i < n; i++)
            out[i] = shade_cone(sh, x + i, y);
        break;
    }
}

/* ---- The canvas ---- */

static void reset_state(sk_vc *vc)
{
    if (vc->st.own_mask)
        free(vc->st.mask);
    while (vc->depth > 0) {
        sk_vc_state *s = &vc->saved[--vc->depth];
        if (s->own_mask)
            free(s->mask);
    }
    memset(&vc->st, 0, sizeof vc->st);
    vc->st.m = (sk_vc_xform){vc->scale, 0, 0, vc->scale, 0, 0};
    vc->st.alpha = 1;
    vc->st.op = SK_BLEND_SRC_OVER;
    vc->st.line_width = 1;
    vc->st.cap = SK_CAP_BUTT;
    vc->st.join = SK_JOIN_MITER;
    vc->st.miter = 10;
    vc->st.clip = vc->im ? sk_image_bounds(vc->im) : sk_rect_make(0, 0, 0, 0);
    vc->st.font_px = 10;
}

static void path_free(sk_vc_path *p);

void sk_vc_init(sk_vc *vc, sk_image *im, double scale)
{
    memset(vc, 0, sizeof *vc);
    vc->path = sk_path_new();
    vc->outline = sk_path_new();
    sk_vc_target(vc, im, scale);
}

void sk_vc_target(sk_vc *vc, sk_image *im, double scale)
{
    vc->im = im;
    vc->scale = scale > 0 ? scale : 1;
    reset_state(vc);
    sk_vc_begin(vc);
}

void sk_vc_free(sk_vc *vc)
{
    vc->im = NULL;
    reset_state(vc);
    sk_path_free(vc->path);
    sk_path_free(vc->outline);
    path_free(&vc->p);
    path_free(&vc->aside);
    sk_vec_free(&vc->squares);
    free(vc->sum);
    sk_vec_free(&vc->cover);
    if (vc->glyphs)
        sk_glyph_cache_free(vc->glyphs);
    if (vc->fonts)
        sk_fontset_free(vc->fonts);
    memset(vc, 0, sizeof *vc);
}

void sk_vc_save(sk_vc *vc)
{
    if (vc->depth >= SK_VC_DEPTH)
        sk_die("visualizer canvas: save nested past %d", SK_VC_DEPTH);
    vc->saved[vc->depth++] = vc->st;
    /* The saved state frees the mask; this one shares it. */
    vc->st.own_mask = false;
}

void sk_vc_restore(sk_vc *vc)
{
    if (!vc->depth)
        return;
    if (vc->st.own_mask)
        free(vc->st.mask);
    vc->st = vc->saved[--vc->depth];
}

void sk_vc_set_transform(sk_vc *vc, double a, double b, double c, double d, double e, double f)
{
    double k = vc->scale;
    vc->st.m = (sk_vc_xform){a * k, b * k, c * k, d * k, e * k, f * k};
}

void sk_vc_reset_transform(sk_vc *vc)
{
    sk_vc_set_transform(vc, 1, 0, 0, 1, 0, 0);
}

void sk_vc_translate(sk_vc *vc, double x, double y)
{
    sk_vc_xform *m = &vc->st.m;
    m->e += m->a * x + m->c * y;
    m->f += m->b * x + m->d * y;
}

void sk_vc_scale(sk_vc *vc, double sx, double sy)
{
    sk_vc_xform *m = &vc->st.m;
    m->a *= sx;
    m->b *= sx;
    m->c *= sy;
    m->d *= sy;
}

void sk_vc_rotate(sk_vc *vc, double angle)
{
    sk_vc_xform *m = &vc->st.m;
    double s, c;
    sk_vm_sincos(angle, &s, &c);
    double a = m->a * c + m->c * s, b = m->b * c + m->d * s;
    double cc = m->c * c - m->a * s, d = m->d * c - m->b * s;
    m->a = a;
    m->b = b;
    m->c = cc;
    m->d = d;
}

void sk_vc_apply(const sk_vc *vc, double x, double y, double *px, double *py)
{
    const sk_vc_xform *m = &vc->st.m;
    *px = m->a * x + m->c * y + m->e;
    *py = m->b * x + m->d * y + m->f;
}

/* The transform's scale for lengths: the square root of its area's. */
static double xscale(const sk_vc_xform *m)
{
    return sqrt(fabs(m->a * m->d - m->b * m->c));
}

void sk_vc_dash(sk_vc *vc, const double *d, int n)
{
    vc->st.n_dash = 0;
    if (n != 2 && n != 4)
        return;
    double sum = 0;
    for (int i = 0; i < n; i++) {
        if (!(d[i] >= 0))
            return;
        sum += d[i];
    }
    if (!(sum > 0))
        return;
    for (int i = 0; i < n; i++)
        vc->st.dash[i] = d[i];
    vc->st.n_dash = n;
}

/* ---- Paths ---- */

static void path_reset(sk_vc_path *p)
{
    p->pts.len = p->subs.len = 0;
    p->open = false;
    p->minx = p->miny = INFINITY;
    p->maxx = p->maxy = -INFINITY;
}

static void path_free(sk_vc_path *p)
{
    sk_vec_free(&p->pts);
    sk_vec_free(&p->subs);
}

void sk_vc_begin(sk_vc *vc)
{
    path_reset(&vc->p);
}

static void put(sk_vc_path *p, double x, double y)
{
    sk_vc_pt q = {(float)x, (float)y};
    sk_vec_push(&p->pts, q);
    p->subs.ptr[p->subs.len - 1].n++;
    if (isfinite(q.x) && isfinite(q.y)) {
        p->minx = SK_MIN(p->minx, q.x);
        p->maxx = SK_MAX(p->maxx, q.x);
        p->miny = SK_MIN(p->miny, q.y);
        p->maxy = SK_MAX(p->maxy, q.y);
    }
}

/* A subpath from the pixel (x, y). */
static void start(sk_vc_path *p, double x, double y)
{
    sk_vc_sub s = {p->pts.len, 0, false};
    sk_vec_push(&p->subs, s);
    put(p, x, y);
    p->open = true;
}

/* A line to the pixel (x, y), or the start of a subpath there. */
static void to(sk_vc_path *p, double x, double y)
{
    if (!p->open)
        start(p, x, y);
    else
        put(p, x, y);
}

void sk_vc_move(sk_vc *vc, double x, double y)
{
    double px, py;
    sk_vc_apply(vc, x, y, &px, &py);
    start(&vc->p, px, py);
}

void sk_vc_line(sk_vc *vc, double x, double y)
{
    double px, py;
    sk_vc_apply(vc, x, y, &px, &py);
    to(&vc->p, px, py);
}

static int steps(double dd, double k)
{
    double n = ceil(sqrt(dd * k / TOLERANCE));
    if (!(n <= MAX_STEPS))
        return MAX_STEPS;
    return n < 1 ? 1 : (int)n;
}

void sk_vc_quad(sk_vc *vc, double cx, double cy, double x, double y)
{
    double x1, y1, x2, y2;
    sk_vc_apply(vc, cx, cy, &x1, &y1);
    sk_vc_apply(vc, x, y, &x2, &y2);
    sk_vc_path *p = &vc->p;
    if (!p->open)
        start(p, x1, y1);
    sk_vc_pt p0 = p->pts.ptr[p->pts.len - 1];
    double x0 = p0.x, y0 = p0.y;
    double ax = x0 - 2 * x1 + x2, ay = y0 - 2 * y1 + y2;
    int n = steps(sqrt(ax * ax + ay * ay), 0.25);
    for (int i = 1; i <= n; i++) {
        double t = (double)i / n, u = 1 - t;
        put(p, u * u * x0 + 2 * u * t * x1 + t * t * x2, u * u * y0 + 2 * u * t * y1 + t * t * y2);
    }
}

void sk_vc_cubic(sk_vc *vc, double c1x, double c1y, double c2x, double c2y, double x, double y)
{
    double x1, y1, x2, y2, x3, y3;
    sk_vc_apply(vc, c1x, c1y, &x1, &y1);
    sk_vc_apply(vc, c2x, c2y, &x2, &y2);
    sk_vc_apply(vc, x, y, &x3, &y3);
    sk_vc_path *p = &vc->p;
    if (!p->open)
        start(p, x1, y1);
    sk_vc_pt p0 = p->pts.ptr[p->pts.len - 1];
    double x0 = p0.x, y0 = p0.y;
    double ax = x0 - 2 * x1 + x2, ay = y0 - 2 * y1 + y2, bx = x1 - 2 * x2 + x3, by = y1 - 2 * y2 + y3;
    int n = steps(SK_MAX(sqrt(ax * ax + ay * ay), sqrt(bx * bx + by * by)), 0.75);
    for (int i = 1; i <= n; i++) {
        double t = (double)i / n, u = 1 - t;
        double a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;
        put(p, a * x0 + b * x1 + c * x2 + d * x3, a * y0 + b * y1 + c * y2 + d * y3);
    }
}

void sk_vc_close(sk_vc *vc)
{
    sk_vc_path *p = &vc->p;
    if (!p->open)
        return;
    sk_vc_sub *s = &p->subs.ptr[p->subs.len - 1];
    s->closed = true;
    sk_vc_pt q = p->pts.ptr[s->start];
    p->open = false;
    /* The next subpath starts where this one did. */
    start(p, q.x, q.y);
}

/* An arc's sweep, by canvas's rule. */
static double sweep_of(double a0, double a1, bool ccw)
{
    if (!ccw && a1 - a0 >= SK_TAU)
        return SK_TAU;
    if (ccw && a0 - a1 >= SK_TAU)
        return -SK_TAU;
    double d = fmod(a1 - a0, SK_TAU);
    if (d < 0)
        d += SK_TAU;
    if (!ccw)
        return d;
    return d == 0 ? 0 : d - SK_TAU;
}

void sk_vc_ellipse(sk_vc *vc, double x, double y, double rx, double ry, double rot, double a0, double a1,
                   bool ccw)
{
    if (!(rx >= 0 && ry >= 0) || !isfinite(a0) || !isfinite(a1) || !isfinite(x) || !isfinite(y))
        return;
    double sweep = sweep_of(a0, a1, ccw);
    double r = SK_MAX(rx, ry) * xscale(&vc->st.m);
    /* A chord over angle h strays r (1 - cos(h / 2)), about r h^2 / 8,
     * half the curves' tolerance. */
    double h = r > TOLERANCE ? 2 * sqrt(TOLERANCE / r) : SK_PI / 2;
    int n = (int)SK_MIN(ceil(fabs(sweep) / h), MAX_STEPS);
    n = SK_MAX(n, sweep != 0 ? 4 : 0);
    /* The points a little out, sqrt(h / sin h), so each chord's
     * triangle has its sector's area and a disc fills as much as the
     * circle would. */
    if (n) {
        double step = fabs(sweep) / n, k = step > 1e-9 ? sqrt(step / sk_vm_sin(step)) : 1;
        rx *= k;
        ry *= k;
    }
    double sr = 0, cr = 1;
    if (rot != 0)
        sk_vm_sincos(rot, &sr, &cr);
    for (int i = 0; i <= n; i++) {
        double s, c;
        sk_vm_sincos(n ? a0 + sweep * i / n : a0, &s, &c);
        double px = x + rx * c * cr - ry * s * sr, py = y + rx * c * sr + ry * s * cr;
        double dx, dy;
        sk_vc_apply(vc, px, py, &dx, &dy);
        to(&vc->p, dx, dy);
    }
}

void sk_vc_arc(sk_vc *vc, double x, double y, double r, double a0, double a1, bool ccw)
{
    sk_vc_ellipse(vc, x, y, r, r, 0, a0, a1, ccw);
}

void sk_vc_rect(sk_vc *vc, double x, double y, double w, double h)
{
    sk_vc_move(vc, x, y);
    sk_vc_line(vc, x + w, y);
    sk_vc_line(vc, x + w, y + h);
    sk_vc_line(vc, x, y + h);
    sk_vc_close(vc);
}

/* A path's subpaths into vc->path, for gfx. */
static void to_gfx(sk_vc *vc, const sk_vc_path *p)
{
    sk_path_clear(vc->path);
    for (size_t i = 0; i < p->subs.len; i++) {
        const sk_vc_sub *s = &p->subs.ptr[i];
        if (s->n < 2)
            continue;
        const sk_vc_pt *q = p->pts.ptr + s->start;
        sk_path_move(vc->path, q[0].x, q[0].y);
        for (size_t k = 1; k < s->n; k++)
            sk_path_line(vc->path, q[k].x, q[k].y);
        if (s->closed)
            sk_path_close(vc->path);
    }
}

/* ---- Painting ---- */

/* The pixels a box of points may touch, within the clip. */
static sk_rect region(const sk_vc *vc, double x0, double y0, double x1, double y1)
{
    if (!(x0 <= x1 && y0 <= y1) || !isfinite(x0) || !isfinite(x1) || !isfinite(y0) || !isfinite(y1))
        return sk_rect_make(0, 0, 0, 0);
    double lx = floor(SK_MAX(x0, -1e6)), ly = floor(SK_MAX(y0, -1e6));
    double hx = ceil(SK_MIN(x1, 1e6)), hy = ceil(SK_MIN(y1, 1e6));
    sk_rect r = sk_rect_make((int)lx, (int)ly, (int)(hx - lx) + 1, (int)(hy - ly) + 1);
    return sk_rect_intersect(r, vc->st.clip);
}

/* n pixels of d under the one colour s, premultiplied and scaled
 * already. */
static void span_solid(sk_px *d, int n, sk_px s, sk_blend op)
{
    if (!s || n <= 0)
        return;
    if (op == SK_BLEND_ADD) {
        for (int i = 0; i < n; i++)
            d[i] = add_px(s, d[i]);
    } else if (s >> 24 == 255) {
        for (int i = 0; i < n; i++)
            d[i] = s;
    } else {
        uint32_t k = 255 - (s >> 24);
        for (int i = 0; i < n; i++)
            d[i] = s + scale_px(d[i], k);
    }
}

/* n pixels of d from (x, y) under the shader's colours, each scaled by
 * its coverage in k, or all by k0 when k is NULL. */
static void span_shaded(const shader *sh, sk_px *d, int x, int y, int n, const uint8_t *k, uint32_t k0, sk_blend op,
                        sk_px *tmp)
{
    if (n <= 0)
        return;
    shade_row(sh, x, y, n, tmp);
    /* With no branch: scaling by 255 leaves a pixel as it is, and an
     * opaque one over d leaves nothing of d. */
    if (op == SK_BLEND_SRC_OVER) {
        if (k)
            for (int i = 0; i < n; i++) {
                sk_px s = scale_px(tmp[i], k[i]);
                d[i] = s + scale_px(d[i], 255 - (s >> 24));
            }
        else
            for (int i = 0; i < n; i++) {
                sk_px s = scale_px(tmp[i], k0);
                d[i] = s + scale_px(d[i], 255 - (s >> 24));
            }
        return;
    }
    for (int i = 0; i < n; i++)
        d[i] = add_px(scale_px(tmp[i], k ? k[i] : k0), d[i]);
}

/* A run of n pixels from (x, y), each covered by k: a colour's span,
 * or a round gradient's flat centre and clear outside as spans and the
 * rest pixel by pixel. */
static void run(const shader *sh, sk_px *d, int x, int y, int n, uint32_t k, sk_blend op, sk_px *tmp)
{
    if (sh->kind == SHADE_SOLID) {
        span_solid(d, n, k == 255 ? sh->solid : scale_px(sh->solid, k), op);
        return;
    }
    if (sh->kind != SHADE_ROUND) {
        span_shaded(sh, d, x, y, n, NULL, k, op, tmp);
        return;
    }
    /* Pixels whose centres lie within radius rr of the centre on this
     * row: [*a, *b), clamped to the run. */
    double dy = y + 0.5 - sh->cy, dy2 = dy * dy;
    int x1 = x + n;
#define WITHIN(r2, a, b)                                                     \
    do {                                                                     \
        if ((r2) > dy2) {                                                    \
            double h_ = sqrt((r2) - dy2);                                    \
            double lo_ = ceil(sh->cx - h_ - 0.5), hi_ = floor(sh->cx + h_ - 0.5) + 1; \
            a = (int)sk_vm_clamp(lo_, x, x1);                                \
            b = (int)sk_vm_clamp(hi_, a, x1);                                \
        } else {                                                             \
            a = b = x1;                                                      \
        }                                                                    \
    } while (0)
    int oa, ob, fa, fb;
    WITHIN(sh->out2, oa, ob);
    if (sh->flat2 > 0) {
        WITHIN(sh->flat2, fa, fb);
        fa = (int)sk_vm_clamp(fa, oa, ob);
        fb = (int)sk_vm_clamp(fb, fa, ob);
    } else {
        fa = fb = oa;
    }
#undef WITHIN
    if (oa == ob)
        oa = ob = fa = fb = x1;
    sk_px out = k == 255 ? sh->lut[255] : scale_px(sh->lut[255], k);
    sk_px in = k == 255 ? sh->lut[0] : scale_px(sh->lut[0], k);
    span_solid(d, oa - x, out, op);
    span_shaded(sh, d + (oa - x), oa, y, fa - oa, NULL, k, op, tmp);
    span_solid(d + (fa - x), fb - fa, in, op);
    span_shaded(sh, d + (fb - x), fb, y, ob - fb, NULL, k, op, tmp);
    span_solid(d + (ob - x), x1 - ob, out, op);
}

/* Coverage cov over r (a row of stride bytes each) painted with p:
 * times the clip mask and globalAlpha, composited by the operation.
 * Runs of full coverage go a span at a time. */
static void paint(sk_vc *vc, sk_rect r, const uint8_t *cov, int stride, const sk_vc_paint *p)
{
    shader sh;
    shader_init(&sh, p, &vc->st.m);
    uint32_t A = byte(vc->st.alpha * 255);
    if (!sh.ok || !A || (sh.kind == SHADE_SOLID && !sh.solid))
        return;
    sk_blend op = vc->st.op;
    const uint8_t *mask = vc->st.mask;
    int mw = vc->im->w;
    sk_px *tmp = sk_xmalloc((size_t)r.w * sizeof *tmp);
    uint8_t *k = sk_xmalloc((size_t)r.w);
    for (int y = 0; y < r.h; y++) {
        const uint8_t *c = cov + (size_t)y * (size_t)stride;
        int x0 = 0, x1 = r.w;
        while (x0 < x1 && !c[x0])
            x0++;
        while (x1 > x0 && !c[x1 - 1])
            x1--;
        if (x0 == x1)
            continue;
        const uint8_t *m = mask ? mask + (size_t)(r.y + y) * (size_t)mw + r.x : NULL;
        for (int x = x0; x < x1; x++)
            k[x] = (uint8_t)(m ? mul8(c[x], m[x]) : c[x]);
        sk_px *d = sk_image_row(vc->im, r.y + y) + r.x;
        int py = r.y + y;
        for (int x = x0; x < x1;) {
            int e = x;
            if (!k[x]) {
                while (e < x1 && !k[e])
                    e++;
            } else if (k[x] == 255) {
                while (e < x1 && k[e] == 255)
                    e++;
                run(&sh, d + x, r.x + x, py, e - x, A, op, tmp);
            } else {
                while (e < x1 && k[e] != 255 && k[e])
                    e++;
                if (A != 255)
                    for (int i = x; i < e; i++)
                        k[i] = (uint8_t)mul8(k[i], A);
                span_shaded(&sh, d + x, r.x + x, py, e - x, k + x, 0, op, tmp);
            }
            x = e;
        }
    }
    free(tmp);
    free(k);
}


/* The colour at globalAlpha, as gfx takes it. */
static sk_color faded(const sk_vc *vc, sk_color c)
{
    uint32_t a = mul8(c >> 24, byte(vc->st.alpha * 255));
    return (c & 0xffffffu) | a << 24;
}

/* gfx's path filled with p over the box (in pixels) it lies in. */
static void fill_gfx(sk_vc *vc, const sk_path *path, double x0, double y0, double x1, double y1, const sk_vc_paint *p,
                     sk_fill_rule rule)
{
    sk_rect r = region(vc, x0, y0, x1, y1);
    if (sk_rect_empty(r))
        return;
    if (p->kind == SK_VC_SOLID && vc->st.op == SK_BLEND_SRC_OVER && !vc->st.mask) {
        sk_fill_path(vc->im, path, NULL, rule, faded(vc, p->color), &r);
        return;
    }
    sk_xform m = {1, 0, 0, 1, (float)-r.x, (float)-r.y};
    uint8_t *cov = sk_path_mask(path, &m, rule, r.w, r.h);
    if (!cov)
        return;
    paint(vc, r, cov, r.w, p);
    free(cov);
}

void sk_vc_fill(sk_vc *vc, const sk_vc_paint *p, sk_fill_rule rule)
{
    const sk_vc_path *q = &vc->p;
    if (!q->subs.len)
        return;
    to_gfx(vc, q);
    fill_gfx(vc, vc->path, q->minx, q->miny, q->maxx, q->maxy, p, rule);
}

/* A path dashed by the state's pattern, at pixels' scale k, into
 * vc->path: each subpath starts the pattern afresh. */
static void dashed(sk_vc *vc, const sk_vc_path *q, double k)
{
    sk_path_clear(vc->path);
    int nd = vc->st.n_dash;
    double pat[4];
    for (int i = 0; i < nd; i++)
        pat[i] = vc->st.dash[i] * k;
    for (size_t i = 0; i < q->subs.len; i++) {
        const sk_vc_sub *s = &q->subs.ptr[i];
        if (s->n < 2)
            continue;
        const sk_vc_pt *p = q->pts.ptr + s->start;
        size_t segs = s->closed ? s->n : s->n - 1;
        int at = 0;
        double left = pat[0];
        bool on = true, drawing = false;
        for (size_t j = 0; j < segs; j++) {
            sk_vc_pt a = p[j], b = p[(j + 1) % s->n];
            double dx = (double)b.x - a.x, dy = (double)b.y - a.y, len = sqrt(dx * dx + dy * dy), pos = 0;
            if (!(len > 0))
                continue;
            while (pos < len) {
                double step = SK_MIN(left, len - pos);
                if (on) {
                    if (!drawing)
                        sk_path_move(vc->path, (float)(a.x + dx * pos / len), (float)(a.y + dy * pos / len));
                    sk_path_line(vc->path, (float)(a.x + dx * (pos + step) / len),
                                 (float)(a.y + dy * (pos + step) / len));
                    drawing = true;
                }
                pos += step;
                left -= step;
                if (left <= 1e-9) {
                    at = (at + 1) % nd;
                    left = pat[at];
                    on = !on;
                    drawing = false;
                }
            }
        }
    }
}

static void stroke_path(sk_vc *vc, const sk_vc_path *q, const sk_vc_paint *p)
{
    if (!q->subs.len)
        return;
    double k = xscale(&vc->st.m);
    double w = vc->st.line_width * k;
    if (!(w > 0))
        return;
    if (vc->st.n_dash)
        dashed(vc, q, k);
    else
        to_gfx(vc, q);
    sk_path_clear(vc->outline);
    sk_path_stroke(vc->outline, vc->path, (float)w, vc->st.cap, vc->st.join, (float)vc->st.miter);
    double pad = w * (vc->st.join == SK_JOIN_MITER ? SK_MAX(vc->st.miter, 1.5) : 1.5) + 2;
    fill_gfx(vc, vc->outline, q->minx - pad, q->miny - pad, q->maxx + pad, q->maxy + pad, p, SK_FILL_NONZERO);
}

void sk_vc_stroke(sk_vc *vc, const sk_vc_paint *p)
{
    stroke_path(vc, &vc->p, p);
}

/* n values of v, stride apart, blurred by a box of radius r, its edges
 * extended by zeros: tmp holds n values. */
static void box_blur(float *v, size_t n, size_t stride, int r, float *tmp)
{
    double sum = 0, k = 1.0 / (2 * r + 1);
    for (size_t i = 0; i < n; i++)
        tmp[i] = v[i * stride];
    for (int i = 0; i < r && (size_t)i < n; i++)
        sum += tmp[i];
    for (size_t i = 0; i < n; i++) {
        if (i + (size_t)r < n)
            sum += tmp[i + (size_t)r];
        if (i >= (size_t)r + 1)
            sum -= tmp[i - (size_t)r - 1];
        v[i * stride] = (float)(sum * k);
    }
}

void sk_vc_stroke_glow(sk_vc *vc, const sk_vc_paint *p, sk_color shadow, double blur)
{
    const sk_vc_path *q = &vc->p;
    double k = xscale(&vc->st.m), w = vc->st.line_width * k;
    double sigma = blur / 2 * vc->scale;
    if (!q->subs.len || !(w > 0))
        return;
    if (!(sigma >= 0.5)) {
        stroke_path(vc, q, p);
        return;
    }
    if (vc->st.n_dash)
        dashed(vc, q, k);
    else
        to_gfx(vc, q);
    sk_path_clear(vc->outline);
    sk_path_stroke(vc->outline, vc->path, (float)w, vc->st.cap, vc->st.join, (float)vc->st.miter);
    double pad = 3 * sigma + w * SK_MAX(vc->st.miter, 1.5) + 2;
    sk_rect r = region(vc, q->minx - pad, q->miny - pad, q->maxx + pad, q->maxy + pad);
    if (sk_rect_empty(r))
        return;
    /* The shadow's coverage rasterized f times coarser, as the average
     * over each cell of f by f, and blurred there. */
    int f = SK_MAX(1, (int)(sigma / 2));
    int lw = (r.w + f - 1) / f + 1, lh = (r.h + f - 1) / f + 1;
    sk_xform m = {(float)(1.0 / f), 0, 0, (float)(1.0 / f), (float)(-r.x / (double)f), (float)(-r.y / (double)f)};
    uint8_t *cov = sk_path_mask(vc->outline, &m, SK_FILL_NONZERO, lw, lh);
    if (!cov)
        return;
    float *low = sk_xmalloc((size_t)lw * (size_t)lh * sizeof *low);
    float *tmp = sk_xmalloc((size_t)SK_MAX(lw, lh) * sizeof *tmp);
    for (size_t i = 0; i < (size_t)lw * (size_t)lh; i++)
        low[i] = cov[i] / 255.0f;
    double s = sigma / f;
    int rb = SK_MAX(1, (int)floor((sqrt(4 * s * s + 1) - 1) / 2));
    for (int pass = 0; pass < 3; pass++) {
        for (int y = 0; y < lh; y++)
            box_blur(low + (size_t)y * (size_t)lw, (size_t)lw, 1, rb, tmp);
        for (int x = 0; x < lw; x++)
            box_blur(low + x, (size_t)lh, (size_t)lw, rb, tmp);
    }
    /* In 16 bits, then back to every pixel, bilinearly in 256ths. */
    uint16_t *lo = sk_xmalloc((size_t)lw * (size_t)lh * sizeof *lo);
    for (size_t i = 0; i < (size_t)lw * (size_t)lh; i++)
        lo[i] = (uint16_t)floor(sk_vm_clamp(low[i], 0, 1) * 65535 + 0.5);
    int *xa = sk_xmalloc((size_t)r.w * sizeof *xa), *fx = sk_xmalloc((size_t)r.w * sizeof *fx);
    for (int x = 0; x < r.w; x++) {
        double lx = (x + 0.5) / f - 0.5;
        int x0 = (int)floor(lx);
        fx[x] = (int)floor((lx - x0) * 256);
        xa[x] = SK_MAX(0, SK_MIN(lw - 2, x0));
        if (x0 < 0)
            fx[x] = 0;
    }
    sk_px solid = sk_premul(shadow);
    uint32_t A = byte(vc->st.alpha * 255);
    const uint8_t *mask = vc->st.mask;
    /* Down first, a row of cells at a time, then across, pixel by
     * pixel, only between the row's first and last lit cells. */
    uint32_t *row = sk_xmalloc((size_t)lw * sizeof *row);
    for (int y = 0; y < r.h && solid && A; y++) {
        double ly = (y + 0.5) / f - 0.5;
        int y0 = (int)floor(ly);
        uint32_t fy = y0 < 0 ? 0 : (uint32_t)floor((ly - y0) * 256);
        int ya = SK_MAX(0, SK_MIN(lh - 2, y0));
        const uint16_t *top = lo + (size_t)ya * (size_t)lw, *bot = top + lw;
        int first = -1, last = -1;
        for (int i = 0; i < lw; i++) {
            row[i] = (top[i] * (256 - fy) + bot[i] * fy) >> 8;
            if (row[i]) {
                if (first < 0)
                    first = i;
                last = i;
            }
        }
        if (first < 0)
            continue;
        int x0 = SK_MAX(0, (first - 1) * f), x1 = SK_MIN(r.w, (last + 2) * f);
        sk_px *d = sk_image_row(vc->im, r.y + y) + r.x;
        const uint8_t *mr = mask ? mask + (size_t)(r.y + y) * (size_t)vc->im->w + r.x : NULL;
        for (int x = x0; x < x1; x++) {
            int i = xa[x];
            uint32_t v = (row[i] * (uint32_t)(256 - fx[x]) + row[i + 1] * (uint32_t)fx[x]) >> 16;
            if (mr)
                v = mul8(v, mr[x]);
            if (A != 255)
                v = mul8(v, A);
            if (v)
                d[x] = blend(vc->st.op, scale_px(solid, v), d[x]);
        }
    }
    free(row);
    free(xa);
    free(fx);
    free(lo);
    free(tmp);
    free(low);
    free(cov);
    fill_gfx(vc, vc->outline, q->minx - pad, q->miny - pad, q->maxx + pad, q->maxy + pad, p, SK_FILL_NONZERO);
}


void sk_vc_clip(sk_vc *vc, sk_fill_rule rule)
{
    sk_vc_state *st = &vc->st;
    const sk_vc_path *q = &vc->p;
    /* One axis-aligned rectangle narrows the clip's box, rounded to
     * whole pixels. */
    bool box = q->subs.len >= 1 && q->subs.ptr[0].n == 4 && q->subs.ptr[0].closed && !st->mask;
    for (size_t i = 1; box && i < q->subs.len; i++)
        box = q->subs.ptr[i].n < 2;
    if (box) {
        const sk_vc_pt *p = q->pts.ptr + q->subs.ptr[0].start;
        box = ((p[0].x == p[1].x && p[1].y == p[2].y && p[2].x == p[3].x && p[3].y == p[0].y) ||
               (p[0].y == p[1].y && p[1].x == p[2].x && p[2].y == p[3].y && p[3].x == p[0].x));
    }
    if (box) {
        double x0 = floor(q->minx + 0.5), y0 = floor(q->miny + 0.5);
        double x1 = floor(q->maxx + 0.5), y1 = floor(q->maxy + 0.5);
        st->clip = sk_rect_intersect(st->clip, sk_rect_make((int)x0, (int)y0, (int)(x1 - x0), (int)(y1 - y0)));
        return;
    }
    int w = vc->im->w, h = vc->im->h;
    to_gfx(vc, q);
    uint8_t *m = sk_path_mask(vc->path, NULL, rule, w, h);
    if (!m)
        return;
    if (st->mask)
        for (size_t i = 0; i < (size_t)w * (size_t)h; i++)
            m[i] = (uint8_t)mul8(m[i], st->mask[i]);
    if (st->own_mask)
        free(st->mask);
    st->mask = m;
    st->own_mask = true;
}

/* A rectangle's coverage of each pixel of r, its sides at x0..x1 and
 * y0..y1 in pixels: the area inside, exactly. */
static void rect_cover(sk_vc *vc, sk_rect r, double x0, double y0, double x1, double y1)
{
    size_t n = (size_t)r.w * (size_t)r.h;
    vc->cover.ptr = sk_vec_grow_(vc->cover.ptr, &vc->cover.cap, n, 1);
    vc->cover.len = n;
    uint8_t *c = vc->cover.ptr;
    /* Each column's share across; a row whole down copies the shares. */
    double *cols = sk_xmalloc((size_t)r.w * sizeof *cols);
    uint8_t *full = sk_xmalloc((size_t)r.w);
    for (int x = 0; x < r.w; x++) {
        double px = r.x + x, cx = SK_MIN(x1, px + 1) - SK_MAX(x0, px);
        cols[x] = cx > 0 ? SK_MIN(cx, 1.0) : 0;
        full[x] = (uint8_t)floor(cols[x] * 255 + 0.5);
    }
    for (int y = 0; y < r.h; y++) {
        double py = r.y + y, cy = SK_MIN(y1, py + 1) - SK_MAX(y0, py);
        uint8_t *row = c + (size_t)y * (size_t)r.w;
        if (cy >= 1) {
            memcpy(row, full, (size_t)r.w);
            continue;
        }
        for (int x = 0; x < r.w; x++) {
            double a = cy > 0 ? cols[x] * cy : 0;
            row[x] = (uint8_t)floor(a * 255 + 0.5);
        }
    }
    free(cols);
    free(full);
}

/* The rectangle as the aside path, the current path left alone. */
static sk_vc_path *rect_aside(sk_vc *vc, double x, double y, double w, double h)
{
    sk_vc_path keep = vc->p;
    vc->p = vc->aside;
    path_reset(&vc->p);
    sk_vc_rect(vc, x, y, w, h);
    vc->aside = vc->p;
    vc->p = keep;
    return &vc->aside;
}

void sk_vc_fill_rect(sk_vc *vc, double x, double y, double w, double h, const sk_vc_paint *p)
{
    const sk_vc_xform *m = &vc->st.m;
    if (m->b != 0 || m->c != 0) {
        const sk_vc_path *q = rect_aside(vc, x, y, w, h);
        to_gfx(vc, q);
        fill_gfx(vc, vc->path, q->minx, q->miny, q->maxx, q->maxy, p, SK_FILL_NONZERO);
        return;
    }
    double x0 = m->a * x + m->e, x1 = m->a * (x + w) + m->e;
    double y0 = m->d * y + m->f, y1 = m->d * (y + h) + m->f;
    if (x0 > x1) {
        double t = x0;
        x0 = x1;
        x1 = t;
    }
    if (y0 > y1) {
        double t = y0;
        y0 = y1;
        y1 = t;
    }
    sk_rect r = region(vc, x0, y0, x1, y1);
    if (sk_rect_empty(r))
        return;
    bool whole = x0 == floor(x0) && x1 == floor(x1) && y0 == floor(y0) && y1 == floor(y1);
    if (whole && p->kind == SK_VC_SOLID && !vc->st.mask) {
        sk_rect in = sk_rect_intersect(sk_rect_make((int)x0, (int)y0, (int)(x1 - x0), (int)(y1 - y0)), r);
        sk_fill_rect(vc->im, in, faded(vc, p->color), vc->st.op, NULL);
        return;
    }
    rect_cover(vc, r, x0, y0, x1, y1);
    paint(vc, r, vc->cover.ptr, r.w, p);
}

void sk_vc_stroke_rect(sk_vc *vc, double x, double y, double w, double h, const sk_vc_paint *p)
{
    stroke_path(vc, rect_aside(vc, x, y, w, h), p);
}

void sk_vc_square(sk_vc *vc, double x, double y, double w, double h, sk_color c, double alpha)
{
    const sk_vc_xform *m = &vc->st.m;
    double x0 = m->a * x + m->e, x1 = m->a * (x + w) + m->e;
    double y0 = m->d * y + m->f, y1 = m->d * (y + h) + m->f;
    if (x0 > x1) {
        double t = x0;
        x0 = x1;
        x1 = t;
    }
    if (y0 > y1) {
        double t = y0;
        y0 = y1;
        y1 = t;
    }
    sk_rect r = region(vc, x0, y0, x1, y1);
    if (sk_rect_empty(r))
        return;
    uint32_t A = byte(alpha * vc->st.alpha * 255);
    sk_px p = sk_premul(c);
    if (!A || !p)
        return;
    sk_blend op = vc->st.op;
    const uint8_t *mask = vc->st.mask;
    for (int j = 0; j < r.h; j++) {
        double py = r.y + j, cy = SK_MIN(y1, py + 1) - SK_MAX(y0, py);
        if (cy <= 0)
            continue;
        sk_px *d = sk_image_row(vc->im, r.y + j) + r.x;
        const uint8_t *mr = mask ? mask + (size_t)(r.y + j) * (size_t)vc->im->w + r.x : NULL;
        for (int i = 0; i < r.w; i++) {
            double px = r.x + i, cx = SK_MIN(x1, px + 1) - SK_MAX(x0, px);
            if (cx <= 0)
                continue;
            uint32_t k = (uint32_t)floor(SK_MIN(cx * cy, 1.0) * A + 0.5);
            if (mr)
                k = mul8(k, mr[i]);
            if (k)
                d[i] = blend(op, k == 255 ? p : scale_px(p, k), d[i]);
        }
    }
}

void sk_vc_batch(sk_vc *vc, sk_color c, double alpha, double x, double y, double w)
{
    double k = SK_MIN(20, sk_vm_round(alpha * 20));
    if (!(k > 0) || !(w > 0) || !isfinite(x) || !isfinite(y))
        return;
    struct sk_vc_square s = {c, (int)k, (float)(x - w / 2), (float)(y - w / 2), (float)w};
    sk_vec_push(&vc->squares, s);
}

/* The squares' order of filling: their colour's first coming, then
 * alpha, then their own order. */
typedef struct {
    uint32_t rank;
    uint32_t i;
} square_key;

static int by_key(const void *a, const void *b)
{
    const square_key *x = a, *y = b;
    if (x->rank != y->rank)
        return x->rank < y->rank ? -1 : 1;
    return (x->i > y->i) - (x->i < y->i);
}

void sk_vc_flush(sk_vc *vc)
{
    size_t n = vc->squares.len;
    if (!n)
        return;
    size_t npx = (size_t)vc->im->w * (size_t)vc->im->h;
    if (vc->sum_n != npx) {
        free(vc->sum);
        vc->sum = sk_xcalloc(npx ? npx : 1, sizeof *vc->sum);
        vc->sum_n = npx;
    }
    const struct sk_vc_square *sq = vc->squares.ptr;
    square_key *keys = sk_xreallocarray(NULL, n, sizeof *keys);
    SK_VEC(sk_color) colors = {0};
    for (size_t i = 0; i < n; i++) {
        size_t c = 0;
        while (c < colors.len && colors.ptr[c] != sq[i].color)
            c++;
        if (c == colors.len)
            sk_vec_push(&colors, sq[i].color);
        keys[i] = (square_key){(uint32_t)(c * 32 + (size_t)sq[i].k), (uint32_t)i};
    }
    qsort(keys, n, sizeof *keys, by_key);
    const sk_vc_xform *m = &vc->st.m;
    sk_rect clip = sk_rect_intersect(vc->st.clip, sk_image_bounds(vc->im));
    SK_VEC(uint32_t) touched = {0};
    for (size_t b0 = 0; b0 < n;) {
        size_t b1 = b0;
        while (b1 < n && keys[b1].rank == keys[b0].rank)
            b1++;
        touched.len = 0;
        for (size_t j = b0; j < b1; j++) {
            const struct sk_vc_square *s = &sq[keys[j].i];
            double x0 = m->a * s->x + m->e, x1 = m->a * (s->x + s->w) + m->e;
            double y0 = m->d * s->y + m->f, y1 = m->d * (s->y + s->w) + m->f;
            if (x0 > x1) {
                double t = x0;
                x0 = x1;
                x1 = t;
            }
            if (y0 > y1) {
                double t = y0;
                y0 = y1;
                y1 = t;
            }
            /* Both ends into +-1e6 before they are ints: a square scaled
             * past it, or not a number, made the conversion undefined. */
            double cx0 = sk_vm_clamp(x0, -1e6, 1e6), cx1 = sk_vm_clamp(x1, -1e6, 1e6);
            double cy0 = sk_vm_clamp(y0, -1e6, 1e6), cy1 = sk_vm_clamp(y1, -1e6, 1e6);
            int ix0 = (int)SK_MAX(clip.x, floor(cx0)), ix1 = (int)SK_MIN(clip.x + clip.w, ceil(cx1));
            int iy0 = (int)SK_MAX(clip.y, floor(cy0)), iy1 = (int)SK_MIN(clip.y + clip.h, ceil(cy1));
            for (int y = iy0; y < iy1; y++) {
                double cy = SK_MIN(y1, y + 1.0) - SK_MAX(y0, (double)y);
                for (int x = ix0; x < ix1; x++) {
                    double cx = SK_MIN(x1, x + 1.0) - SK_MAX(x0, (double)x);
                    uint32_t cov = (uint32_t)floor(SK_MIN(cx * cy, 1.0) * 255 + 0.5);
                    if (!cov)
                        continue;
                    size_t at = (size_t)y * (size_t)vc->im->w + (size_t)x;
                    if (!vc->sum[at])
                        sk_vec_push(&touched, (uint32_t)at);
                    vc->sum[at] = (uint16_t)SK_MIN(65535u, vc->sum[at] + cov);
                }
            }
        }
        const struct sk_vc_square *s = &sq[keys[b0].i];
        sk_px p = sk_premul(s->color);
        uint32_t A = byte(s->k / 20.0 * 255);
        for (size_t t = 0; t < touched.len; t++) {
            uint32_t at = touched.ptr[t];
            uint32_t k = mul8(SK_MIN(255u, vc->sum[at]), A);
            vc->sum[at] = 0;
            if (vc->st.mask)
                k = mul8(k, vc->st.mask[at]);
            sk_px *d = sk_image_row(vc->im, (int)(at / (uint32_t)vc->im->w)) + at % (uint32_t)vc->im->w;
            if (k)
                *d = blend(vc->st.op, scale_px(p, k), *d);
        }
        b0 = b1;
    }
    sk_vec_free(&touched);
    sk_vec_free(&colors);
    free(keys);
    vc->squares.len = 0;
    /* The JavaScript flush leaves globalAlpha at 1. */
    vc->st.alpha = 1;
}

void sk_vc_clear(sk_vc *vc, sk_color c)
{
    sk_fill_rect(vc->im, sk_image_bounds(vc->im), c, SK_BLEND_SRC, NULL);
}

void sk_vc_image(sk_vc *vc, const sk_image *src, double x, double y, double w, double h, bool smooth)
{
    if (!src || src->w <= 0 || src->h <= 0)
        return;
    const sk_vc_xform *m = &vc->st.m;
    double x0 = m->a * x + m->e, x1 = m->a * (x + w) + m->e;
    double y0 = m->d * y + m->f, y1 = m->d * (y + h) + m->f;
    if (!(x1 > x0 && y1 > y0))
        return;
    uint8_t opacity = (uint8_t)byte(vc->st.alpha * 255);
    if (!opacity)
        return;
    int ix = (int)floor(x0 + 0.5), iy = (int)floor(y0 + 0.5);
    int iw = (int)floor(x1 + 0.5) - ix, ih = (int)floor(y1 + 0.5) - iy;
    if (iw <= 0 || ih <= 0)
        return;
    if (iw == src->w && ih == src->h)
        sk_blit(vc->im, ix, iy, src, sk_image_bounds(src), vc->st.op, opacity, &vc->st.clip);
    else
        sk_blit_scaled(vc->im, sk_rect_make(ix, iy, iw, ih), src, sk_image_bounds(src),
                       smooth ? SK_FILTER_BILINEAR : SK_FILTER_NEAREST, vc->st.op, opacity, &vc->st.clip);
}

/* ---- Text ---- */

static void fonts(sk_vc *vc)
{
    if (vc->fonts)
        return;
    vc->fonts = sk_fontset_new(NULL, NULL, true);
    vc->glyphs = sk_glyph_cache_new((size_t)2 << 20);
}

void sk_vc_measure(sk_vc *vc, const char *s, sk_vc_metrics *out)
{
    fonts(vc);
    double k = xscale(&vc->st.m);
    float px = (float)(vc->st.font_px * k);
    size_t n = strlen(s);
    memset(out, 0, sizeof *out);
    if (!(px > 0))
        return;
    double pen = 0, lo = INFINITY, hi = -INFINITY, up = 0, down = 0;
    for (size_t i = 0; i < n;) {
        uint32_t cp;
        i += sk_utf8_decode(s + i, n - i, &cp, NULL);
        if (cp < 0x20 || (cp >= 0x7f && cp < 0xa0))
            continue;
        sk_glyph_ref ref = sk_fontset_lookup(vc->fonts, cp, SK_STYLE_REGULAR);
        const sk_glyph *g = sk_glyph_get(vc->glyphs, ref, px, 0);
        if (g->mask.w > 0) {
            lo = SK_MIN(lo, pen + g->left);
            hi = SK_MAX(hi, pen + g->left + g->mask.w);
            up = SK_MAX(up, -(double)g->top);
            down = SK_MAX(down, (double)(g->top + g->mask.h));
        }
        pen += sk_font_advance(ref.font, ref.glyph, px);
    }
    /* About the point text is drawn at: the pen starts left of it by
     * the alignment's share of the width, and the baseline lies below
     * it by the baseline's offset. */
    double shift = vc->st.align == SK_VC_CENTER ? pen / 2 : vc->st.align == SK_VC_RIGHT ? pen : 0, drop = 0;
    if (vc->st.baseline != SK_VC_ALPHABETIC) {
        sk_font_vmetrics vm;
        sk_font_vmetrics_at(sk_fontset_primary(vc->fonts, SK_STYLE_REGULAR), px, &vm);
        drop = vc->st.baseline == SK_VC_MIDDLE ? (vm.ascent - vm.descent) / 2 : vm.ascent;
    }
    out->width = pen / k;
    if (hi > lo) {
        out->left = (shift - lo) / k;
        out->right = (hi - shift) / k;
    }
    out->ascent = (up - drop) / k;
    out->descent = (down + drop) / k;
}

/* Where the pen starts for t at (x, y), by the alignment and the
 * baseline, in pixels. */
static void anchor(sk_vc *vc, const char *t, double x, double y, double *px, double *py, float *size)
{
    double k = xscale(&vc->st.m);
    float fpx = (float)(vc->st.font_px * k);
    *size = fpx;
    double w = 0;
    if (vc->st.align != SK_VC_LEFT)
        w = sk_measure_text(vc->fonts, SK_STYLE_REGULAR, fpx, t, strlen(t));
    sk_vc_apply(vc, x, y, px, py);
    if (vc->st.align == SK_VC_CENTER)
        *px -= w / 2;
    else if (vc->st.align == SK_VC_RIGHT)
        *px -= w;
    if (vc->st.baseline != SK_VC_ALPHABETIC) {
        sk_font_vmetrics vm;
        sk_font_vmetrics_at(sk_fontset_primary(vc->fonts, SK_STYLE_REGULAR), fpx, &vm);
        if (vc->st.baseline == SK_VC_MIDDLE)
            *py += (vm.ascent - vm.descent) / 2;
        else
            *py += vm.ascent;
    }
}

void sk_vc_text(sk_vc *vc, const char *t, double x, double y, sk_color c)
{
    fonts(vc);
    double px, py;
    float size;
    anchor(vc, t, x, y, &px, &py, &size);
    if (!(size > 0))
        return;
    sk_draw_text(vc->im, vc->glyphs, vc->fonts, SK_STYLE_REGULAR, size, (float)px, (float)py, t, strlen(t),
                 faded(vc, c), &vc->st.clip);
}
