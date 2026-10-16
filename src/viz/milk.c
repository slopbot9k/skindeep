/* The MilkDrop renderer (milk.h): butterchurn 2.6.7's Renderer, its
 * PresetEquationRunner, BlendPattern, warp, blur and composite passes,
 * ported from its JavaScript and GLSL (MIT, Copyright (c) 2013-2018
 * Jordan Berg; third_party/butterchurn/LICENSE) to run on the CPU.
 *
 * A frame, as butterchurn's render() makes it:
 *  - the time steps by 1/fps, fps being a damped average of the frame
 *    times; the audio levels follow;
 *  - the preset's per-frame equations, then its per-pixel equations at
 *    each vertex of a 48 by 36 mesh, which give each vertex the (u, v)
 *    in the last frame its pixel comes from;
 *  - the warp: the last frame drawn through the mesh, sampled
 *    bilinearly, then the preset's warp shader, into an 8-bit texture;
 *  - the blur, for a preset whose shader reads it;
 *  - motion vectors, custom waves, the waveform, the darkened centre
 *    and the borders over it (milkwave.c);
 *  - the composite shader over the texture into the canvas, which the
 *    browser stretches to the pane.
 *
 * The texture is at the pane's size in points, as viz.js makes
 * butterchurn's canvas (pixelRatio 1), and the stretch to the pane's
 * pixels is bilinear. The warp, the composite and the stretch run on
 * the worker pool; the rest is a few thousand pixels a frame.
 *
 * Integer arithmetic carries the warp from the mesh to the texel, so
 * its rounding is the same everywhere; the mesh itself is worked in
 * double. */
#include "milkint.h"

#include <math.h>
#include <string.h>

#define MW SK_MILK_MESH_W
#define MH SK_MILK_MESH_H
#define VERTS SK_MILK_MESH_VERTS
/* The frame times butterchurn averages its fps over. */
#define HIST 120
/* The composite's mesh, whose vertices carry hue_shader. */
#define CW 32
#define CH 24
/* The warp's texel coordinates are 16.16 fixed point, BIAS texture
 * widths over their place so they stay positive; a mesh coordinate is
 * held to -2..3 first. */
#define BIAS 4
#define ONE 65536

typedef struct {
    const sk_milk_preset_def *def;
    int index;
    /* butterchurn's mdVS: the defaults, the preset's base values and
     * what its init_eqs set; and mdVSFrame, the last frame's. */
    sk_milk_vars base, frame;
} preset_run;

struct sk_milk {
    sk_milk_rng rng;
    sk_milk_audio audio;
    /* calcTimeAndFPS: the time, the fps and the frame times. */
    double time, fps, hist[HIST + 1];
    int nhist, frame;
    /* The preset showing and, while blending, the one before it. */
    preset_run cur, prev;
    bool blending;
    double blend_start, blend_dur, progress;
    /* BlendPattern's vertInfoA and vertInfoC: a vertex moves from the
     * old preset's warp to the new one's as A progress + C goes from 0
     * to 1. */
    double blend_a[VERTS], blend_c[VERTS];
    /* The texture's size, and its aspects: on the shorter side's axis
     * the shorter side over the longer, on the other 1. */
    int w, h;
    double aspectx, aspecty;
    /* tex[target] is drawn this frame from the other; mip[i] is level 1
     * of tex[i] as its last generateMipmap left it. */
    sk_milk_tex tex[2], mip[2];
    int target;
    /* warpUVs and warpColor's alpha, the vertices in butterchurn's
     * order, rows from GL's bottom. */
    double uv[VERTS * 2], alpha[VERTS];
    /* The warp: the mesh as texel coordinates with rows from the top
     * (fixed point), each pixel column's and row's cell and its 16-bit
     * fraction across it, and each texel coordinate wrapped. */
    int64_t vx[VERTS], vy[VERTS], va[VERTS];
    int *col_cell, *row_cell, *wrap_x, *wrap_y;
    int32_t *col_frac, *row_frac;
    int wrap;
    /* The blur: its horizontal pass, that pass's level 1, and blur1. */
    sk_milk_tex blur_h, blur_h1, blur1;
    /* The canvas at the texture's size, and the new preset's composite
     * while blending. */
    sk_milk_tex canvas, canvas2;
    double comp_alpha[(CW + 1) * (CH + 1)];
    /* The stretch to the pane: each pixel column's and row's two texels
     * and the 8-bit weight of the second. */
    int out_w, out_h;
    int *sx0, *sx1, *sfx, *sy0, *sy1, *sfy;
};

/* ---- Textures ---- */

static void tex_resize(sk_milk_tex *t, int w, int h)
{
    free(t->px);
    t->px = sk_xcalloc((size_t)w * (size_t)h, 4);
    t->w = w;
    t->h = h;
}

static void tex_free(sk_milk_tex *t)
{
    free(t->px);
    memset(t, 0, sizeof *t);
}

static int wrap_index(int i, int n, bool repeat)
{
    if (repeat)
        return ((i % n) + n) % n;
    return i < 0 ? 0 : i >= n ? n - 1 : i;
}

/* A LINEAR sampler at (u, v), REPEAT or CLAMP_TO_EDGE. */
static void sample(const sk_milk_tex *t, double u, double v, bool repeat, double rgb[3])
{
    double x = u * t->w - 0.5, y = v * t->h - 0.5;
    if (!isfinite(x) || !isfinite(y)) {
        rgb[0] = rgb[1] = rgb[2] = 0;
        return;
    }
    x = fmin(fmax(x, -4.0 * t->w), 5.0 * t->w);
    y = fmin(fmax(y, -4.0 * t->h), 5.0 * t->h);
    double fx = floor(x), fy = floor(y);
    int x0 = (int)fx, y0 = (int)fy;
    double ax = x - fx, ay = y - fy;
    int xa = wrap_index(x0, t->w, repeat), xb = wrap_index(x0 + 1, t->w, repeat);
    int ya = wrap_index(y0, t->h, repeat), yb = wrap_index(y0 + 1, t->h, repeat);
    const uint8_t *r0 = t->px + (size_t)ya * (size_t)t->w * 4, *r1 = t->px + (size_t)yb * (size_t)t->w * 4;
    for (int c = 0; c < 3; c++) {
        double top = r0[xa * 4 + c] * (1 - ax) + r0[xb * 4 + c] * ax;
        double bot = r1[xa * 4 + c] * (1 - ax) + r1[xb * 4 + c] * ax;
        rgb[c] = (top * (1 - ay) + bot * ay) / 255.0;
    }
}

