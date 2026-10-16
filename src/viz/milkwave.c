/* What the MilkDrop renderer draws over the warped picture each frame
 * (milkint.h): the motion vectors, the custom waves, the waveform, the
 * darkened centre and the borders, ported from butterchurn 2.6.7's
 * motionVectors.js, customWaveform.js, basicWaveform.js, waveUtils.js,
 * darkenCenter.js and border.js (MIT, Copyright (c) 2013-2018 Jordan
 * Berg; third_party/butterchurn/LICENSE).
 *
 * WebGL draws them into an 8-bit texture with blending on: a pixel
 * becomes src * a + dst, or src * a + dst * (1 - a), worked in float
 * and rounded back to 8 bits. Lines are aliased and one pixel wide, a
 * point is a square of whole pixels, and a filled shape covers the
 * pixels whose centres it holds; the C does the same. */
#include "milkint.h"

#include <math.h>
#include <string.h>

/* ---- Rasterizing ---- */

typedef struct {
    double c[3], a;
    bool add;
} paint;

/* rgba clamped as a fragment's colour is before blending. */
static paint paint_of(double r, double g, double b, double a, bool add)
{
    paint p = {{r, g, b}, a, add};
    for (int k = 0; k < 3; k++)
        p.c[k] = p.c[k] < 0 || !(p.c[k] == p.c[k]) ? 0 : p.c[k] > 1 ? 1 : p.c[k];
    p.a = p.a < 0 || !(p.a == p.a) ? 0 : p.a > 1 ? 1 : p.a;
    return p;
}

static void put(sk_milk_tex *t, int x, int y, const paint *p)
{
    uint8_t *d = t->px + ((size_t)y * (size_t)t->w + (size_t)x) * 4;
    for (int k = 0; k < 3; k++) {
        double dst = d[k] / 255.0;
        d[k] = sk_milk_unorm(p->c[k] * p->a + (p->add ? dst : dst * (1 - p->a)));
    }
}

/* Clip coordinates to the texture's pixels, y counting GL's rows. */
static double win_x(const sk_milk_tex *t, double cx)
{
    return (cx + 1) * 0.5 * t->w;
}

static double win_y(const sk_milk_tex *t, double cy)
{
    return (cy + 1) * 0.5 * t->h;
}

/* [lo, hi] held to the pixels -1..n, so it converts to int. */
static void hold(double *lo, double *hi, int n)
{
    *lo = fmin(fmax(*lo, 0), n);
    *hi = fmax(fmin(*hi, n - 1), -1);
}

/* GL's aliased line of one pixel from (x0, y0) to (x1, y1) in window
 * pixels: along its longer axis, each pixel whose centre the line
 * passes from the start up to, not including, the end, which the next
 * segment of a strip draws. */
static void line(sk_milk_tex *t, double x0, double y0, double x1, double y1, const paint *p)
{
    if (!isfinite(x0) || !isfinite(y0) || !isfinite(x1) || !isfinite(y1))
        return;
    double dx = x1 - x0, dy = y1 - y0;
    bool across = fabs(dx) >= fabs(dy);
    double a0 = across ? x0 : y0, a1 = across ? x1 : y1, b0 = across ? y0 : x0;
    double da = across ? dx : dy, db = across ? dy : dx;
    if (da == 0)
        return;
    double lo, hi;
    if (da > 0)
        lo = ceil(a0 - 0.5), hi = ceil(a1 - 0.5) - 1;
    else
        lo = floor(a1 - 0.5) + 1, hi = floor(a0 - 0.5);
    int na = across ? t->w : t->h, nb = across ? t->h : t->w;
    hold(&lo, &hi, na);
    double slope = db / da;
    for (int i = (int)lo; i <= (int)hi; i++) {
        double b = floor(b0 + (i + 0.5 - a0) * slope);
        if (b >= 0 && b < nb)
            put(t, across ? i : (int)b, across ? (int)b : i, p);
    }
}

