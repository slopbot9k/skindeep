/* What the MilkDrop renderer's files share (milk.h): a preset's
 * variables as butterchurn names them, the presets as C, the 8-bit
 * textures and the worker pool. Butterchurn is MIT, Copyright (c)
 * 2013-2018 Jordan Berg (third_party/butterchurn/LICENSE). */
#ifndef SK_VIZ_MILKINT_H
#define SK_VIZ_MILKINT_H

#include "milk.h"

/* ---- Random numbers ----
 *
 * Where a preset calls Math.random(), the renderer draws from its own
 * generator, seeded by sk_milk_new: splitmix64. */
typedef struct {
    uint64_t s;
} sk_milk_rng;

/* Uniform in [0, 1), 53 bits. */
double sk_milk_random(sk_milk_rng *r);

/* ---- Variables ---- */

/* A preset's per-frame variables, butterchurn's mdVSFrame: the base
 * values, the globals of the frame, and what the frame equations set.
 * Every value but the q variables starts from its base each frame. */
typedef struct {
    /* The globals. aspectx and aspecty are the inverse aspects, as
     * butterchurn hands them to the equations. */
    double frame, time, fps, bass, mid, treb, bass_att, mid_att, treb_att;
    double meshx, meshy, aspectx, aspecty, pixelsx, pixelsy;
    /* The motion of the feedback. */
    double zoom, zoomexp, rot, warp, cx, cy, dx, dy, sx, sy, warpanimspeed, warpscale;
    /* What the default warp and composite shaders read. A preset with
     * shaders of its own, as all five have, leaves them inert. */
    double decay, gammaadj, echo_zoom, echo_alpha, echo_orient, wrap, darken_center;
    double brighten, darken, solarize, invert, red_blue, fshader;
    double b1n, b2n, b3n, b1x, b2x, b3x, b1ed;
    /* The waveform. */
    double wave_mode, old_wave_mode, additivewave, wave_dots, wave_thick, wave_a, wave_scale;
    double wave_smoothing, wave_mystery, modwavealphabyvolume, modwavealphastart, modwavealphaend;
    double wave_r, wave_g, wave_b, wave_x, wave_y, wave_brighten;
    /* Motion vectors and borders. */
    double mv_x, mv_y, mv_dx, mv_dy, mv_l, mv_r, mv_g, mv_b, mv_a;
    double ob_size, ob_r, ob_g, ob_b, ob_a, ib_size, ib_r, ib_g, ib_b, ib_a;
    /* q1 to q32 at q[1] to q[32]. */
    double q[33];
    double rand_start[4], rand_preset[4];
} sk_milk_vars;

/* A vertex of the warp mesh as the per-pixel equations see it: its
 * place (x right, y down, 0..1), its distance and angle from the
 * centre, and the motion they may change. */
typedef struct {
    double x, y, rad, ang;
    double zoom, zoomexp, rot, warp, cx, cy, dx, dy, sx, sy;
} sk_milk_vertex;

/* A custom wave's variables: its base values, what its frame equation
 * sets, and each point's. wx and wy are the user variables Whirlpool's
 * point equation keeps from one point to the next. */
typedef struct {
    double samples, sep, scaling, smoothing, spectrum, usedots, thick, additive;
    double r, g, b, a;
    double sample, value1, value2, x, y;
    double wx, wy;
} sk_milk_wave_vars;

typedef struct {
    /* The wave's base values. */
    void (*base)(sk_milk_wave_vars *w);
    /* Its frame and point equations; point may draw random numbers. */
    void (*frame)(const sk_milk_vars *f, sk_milk_wave_vars *w);
    void (*point)(const sk_milk_vars *f, sk_milk_wave_vars *w, sk_milk_rng *rng, double param);
    /* What sets one wave of a preset apart from another of the same
     * equations: Whirlpool's phase. */
    double param;
} sk_milk_wave_def;

/* ---- Textures ----
 *
 * A texture is RGBA8 as butterchurn's are, 4 bytes a texel, the alpha
 * unused. Row 0 is GL's row 0: the bottom of the picture as GL draws
 * it, which the composite shader's flip shows at the top. */
typedef struct {
    uint8_t *px;
    int w, h;
} sk_milk_tex;

/* ---- The composite ----
 *
 * One row of the composite pass. The pass runs at the texture's size,
 * so each pixel reads its own texel of main. */