/* generateMipmap's level 1: each texel the mean of four, rounded. */
static void mipmap(const sk_milk_tex *s, sk_milk_tex *d)
{
    int w = SK_MAX(1, s->w / 2), h = SK_MAX(1, s->h / 2);
    if (d->w != w || d->h != h)
        tex_resize(d, w, h);
    for (int y = 0; y < h; y++) {
        const uint8_t *r0 = s->px + (size_t)SK_MIN(2 * y, s->h - 1) * (size_t)s->w * 4;
        const uint8_t *r1 = s->px + (size_t)SK_MIN(2 * y + 1, s->h - 1) * (size_t)s->w * 4;
        uint8_t *o = d->px + (size_t)y * (size_t)w * 4;
        for (int x = 0; x < w; x++) {
            int a = SK_MIN(2 * x, s->w - 1) * 4, b = SK_MIN(2 * x + 1, s->w - 1) * 4;
            for (int c = 0; c < 4; c++)
                o[x * 4 + c] = (uint8_t)((r0[a + c] + r0[b + c] + r1[a + c] + r1[b + c] + 2) >> 2);
        }
    }
}

/* ---- Random numbers and the blend pattern ---- */

static double rnd(sk_milk *m)
{
    return sk_milk_random(&m->rng);
}

/* genPlasma: midpoint displacement over the vertices of [x0, x1] by
 * [y0, y1], the displacement halving each level. */
static void gen_plasma(sk_milk *m, int x0, int x1, int y0, int y1, double dt)
{
    const int row = MW + 1;
    double *c = m->blend_c;
    int midx = (x0 + x1) / 2, midy = (y0 + y1) / 2;
    double t00 = c[y0 * row + x0], t01 = c[y0 * row + x1], t10 = c[y1 * row + x0], t11 = c[y1 * row + x1];
    if (y1 - y0 >= 2) {
        if (x0 == 0)
            c[midy * row + x0] = 0.5 * (t00 + t10) + (rnd(m) * 2 - 1) * dt * m->aspecty;
        c[midy * row + x1] = 0.5 * (t01 + t11) + (rnd(m) * 2 - 1) * dt * m->aspecty;
    }
    if (x1 - x0 >= 2) {
        if (y0 == 0)
            c[y0 * row + midx] = 0.5 * (t00 + t01) + (rnd(m) * 2 - 1) * dt * m->aspectx;
        c[y1 * row + midx] = 0.5 * (t10 + t11) + (rnd(m) * 2 - 1) * dt * m->aspectx;
    }
    if (y1 - y0 >= 2 && x1 - x0 >= 2) {
        t00 = c[midy * row + x0];
        t01 = c[midy * row + x1];
        t10 = c[y0 * row + midx];
        t11 = c[y1 * row + midx];
        c[midy * row + midx] = 0.25 * (t10 + t11 + t00 + t01) + (rnd(m) * 2 - 1) * dt;
        gen_plasma(m, x0, midx, y0, midy, dt * 0.5);
        gen_plasma(m, midx, x1, y0, midy, dt * 0.5);
        gen_plasma(m, x0, midx, midy, y1, dt * 0.5);
        gen_plasma(m, midx, x1, midy, y1, dt * 0.5);
    }
}

/* createBlendPattern: a band sweeping across at a random angle, a
 * plasma, or a circle opening or closing, picked at random. */
static void blend_pattern(sk_milk *m)
{
    int type = 1 + (int)floor(rnd(m) * 3);
    int k = 0;
    if (type == 1) {
        double ang = rnd(m) * 6.28, vx = cos(ang), vy = sin(ang), band = 0.1 + 0.2 * rnd(m), inv = 1.0 / band;
        for (int y = 0; y <= MH; y++) {
            double fy = (double)y / MH * m->aspecty;
            for (int x = 0; x <= MW; x++, k++) {
                double fx = (double)x / MW * m->aspectx;
                double t = (fx - 0.5) * vx + (fy - 0.5) * vy + 0.5;
                t = (t - 0.5) / sqrt(2) + 0.5;
                m->blend_a[k] = inv * (1 + band);
                m->blend_c[k] = -inv + inv * t;
            }
        }
    } else if (type == 2) {
        double band = 0.12 + 0.13 * rnd(m), inv = 1.0 / band;
        m->blend_c[0] = rnd(m);
        m->blend_c[MW] = rnd(m);
        m->blend_c[MH * (MW + 1)] = rnd(m);
        m->blend_c[MH * (MW + 1) + MW] = rnd(m);
        gen_plasma(m, 0, MW, 0, MH, 0.25);
        double lo = m->blend_c[0], hi = m->blend_c[0];
        for (int i = 0; i < VERTS; i++)
            lo = fmin(lo, m->blend_c[i]), hi = fmax(hi, m->blend_c[i]);
        double mult = hi > lo ? 1.0 / (hi - lo) : 1;
        for (int i = 0; i < VERTS; i++) {
            m->blend_a[i] = inv * (1 + band);
            m->blend_c[i] = -inv + inv * (m->blend_c[i] - lo) * mult;
        }
    } else {
        double band = 0.02 + 0.14 * rnd(m) + 0.34 * rnd(m), inv = 1.0 / band;
        int dir = (int)floor(rnd(m) * 2) * 2 - 1;
        for (int y = 0; y <= MH; y++) {
            double dy = ((double)y / MH - 0.5) * m->aspecty;
            for (int x = 0; x <= MW; x++, k++) {
                double dx = ((double)x / MW - 0.5) * m->aspectx;
                double t = sqrt(dx * dx + dy * dy) * 1.41421;
                if (dir == -1)
                    t = 1 - t;
                m->blend_a[k] = inv * (1 + band);
                m->blend_c[k] = -inv + inv * t;
            }
        }
    }
}

/* ---- Presets ---- */

const char *sk_milk_name(int preset)
{
    return preset >= 0 && preset < SK_MILK_PRESETS ? sk_milk_presets[preset].id : NULL;
}

int sk_milk_find(const char *id)
{
    for (int i = 0; id && i < SK_MILK_PRESETS; i++)
        if (!strcmp(id, sk_milk_presets[i].id))
            return i;
    return -1;
}

