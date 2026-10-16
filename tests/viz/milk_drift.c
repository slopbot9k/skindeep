/* Drift (milk.h): a Lissajous wave over a fast zoom, motion vectors and
 * a darkened centre, against golden images at three points of a run. */
#include "../test.h"
#include "milk_shared.h"

static void test_frames(void)
{
    const int at[] = {45, 180};
    sk_image im[2];
    sk_milk_test_frames(SK_MILK_DRIFT, SK_MILK_TEST_W, SK_MILK_TEST_H, 1, at, 2, im);
    sk_image early = sk_milk_test_middle(&im[0]), late = sk_milk_test_middle(&im[1]);
    CHECK_MILK_GOLDEN("milk_drift_45", &early);
    CHECK_MILK_GOLDEN("milk_drift_180", &late);
    /* darken_center leaves the middle darker than about it, as the zoom
     * carries the darkening out. */
    double centre = sk_milk_test_mean(&im[1], sk_rect_make(156, 116, 8, 8), 8);
    double about = sk_milk_test_mean(&im[1], sk_rect_make(136, 96, 48, 48), 8);
    if (!(centre < about * 0.9))
        sk_test_fail(__FILE__, __LINE__, "centre %.1f, about it %.1f", centre, about);
    sk_image_free(&im[0]);
    sk_image_free(&im[1]);
}

static void test_retina(void)
{
    const int at[] = {90};
    sk_image im;
    sk_milk_test_frames(SK_MILK_DRIFT, SK_MILK_TEST_W, SK_MILK_TEST_H, 2, at, 1, &im);
    CHECK_INT(im.w, 640);
    CHECK_INT(im.h, 480);
    sk_image mid = sk_milk_test_middle(&im);
    CHECK_MILK_GOLDEN("milk_drift_2x_90", &mid);
    sk_image_free(&im);
}

const sk_test sk_suite_milk_drift[] = {
    T(test_frames),
    T(test_retina),
    {0},
};
