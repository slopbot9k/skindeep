/* The five presets of app/viz/presets.js as C (milk.h): their base
 * values, their per-frame, per-pixel and custom wave equations, and the
 * warp and composite shaders, each translated by hand from its
 * JavaScript or GLSL string, the original quoted beside it. What the
 * strings leave unset comes from butterchurn's defaults, below, as its
 * loadPreset fills them (butterchurn 2.6.7, MIT, Copyright (c)
 * 2013-2018 Jordan Berg; third_party/butterchurn/LICENSE).
 *
 * Butterchurn runs a preset's init_eqs once and resets every q variable
 * to what they left before each frame, so q1, which all five set to 0
 * in init_eqs, never carries from one frame to the next: Drift's
 * "q1 = 0.9*q1 + 0.1*mid_att" is 0.1 mid_att every frame. The C keeps
 * that. Each preset's own warp and composite shaders replace the
 * defaults, so decay, gammaadj, echo, darken and the like, which only
 * the defaults read, change nothing here, as they change nothing in
 * butterchurn. */
#include "milkint.h"

#include <math.h>

double sk_milk_random(sk_milk_rng *r)
{
    uint64_t x = (r->s += 0x9e3779b97f4a7c15u);
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9u;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebu;
    x ^= x >> 31;
    return (double)(x >> 11) / 9007199254740992.0;
}

void sk_milk_defaults(sk_milk_vars *v)
{
    *v = (sk_milk_vars){
        .decay = 0.98, .gammaadj = 2, .echo_zoom = 2, .echo_alpha = 0, .echo_orient = 0,
        .red_blue = 0, .brighten = 0, .darken = 0, .wrap = 1, .darken_center = 0,
        .solarize = 0, .invert = 0, .fshader = 0,
        .b1n = 0, .b2n = 0, .b3n = 0, .b1x = 1, .b2x = 1, .b3x = 1, .b1ed = 0.25,
        .wave_mode = 0, .additivewave = 0, .wave_dots = 0, .wave_thick = 0, .wave_a = 0.8,
        .wave_scale = 1, .wave_smoothing = 0.75, .wave_mystery = 0, .modwavealphabyvolume = 0,
        .modwavealphastart = 0.75, .modwavealphaend = 0.95,
        .wave_r = 1, .wave_g = 1, .wave_b = 1, .wave_x = 0.5, .wave_y = 0.5, .wave_brighten = 1,
        .mv_x = 12, .mv_y = 9, .mv_dx = 0, .mv_dy = 0, .mv_l = 0.9, .mv_r = 1, .mv_g = 1, .mv_b = 1,
        .mv_a = 1,
        .warpanimspeed = 1, .warpscale = 1, .zoomexp = 1, .zoom = 1, .rot = 0, .cx = 0.5, .cy = 0.5,
        .dx = 0, .dy = 0, .warp = 1, .sx = 1, .sy = 1,
        .ob_size = 0.01, .ob_r = 0, .ob_g = 0, .ob_b = 0, .ob_a = 0,
        .ib_size = 0.01, .ib_r = 0.25, .ib_g = 0.25, .ib_b = 0.25, .ib_a = 0,
    };
}

void sk_milk_wave_defaults(sk_milk_wave_vars *w)
{
    *w = (sk_milk_wave_vars){
        .samples = 512, .sep = 0, .scaling = 1, .smoothing = 0.5,
        .r = 1, .g = 1, .b = 1, .a = 1, .spectrum = 0, .usedots = 0, .thick = 0, .additive = 0,
    };
}

/* ---- The shared shaders ---- */

/* WARP: "ret = texture2D(sampler_main, uv).rgb; ret -= 0.004;", the
 * warp of Drift, Rings, Plasma and Star dust. On an 8-bit texture it
 * takes one level of 255 a frame, so a trail fades in about 4 s. */
#define WARP_MUL {1, 1, 1}
#define WARP_SUB 0.004

/* COMP: "ret = texture2D(sampler_main, uv).rgb; ret *= hue_shader;".
 * hue_shader is interpolated across the screen from its corners, each
 * channel 0.5..1, drifting with time. */
