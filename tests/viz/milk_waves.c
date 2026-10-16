/* What the renderer draws over the warp (milkint.h), down to the paths
 * none of the five presets takes: every waveform mode, the blend from
 * one mode to another, dots, a custom wave of the spectrum, motion
 * vectors, the borders and the darkened centre. */
#include "../../src/viz/milkint.h"
#include "../test.h"

#include <math.h>
#include <stdlib.h>

enum { W = 160, H = 120 };

static sk_milk_tex blank(void)
{
    sk_milk_tex t = {sk_xcalloc((size_t)W * H, 4), W, H};
    return t;
}

static int lit(const sk_milk_tex *t)
{
    int n = 0;
    for (int i = 0; i < t->w * t->h; i++)
        n += t->px[i * 4] || t->px[i * 4 + 1] || t->px[i * 4 + 2];
    return n;
}

/* The levels of a loud chord, and a frame's variables about them. */
static void loud(sk_milk_audio *a, sk_milk_vars *f)
{
    sk_milk_audio_init(a);
    float s[SK_MILK_SAMPLES];
    for (int i = 0; i < SK_MILK_SAMPLES; i++)
        s[i] = (float)(0.5 * sin(2 * M_PI * 4 * i / SK_MILK_SAMPLES) + 0.3 * sin(2 * M_PI * 37 * i / SK_MILK_SAMPLES));
    sk_milk_audio_update(a, s, 60, 1);
    sk_milk_defaults(f);
    f->time = 1.5;
    f->bass = f->mid = f->treb = 1;
    f->wave_a = 1;
}

static void test_modes(void)
{
    sk_milk_audio a;
    sk_milk_vars f;
    loud(&a, &f);
    for (int mode = 0; mode < 8; mode++) {
        sk_milk_tex t = blank();
        f.wave_mode = mode;
        sk_milk_draw_basic_wave(&t, &f, &a, false, 1, 1, 0.75);
        if (lit(&t) < 20)
            sk_test_fail(__FILE__, __LINE__, "mode %d lit %d pixels", mode, lit(&t));
        free(t.px);
    }
    /* No alpha, nothing drawn. */
    sk_milk_tex t = blank();
    f.wave_a = 0;
    sk_milk_draw_basic_wave(&t, &f, &a, false, 1, 1, 0.75);
    CHECK_INT(lit(&t), 0);
    free(t.px);
}

/* While blending, a mode's points move from the old mode's, mode 7's
 * two waves among them. */
static void test_blend_modes(void)
{
    sk_milk_audio a;
    sk_milk_vars f;
    loud(&a, &f);
    const int pairs[][2] = {{0, 7}, {7, 0}, {7, 2}, {6, 7}, {1, 4}, {5, 3}};
    for (size_t i = 0; i < SK_LEN(pairs); i++) {
        sk_milk_tex t = blank();
        f.old_wave_mode = pairs[i][0];
        f.wave_mode = pairs[i][1];
        sk_milk_draw_basic_wave(&t, &f, &a, true, 0.4, 0.75, 1);
        if (lit(&t) < 20)
            sk_test_fail(__FILE__, __LINE__, "%d to %d lit %d pixels", pairs[i][0], pairs[i][1], lit(&t));
        free(t.px);
    }
}

/* Dots: a point at each of the smoothed wave's points, four times over
 * a pixel apart, where the line joined them. */
static void test_dots(void)
{
    sk_milk_audio a;
    sk_milk_vars f;
    loud(&a, &f);
    f.wave_mode = 6;
    sk_milk_tex line = blank(), dots = blank();
    sk_milk_draw_basic_wave(&line, &f, &a, false, 1, 1, 0.75);
    f.wave_dots = 1;
    sk_milk_draw_basic_wave(&dots, &f, &a, false, 1, 1, 0.75);
    CHECK(lit(&dots) > 0);
    CHECK(memcmp(line.px, dots.px, (size_t)W * H * 4) != 0);
    free(line.px);
    free(dots.px);
}

static void spectrum_base(sk_milk_wave_vars *w)
{
    sk_milk_wave_defaults(w);
    w->spectrum = 1, w->samples = 100, w->sep = 4, w->smoothing = 0.5, w->thick = 1, w->additive = 1;
}