/* The globals of the frame, as render() hands them to the equations. */
static void globals(const sk_milk *m, sk_milk_vars *v)
{
    v->frame = m->frame;
    v->time = m->time;
    v->fps = m->fps;
    v->bass = m->audio.bass, v->mid = m->audio.mid, v->treb = m->audio.treb;
    v->bass_att = m->audio.bass_att, v->mid_att = m->audio.mid_att, v->treb_att = m->audio.treb_att;
    v->meshx = MW, v->meshy = MH;
    v->aspectx = 1 / m->aspectx, v->aspecty = 1 / m->aspecty;
    v->pixelsx = m->w, v->pixelsy = m->h;
}

void sk_milk_load(sk_milk *m, int preset, double blend)
{
    if (preset < 0 || preset >= SK_MILK_PRESETS)
        return;
    blend_pattern(m);
    double old_mode = m->cur.def ? m->cur.base.wave_mode : 0;
    m->prev = m->cur;
    /* With no time to blend over, butterchurn ends the blend before the
     * next frame draws. */
    m->blending = m->cur.def && blend > 0;
    m->blend_start = m->time;
    m->blend_dur = blend;
    m->progress = 0;
    preset_run *r = &m->cur;
    r->def = &sk_milk_presets[preset];
    r->index = preset;
    sk_milk_defaults(&r->base);
    r->def->base(&r->base);
    r->base.old_wave_mode = old_mode;
    for (int i = 0; i < 4; i++)
        r->base.rand_start[i] = rnd(m);
    for (int i = 0; i < 4; i++)
        r->base.rand_preset[i] = rnd(m);
    globals(m, &r->base);
    r->frame = r->base;
}

int sk_milk_preset_of(const sk_milk *m)
{
    return m->cur.index;
}

const sk_milk_audio *sk_milk_levels(const sk_milk *m)
{
    return &m->audio;
}

sk_milk *sk_milk_new(int preset, uint64_t seed)
{
    sk_milk *m = sk_xcalloc(1, sizeof *m);
    m->rng.s = seed;
    sk_milk_audio_init(&m->audio);
    m->fps = 30;
    m->nhist = 1;
    m->aspectx = m->aspecty = 1;
    m->wrap = -1;
    blend_pattern(m);
    sk_milk_load(m, preset >= 0 && preset < SK_MILK_PRESETS ? preset : SK_MILK_DRIFT, 0);
    return m;
}

void sk_milk_free(sk_milk *m)
{
    if (!m)
        return;
    sk_milk_tex *texs[] = {&m->tex[0], &m->tex[1], &m->mip[0], &m->mip[1], &m->blur_h,
                           &m->blur_h1, &m->blur1, &m->canvas, &m->canvas2};
    for (size_t i = 0; i < SK_LEN(texs); i++)
        tex_free(texs[i]);
    void *tables[] = {m->col_cell, m->row_cell, m->wrap_x, m->wrap_y, m->col_frac, m->row_frac,
                      m->sx0, m->sx1, m->sfx, m->sy0, m->sy1, m->sfy};
    for (size_t i = 0; i < SK_LEN(tables); i++)
        free(tables[i]);
    free(m);
}

/* ---- Size ---- */

/* Each pixel's cell of n across a side of size pixels, and how far
 * across it its centre lies, of 65536. */
static void cells(int size, int n, int *cell, int32_t *frac)
{
    for (int p = 0; p < size; p++) {
        int64_t num = (int64_t)(2 * p + 1) * n, den = 2 * (int64_t)size;
        int64_t c = SK_MIN(num / den, n - 1);
        cell[p] = (int)c;
        frac[p] = (int32_t)(((num - c * den) * ONE) / den);
    }
}

/* setRendererSize: the newest frame resampled to the new size, the
 * one before it cleared, as butterchurn rebinds it. */
static void resize(sk_milk *m, int w, int h)
{
    sk_milk_tex *t = &m->tex[m->target], fresh = {0};
    tex_resize(&fresh, w, h);
    if (t->px) {
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                double rgb[3];
                sample(t, (x + 0.5) / w, (y + 0.5) / h, false, rgb);
                uint8_t *o = fresh.px + ((size_t)y * (size_t)w + (size_t)x) * 4;
                for (int c = 0; c < 3; c++)
                    o[c] = sk_milk_unorm(rgb[c]);
            }
    }
    tex_free(t);
    *t = fresh;
    tex_resize(&m->tex[!m->target], w, h);
    tex_free(&m->mip[0]);
    tex_free(&m->mip[1]);
    tex_free(&m->blur_h);
    tex_free(&m->blur_h1);
    tex_free(&m->blur1);
    tex_resize(&m->canvas, w, h);
    tex_free(&m->canvas2);
    m->w = w;
    m->h = h;
    m->aspectx = h > w ? (double)w / h : 1;
    m->aspecty = w > h ? (double)h / w : 1;
    free(m->col_cell), free(m->row_cell), free(m->col_frac), free(m->row_frac);
    m->col_cell = sk_xmalloc((size_t)w * sizeof *m->col_cell);
    m->col_frac = sk_xmalloc((size_t)w * sizeof *m->col_frac);
    m->row_cell = sk_xmalloc((size_t)h * sizeof *m->row_cell);
    m->row_frac = sk_xmalloc((size_t)h * sizeof *m->row_frac);
    cells(w, MW, m->col_cell, m->col_frac);
    cells(h, MH, m->row_cell, m->row_frac);
    m->wrap = -1;
    m->out_w = m->out_h = 0;
}

/* The texel wrap tables for coordinates biased by BIAS sizes: REPEAT
 * or CLAMP_TO_EDGE, as the preset's wrap asks. */
static void wrap_tables(sk_milk *m, bool repeat)
{
    if (m->wrap == (int)repeat)
        return;
    int *tab[2] = {m->wrap_x, m->wrap_y}, n[2] = {m->w, m->h};
    for (int k = 0; k < 2; k++) {
        free(tab[k]);
        int len = (BIAS * 2) * n[k] + 2;
        tab[k] = sk_xmalloc((size_t)len * sizeof *tab[k]);
        for (int i = 0; i < len; i++)
            tab[k][i] = wrap_index(i - BIAS * n[k], n[k], repeat);
    }
    m->wrap_x = tab[0];
    m->wrap_y = tab[1];
    m->wrap = repeat;
}

/* ---- Time ---- */