/* The pixels whose centres fall in [x0, x1) by [y0, y1). */
static void fill(sk_milk_tex *t, double x0, double y0, double x1, double y1, const paint *p)
{
    double c0 = ceil(x0 - 0.5), c1 = ceil(x1 - 0.5) - 1;
    double r0 = ceil(y0 - 0.5), r1 = ceil(y1 - 0.5) - 1;
    hold(&c0, &c1, t->w);
    hold(&r0, &r1, t->h);
    for (int y = (int)r0; y <= (int)r1; y++)
        for (int x = (int)c0; x <= (int)c1; x++)
            put(t, x, y, p);
}

/* A GL point of size pixels: a square about (x, y). */
static void point(sk_milk_tex *t, double x, double y, double size, const paint *p)
{
    if (!isfinite(x) || !isfinite(y))
        return;
    fill(t, x - size / 2, y - size / 2, x + size / 2, y + size / 2, p);
}

/* thickOffset of the four instances a thick wave is drawn in: a pixel
 * right, a pixel up, both. */
static const double thick_dx[4] = {0, 1, 0, 1}, thick_dy[4] = {0, 0, 1, 1};

/* ---- Motion vectors ---- */

/* Where the mesh's (u, v) pairs carry the point (fx, fy), as
 * getMotionDir reads them: bilinearly between the four vertices
 * about it, the v flipped. */
static void motion_dir(const double *uv, double fx, double fy, double *ox, double *oy)
{
    int y0 = (int)floor(fy * SK_MILK_MESH_H), x0 = (int)floor(fx * SK_MILK_MESH_W);
    double dy = fy * SK_MILK_MESH_H - y0, dx = fx * SK_MILK_MESH_W - x0;
    const int row = SK_MILK_MESH_W + 1;
    const double *a = uv + (y0 * row + x0) * 2, *b = a + 2, *c = a + row * 2, *d = c + 2;
    *ox = a[0] * (1 - dx) * (1 - dy) + b[0] * dx * (1 - dy) + c[0] * (1 - dx) * dy + d[0] * dx * dy;
    *oy = 1 - (a[1] * (1 - dx) * (1 - dy) + b[1] * dx * (1 - dy) + c[1] * (1 - dx) * dy + d[1] * dx * dy);
}

void sk_milk_draw_motion_vectors(sk_milk_tex *t, const sk_milk_vars *f, const double *uv)
{
    int nx = (int)fmin(fmax(floor(f->mv_x), -1), 1000), ny = (int)fmin(fmax(floor(f->mv_y), -1), 1000);
    if (!(f->mv_a > 0.001) || nx <= 0 || ny <= 0)
        return;
    double dx = f->mv_x - nx, dy = f->mv_y - ny;
    if (nx > 64)
        nx = 64, dx = 0;
    if (ny > 48)
        ny = 48, dy = 0;
    double minlen = 1.0 / t->w;
    paint p = paint_of(f->mv_r, f->mv_g, f->mv_b, f->mv_a, false);
    for (int j = 0; j < ny; j++) {
        double fy = (j + 0.25) / (ny + dy + 0.25 - 1.0) - f->mv_dy;
        if (!(fy > 0.0001 && fy < 0.9999))
            continue;
        for (int i = 0; i < nx; i++) {
            double fx = (i + 0.25) / (nx + dx + 0.25 - 1.0) + f->mv_dx;
            if (!(fx > 0.0001 && fx < 0.9999))
                continue;
            double fx2, fy2;
            motion_dir(uv, fx, fy, &fx2, &fy2);
            double dxi = (fx2 - fx) * f->mv_l, dyi = (fy2 - fy) * f->mv_l;
            double dist = sqrt(dxi * dxi + dyi * dyi);
            /* As butterchurn has it: a short vector is stretched to a
             * pixel, and any other loses its x to one pixel's width. */
            if (dist < minlen && dist > 0.00000001) {
                dxi *= minlen / dist;
                dyi *= minlen / dist;
            } else {
                dxi = minlen;
            }
            line(t, win_x(t, 2 * fx - 1), win_y(t, 2 * fy - 1), win_x(t, 2 * (fx + dxi) - 1),
                 win_y(t, 2 * (fy + dyi) - 1), &p);
        }
    }
}

/* ---- Smoothing ---- */

/* waveUtils.smoothWave: a point between each two, from the four about
 * it with weights -0.15, 1.15, 1.15, -0.15. n points make 2n - 1. */