static void comp_hue(const sk_milk_comp_row *r)
{
    double y = (r->y + 0.5) / r->h, left[3], span[3];
    for (int c = 0; c < 3; c++) {
        left[c] = r->hue[1][c] * y + r->hue[3][c] * (1 - y);
        span[c] = r->hue[0][c] * y + r->hue[2][c] * (1 - y) - left[c];
    }
    const uint8_t *m = r->main;
    uint8_t *o = r->out;
    for (int x = 0; x < r->w; x++, m += 4, o += 4) {
        double fx = (x + 0.5) / r->w;
        for (int c = 0; c < 3; c++)
            o[c] = sk_milk_unorm(m[c] / 255.0 * (left[c] + span[c] * fx));
    }
}

/* ---- Drift ----
 *
 * decay: 0.97, gammaadj: 1.9, wave_mode: 2, additivewave: 1, wave_a: 0.9,
 * wave_scale: 1.4, wave_smoothing: 0.5, wave_r: 0.4, wave_g: 0.9,
 * wave_b: 0.6, zoom: 1.02, warp: 0.3, warpscale: 1.6, warpanimspeed: 1.2,
 * darken_center: 1, ob_size: 0.005, ob_a: 0.4, ob_r: 0.1, ob_g: 0.6,
 * ob_b: 0.3 */
static void drift_base(sk_milk_vars *v)
{
    v->decay = 0.97, v->gammaadj = 1.9, v->wave_mode = 2, v->additivewave = 1, v->wave_a = 0.9;
    v->wave_scale = 1.4, v->wave_smoothing = 0.5, v->wave_r = 0.4, v->wave_g = 0.9, v->wave_b = 0.6;
    v->zoom = 1.02, v->warp = 0.3, v->warpscale = 1.6, v->warpanimspeed = 1.2, v->darken_center = 1;
    v->ob_size = 0.005, v->ob_a = 0.4, v->ob_r = 0.1, v->ob_g = 0.6, v->ob_b = 0.3;
    v->q[1] = 0; /* a.q1 = 0; */
}

static void drift_frame(sk_milk_vars *a)
{
    a->q[1] = 0.9 * a->q[1] + 0.1 * a->mid_att;
    a->zoom = 1.01 + 0.05 * a->bass_att;
    a->rot = 0.015 * sin(a->time * 0.4) + 0.04 * a->q[1];
    a->wave_r = 0.5 + 0.5 * sin(a->time * 0.7);
    a->wave_g = 0.6 + 0.4 * sin(a->time * 0.53 + 1.3);
    a->wave_b = 0.4 + 0.6 * a->treb_att;
    a->warp = 0.2 + 0.6 * a->mid_att;
}

static void drift_pixel(const sk_milk_vars *f, sk_milk_vertex *a)
{
    a->zoom = a->zoom + 0.03 * a->rad * f->bass;
    a->rot = a->rot + 0.02 * sin(a->ang * 3 + f->time) * f->mid;
}

/* ---- Rings ----
 *
 * decay: 0.94, gammaadj: 2.0, wave_mode: 0, additivewave: 1, wave_a: 1.0,
 * wave_scale: 0.9, wave_smoothing: 0.7, wave_thick: 1, zoom: 0.97,
 * rot: 0.02, warp: 0.05, echo_zoom: 1.0, brighten: 0, ib_size: 0.01,
 * ib_a: 0.3, ib_r: 0.2, ib_g: 0.2, ib_b: 0.8 */
static void rings_base(sk_milk_vars *v)
{
    v->decay = 0.94, v->gammaadj = 2.0, v->wave_mode = 0, v->additivewave = 1, v->wave_a = 1.0;
    v->wave_scale = 0.9, v->wave_smoothing = 0.7, v->wave_thick = 1, v->zoom = 0.97, v->rot = 0.02;
    v->warp = 0.05, v->echo_zoom = 1.0, v->brighten = 0;
    v->ib_size = 0.01, v->ib_a = 0.3, v->ib_r = 0.2, v->ib_g = 0.2, v->ib_b = 0.8;
    v->q[1] = 0;
}

static void rings_frame(sk_milk_vars *a)
{
    a->zoom = 0.96 + 0.04 * a->bass_att;
    a->rot = 0.03 * cos(a->time * 0.3) + 0.05 * a->mid;
    a->wave_r = 0.5 + 0.5 * sin(a->time * 1.1);
    a->wave_g = 0.5 + 0.5 * sin(a->time * 0.9 + 2);
    a->wave_b = 0.5 + 0.5 * sin(a->time * 0.7 + 4);
    a->cx = 0.5 + 0.1 * sin(a->time * 0.21);
    a->cy = 0.5 + 0.1 * cos(a->time * 0.17);
}