/* calcTimeAndFPS. The time steps by the fps before this frame's; the
 * fps then moves 7% toward the rate over the last 120 frames, as
 * butterchurn's, whose other branch never runs. A frame time it would
 * not believe, past a second or not after the last, counts as 1/30. */
static void advance(sk_milk *m, double dt)
{
    double elapsed = dt > 0 && dt <= 1 ? dt : 1.0 / 30;
    m->time += 1.0 / m->fps;
    if (m->blending) {
        m->progress = m->blend_dur > 0 ? (m->time - m->blend_start) / m->blend_dur : 2;
        if (m->progress > 1)
            m->blending = false;
    }
    double newest = m->hist[m->nhist - 1] + elapsed;
    m->hist[m->nhist++] = newest;
    if (m->nhist > HIST) {
        memmove(m->hist, m->hist + 1, (size_t)(m->nhist - 1) * sizeof *m->hist);
        m->nhist--;
    }
    double rate = m->nhist / (newest - m->hist[0]);
    m->fps = 0.93 * m->fps + 0.07 * rate;
}

/* ---- Equations ---- */

/* runFrameEquations: the base values, the q variables as init_eqs left
 * them, the globals, then the preset's equations. */
static void run_frame(const sk_milk *m, preset_run *r)
{
    r->frame = r->base;
    globals(m, &r->frame);
    r->def->frame(&r->frame);
}

/* utils.atan2: 0..2 pi. */
static double atan2_pos(double y, double x)
{
    double a = atan2(y, x);
    return a < 0 ? a + 2 * M_PI : a;
}

/* runPixelEquations: each vertex's motion from the per-pixel equations,
 * then where in the last frame its pixel comes from. Blending, the
 * preset blended from mixes in by the pattern. */
static void pixel_eqs(sk_milk *m, const preset_run *r, bool blend)
{
    const sk_milk_vars *f = &r->frame;
    double ax = m->aspectx, ay = m->aspecty;
    double tv = m->time * f->warpanimspeed, sinv = 1.0 / f->warpscale;
    double f0 = 11.68 + 4.0 * cos(tv * 1.413 + 10), f1 = 8.77 + 3.0 * cos(tv * 1.113 + 7);
    double f2 = 10.54 + 3.0 * cos(tv * 1.233 + 3), f3 = 11.49 + 4.0 * cos(tv * 0.933 + 5);
    int k = 0;
    for (int iz = 0; iz <= MH; iz++)
        for (int ix = 0; ix <= MW; ix++, k++) {
            double x = (double)ix / MW * 2 - 1, y = (double)iz / MH * 2 - 1;
            double rad = sqrt(x * x * ax * ax + y * y * ay * ay);
            sk_milk_vertex v = {
                .x = x * 0.5 * ax + 0.5, .y = y * -0.5 * ay + 0.5, .rad = rad,
                .ang = iz == MH / 2 && ix == MW / 2 ? 0 : atan2_pos(y * ay, x * ax),
                .zoom = f->zoom, .zoomexp = f->zoomexp, .rot = f->rot, .warp = f->warp, .cx = f->cx,
                .cy = f->cy, .dx = f->dx, .dy = f->dy, .sx = f->sx, .sy = f->sy,
            };
            if (r->def->pixel)
                r->def->pixel(f, &v);
            double zoom = pow(v.zoom, pow(v.zoomexp, rad * 2 - 1)), zinv = 1 / zoom;
            double u = x * 0.5 * ax * zinv + 0.5, w = -y * 0.5 * ay * zinv + 0.5;
            u = (u - v.cx) / v.sx + v.cx;
            w = (w - v.cy) / v.sy + v.cy;
            if (v.warp != 0) {
                u += v.warp * 0.0035 * sin(tv * 0.333 + sinv * (x * f0 - y * f3));
                w += v.warp * 0.0035 * cos(tv * 0.375 - sinv * (x * f2 + y * f1));
                u += v.warp * 0.0035 * cos(tv * 0.753 - sinv * (x * f1 - y * f2));
                w += v.warp * 0.0035 * sin(tv * 0.825 + sinv * (x * f0 + y * f3));
            }
            double u2 = u - v.cx, w2 = w - v.cy, c = cos(v.rot), s = sin(v.rot);
            u = u2 * c - w2 * s + v.cx;
            w = u2 * s + w2 * c + v.cy;
            u -= v.dx;
            w -= v.dy;
            u = (u - 0.5) / ax + 0.5;
            w = (w - 0.5) / ay + 0.5;
            if (!blend) {
                m->uv[k * 2] = u;
                m->uv[k * 2 + 1] = w;
                m->alpha[k] = 1;
            } else {
                double mix = fmin(fmax(m->blend_a[k] * m->progress + m->blend_c[k], 0), 1);
                m->uv[k * 2] = m->uv[k * 2] * mix + u * (1 - mix);
                m->uv[k * 2 + 1] = m->uv[k * 2 + 1] * mix + w * (1 - mix);
                m->alpha[k] = mix;
            }
        }
}

/* mixFrameEquations: the frame the waves, borders and motion vectors
 * use while blending, cur's with the listed values eased from prev's
 * and the switches snapped halfway. */
static void mix_frames(double progress, const sk_milk_vars *cur, const sk_milk_vars *prev, sk_milk_vars *out)
{
    double mix = 0.5 - 0.5 * cos(progress * M_PI), mix2 = 1 - mix;
    bool snap = mix < 0.5;
    *out = *cur;
#define MIX(k) out->k = mix * cur->k + mix2 * prev->k
#define SNAP(k) out->k = snap ? prev->k : cur->k
    MIX(decay), MIX(wave_a), MIX(wave_r), MIX(wave_g), MIX(wave_b), MIX(wave_x), MIX(wave_y);
    MIX(wave_mystery), MIX(ob_size), MIX(ob_r), MIX(ob_g), MIX(ob_b), MIX(ob_a);
    MIX(ib_size), MIX(ib_r), MIX(ib_g), MIX(ib_b), MIX(ib_a);
    MIX(mv_x), MIX(mv_y), MIX(mv_dx), MIX(mv_dy), MIX(mv_l), MIX(mv_r), MIX(mv_g), MIX(mv_b), MIX(mv_a);
    MIX(echo_zoom), MIX(echo_alpha), MIX(echo_orient);
    SNAP(wave_dots), SNAP(wave_thick), SNAP(additivewave), SNAP(wave_brighten), SNAP(darken_center);
    SNAP(gammaadj), SNAP(wrap), SNAP(invert), SNAP(brighten), SNAP(darken);
    /* As butterchurn has it: before the snap, prev's brighten. */
    out->solarize = snap ? prev->brighten : cur->solarize;
    MIX(b1n), MIX(b2n), MIX(b3n), MIX(b1x), MIX(b2x), MIX(b3x), MIX(b1ed);
#undef MIX
#undef SNAP
}