static void smooth_wave(const float *in, float *out, int n)
{
    const double c1 = -0.15, c2 = 1.15, c3 = 1.15, c4 = -0.15, inv = 1.0 / (c1 + c2 + c3 + c4);
    int j = 0, below = 0, above2 = 1;
    for (int i = 0; i < n - 1; i++) {
        int above = above2;
        above2 = SK_MIN(n - 1, i + 2);
        for (int k = 0; k < 2; k++) {
            out[j * 2 + k] = in[i * 2 + k];
            out[(j + 1) * 2 + k] = (float)((c1 * in[below * 2 + k] + c2 * in[i * 2 + k] + c3 * in[above * 2 + k] +
                                            c4 * in[above2 * 2 + k]) *
                                           inv);
        }
        below = i;
        j += 2;
    }
    out[j * 2] = in[(n - 1) * 2];
    out[j * 2 + 1] = in[(n - 1) * 2 + 1];
}

/* ---- Custom waves ---- */

void sk_milk_draw_custom_waves(sk_milk_tex *t, const sk_milk_preset_def *def, const sk_milk_vars *base,
                               const sk_milk_vars *f, const sk_milk_audio *a, double alpha_mult, double aspectx,
                               double aspecty, sk_milk_rng *rng)
{
    enum { MAX = SK_MILK_WAVE };
    double left[MAX], right[MAX];
    float pos[MAX * 2], smooth[(MAX * 2 - 1) * 2];
    paint col[MAX];
    for (int wi = 0; wi < def->nwaves; wi++) {
        const sk_milk_wave_def *d = &def->waves[wi];
        sk_milk_wave_vars w;
        d->base(&w);
        d->frame(f, &w);
        double ns = fmin(w.samples, MAX);
        int samples = (int)floor(fmax(ns, -1)), sep = (int)floor(fmin(fmax(w.sep, -MAX), MAX));
        samples -= sep;
        bool dots = w.usedots != 0;
        if (!(samples >= 2 || (dots && samples >= 1)) || samples > MAX)
            continue;
        bool spectrum = w.spectrum != 0;
        double scale = (spectrum ? 0.15 : 0.004) * w.scaling * base->wave_scale;
        int j0 = spectrum ? 0 : (int)floor((MAX - samples) / 2.0 - sep / 2.0);
        int j1 = spectrum ? 0 : (int)floor((MAX - samples) / 2.0 + sep / 2.0);
        double step = spectrum ? (double)(MAX - sep) / samples : 1;
        double mix1 = sqrt(fmax(0, w.smoothing * 0.98)), mix2 = 1 - mix1;
        /* MilkDrop smooths the wave forward, then back, then scales. */
#define AT(arr, k) (spectrum ? a->spectrum[SK_MIN(MAX - 1, SK_MAX(0, (k)))] : (double)(arr)[SK_MIN(MAX - 1, SK_MAX(0, (k)))])
        left[0] = AT(a->wave_l, j0);
        right[0] = AT(a->wave_r, j1);
        for (int j = 1; j < samples; j++) {
            left[j] = AT(a->wave_l, (int)floor(j * step + j0)) * mix2 + left[j - 1] * mix1;
            right[j] = AT(a->wave_r, (int)floor(j * step + j1)) * mix2 + right[j - 1] * mix1;
        }
#undef AT
        for (int j = samples - 2; j >= 0; j--) {
            left[j] = left[j] * mix2 + left[j + 1] * mix1;
            right[j] = right[j] * mix2 + right[j + 1] * mix1;
        }
        double fr = w.r, fg = w.g, fb = w.b, fa = w.a;
        w.wx = w.wy = 0;
        for (int j = 0; j < samples; j++) {
            w.sample = samples > 1 ? (double)j / (samples - 1) : 0;
            w.value1 = left[j] * scale;
            w.value2 = right[j] * scale;
            w.x = 0.5 + w.value1;
            w.y = 0.5 + w.value2;
            w.r = fr, w.g = fg, w.b = fb, w.a = fa;
            if (d->point)
                d->point(f, &w, rng, d->param);
            pos[j * 2] = (float)((w.x * 2 - 1) / aspectx);
            pos[j * 2 + 1] = (float)((w.y * -2 + 1) / aspecty);
            col[j] = paint_of(w.r, w.g, w.b, w.a * alpha_mult, w.additive != 0);
        }
        if (dots) {
            /* gl_PointSize: 1, 2 for thick, one more from 1024 wide. */
            double size = (w.thick != 0 ? 2 : 1) + (t->w >= 1024 ? 1 : 0);
            for (int j = 0; j < samples; j++)
                point(t, win_x(t, pos[j * 2]), win_y(t, pos[j * 2 + 1]), size, &col[j]);
            continue;
        }
        smooth_wave(pos, smooth, samples);
        int n = samples * 2 - 1, instances = w.thick != 0 ? 4 : 1;
        for (int k = 0; k < instances; k++)
            for (int j = 0; j + 1 < n; j++)
                line(t, win_x(t, smooth[j * 2]) + thick_dx[k], win_y(t, smooth[j * 2 + 1]) + thick_dy[k],
                     win_x(t, smooth[j * 2 + 2]) + thick_dx[k], win_y(t, smooth[j * 2 + 3]) + thick_dy[k],
                     &col[j / 2]);
    }
}

