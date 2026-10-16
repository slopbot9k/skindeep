/* Plasma (milk.h): a wave across the screen through a strong warp,
 * inside a dark border, against golden images. */
#include "../test.h"
#include "milk_shared.h"

static void test_frames(void)
{
    const int at[] = {45, 180};
    sk_image im[2];
    sk_milk_test_frames(SK_MILK_PLASMA, SK_MILK_TEST_W, SK_MILK_TEST_H, 1, at, 2, im);
    sk_image early = sk_milk_test_middle(&im[0]), late = sk_milk_test_middle(&im[1]);
    CHECK_MILK_GOLDEN("milk_plasma_45", &early);
    CHECK_MILK_GOLDEN("milk_plasma_180", &late);
    /* The outer border, black at 0.15 each frame, darkens the edge. */
    double edge = sk_milk_test_mean(&im[1], sk_rect_make(0, 0, SK_MILK_TEST_W, 1), 8);
    double inside = sk_milk_test_mean(&im[1], sk_rect_make(40, 40, 240, 160), 8);
    if (!(edge < inside * 0.8))
        sk_test_fail(__FILE__, __LINE__, "edge %.1f, inside %.1f", edge, inside);
    sk_image_free(&im[0]);
    sk_image_free(&im[1]);
}

static void test_retina(void)
{
    const int at[] = {90};
    sk_image im;
    sk_milk_test_frames(SK_MILK_PLASMA, SK_MILK_TEST_W, SK_MILK_TEST_H, 2, at, 1, &im);
    sk_image mid = sk_milk_test_middle(&im);
    CHECK_MILK_GOLDEN("milk_plasma_2x_90", &mid);
    sk_image_free(&im);
}

const sk_test sk_suite_milk_plasma[] = {
    T(test_frames),
    T(test_retina),
    {0},
};