static void rings_pixel(const sk_milk_vars *f, sk_milk_vertex *a)
{
    a->dx = 0.005 * sin(a->y * 12 + f->time) * f->treb;
    a->dy = 0.005 * cos(a->x * 12 + f->time) * f->treb;
}

/* ---- Plasma ----
 *
 * decay: 0.99, gammaadj: 1.6, wave_mode: 6, additivewave: 1, wave_a: 0.6,
 * wave_scale: 2.0, wave_smoothing: 0.3, zoom: 1.0, warp: 1.0,
 * warpscale: 3.0, warpanimspeed: 2.0, mv_a: 0, darken: 1, ob_size: 0.02,
 * ob_a: 0.15 */
static void plasma_base(sk_milk_vars *v)
{
    v->decay = 0.99, v->gammaadj = 1.6, v->wave_mode = 6, v->additivewave = 1, v->wave_a = 0.6;
    v->wave_scale = 2.0, v->wave_smoothing = 0.3, v->zoom = 1.0, v->warp = 1.0, v->warpscale = 3.0;
    v->warpanimspeed = 2.0, v->mv_a = 0, v->darken = 1, v->ob_size = 0.02, v->ob_a = 0.15;
    v->q[1] = 0;
}

static void plasma_frame(sk_milk_vars *a)
{
    a->warp = 0.5 + 1.5 * a->bass_att;
    a->zoom = 1.0 + 0.02 * sin(a->time * 0.6) + 0.03 * a->treb;
    a->wave_r = 0.6 + 0.4 * sin(a->time * 0.31);
    a->wave_g = 0.3 + 0.3 * a->mid_att;
    a->wave_b = 0.7 + 0.3 * sin(a->time * 0.43 + 1);
}

static void plasma_pixel(const sk_milk_vars *f, sk_milk_vertex *a)
{
    a->rot = 0.04 * sin(a->rad * 6 - f->time * 1.5) * (0.3 + f->mid);
    a->sx = 1 + 0.02 * sin(a->ang * 2 + f->time) * f->bass;
}

/* ---- Star dust ----
 *
 * decay: 0.95, gammaadj: 1.4, wave_a: 0, zoom: 1.004, warp: 0.45,
 * warpscale: 2.4, warpanimspeed: 0.4, mv_a: 0, echo_zoom: 1, wrap: 1 */
static void stardust_base(sk_milk_vars *v)
{
    v->decay = 0.95, v->gammaadj = 1.4, v->wave_a = 0, v->zoom = 1.004, v->warp = 0.45;
    v->warpscale = 2.4, v->warpanimspeed = 0.4, v->mv_a = 0, v->echo_zoom = 1, v->wrap = 1;
    v->q[1] = 0;
}

static void stardust_frame(sk_milk_vars *a)
{
    a->q[1] = 0.95 * a->q[1] + 0.05 * (a->bass_att + a->mid_att);
    a->cx = 0.5 + 0.12 * sin(a->time * 0.11);
    a->cy = 0.5 + 0.10 * cos(a->time * 0.083);
    a->warp = 0.3 + 0.5 * a->mid_att;
}

static void stardust_pixel(const sk_milk_vars *f, sk_milk_vertex *a)
{
    a->rot = 0.012 + 0.03 * fmax(0, 0.7 - a->rad) * (0.6 + f->q[1]);
    a->zoom = a->zoom + 0.012 * a->rad * f->bass;
}

/* dots(samples, r, g, b, frame, thick): enabled: 1, usedots: 1,
 * additive: 1, scaling: 1, smoothing: 0, a: 0.6, and each point
 * "a.x = Math.random(); a.y = Math.random();
 *  a.a = a.a*(0.25 + 0.75*Math.random());" */
static void dots_point(const sk_milk_vars *f, sk_milk_wave_vars *a, sk_milk_rng *rng, double param)
{
    (void)f, (void)param;
    a->x = sk_milk_random(rng);
    a->y = sk_milk_random(rng);
    a->a = a->a * (0.25 + 0.75 * sk_milk_random(rng));
}