/* ---- The warp ---- */

typedef struct {
    sk_milk *m;
    const uint8_t *src;
    uint8_t *dst;
    /* The warp shader as integers: ret = texel * mul >> 16 - sub, in
     * 65536ths of a level; and the blended-from preset's. */
    int64_t mul[3], sub, pmul[3], psub;
    bool blend;
} warp_job;

static void shader_ints(const sk_milk_preset_def *d, int64_t mul[3], int64_t *sub)
{
    for (int c = 0; c < 3; c++)
        mul[c] = (int64_t)floor(d->warp_mul[c] * ONE + 0.5);
    *sub = (int64_t)floor(d->warp_sub * 255 * ONE + 0.5);
}

/* A value across a cell of the mesh at (s, t), 16-bit fractions from
 * its top left, as GL interpolates across the cell's two triangles:
 * top left, bottom left, bottom right below the diagonal; top left,
 * top right, bottom right above it. */
static inline int64_t across(const int64_t *top, const int64_t *bot, int i, int64_t s, int64_t t)
{
    if (t > s)
        return (top[i] * ONE + (bot[i] - top[i]) * t + (bot[i + 1] - bot[i]) * s) >> 16;
    return (top[i] * ONE + (top[i + 1] - top[i]) * s + (bot[i + 1] - top[i + 1]) * t) >> 16;
}

static void warp_rows(void *ctx, int lo, int hi)
{
    const warp_job *j = ctx;
    const sk_milk *m = j->m;
    const int w = m->w, row = MW + 1;
    for (int y = lo; y < hi; y++) {
        int cj = m->row_cell[y];
        int64_t t = m->row_frac[y];
        const int64_t *xt = m->vx + cj * row, *yt = m->vy + cj * row, *at = m->va + cj * row;
        uint8_t *o = j->dst + (size_t)y * (size_t)w * 4;
        for (int x = 0; x < w; x++, o += 4) {
            int ci = m->col_cell[x];
            int64_t s = m->col_frac[x];
            int64_t X = across(xt, xt + row, ci, s, t), Y = across(yt, yt + row, ci, s, t);
            int xi = (int)(X >> 16), yi = (int)(Y >> 16);
            int fx = (int)((X >> 8) & 255), fy = (int)((Y >> 8) & 255);
            int xa = m->wrap_x[xi] * 4, xb = m->wrap_x[xi + 1] * 4;
            const uint8_t *r0 = j->src + (size_t)m->wrap_y[yi] * (size_t)w * 4;
            const uint8_t *r1 = j->src + (size_t)m->wrap_y[yi + 1] * (size_t)w * 4;
            int64_t a = j->blend ? across(at, at + row, ci, s, t) : ONE;
            for (int c = 0; c < 3; c++) {
                int top = r0[xa + c] * (256 - fx) + r0[xb + c] * fx;
                int bot = r1[xa + c] * (256 - fx) + r1[xb + c] * fx;
                int64_t v = (int64_t)top * (256 - fy) + (int64_t)bot * fy;
                int64_t r = ((v * j->mul[c]) >> 16) - j->sub;
                r = r < 0 ? 0 : SK_MIN(r, 255 * (int64_t)ONE);
                if (!j->blend) {
                    o[c] = (uint8_t)((r + ONE / 2) >> 16);
                    continue;
                }
                /* The preset blended from draws first, to 8 bits; the
                 * new one blends over it by the vertex's alpha. */
                int64_t p = ((v * j->pmul[c]) >> 16) - j->psub;
                p = p < 0 ? 0 : SK_MIN((p + ONE / 2) >> 16, 255);
                o[c] = (uint8_t)((r * a + (p << 16) * (ONE - a) + ((int64_t)1 << 31)) >> 32);
            }
        }
    }
}

static int64_t texel_fixed(double c, int n)
{
    c = !(c == c) ? 0 : c < -2 ? -2 : c > 3 ? 3 : c;
    return (int64_t)((c * n - 0.5 + BIAS * n) * ONE);
}

static void warp(sk_milk *m, const sk_milk_vars *mixed)
{
    const int row = MW + 1;
    for (int j = 0; j <= MH; j++)
        for (int i = 0; i <= MW; i++) {
            int k = (MH - j) * row + i;
            m->vx[j * row + i] = texel_fixed(m->uv[k * 2], m->w);
            m->vy[j * row + i] = texel_fixed(m->uv[k * 2 + 1], m->h);
            m->va[j * row + i] = (int64_t)floor(m->alpha[k] * ONE + 0.5);
        }
    wrap_tables(m, (m->blending ? mixed->wrap : m->cur.frame.wrap) != 0);
    warp_job j = {.m = m, .src = m->tex[!m->target].px, .dst = m->tex[m->target].px, .blend = m->blending};
    shader_ints(m->cur.def, j.mul, &j.sub);
    if (m->blending)
        shader_ints(m->prev.def, j.pmul, &j.psub);
    sk_milk_parallel(warp_rows, &j, m->h);
}

/* ---- The blur ---- */

/* blur.js's getTextureSize: ratio of the texture, at least 16, its
 * width to a multiple of 16 and its height to one of 4. */
static void blur_size(const sk_milk *m, double ratio, int *w, int *h)
{
    *w = (int)floor((fmax(m->w * ratio, 16) + 3) / 16) * 16;
    *h = (int)floor((fmax(m->h * ratio, 16) + 3) / 4) * 4;
}

/* A LINEAR sampler's two texels along a side of n at texture
 * coordinate c, and the second's weight. */
typedef struct {
    int a, b;
    float f;
} tap;

static tap tap_at(double c, int n, bool repeat)
{
    double x = c * n - 0.5;
    x = !isfinite(x) ? 0 : fmin(fmax(x, -4.0 * n), 5.0 * n);
    double fl = floor(x);
    tap t = {wrap_index((int)fl, n, repeat), wrap_index((int)fl + 1, n, repeat), (float)(x - fl)};
    return t;
}

