/*
  Two render effects of Advanced Visualization Studio (AVS), ported to
  C for the visualization (decisions 0015, 0036) from app/viz/avs.js:
  the Superscope, running three examples of its built-in list, and the
  Dot Plane. avs.js ported them from the BSD-licensed AVS source as the
  vis_avs port keeps it, https://codeberg.org/exo-cortex/vis_avs at
  commit 1cee1f3d4f783b830538c3e4d2082a2c59521dee: avs/vis_avs/
  e_superscope.h (the examples), e_superscope.cpp, e_dotplane.cpp,
  e_dotplane.h and matrix.cpp. The audio they read is the
  visualization's synthetic signal.

  LICENSE
  -------
Copyright 2005 Nullsoft, Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

  * Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.

  * Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.

  * Neither the name of Nullsoft nor the names of its contributors may be used to
    endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include "internal.h"

/* EEL's rand(x): an integer from 0 below x. */
static double eel_rand(sk_vmode *m, double x)
{
    double n = SK_MAX(1, floor(x));
    return floor(sk_vmode_rand(m) * n);
}

/* The audio value AVS gives the Superscope at i in [0, 1]: the
 * waveform, linearly interpolated, in [-1, 1]. */
static double wave(const float *samples, double i)
{
    double p = sk_vm_clamp(i, 0, 1) * (SK_SYNTH_N - 1);
    int k = SK_MIN(SK_SYNTH_N - 2, (int)floor(p));
    return samples[k] + (samples[k + 1] - samples[k]) * (p - k);
}

/* The spectrum the Dot Plane reads per column, 0 to 255: the
 * synthesiser's log-spaced bands stretched across the columns. */
static int spectrum(const double *bands, int x, int cols)
{
    double p = (double)x * (SK_SYNTH_BANDS - 1) / (cols - 1);
    int k = SK_MIN(SK_SYNTH_BANDS - 2, (int)floor(p));
    double b = bands[k] + (bands[k + 1] - bands[k]) * (p - k);
    return (int)SK_MIN(255, floor(255 * sk_vm_log10(1 + 30 * b) / 1.2));
}

/* ---- Superscope ---- */

/* An example's variables, kept across frames as EEL keeps them. */
typedef struct {
    double n, t, i, v, x, y;
} eel;

enum { SPIRAL, BOW, GRAPH };

/* Examples of the Superscope's built-in list, translated from EEL:
 * "Spiral", "Rotating Bow Thing" and "Spiral Graph Fun". */
static void ex_init(int ex, eel *s)
{
    if (ex == SPIRAL) {
        s->n = 800;
    } else {
        s->n = ex == BOW ? 80 : 100;
        s->t = 0;
    }
}

static void ex_frame(int ex, eel *s)
{
    s->t += ex == SPIRAL ? -0.05 : 0.01;
}

static void ex_point(int ex, eel *s)
{
    if (ex == SPIRAL) {
        double d = s->i + s->v * 0.2, r = s->t + s->i * SK_PI * 4;
        s->x = sk_vm_cos(r) * d;
        s->y = sk_vm_sin(r) * d;
    } else if (ex == BOW) {
        double r = s->i * SK_PI * 2, d = sk_vm_sin(r * 3) + s->v * 0.5;
        s->x = sk_vm_cos(s->t + r) * d;
        s->y = sk_vm_sin(s->t - r) * d;
    } else {
        double r = s->i * SK_PI * 128 + s->t;
        s->x = sk_vm_cos(r / 64) * 0.7 + sk_vm_sin(r) * 0.3;
        s->y = sk_vm_sin(r / 64) * 0.7 + sk_vm_cos(r) * 0.3;
    }
}

/* One Superscope of an effect list: its example and its colours, which
 * it fades through, 64 frames from one to the next. */
typedef struct {
    int ex;
    const uint32_t *colors;
    int n_colors;
    eel s;
    bool fresh;
    int pos;
} scope;

static void scope_frame(sk_vmode *m, scope *sc, sk_vc *g, double w, double h, const float *samples, bool beat)
{
    sc->pos = (sc->pos + 1) % (sc->n_colors * 64);
    int p = sc->pos / 64, f = sc->pos & 63;
    uint32_t c1 = sc->colors[p], c2 = sc->colors[(p + 1) % sc->n_colors];
    int rgb[3];
    for (int k = 0, sh = 16; k < 3; k++, sh -= 8)
        rgb[k] = (int)((((c1 >> sh) & 255) * (63 - f) + ((c2 >> sh) & 255) * f) / 64);
    eel *s = &sc->s;
    if (sc->fresh) {
        ex_init(sc->ex, s);
        sc->fresh = false;
    }
    ex_frame(sc->ex, s);
    if (beat && sc->ex == GRAPH)
        s->n = 80 + eel_rand(m, 120.0);
    int n = (int)SK_MIN(128 * 1024, floor(s->n));
    if (n < 2)
        return;
    sk_vc_paint paint = sk_vc_solid(SK_RGB(rgb[0], rgb[1], rgb[2]));
    sk_vc_begin(g);
    for (int k = 0; k < n; k++) {
        s->i = (double)k / (n - 1);
        s->v = wave(samples, s->i);
        ex_point(sc->ex, s);
        double x = (s->x + 1) * w * 0.5, y = (s->y + 1) * h * 0.5;
        /* A point the equations blew up is skipped, and the line
         * restarts. */
        if (!isfinite(x) || !isfinite(y)) {
            sk_vc_stroke(g, &paint);
            sk_vc_begin(g);
            continue;
        }
        if (k)
            sk_vc_line(g, x, y);
        else
            sk_vc_move(g, x, y);
    }
    sk_vc_stroke(g, &paint);
}

