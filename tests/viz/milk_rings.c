/* Rings (milk.h): a thick circular wave over a zoom out that wraps, its
 * inner border streaming inward, against golden images. */
#include "../test.h"
#include "milk_shared.h"

static void test_frames(void)
{
    const int at[] = {45, 180};
    sk_image im[2];
    sk_milk_test_frames(SK_MILK_RINGS, SK_MILK_TEST_W, SK_MILK_TEST_H, 1, at, 2, im);
    sk_image early = sk_milk_test_middle(&im[0]), late = sk_milk_test_middle(&im[1]);
    CHECK_MILK_GOLDEN("milk_rings_45", &early);
    CHECK_MILK_GOLDEN("milk_rings_180", &late);
    /* The blue inner border, drawn each frame and zoomed out, streams
     * in from the edges. */
    sk_rect side = sk_rect_make(0, 0, 16, SK_MILK_TEST_H);
    double b = sk_milk_test_mean(&im[1], side, 0), r = sk_milk_test_mean(&im[1], side, 16);
    if (!(b > r + 20))
        sk_test_fail(__FILE__, __LINE__, "blue %.1f, red %.1f", b, r);
    sk_image_free(&im[0]);
    sk_image_free(&im[1]);
}

static void test_retina(void)
{
    const int at[] = {90};
    sk_image im;
    sk_milk_test_frames(SK_MILK_RINGS, SK_MILK_TEST_W, SK_MILK_TEST_H, 2, at, 1, &im);
    sk_image mid = sk_milk_test_middle(&im);
    CHECK_MILK_GOLDEN("milk_rings_2x_90", &mid);
    sk_image_free(&im);
}

const sk_test sk_suite_milk_rings[] = {
    T(test_frames),
    T(test_retina),
    {0},
};