/* The blur's weights: eight either side in pairs, 4, 3.8, 3.5, 2.9,
 * 1.9, 1.2, 0.7 and 0.3, each pair one bilinear tap between its two;
 * the vertical pass takes them in fours. */
static const double blur_w[8] = {4.0, 3.8, 3.5, 2.9, 1.9, 1.2, 0.7, 0.3};

typedef struct {
    const sk_milk_tex *src;
    sk_milk_tex *dst;
    bool repeat;
    double b1ed;
    /* Each output column's taps: four either side across for the
     * horizontal pass, one for the vertical; and the vertical pass's
     * offsets in v. */
    tap *cols;
    double ws[4], div, dv[4];
} blur_job;

/* blurHorizontal, a row of the source between its two rows at a time:
 * the row's v is fixed, so each tap is a lerp along it. */
static void blur_h_rows(void *ctx, int lo, int hi)
{
    const blur_job *j = ctx;
    const sk_milk_tex *s = j->src, *d = j->dst;
    float *mid = sk_xmalloc((size_t)s->w * 3 * sizeof *mid);
    for (int y = lo; y < hi; y++) {
        tap ty = tap_at((y + 0.5) / d->h, s->h, j->repeat);
        const uint8_t *r0 = s->px + (size_t)ty.a * (size_t)s->w * 4, *r1 = s->px + (size_t)ty.b * (size_t)s->w * 4;
        for (int x = 0; x < s->w; x++)
            for (int c = 0; c < 3; c++)
                mid[x * 3 + c] = (r0[x * 4 + c] * (1 - ty.f) + r1[x * 4 + c] * ty.f) / 255.0f;
        uint8_t *o = d->px + (size_t)y * (size_t)d->w * 4;
        for (int x = 0; x < d->w; x++, o += 4) {
            const tap *t = j->cols + x * 8;
            for (int c = 0; c < 3; c++) {
                double sum = 0;
                for (int k = 0; k < 8; k++)
                    sum += (mid[t[k].a * 3 + c] * (1 - t[k].f) + mid[t[k].b * 3 + c] * t[k].f) * j->ws[k / 2];
                o[c] = sk_milk_unorm(sum * j->div);
            }
        }
    }
    free(mid);
}

/* blurVertical: two taps either side, then darkened toward the edges
 * by b1ed. */
static void blur_v_rows(void *ctx, int lo, int hi)
{
    const blur_job *j = ctx;
    const sk_milk_tex *s = j->src, *d = j->dst;
    for (int y = lo; y < hi; y++) {
        double v = (y + 0.5) / d->h;
        tap ty[4];
        for (int k = 0; k < 4; k++)
            ty[k] = tap_at(v + j->dv[k], s->h, j->repeat);
        uint8_t *o = d->px + (size_t)y * (size_t)d->w * 4;
        for (int x = 0; x < d->w; x++, o += 4) {
            const tap *tx = j->cols + x;
            double u = (x + 0.5) / d->w;
            double e = sqrt(fmin(fmin(u, v), 1 - fmax(u, v)));
            e = (1 - j->b1ed) + j->b1ed * fmin(fmax(e * 5, 0), 1);
            for (int c = 0; c < 3; c++) {
                double sum = 0;
                for (int k = 0; k < 4; k++) {
                    const uint8_t *r0 = s->px + (size_t)ty[k].a * (size_t)s->w * 4;
                    const uint8_t *r1 = s->px + (size_t)ty[k].b * (size_t)s->w * 4;
                    double top = r0[tx->a * 4 + c] * (1 - tx->f) + r0[tx->b * 4 + c] * tx->f;
                    double bot = r1[tx->a * 4 + c] * (1 - tx->f) + r1[tx->b * 4 + c] * tx->f;
                    sum += (top * (1 - ty[k].f) + bot * ty[k].f) * j->ws[k / 2];
                }
                o[c] = sk_milk_unorm(sum / 255.0 * j->div * e);
            }
        }
    }
}

/* blur1, from level 1 of the texture drawn this frame. That level was
 * made when the texture was last the one drawn from, two frames ago,
 * and the blur reads it, since its half-size pass minifies by two:
 * butterchurn's blur runs two frames behind, and so does this. Each
 * pass reads through the sampler the warp left on its texture unit,
 * which wraps as the preset does. */
static void blur(sk_milk *m, const sk_milk_tex *level1, const sk_milk_vars *f)
{
    int hw, hh, vw, vh, sw, sh;
    blur_size(m, 0.5, &hw, &hh);
    blur_size(m, 0.25, &vw, &vh);
    blur_size(m, 1, &sw, &sh);
    if (m->blur_h.w != hw || m->blur_h.h != hh)
        tex_resize(&m->blur_h, hw, hh);
    if (m->blur1.w != vw || m->blur1.h != vh)
        tex_resize(&m->blur1, vw, vh);
    bool repeat = m->wrap == 1;
    const double *w = blur_w;
    tap *cols = sk_xmalloc((size_t)hw * 8 * sizeof *cols);
    if (level1->px) {
        blur_job j = {.src = level1, .dst = &m->blur_h, .repeat = repeat, .cols = cols};
        for (int k = 0; k < 4; k++) {
            j.ws[k] = w[2 * k] + w[2 * k + 1];
            /* The pair's tap between its two, in texels of the source. */
            double d = 2 * k + 2 * w[2 * k + 1] / j.ws[k];
            for (int x = 0; x < hw; x++) {
                double u = (x + 0.5) / hw;
                cols[x * 8 + k * 2] = tap_at(u + d / sw, level1->w, repeat);
                cols[x * 8 + k * 2 + 1] = tap_at(u - d / sw, level1->w, repeat);
            }
        }
        j.div = 0.5 / (j.ws[0] + j.ws[1] + j.ws[2] + j.ws[3]);
        sk_milk_parallel(blur_h_rows, &j, hh);
    }
    mipmap(&m->blur_h, &m->blur_h1);
    double w1 = w[0] + w[1] + w[2] + w[3], w2 = w[4] + w[5] + w[6] + w[7];
    double d1 = 2 * ((w[2] + w[3]) / w1), d2 = 2 + 2 * ((w[6] + w[7]) / w2);
    blur_job j = {.src = &m->blur_h1, .dst = &m->blur1, .repeat = repeat, .b1ed = f->b1ed, .cols = cols,
                  .ws = {w1, w2}, .div = 1.0 / ((w1 + w2) * 2), .dv = {d1 / hh, -d1 / hh, d2 / hh, -d2 / hh}};
    for (int x = 0; x < vw; x++)
        cols[x] = tap_at((x + 0.5) / vw, m->blur_h1.w, repeat);
    sk_milk_parallel(blur_v_rows, &j, vh);
    free(cols);
}