static const uint32_t GRAPH_COLORS[] = {0x1c2899, 0x5a2290, 0x125e5e};
static const uint32_t BOW_COLORS[] = {0xff7a1a, 0xff2a5a, 0xffd21a};
static const uint32_t SPIRAL_COLORS[] = {0xffffff, 0x9fe8ff, 0xffe08a};

#define STEP (1.0 / 60)

typedef struct {
    sk_vmode base;
    scope list[3];
    double acc;
    long beats;
} superscope;

static void super_init(sk_vmode *m)
{
    superscope *ss = (superscope *)m;
    const uint32_t *cols[3] = {GRAPH_COLORS, BOW_COLORS, SPIRAL_COLORS};
    int exs[3] = {GRAPH, BOW, SPIRAL};
    for (int i = 0; i < 3; i++) {
        ss->list[i].ex = exs[i];
        ss->list[i].colors = cols[i];
        ss->list[i].n_colors = 3;
        ss->list[i].s.n = 100;
        ss->list[i].fresh = true;
    }
    ss->beats = -1;
}

/* The effect list of the superscope mode: a fade, then three scopes. */
static void super_draw(sk_vmode *m, sk_synth *s, double dt)
{
    superscope *ss = (superscope *)m;
    sk_vc *g = &m->vc;
    double w = m->w, h = m->h;
    ss->acc = SK_MIN(ss->acc + dt, 0.1);
    sk_vc_line_width(g, SK_MAX(1, sk_vm_round(SK_MIN(w, h) / 360)));
    sk_vc_join(g, SK_JOIN_ROUND);
    while (ss->acc >= STEP) {
        ss->acc -= STEP;
        bool beat = ss->beats >= 0 && s->beats != ss->beats;
        ss->beats = s->beats;
        sk_vc_paint fade = sk_vc_solid(sk_vc_rgba(0, 0, 0, 0.14));
        sk_vc_fill_rect(g, 0, 0, w, h, &fade);
        for (int i = 0; i < 3; i++)
            scope_frame(m, &ss->list[i], g, w, h, s->samples, beat);
    }
}

const sk_vmode_class sk_vmode_superscope = {sizeof(superscope), super_init, super_draw, NULL, NULL};

/* ---- Dot Plane ---- */

#define GRID 64

/* matrix.cpp's, in floats as AVS keeps them. */
static void rotate(float *m, int axis, double deg)
{
    double d = deg * 3.141592653589 / 180;
    memset(m, 0, 16 * sizeof *m);
    m[((axis - 1) << 2) + axis - 1] = m[15] = 1;
    int m1 = axis % 3, m2 = (m1 + 1) % 3;
    double c = sk_vm_cos(d), s = sk_vm_sin(d);
    m[(m1 << 2) + m1] = (float)c;
    m[(m1 << 2) + m2] = (float)s;
    m[(m2 << 2) + m2] = (float)c;
    m[(m2 << 2) + m1] = (float)-s;
}

static void translate(float *m, double x, double y, double z)
{
    memset(m, 0, 16 * sizeof *m);
    m[0] = m[5] = m[10] = m[15] = 1;
    m[3] = (float)x;
    m[7] = (float)y;
    m[11] = (float)z;
}

static void multiply(float *dest, const float *src)
{
    float t[16];
    memcpy(t, dest, sizeof t);
    for (int i = 0; i < 16; i += 4)
        for (int j = 0; j < 4; j++)
            dest[i + j] = (float)((double)src[i] * t[j] + (double)src[i + 1] * t[4 + j] +
                                  (double)src[i + 2] * t[8 + j] + (double)src[i + 3] * t[12 + j]);
}

/* The default colours of the Dot Plane, low to high, as 0xRRGGBB. */
static const uint32_t PLANE_COLORS[] = {0x1c6b18, 0xff0a23, 0x2a1d74, 0x9036d9, 0x6b88ff};

static void color_map(uint32_t map[64])
{
    for (int t = 0; t < 4; t++) {
        uint32_t c1 = PLANE_COLORS[t], c2 = PLANE_COLORS[t + 1];
#define CH(c, sh) ((int32_t)(((c) >> (sh)) & 255))
        int32_t r = CH(c1, 0) * 65536, g = CH(c1, 8) * 65536, b = CH(c1, 16) * 65536;
        int32_t dr = (CH(c2, 0) - CH(c1, 0)) * 65536 / 16;
        int32_t dg = (CH(c2, 8) - CH(c1, 8)) * 65536 / 16;
        int32_t db = (CH(c2, 16) - CH(c1, 16)) * 65536 / 16;
#undef CH
        for (int x = 0; x < 16; x++) {
            map[t * 16 + x] = (uint32_t)(r >> 16) | (uint32_t)(g >> 16) << 8 | (uint32_t)(b >> 16) << 16;
            r += dr;
            g += dg;
            b += db;
        }
    }
}