static void spectrum_frame(const sk_milk_vars *f, sk_milk_wave_vars *w)
{
    (void)f;
    w->r = 1, w->g = 0.5, w->b = 0.25;
}

/* A custom wave of the spectrum, with sep, smoothed and thick. */
static void test_custom(void)
{
    sk_milk_audio a;
    sk_milk_vars f;
    loud(&a, &f);
    static const sk_milk_wave_def wave = {spectrum_base, spectrum_frame, NULL, 0};
    sk_milk_preset_def def = sk_milk_presets[SK_MILK_DRIFT];
    def.waves = &wave;
    def.nwaves = 1;
    sk_milk_tex t = blank();
    sk_milk_rng rng = {1};
    sk_milk_draw_custom_waves(&t, &def, &f, &f, &a, 1, 1, 0.75, &rng);
    CHECK(lit(&t) > 50);
    /* Its red is the strongest channel wherever it drew. */
    int bad = 0;
    for (int i = 0; i < W * H; i++)
        bad += t.px[i * 4 + 2] > t.px[i * 4];
    CHECK_INT(bad, 0);
    free(t.px);
}

/* Motion vectors from a mesh that zooms in: each a pixel across and
 * reaching toward the centre, white; more than 64 by 48 are held there. */
static void test_motion_vectors(void)
{
    sk_milk_vars f;
    sk_milk_defaults(&f);
    double *uv = sk_xmalloc(SK_MILK_MESH_VERTS * 2 * sizeof *uv);
    for (int iz = 0, k = 0; iz <= SK_MILK_MESH_H; iz++)
        for (int ix = 0; ix <= SK_MILK_MESH_W; ix++, k++) {
            double x = (double)ix / SK_MILK_MESH_W, y = 1 - (double)iz / SK_MILK_MESH_H;
            uv[k * 2] = 0.5 + (x - 0.5) / 1.1;
            uv[k * 2 + 1] = 0.5 + (y - 0.5) / 1.1;
        }
    sk_milk_tex t = blank();
    sk_milk_draw_motion_vectors(&t, &f, uv);
    int few = lit(&t);
    CHECK(few > 88);
    free(t.px);
    t = blank();
    f.mv_x = 200.5, f.mv_y = 100;
    sk_milk_draw_motion_vectors(&t, &f, uv);
    CHECK(lit(&t) > few);
    free(t.px);
    t = blank();
    f.mv_a = 0;
    sk_milk_draw_motion_vectors(&t, &f, uv);
    CHECK_INT(lit(&t), 0);
    free(t.px);
    free(uv);
}

/* Borders: the outer in from the edge, the inner inside it; and the
 * darkened centre, darkest at the middle. */
static void test_sprites(void)
{
    sk_milk_tex t = blank();
    const double red[4] = {1, 0, 0, 1}, blue[4] = {0, 0, 1, 1};
    sk_milk_draw_border(&t, red, 0.05, 0);
    sk_milk_draw_border(&t, blue, 0.05, 0.05);
    /* 0.05 of clip space is 2.5% of the side: 4 pixels across. */
    CHECK_INT(t.px[(60 * W + 1) * 4], 255);
    CHECK_INT(t.px[(60 * W + 1) * 4 + 2], 0);
    CHECK_INT(t.px[(60 * W + 5) * 4 + 2], 255);
    CHECK_INT(t.px[(60 * W + 5) * 4], 0);
    CHECK_INT(t.px[(1 * W + 80) * 4], 255);
    CHECK_INT(t.px[(60 * W + 80) * 4], 0);
    for (int i = 0; i < W * H * 4; i++)
        t.px[i] = 200;
    sk_milk_vars f;
    sk_milk_defaults(&f);
    f.darken_center = 1;
    sk_milk_draw_darken_center(&t, &f, 0.75);
    /* The diamond reaches 3 pixels either way here. */
    int mid = t.px[(60 * W + 80) * 4], off = t.px[(60 * W + 81) * 4], out = t.px[(60 * W + 84) * 4];
    CHECK(mid < off && off < out && out == 200);
    free(t.px);
}

const sk_test sk_suite_milk_waves[] = {
    T(test_modes),
    T(test_blend_modes),
    T(test_dots),
    T(test_custom),
    T(test_motion_vectors),
    T(test_sprites),
    {0},
};