/* ---- The composite ---- */

/* generateHueBase: four corners of colour, each channel drifting
 * 0.3..0.9 then scaled so the brightest is 1, then halved toward
 * white. */
static void hue_base(const sk_milk_vars *f, double hue[4][3])
{
    for (int i = 0; i < 4; i++) {
        double h[3] = {
            0.6 + 0.3 * sin(f->time * 30.0 * 0.0143 + 3 + i * 21 + f->rand_start[3]),
            0.6 + 0.3 * sin(f->time * 30.0 * 0.0107 + 1 + i * 13 + f->rand_start[1]),
            0.6 + 0.3 * sin(f->time * 30.0 * 0.0129 + 6 + i * 9 + f->rand_start[2]),
        };
        double top = fmax(h[0], fmax(h[1], h[2]));
        for (int c = 0; c < 3; c++)
            hue[i][c] = 0.5 + 0.5 * h[c] / top;
    }
}

typedef struct {
    const sk_milk *m;
    const sk_milk_preset_def *def;
    const sk_milk_vars *f;
    sk_milk_tex *dst;
    double hue[4][3];
    /* blur1's two texels across under each pixel column, for a preset
     * that reads it. */
    tap *blur_cols;
} comp_job;

static void comp_rows(void *ctx, int lo, int hi)
{
    const comp_job *j = ctx;
    const sk_milk *m = j->m;
    const sk_milk_tex *b = &m->blur1;
    float *blur = j->blur_cols ? sk_xcalloc((size_t)m->w * 3, sizeof *blur) : NULL;
    sk_milk_comp_row r = {.w = m->w, .h = m->h, .blur1 = blur, .f = j->f};
    memcpy(r.hue, j->hue, sizeof r.hue);
    for (int y = lo; y < hi; y++) {
        r.y = y;
        r.main = m->tex[m->target].px + (size_t)y * (size_t)m->w * 4;
        r.out = j->dst->px + (size_t)y * (size_t)m->w * 4;
        /* sampler_blur1 at the row's pixels: CLAMP_TO_EDGE, as the blur
         * textures are made. */
        if (blur && b->px) {
            tap ty = tap_at((y + 0.5) / m->h, b->h, false);
            const uint8_t *r0 = b->px + (size_t)ty.a * (size_t)b->w * 4, *r1 = b->px + (size_t)ty.b * (size_t)b->w * 4;
            for (int x = 0; x < m->w; x++) {
                const tap *tx = j->blur_cols + x;
                for (int c = 0; c < 3; c++) {
                    float top = r0[tx->a * 4 + c] * (1 - tx->f) + r0[tx->b * 4 + c] * tx->f;
                    float bot = r1[tx->a * 4 + c] * (1 - tx->f) + r1[tx->b * 4 + c] * tx->f;
                    blur[x * 3 + c] = (top * (1 - ty.f) + bot * ty.f) / 255.0f;
                }
            }
        }
        j->def->comp(&r);
    }
    free(blur);
}

/* A composite pass of def with frame f into dst. */
static void comp_pass(sk_milk *m, const sk_milk_preset_def *def, const sk_milk_vars *f, sk_milk_tex *dst)
{
    comp_job j = {.m = m, .def = def, .f = f, .dst = dst};
    hue_base(f, j.hue);
    if (def->blur) {
        j.blur_cols = sk_xmalloc((size_t)m->w * sizeof *j.blur_cols);
        for (int x = 0; x < m->w; x++)
            j.blur_cols[x] = tap_at((x + 0.5) / m->w, SK_MAX(m->blur1.w, 1), false);
    }
    sk_milk_parallel(comp_rows, &j, m->h);
    free(j.blur_cols);
}

/* While blending, the new preset's composite over the old one's by an
 * alpha that generateCompColors reads off the warp mesh, as it does. */
static void comp_blend_rows(void *ctx, int lo, int hi)
{
    const sk_milk *m = ctx;
    for (int y = lo; y < hi; y++) {
        double gy = (y + 0.5) / m->h * CH;
        int y0 = SK_MIN((int)gy, CH - 1);
        double ty = gy - y0;
        uint8_t *o = m->canvas.px + (size_t)y * (size_t)m->w * 4;
        const uint8_t *n = m->canvas2.px + (size_t)y * (size_t)m->w * 4;
        for (int x = 0; x < m->w; x++, o += 4, n += 4) {
            double gx = (x + 0.5) / m->w * CW;
            int x0 = SK_MIN((int)gx, CW - 1);
            double tx = gx - x0;
            const double *g = m->comp_alpha + y0 * (CW + 1) + x0;
            double a = (g[0] * (1 - tx) + g[1] * tx) * (1 - ty) + (g[CW + 1] * (1 - tx) + g[CW + 2] * tx) * ty;
            for (int c = 0; c < 3; c++)
                o[c] = sk_milk_unorm((n[c] * a + o[c] * (1 - a)) / 255.0);
        }
    }
}

static void composite(sk_milk *m, const sk_milk_vars *mixed)
{
    if (!m->blending) {
        comp_pass(m, m->cur.def, &m->cur.frame, &m->canvas);
        return;
    }
    comp_pass(m, m->prev.def, &m->prev.frame, &m->canvas);
    if (m->canvas2.w != m->w || m->canvas2.h != m->h)
        tex_resize(&m->canvas2, m->w, m->h);
    comp_pass(m, m->cur.def, mixed, &m->canvas2);
    /* The alpha at each vertex of the composite's 32 by 24 mesh, read
     * where generateCompColors reads it: a vertex's place times the
     * warp mesh's vertex count, not its cell count, held inside. */
    for (int cj = 0; cj <= CH; cj++)
        for (int ci = 0; ci <= CW; ci++) {
            double x = fmin(fmax((double)ci / CW * (MW + 1), 0), MW - 1);
            double y = fmin(fmax((double)cj / CH * (MH + 1), 0), MH - 1);
            int nx = (int)x, ny = (int)y;
            double dx = x - nx, dy = y - ny;
            const double *a = m->alpha + ny * (MW + 1) + nx;
            m->comp_alpha[cj * (CW + 1) + ci] = a[0] * (1 - dx) * (1 - dy) + a[1] * dx * (1 - dy) +
                                                a[MW + 1] * (1 - dx) * dy + a[MW + 2] * dx * dy;
        }
    sk_milk_parallel(comp_blend_rows, m, m->h);
}