/* AVS's rotation speed runs from -50 to 50, 16 by default, in fifths of
 * a degree per frame. 6 turns the plane once in 10 s at 30 frames. */
#define SPEED 6
#define ANGLE (-20)
#define PLANE_STEP (1.0 / 30)

typedef struct {
    sk_vmode base;
    /* AVS drew into a frame buffer one pixel per dot. Here the buffer
     * is half the pane, in points, scaled up without smoothing. */
    sk_image buf;
    float height[GRID * GRID], delta[GRID * GRID];
    uint32_t color[GRID * GRID];
    uint32_t map[64];
    float line[GRID];
    double rotation, acc;
} dotplane;

static void plane_init(sk_vmode *m)
{
    dotplane *d = (dotplane *)m;
    color_map(d->map);
    d->acc = PLANE_STEP;
}

static void plane_frame(dotplane *d, const double *bands)
{
    memcpy(d->line, d->height, sizeof d->line);
    for (int y = 0, row = (GRID - 2) * GRID; y < GRID; y++, row -= GRID) {
        int next = row + GRID;
        if (y == GRID - 1) {
            for (int x = 0; x < GRID; x++) {
                int a = spectrum(bands, x, GRID);
                d->height[next + x] = (float)a;
                d->color[next + x] = d->map[SK_MIN(a >> 2, 63)];
                d->delta[next + x] = (float)((a - d->line[x]) / 90.0);
            }
        } else {
            for (int x = 0; x < GRID; x++) {
                double hgt = (double)d->height[row + x] + d->delta[row + x];
                if (hgt < 0)
                    hgt = 0;
                d->height[next + x] = (float)hgt;
                d->delta[next + x] = (float)(d->delta[row + x] - 0.15 * (hgt / 255));
                d->color[next + x] = d->color[row + x];
            }
        }
    }
}

static void plane_plot(dotplane *d, int w, int h)
{
    sk_fill_rect(&d->buf, sk_image_bounds(&d->buf), SK_RGB(0, 0, 0), SK_BLEND_SRC, NULL);
    float m[16], t2[16];
    rotate(m, 2, d->rotation);
    rotate(t2, 1, ANGLE);
    multiply(m, t2);
    translate(t2, 0, -20, 400);
    multiply(m, t2);
    double zoom = SK_MIN(w * 440.0 / 640, h * 440.0 / 480);
    for (int y = 0; y < GRID; y++) {
        int start = (d->rotation < 90 || d->rotation > 270) ? GRID - y - 1 : y;
        double step = 350.0 / GRID;
        double cy = -(GRID * 0.5) * step;
        double cx = (start - GRID * 0.5) * step;
        int at = start * GRID, dir = 1;
        if (d->rotation < 180) {
            dir = -1;
            step = -step;
            cy = -cy + step;
            at += GRID - 1;
        }
        for (int x = 0; x < GRID; x++) {
            double vy = 64 - (double)d->height[at];
            double ox = cy * m[0] + vy * m[1] + cx * m[2] + m[3];
            double oy = cy * m[4] + vy * m[5] + cx * m[6] + m[7];
            double oz = cy * m[8] + vy * m[9] + cx * m[10] + m[11];
            if (oz > 1) {
                double z = zoom / oz;
                double sx = trunc(ox * z) + (w >> 1), sy = trunc(oy * z) + (h >> 1);
                if (sy >= 0 && sy < h && sx >= 0 && sx < w)
                    sk_image_row(&d->buf, (int)sy)[(int)sx] = 0xff000000u | d->color[at];
            }
            cy += step;
            at += dir;
        }
    }
    d->rotation += SPEED / 5.0;
    if (d->rotation >= 360)
        d->rotation -= 360;
    if (d->rotation < 0)
        d->rotation += 360;
}

static void plane_draw(sk_vmode *m, sk_synth *s, double dt)
{
    dotplane *d = (dotplane *)m;
    int w = SK_MAX(1, m->w >> 1), h = SK_MAX(1, m->h >> 1);
    if (d->buf.w != w || d->buf.h != h) {
        sk_image_free(&d->buf);
        sk_image_init(&d->buf, w, h);
    }
    d->acc = SK_MIN(d->acc + dt, 0.2);
    if (d->acc < PLANE_STEP)
        return;
    while (d->acc >= PLANE_STEP) {
        d->acc -= PLANE_STEP;
        plane_frame(d, s->bands);
    }
    plane_plot(d, w, h);
    sk_vc_image(&m->vc, &d->buf, 0, 0, m->w, m->h, false);
}

static void plane_fini(sk_vmode *m)
{
    sk_image_free(&((dotplane *)m)->buf);
}

const sk_vmode_class sk_vmode_dotplane = {sizeof(dotplane), plane_init, plane_draw, NULL, plane_fini};
