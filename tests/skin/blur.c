/* The blur Chromium's shadows and filters use (src/skin/blur.h): its
 * reach, its symmetry and what it keeps, the true kernel below a sigma
 * of 2 and three boxes above, near the Gaussian either way. */
#include "../../src/gfx/draw.h"
#include "../../src/skin/blur.h"
#include "../test.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static void test_reach(void)
{
    /* sigma 2: a box 4 wide (floor(2 * 1.88 + 0.5)), even: 4, 4, 5. */
    CHECK_INT(sk_blur_reach(2), 6);
    /* sigma 10: 19 wide, odd: three of 19, 9 each side. */
    CHECK_INT(sk_blur_reach(10), 27);
    /* Below 2, the kernel out to 3 sigma. */
    CHECK_INT(sk_blur_reach(0.5f), 2);
    CHECK_INT(sk_blur_reach(1.5f), 5);
    CHECK_INT(sk_blur_reach(0), 0);
}

/* A w by w mask with a square of 255 in its middle. */
static uint8_t *square(int w, int side)
{
    uint8_t *a = sk_xcalloc((size_t)w * (size_t)w, 1);
    int o = (w - side) / 2;
    for (int y = o; y < o + side; y++)
        memset(a + (size_t)y * (size_t)w + (size_t)o, 255, (size_t)side);
    return a;
}

static void check_symmetric_and_whole(float sigma)
{
    int w = 61;
    uint8_t *a = square(w, 5);
    sk_blur_a8(a, w, w, w, sigma);
    long sum = 0;
    int asym = 0;
    for (int y = 0; y < w; y++)
        for (int x = 0; x < w; x++) {
            int v = a[y * w + x];
            sum += v;
            asym = SK_MAX(asym, abs(v - a[y * w + (w - 1 - x)]));
            asym = SK_MAX(asym, abs(v - a[(w - 1 - y) * w + x]));
            asym = SK_MAX(asym, abs(v - a[x * w + y]));
        }
    /* Mirror images every way, and what it spreads all kept. */
    CHECK(asym <= 1);
    CHECK(labs(sum - 25 * 255) < 25 * 255 / 50);
    free(a);
}

static void test_symmetric_and_whole(void)
{
    check_symmetric_and_whole(0.8f);
    check_symmetric_and_whole(1.9f);
    check_symmetric_and_whole(2.0f);
    check_symmetric_and_whole(3.3f);
    check_symmetric_and_whole(6.0f);
}

/* A step from 255 to 0 at x = 64, blurred: within a few levels of the
 * Gaussian's 255 (1 - erf((x + 1/2 - 64) / (sigma sqrt 2))) / 2. */
static void check_step(float sigma, int tol)
{
    int w = 128, h = 128;
    uint8_t *a = sk_xcalloc((size_t)w * (size_t)h, 1);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < 64; x++)
            a[y * w + x] = 255;
    sk_blur_a8(a, w, h, w, sigma);
    /* The middle row, far from the buffer's top and bottom, beyond
     * which all is transparent. */
    int worst = 0;
    for (int x = 40; x < 88; x++) {
        double want = 255 * (1 - erf((x + 0.5 - 64) / (sigma * M_SQRT2))) / 2;
        worst = SK_MAX(worst, abs((int)a[64 * w + x] - (int)lround(want)));
    }
    if (worst > tol)
        sk_test_fail(__FILE__, __LINE__, "sigma %.1f: %d levels from the Gaussian", (double)sigma, worst);
    free(a);
}

static void test_near_the_gaussian(void)
{
    check_step(1.0f, 4);
    check_step(1.5f, 4);
    check_step(2.0f, 8);
    check_step(4.0f, 8);
    check_step(9.0f, 8);
}

static void test_image_stays_premultiplied(void)
{
    sk_image im;
    sk_image_init(&im, 40, 60);
    sk_fill_rect(&im, sk_rect_make(10, 10, 20, 20), SK_RGBA(255, 128, 0, 200), SK_BLEND_SRC, NULL);
    sk_fill_rect(&im, sk_rect_make(18, 0, 4, 60), SK_RGBA(0, 0, 255, 255), SK_BLEND_SRC_OVER, NULL);
    sk_blur_image(&im, 3);
    bool ok = true;
    for (int y = 0; y < im.h; y++)
        for (int x = 0; x < im.w; x++) {
            sk_px p = sk_image_row(&im, y)[x];
            uint32_t a = p >> 24;
            ok = ok && ((p >> 16) & 255) <= a && ((p >> 8) & 255) <= a && (p & 255) <= a;
        }
    CHECK(ok);
    /* The blue bar's colour spreads with its alpha, far from the square. */
    sk_px mid = sk_image_row(&im, 50)[20];
    CHECK((mid & 255) > 100 && (mid & 255) == (mid >> 24));
    sk_image_free(&im);
}

const sk_test sk_suite_skin_blur[] = {
    T(test_reach),
    T(test_symmetric_and_whole),
    T(test_near_the_gaussian),
    T(test_image_stays_premultiplied),
    {0},
};