typedef struct {
    int w, h, y;
    /* Row y of the main texture, the one the warp and waves drew. */
    const uint8_t *main;
    /* For a preset whose shader names sampler_blur1, that sampler at
     * each pixel of the row, 3 floats 0..1 a pixel; else NULL. */
    const float *blur1;
    const sk_milk_vars *f;
    /* comp.js's generateHueBase: hue_shader is hue[0] x y + hue[1]
     * (1 - x) y + hue[2] x (1 - y) + hue[3] (1 - x) (1 - y), x from the
     * left and y from the top, 0..1. */
    double hue[4][3];
    /* The row out, RGBA8, alpha unwritten. */
    uint8_t *out;
} sk_milk_comp_row;

/* ret 0..1 as the canvas stores it: clamped, then rounded to 8 bits. */
static inline uint8_t sk_milk_unorm(double v)
{
    v = v * 255 + 0.5;
    return (uint8_t)(v < 0 ? 0 : v >= 255 ? 255 : (int)v);
}

/* ---- Presets ---- */

typedef struct {
    const char *id;
    /* The base values over butterchurn's defaults, and init_eqs. */
    void (*base)(sk_milk_vars *v);
    /* The per-frame and per-pixel equations. */
    void (*frame)(sk_milk_vars *v);
    void (*pixel)(const sk_milk_vars *f, sk_milk_vertex *v);
    const sk_milk_wave_def *waves;
    int nwaves;
    /* The warp shader, which is texture * mul - sub for all five. */
    double warp_mul[3], warp_sub;
    /* The composite shader, a row at a time. */
    void (*comp)(const sk_milk_comp_row *r);
    /* The highest blur sampler the shaders name, 0 for none. */
    int blur;
} sk_milk_preset_def;

extern const sk_milk_preset_def sk_milk_presets[SK_MILK_PRESETS];

/* Butterchurn's baseValsDefaults (visualizer.js), all q variables 0. */
void sk_milk_defaults(sk_milk_vars *v);
/* Its waveBaseValsDefaults. */
void sk_milk_wave_defaults(sk_milk_wave_vars *w);

/* ---- Drawing into the texture (milkwave.c) ----
 *
 * What butterchurn draws over the warped picture each frame, in its
 * order, each as GL rasterizes it: aliased 1-pixel lines, square
 * points, and blending in float rounded back to 8 bits. Positions are
 * GL clip coordinates; aspectx and aspecty are the renderer's: on the
 * shorter side's axis the shorter side over the longer, on the other
 * 1. */

/* The warp mesh: butterchurn's 48 by 36 cells. Its vertices run in
 * rows from GL's bottom, as warpUVs holds them. */
#define SK_MILK_MESH_W 48
#define SK_MILK_MESH_H 36
#define SK_MILK_MESH_VERTS ((SK_MILK_MESH_W + 1) * (SK_MILK_MESH_H + 1))

/* The motion vectors (motionVectors.js), from the mesh's u, v pairs. */
void sk_milk_draw_motion_vectors(sk_milk_tex *t, const sk_milk_vars *f, const double *uv);
/* A preset's custom waves (customWaveform.js), each alpha times
 * alpha_mult. base is the preset's base values, f its frame. */
void sk_milk_draw_custom_waves(sk_milk_tex *t, const sk_milk_preset_def *def, const sk_milk_vars *base,
                               const sk_milk_vars *f, const sk_milk_audio *a, double alpha_mult, double aspectx,
                               double aspecty, sk_milk_rng *rng);
/* The waveform of wave_mode (basicWaveform.js): f is the frame mixed
 * from two presets while blending, which mixes the old wave_mode's
 * shape in by progress. */
void sk_milk_draw_basic_wave(sk_milk_tex *t, const sk_milk_vars *f, const sk_milk_audio *a, bool blending,
                             double progress, double aspectx, double aspecty);
/* darken_center (darkenCenter.js): a small dark diamond at the centre. */
void sk_milk_draw_darken_center(sk_milk_tex *t, const sk_milk_vars *f, double aspecty);
/* A border (border.js) of size inside one of size prev, rgba. */
void sk_milk_draw_border(sk_milk_tex *t, const double rgba[4], double size, double prev);

/* ---- Workers ----
 *
 * fn(ctx, lo, hi) over [0, n) in bands, on a pool of threads made the
 * first time and kept for the process, the caller one of them. Bands
 * of rows are independent, so what is drawn does not depend on which
 * thread draws it. One job runs at a time. */
typedef void (*sk_milk_job)(void *ctx, int lo, int hi);
void sk_milk_parallel(sk_milk_job fn, void *ctx, int n);

#endif
