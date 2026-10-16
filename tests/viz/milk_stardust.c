/* Star dust (milk.h): three swarms of dots at random places, a slow
 * vortex and a blur that glows, against golden images. */
#include "../test.h"
#include "milk_shared.h"

static void test_frames(void)
{
    const int at[] = {45, 180};
    sk_image im[2];
    sk_milk_test_frames(SK_MILK_STARDUST, SK_MILK_TEST_W, SK_MILK_TEST_H, 1, at, 2, im);
    sk_image early = sk_milk_test_middle(&im[0]), late = sk_milk_test_middle(&im[1]);
    CHECK_MILK_GOLDEN("milk_stardust_45", &early);
    CHECK_MILK_GOLDEN("milk_stardust_180", &late);
    /* Dim, and green-gold: green over red over blue. */
    sk_rect all = sk_image_bounds(&im[1]);
    double r = sk_milk_test_mean(&im[1], all, 16), g = sk_milk_test_mean(&im[1], all, 8);
    double b = sk_milk_test_mean(&im[1], all, 0);
    if (!(g > r && r > b && g > 5 && g < 80))
        sk_test_fail(__FILE__, __LINE__, "red %.1f, green %.1f, blue %.1f", r, g, b);
    sk_image_free(&im[0]);
    sk_image_free(&im[1]);
}

static void test_retina(void)
{
    const int at[] = {90};
    sk_image im;
    sk_milk_test_frames(SK_MILK_STARDUST, SK_MILK_TEST_W, SK_MILK_TEST_H, 2, at, 1, &im);
    sk_image mid = sk_milk_test_middle(&im);
    CHECK_MILK_GOLDEN("milk_stardust_2x_90", &mid);
    sk_image_free(&im);
}

/* The dots fall where the seed puts them. */
static void test_seed(void)
{
    sk_milk *a = sk_milk_new(SK_MILK_STARDUST, 1), *b = sk_milk_new(SK_MILK_STARDUST, 2);
    sk_image ia, ib;
    sk_image_init(&ia, 64, 48);
    sk_image_init(&ib, 64, 48);
    float s[SK_MILK_SAMPLES];
    for (int n = 0; n < 10; n++) {
        sk_milk_demo_signal(n, s);
        sk_milk_frame(a, s, 1.0 / 60, &ia, 1);
        sk_milk_frame(b, s, 1.0 / 60, &ib, 1);
    }
    CHECK(memcmp(ia.px, ib.px, (size_t)ia.w * (size_t)ia.h * sizeof *ia.px) != 0);
    sk_image_free(&ia);
    sk_image_free(&ib);
    sk_milk_free(a);
    sk_milk_free(b);
}

const sk_test sk_suite_milk_stardust[] = {
    T(test_frames),
    T(test_retina),
    T(test_seed),
    {0},
};
