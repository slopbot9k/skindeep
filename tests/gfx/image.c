#include "../../src/gfx/image.h"
#include "../test.h"

#include <limits.h>

static void check_rect(sk_rect r, int x, int y, int w, int h)
{
    CHECK_INT(r.x, x);
    CHECK_INT(r.y, y);
    CHECK_INT(r.w, w);
    CHECK_INT(r.h, h);
}

static void test_intersect(void)
{
    check_rect(sk_rect_intersect(sk_rect_make(0, 0, 10, 10), sk_rect_make(5, 3, 10, 4)), 5, 3, 5, 4);
    /* Inside, and in either order. */
    check_rect(sk_rect_intersect(sk_rect_make(2, 2, 3, 3), sk_rect_make(0, 0, 10, 10)), 2, 2, 3, 3);
    check_rect(sk_rect_intersect(sk_rect_make(0, 0, 10, 10), sk_rect_make(2, 2, 3, 3)), 2, 2, 3, 3);
    /* Touching edges share nothing. */
    CHECK(sk_rect_empty(sk_rect_intersect(sk_rect_make(0, 0, 4, 4), sk_rect_make(4, 0, 4, 4))));
    CHECK(sk_rect_empty(sk_rect_intersect(sk_rect_make(0, 0, 4, 4), sk_rect_make(0, 4, 4, 4))));
    CHECK(sk_rect_empty(sk_rect_intersect(sk_rect_make(0, 0, 4, 4), sk_rect_make(9, 9, 4, 4))));
    /* A negative size is empty. */
    CHECK(sk_rect_empty(sk_rect_intersect(sk_rect_make(0, 0, -4, 4), sk_rect_make(-10, -10, 20, 20))));
    /* Far edges past INT_MAX. */
    check_rect(sk_rect_intersect(sk_rect_make(INT_MAX - 5, 0, INT_MAX, 10), sk_rect_make(0, 0, INT_MAX, 5)),
               INT_MAX - 5, 0, 5, 5);
    check_rect(sk_rect_intersect(sk_rect_make(INT_MIN, INT_MIN, INT_MAX, INT_MAX), sk_rect_make(-3, -3, 10, 10)),
               -3, -3, 2, 2);
}

static void test_union(void)
{
    check_rect(sk_rect_union(sk_rect_make(0, 0, 2, 2), sk_rect_make(5, 6, 1, 1)), 0, 0, 6, 7);
    check_rect(sk_rect_union(sk_rect_make(0, 0, 0, 0), sk_rect_make(5, 6, 1, 1)), 5, 6, 1, 1);
    check_rect(sk_rect_union(sk_rect_make(5, 6, 1, 1), sk_rect_make(-9, -9, 0, 5)), 5, 6, 1, 1);
    /* Wider than an int stops at INT_MAX. */
    sk_rect r = sk_rect_union(sk_rect_make(INT_MIN, 0, 10, 1), sk_rect_make(INT_MAX - 10, 0, 10, 1));
    CHECK_INT(r.x, INT_MIN);
    CHECK_INT(r.w, INT_MAX);
}

static void test_premul_known(void)
{
    CHECK_INT(sk_premul(SK_RGB(1, 2, 3)), 0xff010203u);
    CHECK_INT(sk_premul(SK_RGBA(255, 255, 255, 0)), 0);
    /* 255 * 128 / 255 = 128; 128 * 128 / 255 = 64.25. */
    CHECK_INT(sk_premul(SK_RGBA(255, 128, 0, 128)), 0x80804000u);
    /* 200 * 51 / 255 = 40; 100 * 51 / 255 = 20; 3 * 51 / 255 = 0.6. */
    CHECK_INT(sk_premul(SK_RGBA(200, 100, 3, 51)), 0x33281401u);
    CHECK_INT(sk_unpremul(0x80804000u), 0x80ff8000u);
    CHECK_INT(sk_unpremul(0), 0);
    CHECK_INT(sk_unpremul(0xff123456u), 0xff123456u);
    /* A colour above its alpha clamps rather than wraps. */
    CHECK_INT(sk_unpremul(0x10ff0000u), 0x10ff0000u);
}

/* Unpremultiplying then premultiplying again gives every premultiplied
 * pixel back: each step rounds by less than half a step of the other. */
static void test_premul_round_trip(void)
{
    int bad = 0;
    for (uint32_t a = 1; a < 256; a++)
        for (uint32_t c = 0; c <= a; c++) {
            sk_px p = a << 24 | c << 16 | (a - c) << 8 | c / 2;
            if (sk_premul(sk_unpremul(p)) != p)
                bad++;
        }
    CHECK_INT(bad, 0);
}

static void test_view(void)
{
    sk_image im;
    sk_image_init(&im, 8, 6);
    sk_image v = sk_image_view(&im, sk_rect_make(5, 4, 10, 10));
    CHECK_INT(v.w, 3);
    CHECK_INT(v.h, 2);
    CHECK_INT(v.stride, 8);
    CHECK(!v.owned);
    CHECK(v.px == im.px + 4 * 8 + 5);
    sk_image none = sk_image_view(&im, sk_rect_make(8, 0, 2, 2));
    CHECK(none.px == NULL && none.w == 0);
    sk_image_free(&v);
    CHECK(im.px != NULL);
    sk_image_free(&im);
}

const sk_test sk_suite_gfx_image[] = {
    T(test_intersect), T(test_union), T(test_premul_known), T(test_premul_round_trip), T(test_view), {0},
};