/* ---- The stretch to the pane ---- */

typedef struct {
    const sk_milk *m;
    sk_image *out;
} stretch_job;

/* Each output pixel's two source texels across a side and the second's
 * weight of 256, as bilinear filtering takes them, held to the edge. */
static void stretch_table(int src, int dst, int *i0, int *i1, int *f)
{
    for (int p = 0; p < dst; p++) {
        double s = (p + 0.5) * src / dst - 0.5, fs = floor(s);
        int a = (int)fs, frac = (int)floor((s - fs) * 256);
        i0[p] = SK_MIN(SK_MAX(a, 0), src - 1);
        i1[p] = SK_MIN(SK_MAX(a + 1, 0), src - 1);
        f[p] = frac;
    }
}

/* A row of the canvas between its two source rows first, then across:
 * the same sums as the four-texel form, in another order. */
static void stretch_rows(void *ctx, int lo, int hi)
{
    const stretch_job *j = ctx;
    const sk_milk *m = j->m;
    const sk_milk_tex *c = &m->canvas;
    if (m->out_w == m->w && m->out_h == m->h) {
        for (int y = lo; y < hi; y++) {
            sk_px *o = sk_image_row(j->out, y);
            const uint8_t *s = c->px + (size_t)y * (size_t)c->w * 4;
            for (int x = 0; x < m->w; x++, s += 4)
                o[x] = 0xff000000u | (uint32_t)s[0] << 16 | (uint32_t)s[1] << 8 | s[2];
        }
        return;
    }
    uint32_t *mid = sk_xmalloc((size_t)c->w * 4 * sizeof *mid);
    for (int y = lo; y < hi; y++) {
        const uint8_t *r0 = c->px + (size_t)m->sy0[y] * (size_t)c->w * 4;
        const uint8_t *r1 = c->px + (size_t)m->sy1[y] * (size_t)c->w * 4;
        uint32_t fy = (uint32_t)m->sfy[y], gy = 256 - fy;
        for (int i = 0; i < c->w * 4; i++)
            mid[i] = r0[i] * gy + r1[i] * fy;
        sk_px *o = sk_image_row(j->out, y);
        for (int x = 0; x < m->out_w; x++) {
            const uint32_t *a = mid + m->sx0[x] * 4, *b = mid + m->sx1[x] * 4;
            uint32_t fx = (uint32_t)m->sfx[x], gx = 256 - fx;
            o[x] = 0xff000000u | ((a[0] * gx + b[0] * fx + 32768) >> 16) << 16 |
                   ((a[1] * gx + b[1] * fx + 32768) >> 16) << 8 | ((a[2] * gx + b[2] * fx + 32768) >> 16);
        }
    }
    free(mid);
}

static void stretch(sk_milk *m, sk_image *out)
{
    if (m->out_w != out->w || m->out_h != out->h) {
        int **tabs[] = {&m->sx0, &m->sx1, &m->sfx, &m->sy0, &m->sy1, &m->sfy};
        for (size_t i = 0; i < SK_LEN(tabs); i++) {
            free(*tabs[i]);
            *tabs[i] = sk_xmalloc((size_t)(i < 3 ? out->w : out->h) * sizeof **tabs[i]);
        }
        stretch_table(m->w, out->w, m->sx0, m->sx1, m->sfx);
        stretch_table(m->h, out->h, m->sy0, m->sy1, m->sfy);
        m->out_w = out->w;
        m->out_h = out->h;
    }
    stretch_job j = {m, out};
    sk_milk_parallel(stretch_rows, &j, out->h);
}

/* ---- A frame ---- */

void sk_milk_frame(sk_milk *m, const float *samples, double dt, sk_image *out, int scale)
{
    if (!out || out->w <= 0 || out->h <= 0)
        return;
    if (scale < 1)
        scale = 1;
    int w = SK_MAX(1, out->w / scale), h = SK_MAX(1, out->h / scale);
    if (w != m->w || h != m->h)
        resize(m, w, h);

    advance(m, dt);
    m->frame++;
    sk_milk_audio_update(&m->audio, samples, m->fps, m->frame);
    run_frame(m, &m->cur);
    pixel_eqs(m, &m->cur, false);
    sk_milk_vars mixed = m->cur.frame;
    if (m->blending) {
        run_frame(m, &m->prev);
        pixel_eqs(m, &m->prev, true);
        mix_frames(m->progress, &m->cur.frame, &m->prev.frame, &mixed);
    }

    /* The texture drawn last becomes the one drawn from, and its level
     * 1 is made now, as generateMipmap makes it; only the blur reads
     * it. */
    m->target = !m->target;
    if (m->cur.def->blur)
        mipmap(&m->tex[!m->target], &m->mip[!m->target]);
    warp(m, &mixed);
    if (m->cur.def->blur)
        blur(m, &m->mip[m->target], &m->cur.frame);

    sk_milk_tex *t = &m->tex[m->target];
    sk_milk_draw_motion_vectors(t, &mixed, m->uv);
    sk_milk_draw_custom_waves(t, m->cur.def, &m->cur.base, &m->cur.frame, &m->audio,
                              m->blending ? m->progress : 1, m->aspectx, m->aspecty, &m->rng);
    if (m->blending)
        sk_milk_draw_custom_waves(t, m->prev.def, &m->prev.base, &m->prev.frame, &m->audio, 1 - m->progress,
                                  m->aspectx, m->aspecty, &m->rng);
    sk_milk_draw_basic_wave(t, &mixed, &m->audio, m->blending, m->progress, m->aspectx, m->aspecty);
    sk_milk_draw_darken_center(t, &mixed, m->aspecty);
    double ob[4] = {mixed.ob_r, mixed.ob_g, mixed.ob_b, mixed.ob_a};
    double ib[4] = {mixed.ib_r, mixed.ib_g, mixed.ib_b, mixed.ib_a};
    sk_milk_draw_border(t, ob, mixed.ob_size, 0);
    sk_milk_draw_border(t, ib, mixed.ib_size, mixed.ob_size);

    composite(m, &mixed);
    stretch(m, out);
}
