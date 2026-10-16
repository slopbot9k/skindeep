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

/* A gradient set up for one fill: the LUT and the pixels-to-points
 * transform. */
typedef struct {
    const sk_vc_paint *p;
    sk_px lut[256];
    sk_vc_xform inv;
    bool ok;
    /* radial: the quadratic's constant parts */
    double cdx, cdy, dr, a;
} shader;

static void shader_init(shader *sh, const sk_vc_paint *p, const sk_vc_xform *m)
{
    sh->p = p;
    sh->ok = p->kind == SK_VC_SOLID || invert(m, &sh->inv);
    if (p->kind == SK_VC_SOLID)
        return;
    gradient_lut(p, sh->lut);
    sh->cdx = p->x1 - p->x0;
    sh->cdy = p->y1 - p->y0;
    sh->dr = p->r1 - p->r0;
    sh->a = sh->cdx * sh->cdx + sh->cdy * sh->cdy - (p->kind == SK_VC_RADIAL ? sh->dr * sh->dr : 0);
}

/* The gradient's colour at pixel (x, y), or 0 where a two-circle
 * gradient paints nothing. */
static sk_px shade(const shader *sh, int x, int y)
{
    const sk_vc_paint *p = sh->p;
    double px = x + 0.5, py = y + 0.5;
    double ux = sh->inv.a * px + sh->inv.c * py + sh->inv.e;
    double uy = sh->inv.b * px + sh->inv.d * py + sh->inv.f;
    double t;
    if (p->kind == SK_VC_LINEAR) {
        if (!(sh->a > 0))
            return 0;
        t = ((ux - p->x0) * sh->cdx + (uy - p->y0) * sh->cdy) / sh->a;
    } else {
        /* The largest w with |q - c(w)| = r(w) >= 0, c(w) = c0 + w (c1 -
         * c0), r(w) = r0 + w (r1 - r0): a w^2 - 2 b w + c = 0. */
        double qx = ux - p->x0, qy = uy - p->y0;
        double b = qx * sh->cdx + qy * sh->cdy + p->r0 * sh->dr;
        double c = qx * qx + qy * qy - p->r0 * p->r0;
        if (fabs(sh->a) < 1e-12) {
            if (fabs(b) < 1e-12)
                return 0;
            t = c / (2 * b);
            if (p->r0 + t * sh->dr < 0)
                return 0;
        } else {
            double disc = b * b - sh->a * c;
            if (disc < 0)
                return 0;
            double s = sqrt(disc);
            double t1 = (b + s) / sh->a, t2 = (b - s) / sh->a;
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
    }
    int i = (int)floor(sk_vm_clamp(t, 0, 1) * 255 + 0.5);
    return sh->lut[i];
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

/* Coverage cov over r (a row of stride bytes each) painted with p: times
 * the clip mask and globalAlpha, composited by the operation. */
static void paint(sk_vc *vc, sk_rect r, const uint8_t *cov, int stride, const sk_vc_paint *p)
{
    shader sh;
    shader_init(&sh, p, &vc->st.m);
    if (!sh.ok)
        return;
    uint32_t A = byte(vc->st.alpha * 255);
    if (!A)
        return;
    sk_px solid = p->kind == SK_VC_SOLID ? sk_premul(p->color) : 0;
    if (p->kind == SK_VC_SOLID && !solid)
        return;
    sk_blend op = vc->st.op;
    const uint8_t *mask = vc->st.mask;
    int mw = vc->im->w;
    for (int y = 0; y < r.h; y++) {
        const uint8_t *c = cov + (size_t)y * (size_t)stride;
        sk_px *d = sk_image_row(vc->im, r.y + y) + r.x;
        const uint8_t *m = mask ? mask + (size_t)(r.y + y) * (size_t)mw + r.x : NULL;
        for (int x = 0; x < r.w; x++) {
            uint32_t k = c[x];
            if (!k)
                continue;
            if (m)
                k = mul8(k, m[x]);
            if (A != 255)
                k = mul8(k, A);
            if (!k)
                continue;
            sk_px s = p->kind == SK_VC_SOLID ? solid : shade(&sh, r.x + x, r.y + y);
            if (k != 255)
                s = scale_px(s, k);
            if (s)
                d[x] = blend(op, s, d[x]);
        }
    }
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
    for (int y = 0; y < r.h; y++) {
        double py = r.y + y, cy = SK_MIN(y1, py + 1) - SK_MAX(y0, py);
        for (int x = 0; x < r.w; x++) {
            double px = r.x + x, cx = SK_MIN(x1, px + 1) - SK_MAX(x0, px);
            double a = cx > 0 && cy > 0 ? cx * cy : 0;
            c[(size_t)y * (size_t)r.w + x] = (uint8_t)floor(SK_MIN(a, 1.0) * 255 + 0.5);
        }
    }
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
    out->width = pen / k;
    if (hi > lo) {
        out->left = -lo / k;
        out->right = hi / k;
    }
    out->ascent = up / k;
    out->descent = down / k;
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
