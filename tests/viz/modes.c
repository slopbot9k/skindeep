/* The visualizer's modes against golden images: each run on the
 * scripted session (src/viz/script.h) as vizshot runs it, at the start
 * of the first turn, deep in it at 2x, and in the idle spell that ends
 * the loop. A mode draws from its inputs alone, so a second run draws
 * the same pixels. */
#include "../../src/viz/script.h"
#include "../golden.h"
#include "../test.h"

/* The mode run for frames from start, its last frame against the
 * golden image name, or against the image *again when it is not NULL. */
static void shot(const char *mode, double start, int frames, int w, int h, double scale, const char *name, int tol)
{
    sk_viz_run r;
    if (!sk_viz_run_init(&r, sk_viz_mode_find(mode), start, w, h, scale)) {
        sk_test_fail(__FILE__, __LINE__, "%s: no renderer", mode);
        return;
    }
    const sk_image *im = NULL;
    for (int i = 0; i < frames; i++) {
        sk_viz_run_feed(&r);
        im = sk_viz_run_draw(&r);
    }
    CHECK_INT(im->w, (int)(w * scale + 0.5));
    CHECK_GOLDEN(name, im, tol);
    sk_viz_run_free(&r);
}

/* The pointer at a frame: kind, place in points, the button held. */
typedef struct {
    int frame, kind;
    double x, y;
    bool down;
} touch;

/* The mode run for frames from the start of the script at 1x, the
 * pointer acting as touches says, its last frame against name. */
static void shot_pointer(const char *mode, int frames, const touch *t, size_t n, const char *name, int tol)
{
    sk_viz_run r;
    if (!sk_viz_run_init(&r, sk_viz_mode_find(mode), 0, 240, 150, 1)) {
        sk_test_fail(__FILE__, __LINE__, "%s: no renderer", mode);
        return;
    }
    const sk_image *im = NULL;
    for (int i = 0; i < frames; i++) {
        for (size_t k = 0; k < n; k++)
            if (t[k].frame == i)
                CHECK(sk_vmode_pointer(r.mode, t[k].kind, t[k].x, t[k].y, t[k].down));
        sk_viz_run_feed(&r);
        im = sk_viz_run_draw(&r);
    }
    CHECK_GOLDEN(name, im, tol);
    sk_viz_run_free(&r);
}

/* Three shots of a mode: 0.75 s into the first turn at 1x, 6 s into it
 * at 2x, and 5 s from 27 s into the loop, idle, at 1x. */
static void shots(const char *mode, int tol)
{
    char name[64];
    snprintf(name, sizeof name, "viz_%s_start", mode);
    shot(mode, 0, 45, 240, 150, 1, name, tol);
    snprintf(name, sizeof name, "viz_%s_busy_2x", mode);
    shot(mode, 0, 360, 240, 150, 2, name, tol);
    snprintf(name, sizeof name, "viz_%s_idle", mode);
    shot(mode, 27, 300, 240, 150, 1, name, tol);
}

static void test_scope(void)
{
    shots("scope", 2);
}

static void test_analyzer(void)
{
    shots("analyzer", 2);
}

static void test_fire(void)
{
    shots("fire", 2);
}

static void test_superscope(void)
{
    shots("superscope", 2);
}

static void test_dotplane(void)
{
    shots("dotplane", 2);
}

/* A click curls the fronds near it and looses gold spores. */
static void test_fern(void)
{
    shots("fern", 2);
    static const touch t[] = {{100, SK_VIZ_DOWN, 120, 90, true}, {101, SK_VIZ_UP, 120, 90, false}};
    shot_pointer("fern", 130, t, SK_LEN(t), "viz_fern_click", 2);
}

/* A click on a point plays it for the side to move. */
static void test_go(void)
{
    shots("go", 2);
    /* The board of a 240 by 150 pane: 144 points square from (48, 3),
     * a step of 144 / 13.6, the first line 0.8 of a step in. */
    double step = 144 / 13.6, x0 = 48 + 0.8 * step, y0 = 3 + 0.8 * step;
    static touch t[2];
    t[0] = (touch){60, SK_VIZ_DOWN, x0 + 6 * step, y0 + 6 * step, true};
    t[1] = (touch){62, SK_VIZ_DOWN, x0 + 2 * step + 1, y0 + 10 * step - 1, true};
    shot_pointer("go", 90, t, SK_LEN(t), "viz_go_click", 2);
}

/* A click leaves a white ring on the scene in view. */
static void test_zoom(void)
{
    shots("zoom", 2);
    static const touch t[] = {{40, SK_VIZ_DOWN, 150, 60, true}};
    shot_pointer("zoom", 100, t, SK_LEN(t), "viz_zoom_click", 2);
}

/* A drag throws a moon, its aim drawn while pressed. */
static void test_gravity(void)
{
    shots("gravity", 2);
    static const touch t[] = {{30, SK_VIZ_DOWN, 60, 40, true},
                              {40, SK_VIZ_MOVE, 90, 50, true},
                              {60, SK_VIZ_UP, 100, 55, false},
                              {70, SK_VIZ_DOWN, 180, 110, true},
                              {75, SK_VIZ_MOVE, 160, 120, true}};
    shot_pointer("gravity", 80, t, SK_LEN(t), "viz_gravity_throw", 2);
}

/* Two runs of every mode draw the same pixels. */
static void test_deterministic(void)
{
    for (int mode = 0; mode < SK_VIZ_MODES; mode++) {
        sk_viz_run a, b;
        if (!sk_viz_run_init(&a, mode, 0, 120, 80, 1))
            continue;
        sk_viz_run_init(&b, mode, 0, 120, 80, 1);
        const sk_image *ia = NULL, *ib = NULL;
        for (int i = 0; i < 90; i++) {
            sk_viz_run_feed(&a);
            sk_viz_run_feed(&b);
            ia = sk_viz_run_draw(&a);
            ib = sk_viz_run_draw(&b);
        }
        bool same = ia->w == ib->w && ia->h == ib->h &&
                    memcmp(ia->px, ib->px, (size_t)ia->w * (size_t)ia->h * sizeof *ia->px) == 0;
        if (!same)
            sk_test_fail(__FILE__, __LINE__, "%s: two runs differ", sk_viz_modes[mode].id);
        sk_viz_run_free(&a);
        sk_viz_run_free(&b);
    }
}

const sk_test sk_suite_viz_modes[] = {T(test_scope), T(test_analyzer), T(test_fire),          T(test_superscope),
                                      T(test_dotplane), T(test_fern), T(test_go),              T(test_zoom),
                                      T(test_gravity),  T(test_deterministic), {0}};