/* ---- The waveform ---- */

/* processWaveform: the samples scaled by wave_scale / 128 and smoothed
 * forward by wave_smoothing. */
static void process_wave(const int8_t *in, const sk_milk_vars *f, double *out)
{
    double scale = f->wave_scale / 128.0, smooth = f->wave_smoothing, smooth2 = scale * (1 - smooth);
    out[0] = in[0] * scale;
    for (int i = 1; i < SK_MILK_WAVE; i++)
        out[i] = in[i] * smooth2 + out[i - 1] * smooth;
}

/* The wave of mode into pos and, for mode 7, pos2; its point count,
 * and its alpha in *alpha. Positions are clip coordinates, y up. */
static int wave_shape(int mode, const sk_milk_vars *f, const double *wl, const double *wr, double vol,
                      double aspectx, double aspecty, int texw, float *pos, float *pos2, double *alpha)
{
    const int len = SK_MILK_WAVE;
    double a = f->wave_a, p2 = f->wave_mystery;
    double px = f->wave_x * 2 - 1, py = f->wave_y * 2 - 1;
    if ((mode == 0 || mode == 1 || mode == 4) && (p2 < -1 || p2 > 1)) {
        p2 = p2 * 0.5 + 0.5;
        p2 -= floor(p2);
        p2 = fabs(p2) * 2 - 1;
    }
    /* The modes that fill the screen with lines dim by its width. */
    double thin = texw < 1024 ? 0.09 : texw < 2048 ? 0.11 : 0.13;
    switch (mode) {
    case 1:
        a *= 1.25;
        break;
    case 2:
    case 5:
        a *= thin;
        break;
    case 3:
        a *= (texw < 1024 ? 0.15 : texw < 2048 ? 0.22 : 0.33) * 1.3 * f->treb * f->treb;
        break;
    default:
        break;
    }
    if (f->modwavealphabyvolume > 0)
        a *= (vol - f->modwavealphastart) / (f->modwavealphaend - f->modwavealphastart);
    *alpha = fmin(fmax(a, 0), 1);
    int n = 0;
    switch (mode) {
    case 0: {
        n = len / 2 + 1;
        double inv = 1.0 / (n - 1);
        int off = (len - n) / 2;
        for (int i = 0; i < n - 1; i++) {
            double rad = 0.5 + 0.4 * wr[i + off] + p2;
            double ang = i * inv * 2 * M_PI + f->time * 0.2;
            if (i < n / 10.0) {
                double mix = i / (n * 0.1);
                mix = 0.5 - 0.5 * cos(mix * M_PI);
                double rad2 = 0.5 + 0.4 * wr[i + n + off] + p2;
                rad = (1 - mix) * rad2 + rad * mix;
            }
            pos[i * 2] = (float)(rad * cos(ang) * aspecty + px);
            pos[i * 2 + 1] = (float)(rad * sin(ang) * aspectx + py);
        }
        pos[(n - 1) * 2] = pos[0];
        pos[(n - 1) * 2 + 1] = pos[1];
        break;
    }
    case 1:
        n = len / 2;
        for (int i = 0; i < n; i++) {
            double rad = 0.53 + 0.43 * wr[i] + p2;
            double ang = wl[i + 32] * 0.5 * M_PI + f->time * 2.3;
            pos[i * 2] = (float)(rad * cos(ang) * aspecty + px);
            pos[i * 2 + 1] = (float)(rad * sin(ang) * aspectx + py);
        }
        break;
    case 2:
    case 3:
        n = len;
        for (int i = 0; i < n; i++) {
            pos[i * 2] = (float)(wr[i] * aspecty + px);
            pos[i * 2 + 1] = (float)(wl[(i + 32) % len] * aspectx + py);
        }
        break;
    case 4: {
        n = SK_MIN(len, texw / 3);
        if (n < 1)
            return 0;
        double inv = 1.0 / n, w1 = 0.45 + 0.5 * (p2 * 0.5 + 0.5), w2 = 1 - w1;
        int off = (len - n) / 2;
        for (int i = 0; i < n; i++) {
            double x = 2.0 * i * inv + (px - 1) + wr[(i + 25 + off) % len] * 0.44;
            double y = wl[i + off] * 0.47 + py;
            /* Momentum. */
            if (i > 1) {
                x = x * w2 + w1 * (pos[(i - 1) * 2] * 2.0 - pos[(i - 2) * 2]);
                y = y * w2 + w1 * (pos[(i - 1) * 2 + 1] * 2.0 - pos[(i - 2) * 2 + 1]);
            }
            pos[i * 2] = (float)x;
            pos[i * 2 + 1] = (float)y;
        }
        break;
    }
    case 5: {
        double c = cos(f->time * 0.3), s = sin(f->time * 0.3);
        n = len;
        for (int i = 0; i < n; i++) {
            int o = (i + 32) % len;
            double x0 = wr[i] * wl[o] + wl[i] * wr[o], y0 = wr[i] * wr[i] - wl[o] * wl[o];
            pos[i * 2] = (float)((x0 * c - y0 * s) * (aspecty + px));
            pos[i * 2 + 1] = (float)((x0 * s + y0 * c) * (aspectx + py));
        }
        break;
    }
    default: { /* 6 and 7: a line across the screen at angle p2. */
        n = SK_MIN(len / 2, texw / 3);
        if (n < 1)
            return 0;
        int off = (len - n) / 2;
        double ang = M_PI * 0.5 * p2, dx = cos(ang), dy = sin(ang);
        double ex[2] = {px * cos(ang + M_PI * 0.5) - dx * 3.0, px * cos(ang + M_PI * 0.5) + dx * 3.0};
        double ey[2] = {px * sin(ang + M_PI * 0.5) - dy * 3.0, px * sin(ang + M_PI * 0.5) + dy * 3.0};
        /* Each end pulled in to 1.1 from the centre. */
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < 4; j++) {
                double edge = j < 2 ? ex[i] : ey[i], other = j < 2 ? ex[1 - i] : ey[1 - i];
                double lim = j % 2 == 0 ? 1.1 : -1.1;
                if (j % 2 == 0 ? edge > lim : edge < lim) {
                    double tt = (lim - other) / (edge - other);
                    double dxi = ex[i] - ex[1 - i], dyi = ey[i] - ey[1 - i];
                    ex[i] = ex[1 - i] + dxi * tt;
                    ey[i] = ey[1 - i] + dyi * tt;
                }
            }
        dx = (ex[1] - ex[0]) / n;
        dy = (ey[1] - ey[0]) / n;
        double a2 = atan2(dy, dx), perpx = cos(a2 + M_PI * 0.5), perpy = sin(a2 + M_PI * 0.5);
        double sep = mode == 7 ? pow(py * 0.5 + 0.5, 2) : 0;
        for (int i = 0; i < n; i++) {
            double s = wl[i + off];
            pos[i * 2] = (float)(ex[0] + dx * i + perpx * (0.25 * s + sep));
            pos[i * 2 + 1] = (float)(ey[0] + dy * i + perpy * (0.25 * s + sep));
        }
        if (mode == 7)
            for (int i = 0; i < n; i++) {
                double s = wr[i + off];
                pos2[i * 2] = (float)(ex[0] + dx * i + perpx * (0.25 * s - sep));
                pos2[i * 2 + 1] = (float)(ey[0] + dy * i + perpy * (0.25 * s - sep));
            }
        break;
    }
    }
    return n;
}

