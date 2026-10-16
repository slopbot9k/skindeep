/* Whirlpool (milk.h): two scribbles orbiting the centre, wound into
 * arms by a rotation faster inside, fading to copper, against golden
 * images. */
#include "../test.h"
#include "milk_shared.h"

static void test_frames(void)
{
    const int at[] = {45, 180};
    sk_image im[2];
    sk_milk_test_frames(SK_MILK_WHIRLPOOL, SK_MILK_TEST_W, SK_MILK_TEST_H, 1, at, 2, im);
    sk_image early = sk_milk_test_middle(&im[0]), late = sk_milk_test_middle(&im[1]);
    CHECK_MILK_GOLDEN("milk_whirlpool_45", &early);
    CHECK_MILK_GOLDEN("milk_whirlpool_180", &late);
    /* Copper: its warp keeps red longest. And the core glows. */
    sk_rect all = sk_image_bounds(&im[1]);
    double r = sk_milk_test_mean(&im[1], all, 16), g = sk_milk_test_mean(&im[1], all, 8);
    double b = sk_milk_test_mean(&im[1], all, 0);
    if (!(r > g && g > b))
        sk_test_fail(__FILE__, __LINE__, "red %.1f, green %.1f, blue %.1f", r, g, b);
    double core = sk_milk_test_mean(&im[1], sk_rect_make(150, 110, 20, 20), 16);
    double corner = sk_milk_test_mean(&im[1], sk_rect_make(0, 0, 20, 20), 16);
    if (!(core > corner + 40))
        sk_test_fail(__FILE__, __LINE__, "core %.1f, corner %.1f", core, corner);
    sk_image_free(&im[0]);
    sk_image_free(&im[1]);
}

static void test_retina(void)
{
    const int at[] = {90};
    sk_image im;
    sk_milk_test_frames(SK_MILK_WHIRLPOOL, SK_MILK_TEST_W, SK_MILK_TEST_H, 2, at, 1, &im);
    sk_image mid = sk_milk_test_middle(&im);
    CHECK_MILK_GOLDEN("milk_whirlpool_2x_90", &mid);
    sk_image_free(&im);
}

const sk_test sk_suite_milk_whirlpool[] = {
    T(test_frames),
    T(test_retina),
    {0},
};