static void dots_base(sk_milk_wave_vars *w, double samples, double r, double g, double b, double thick)
{
    sk_milk_wave_defaults(w);
    w->samples = samples, w->usedots = 1, w->thick = thick, w->additive = 1, w->scaling = 1;
    w->smoothing = 0, w->r = r, w->g = g, w->b = b, w->a = 0.6;
}

static void gold_base(sk_milk_wave_vars *w)
{
    dots_base(w, 200, 0.85, 0.95, 0.45, 1);
}

static void gold_frame(const sk_milk_vars *f, sk_milk_wave_vars *a)
{
    a->a = 0.3 + 0.25 * f->treb_att + 0.15 * f->bass;
}

static void green_base(sk_milk_wave_vars *w)
{
    dots_base(w, 120, 0.70, 0.90, 0.55, 0);
}

static void green_frame(const sk_milk_vars *f, sk_milk_wave_vars *a)
{
    a->a = 0.2 + 0.3 * f->mid_att;
}

static void violet_base(sk_milk_wave_vars *w)
{
    dots_base(w, 60, 0.65, 0.45, 0.85, 0);
}

static void violet_frame(const sk_milk_vars *f, sk_milk_wave_vars *a)
{
    a->a = 0.15 + 0.2 * f->bass_att;
}

static const sk_milk_wave_def stardust_waves[] = {
    {gold_base, gold_frame, dots_point, 0},
    {green_base, green_frame, dots_point, 0},
    {violet_base, violet_frame, dots_point, 0},
};

/* "vec3 m = texture2D(sampler_main, uv).rgb;
 *  vec3 b = texture2D(sampler_blur1, uv).rgb;
 *  ret = m*vec3(0.95, 1.0, 0.85) + b*vec3(0.45, 0.6, 0.65)
 *      + vec3(0.01, 0.02, 0.017);" */
static void stardust_comp(const sk_milk_comp_row *r)
{
    static const double km[3] = {0.95, 1.0, 0.85}, kb[3] = {0.45, 0.6, 0.65}, k[3] = {0.01, 0.02, 0.017};
    const uint8_t *m = r->main;
    const float *b = r->blur1;
    uint8_t *o = r->out;
    for (int x = 0; x < r->w; x++, m += 4, b += 3, o += 4)
        for (int c = 0; c < 3; c++)
            o[c] = sk_milk_unorm(m[c] / 255.0 * km[c] + b[c] * kb[c] + k[c]);
}

/* ---- Whirlpool ----
 *
 * decay: 0.93, gammaadj: 1.6, wave_a: 0, zoom: 1.02, warp: 0.1, mv_a: 0,
 * echo_zoom: 1 */
static void whirlpool_base(sk_milk_vars *v)
{
    v->decay = 0.93, v->gammaadj = 1.6, v->wave_a = 0, v->zoom = 1.02, v->warp = 0.1, v->mv_a = 0;
    v->echo_zoom = 1;
    v->q[1] = 0;
}

static void whirlpool_frame(sk_milk_vars *a)
{
    a->q[1] = 0.9 * a->q[1] + 0.1 * a->bass_att;
    a->zoom = 1.015 + 0.01 * a->q[1];
}

static void whirlpool_pixel(const sk_milk_vars *f, sk_milk_vertex *a)
{
    a->rot = 0.08 * fmax(0, 1 - 1.4 * a->rad) + 0.006 * f->q[1];
    a->zoom = a->zoom + 0.01 * a->rad;
}

/* scribble(phase): enabled: 1, samples: 90, usedots: 0, thick: 0,
 * additive: 1, scaling: 1, smoothing: 0, r: 0.85, g: 0.92, b: 1.0,
 * a: 0.45 */
static void scribble_base(sk_milk_wave_vars *w)
{
    sk_milk_wave_defaults(w);
    w->samples = 90, w->usedots = 0, w->thick = 0, w->additive = 1, w->scaling = 1, w->smoothing = 0;
    w->r = 0.85, w->g = 0.92, w->b = 1.0, w->a = 0.45;
}

/* "a.a = 0.3 + 0.3*Math.min(1, a.mid_att + a.treb);" */
static void scribble_frame(const sk_milk_vars *f, sk_milk_wave_vars *a)
{
    a->a = 0.3 + 0.3 * fmin(1, f->mid_att + f->treb);
}