/* The old wave's point at fraction index fi of its n, by linear
 * interpolation; the next point held to the last. */
static void old_at(const float *old, int n, double fi, double *x, double *y)
{
    int i = (int)floor(fi);
    double t = fi - i;
    i = SK_MIN(SK_MAX(i, 0), n - 1);
    int k = SK_MIN(i + 1, n - 1);
    *x = old[i * 2] * (1 - t) + old[k * 2] * t;
    *y = old[i * 2 + 1] * (1 - t) + old[k * 2 + 1] * t;
}

void sk_milk_draw_basic_wave(sk_milk_tex *t, const sk_milk_vars *f, const sk_milk_audio *a, bool blending,
                             double progress, double aspectx, double aspecty)
{
    enum { LEN = SK_MILK_WAVE };
    double vol = (f->bass + f->mid + f->treb) / 3.0;
    if (!(vol > -0.01) || !(f->wave_a > 0.001))
        return;
    double wl[LEN], wr[LEN];
    process_wave(a->wave_l, f, wl);
    process_wave(a->wave_r, f, wr);
    int mode = (int)fmod(floor(fmax(f->wave_mode, 0)), 8), old_mode = (int)fmod(floor(fmax(f->old_wave_mode, 0)), 8);
    float pos[LEN * 2], pos2[LEN * 2], old[LEN * 2], old2[LEN * 2];
    float smooth[(LEN * 2 - 1) * 2], smooth2[(LEN * 2 - 1) * 2];
    memset(pos2, 0, sizeof pos2);
    memset(old2, 0, sizeof old2);
    double alpha, old_alpha = 0;
    int n = wave_shape(mode, f, wl, wr, vol, aspectx, aspecty, t->w, pos, pos2, &alpha);
    int old_n = 0;
    if (blending && mode != old_mode)
        old_n = wave_shape(old_mode, f, wl, wr, vol, aspectx, aspecty, t->w, old, old2, &old_alpha);
    if (n < 1)
        return;
    /* While blending from another mode, each point moves from the old
     * wave's point at the same fraction along it. */
    double mix = 0.5 - 0.5 * cos(progress * M_PI), mix2 = 1 - mix;
    if (old_n > 0) {
        alpha = mix * alpha + mix2 * old_alpha;
        double x, y;
        if (mode == 7) {
            double m = (old_n - 1) / (n * 2.0);
            for (int i = 0; i < n; i++) {
                old_at(old, old_n, i * m, &x, &y);
                pos[i * 2] = (float)(pos[i * 2] * mix + x * mix2);
                pos[i * 2 + 1] = (float)(pos[i * 2 + 1] * mix + y * mix2);
            }
            for (int i = 0; i < n; i++) {
                old_at(old, old_n, (i + n) * m, &x, &y);
                pos2[i * 2] = (float)(pos2[i * 2] * mix + x * mix2);
                pos2[i * 2 + 1] = (float)(pos2[i * 2 + 1] * mix + y * mix2);
            }
        } else if (old_mode == 7) {
            int half = n / 2;
            double m = (old_n - 1) / (n / 2.0);
            for (int i = 0; i < half; i++) {
                old_at(old, old_n, i * m, &x, &y);
                pos[i * 2] = (float)(pos[i * 2] * mix + x * mix2);
                pos[i * 2 + 1] = (float)(pos[i * 2 + 1] * mix + y * mix2);
            }
            for (int i = 0; i < half; i++) {
                old_at(old2, old_n, i * m, &x, &y);
                pos2[i * 2] = (float)(pos[(i + half) * 2] * mix + x * mix2);
                pos2[i * 2 + 1] = (float)(pos[(i + half) * 2 + 1] * mix + y * mix2);
            }
        } else {
            double m = (old_n - 1) / (double)n;
            for (int i = 0; i < n; i++) {
                old_at(old, old_n, i * m, &x, &y);
                pos[i * 2] = (float)(pos[i * 2] * mix + x * mix2);
                pos[i * 2 + 1] = (float)(pos[i * 2 + 1] * mix + y * mix2);
            }
        }
    }
    double r = fmin(fmax(f->wave_r, 0), 1), g = fmin(fmax(f->wave_g, 0), 1), b = fmin(fmax(f->wave_b, 0), 1);
    if (f->wave_brighten != 0) {
        double m = fmax(r, fmax(g, b));
        if (m > 0.01)
            r /= m, g /= m, b /= m;
    }
    paint p = paint_of(r, g, b, alpha, f->additivewave != 0);
    /* GL's y runs up; the waves were made with it down. */
    for (int i = 0; i < n; i++)
        pos[i * 2 + 1] = -pos[i * 2 + 1];
    smooth_wave(pos, smooth, n);
    bool second = mode == 7 || old_mode == 7;
    if (second) {
        for (int i = 0; i < n; i++)
            pos2[i * 2 + 1] = -pos2[i * 2 + 1];
        smooth_wave(pos2, smooth2, n);
    }
    bool dots = f->wave_dots != 0;
    int instances = f->wave_thick != 0 || dots ? 4 : 1, count = n * 2 - 1;
    for (int pass = 0; pass < (mode == 7 ? 2 : 1); pass++) {
        const float *s = pass ? smooth2 : smooth;
        for (int k = 0; k < instances; k++) {
            if (dots) {
                for (int j = 0; j < count; j++)
                    point(t, win_x(t, s[j * 2]) + thick_dx[k], win_y(t, s[j * 2 + 1]) + thick_dy[k], 1, &p);
                continue;
            }
            for (int j = 0; j + 1 < count; j++)
                line(t, win_x(t, s[j * 2]) + thick_dx[k], win_y(t, s[j * 2 + 1]) + thick_dy[k],
                     win_x(t, s[j * 2 + 2]) + thick_dx[k], win_y(t, s[j * 2 + 3]) + thick_dy[k], &p);
        }
    }
}

