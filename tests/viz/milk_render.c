/* The MilkDrop renderer apart from any one preset (milk.h): names, a
 * frame from its inputs alone, the worker pool, a change of size, and
 * the blend from one preset to another. */
#include "../test.h"
#include "milk_shared.h"

static bool same(const sk_image *a, const sk_image *b)
{
    if (a->w != b->w || a->h != b->h)
        return false;
    for (int y = 0; y < a->h; y++)
        if (memcmp(sk_image_row(a, y), sk_image_row(b, y), (size_t)a->w * sizeof(sk_px)))
            return false;
    return true;
}

static void test_names(void)
{
    const char *const ids[] = {"drift", "rings", "plasma", "stardust", "whirlpool"};
    for (int i = 0; i < SK_MILK_PRESETS; i++) {
        CHECK_STR(sk_milk_name(i), ids[i]);
        CHECK_INT(sk_milk_find(ids[i]), i);
    }
    CHECK(sk_milk_name(SK_MILK_PRESETS) == NULL);
    CHECK(sk_milk_name(-1) == NULL);
    CHECK_INT(sk_milk_find("scope"), -1);
    CHECK_INT(sk_milk_find(NULL), -1);
}

/* Two runs of the same inputs draw the same, to the bit, whatever the
 * threads. */
static void test_deterministic(void)
{
    for (int p = 0; p < SK_MILK_PRESETS; p++) {
        sk_milk_set_threads(1);
        sk_image a = sk_milk_test_run(p, 40, 72, 40, 2, -1, -1);
        sk_milk_set_threads(0);
        sk_image b = sk_milk_test_run(p, 40, 72, 40, 2, -1, -1);
        sk_image c = sk_milk_test_run(p, 40, 72, 40, 2, -1, -1);
        if (!same(&a, &b) || !same(&b, &c))
            sk_test_fail(__FILE__, __LINE__, "%s: runs differ", sk_milk_name(p));
        sk_image_free(&a);
        sk_image_free(&b);
        sk_image_free(&c);
    }
}

/* A change of size resamples the picture so far, and a frame time it
 * would not believe counts as 1/30 s. */
static void test_resize(void)
{
    sk_milk *m = sk_milk_new(SK_MILK_WHIRLPOOL, 7);
    sk_image small, big;
    sk_image_init(&small, 60, 40);
    sk_image_init(&big, 150, 100);
    float s[SK_MILK_SAMPLES];
    const double dts[] = {1.0 / 60, 0, -1, 5, 1.0 / 30};
    for (int n = 0; n < 60; n++) {
        sk_milk_demo_signal(n, s);
        sk_milk_frame(m, s, dts[n % 5], &small, 1);
    }
    sk_milk_demo_signal(60, s);
    sk_milk_frame(m, s, 1.0 / 60, &big, 2);
    /* The core still glows after the change. */
    double core = sk_milk_test_mean(&big, sk_rect_make(65, 40, 20, 20), 16);
    CHECK(core > 60);
    /* An empty image draws nothing and leaves the renderer be. */
    sk_image none = {0};
    sk_milk_frame(m, s, 1.0 / 60, &none, 1);
    sk_milk_frame(m, s, 1.0 / 60, &big, 0);
    CHECK_INT(big.w, 150);
    sk_image_free(&small);
    sk_image_free(&big);
    sk_milk_free(m);
}

/* From Drift to Rings over half a second: halfway, a picture of both;
 * after, Rings alone. */
static void test_blend(void)
{
    sk_image mid = sk_milk_test_run(SK_MILK_DRIFT, 75, 128, 96, 1, SK_MILK_RINGS, 60);
    CHECK_MILK_GOLDEN("milk_blend_75", &mid);
    sk_image_free(&mid);
    sk_milk *m = sk_milk_new(SK_MILK_DRIFT, 1);
    sk_milk_load(m, SK_MILK_RINGS, 0.5);
    CHECK_INT(sk_milk_preset_of(m), SK_MILK_RINGS);
    sk_milk_load(m, SK_MILK_PRESETS, 0.5);
    CHECK_INT(sk_milk_preset_of(m), SK_MILK_RINGS);
    sk_milk_free(m);
}

const sk_test sk_suite_milk_render[] = {
    T(test_names),
    T(test_deterministic),
    T(test_resize),
    T(test_blend),
    {0},
};