/* A random walk held near its cluster's centre, its steps longer as
 * the waveform swings wider:
 * "var o = a.time*0.8 + phase;
 *  var d = 0.012 + 0.08*Math.abs(a.value1);
 *  a.wx = 0.9*(a.sample > 0 ? (a.wx || 0) : 0) + d*(Math.random()*2 - 1);
 *  a.wy = 0.9*(a.sample > 0 ? (a.wy || 0) : 0) + d*(Math.random()*2 - 1);
 *  a.x = 0.5 + 0.07*Math.cos(o) + a.wx;
 *  a.y = 0.5 + 0.09*Math.sin(o) + 1.3*a.wy;" */
static void scribble_point(const sk_milk_vars *f, sk_milk_wave_vars *a, sk_milk_rng *rng, double phase)
{
    double o = f->time * 0.8 + phase;
    double d = 0.012 + 0.08 * fabs(a->value1);
    a->wx = 0.9 * (a->sample > 0 ? a->wx : 0) + d * (sk_milk_random(rng) * 2 - 1);
    a->wy = 0.9 * (a->sample > 0 ? a->wy : 0) + d * (sk_milk_random(rng) * 2 - 1);
    a->x = 0.5 + 0.07 * cos(o) + a->wx;
    a->y = 0.5 + 0.09 * sin(o) + 1.3 * a->wy;
}

static const sk_milk_wave_def whirlpool_waves[] = {
    {scribble_base, scribble_frame, scribble_point, 0},
    {scribble_base, scribble_frame, scribble_point, 3.1416},
};

/* "vec3 m = texture2D(sampler_main, uv).rgb;
 *  float core = max(0.0, 1.0 - rad*7.0);
 *  ret = m + vec3(1.0, 0.85, 0.65)*core*core*(0.12 + 0.2*bass_att)
 *      + vec3(0.016, 0.013, 0.012);"
 * rad is the distance from the centre in uv, so the core is an ellipse
 * on a pane that is not square. */
static void whirlpool_comp(const sk_milk_comp_row *r)
{
    static const double kc[3] = {1.0, 0.85, 0.65}, k[3] = {0.016, 0.013, 0.012};
    double v = (r->y + 0.5) / r->h - 0.5, glow = 0.12 + 0.2 * r->f->bass_att;
    const uint8_t *m = r->main;
    uint8_t *o = r->out;
    for (int x = 0; x < r->w; x++, m += 4, o += 4) {
        double u = (x + 0.5) / r->w - 0.5;
        double core = fmax(0, 1 - sqrt(u * u + v * v) * 7);
        for (int c = 0; c < 3; c++)
            o[c] = sk_milk_unorm(m[c] / 255.0 + kc[c] * core * core * glow + k[c]);
    }
}

/* ---- The table ---- */

const sk_milk_preset_def sk_milk_presets[SK_MILK_PRESETS] = {
    [SK_MILK_DRIFT] = {"drift", drift_base, drift_frame, drift_pixel, NULL, 0, WARP_MUL, WARP_SUB, comp_hue, 0},
    [SK_MILK_RINGS] = {"rings", rings_base, rings_frame, rings_pixel, NULL, 0, WARP_MUL, WARP_SUB, comp_hue, 0},
    [SK_MILK_PLASMA] = {"plasma", plasma_base, plasma_frame, plasma_pixel, NULL, 0, WARP_MUL, WARP_SUB, comp_hue, 0},
    [SK_MILK_STARDUST] = {"stardust", stardust_base, stardust_frame, stardust_pixel, stardust_waves,
                          (int)SK_LEN(stardust_waves), WARP_MUL, WARP_SUB, stardust_comp, 1},
    /* "ret = texture2D(sampler_main, uv).rgb*vec3(0.985, 0.95, 0.9);
     *  ret -= 0.006;": red outlasts blue, so trails turn copper. */
    [SK_MILK_WHIRLPOOL] = {"whirlpool", whirlpool_base, whirlpool_frame, whirlpool_pixel, whirlpool_waves,
                           (int)SK_LEN(whirlpool_waves), {0.985, 0.95, 0.9}, 0.006, whirlpool_comp, 0},
};