/* ---- Sprites ---- */

void sk_milk_draw_darken_center(sk_milk_tex *t, const sk_milk_vars *f, double aspecty)
{
    if (f->darken_center == 0)
        return;
    /* A fan about the centre, 3/32 black there and clear at its four
     * points, 0.05 of the height out and as far across in pixels. */
    double cx = t->w * 0.5, cy = t->h * 0.5;
    double hx = 0.05 * aspecty * t->w * 0.5, hy = 0.05 * t->h * 0.5;
    double c0 = ceil(cx - hx - 0.5), c1 = ceil(cx + hx - 0.5) - 1, r0 = ceil(cy - hy - 0.5), r1 = ceil(cy + hy - 0.5) - 1;
    hold(&c0, &c1, t->w);
    hold(&r0, &r1, t->h);
    for (int y = (int)r0; y <= (int)r1; y++)
        for (int x = (int)c0; x <= (int)c1; x++) {
            double d = fabs(x + 0.5 - cx) / hx + fabs(y + 0.5 - cy) / hy;
            if (d < 1) {
                paint p = paint_of(0, 0, 0, 3.0 / 32 * (1 - d), false);
                put(t, x, y, &p);
            }
        }
}

void sk_milk_draw_border(sk_milk_tex *t, const double rgba[4], double size, double prev)
{
    if (!(size > 0) || !(rgba[3] > 0))
        return;
    /* In clip units: the border runs from prev to prev + size in from
     * each edge, the sides between the top and bottom strips. */
    double in = prev, out = size + prev;
    paint p = paint_of(rgba[0], rgba[1], rgba[2], rgba[3], false);
    double x[4] = {win_x(t, -1 + in), win_x(t, -1 + out), win_x(t, 1 - out), win_x(t, 1 - in)};
    double y[4] = {win_y(t, -1 + in), win_y(t, -1 + out), win_y(t, 1 - out), win_y(t, 1 - in)};
    fill(t, x[0], y[1], x[1], y[2], &p);
    fill(t, x[2], y[1], x[3], y[2], &p);
    fill(t, x[0], y[0], x[3], y[1], &p);
    fill(t, x[0], y[2], x[3], y[3], &p);
}
