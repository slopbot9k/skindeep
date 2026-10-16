/* Rectangles, blits and masks: where they land and where they stop. */
#include "../../src/gfx/draw.h"
#include "../golden.h"
#include "../test.h"

#include <limits.h>

#define RED 0xffff0000u
#define BLUE 0xff0000ffu

/* The pixels of im that are not 0, as a w by h picture of '#' and '.',
 * row after row. */
static void picture(const sk_image *im, char *out)
{
    for (int y = 0; y < im->h; y++) {
        for (int x = 0; x < im->w; x++)
            *out++ = sk_image_row(im, y)[x] ? '#' : '.';
        *out++ = '/';
    }
    *out = 0;
}

static void check_fill(sk_rect r, const sk_rect *clip, const char *want)
{
    sk_image im;
    sk_image_init(&im, 6, 4);
    sk_fill_rect(&im, r, SK_RGB(255, 0, 0), SK_BLEND_SRC_OVER, clip);
    char got[64];
    picture(&im, got);
    CHECK_STR(got, want);
    sk_image_free(&im);
}

static void test_fill_rect_clips_at_every_edge(void)
{
    check_fill(sk_rect_make(1, 1, 2, 2), NULL, "....../.##.../.##.../....../");
    check_fill(sk_rect_make(-3, 1, 5, 1), NULL, "....../##..../....../....../");
    check_fill(sk_rect_make(4, 2, 9, 9), NULL, "....../....../....##/....##/");
    check_fill(sk_rect_make(2, -7, 1, 8), NULL, "..#.../....../....../....../");
    check_fill(sk_rect_make(0, 3, 6, 1), NULL, "....../....../....../######/");
    check_fill(sk_rect_make(-100, -100, 1000, 1000), NULL, "######/######/######/######/");
    check_fill(sk_rect_make(6, 0, 2, 2), NULL, "....../....../....../....../");
    check_fill(sk_rect_make(0, 4, 2, 2), NULL, "....../....../....../....../");
    check_fill(sk_rect_make(INT_MAX - 2, INT_MAX - 2, INT_MAX, INT_MAX), NULL, "....../....../....../....../");
    check_fill(sk_rect_make(INT_MIN, INT_MIN, INT_MAX, INT_MAX), NULL, "....../....../....../....../");
    /* The clip at each side, and a clip outside the image. */
    sk_rect clip = sk_rect_make(1, 1, 4, 2);
    check_fill(sk_rect_make(0, 0, 6, 4), &clip, "....../.####./.####./....../");
    check_fill(sk_rect_make(0, 0, 2, 4), &clip, "....../.#..../.#..../....../");
    check_fill(sk_rect_make(4, 0, 2, 4), &clip, "....../....#./....#./....../");
    check_fill(sk_rect_make(0, 2, 6, 2), &clip, "....../....../.####./....../");
    sk_rect away = sk_rect_make(10, 10, 5, 5);
    check_fill(sk_rect_make(0, 0, 6, 4), &away, "....../....../....../....../");
}

/* A 3 by 2 source of distinct pixels; where each lands says where the
 * blit put it. */
static void make_src(sk_image *src)
{
    sk_image_init(src, 3, 2);
    for (int i = 0; i < 6; i++)
        src->px[i] = 0xff000000u | (uint32_t)(i + 1);
}

/* The source index at each pixel of a 5 by 3 destination, 0 for none. */
static void check_blit(int x, int y, sk_rect src_r, const sk_rect *clip, const char *want)
{
    sk_image src, dst;
    make_src(&src);
    sk_image_init(&dst, 5, 3);
    sk_blit(&dst, x, y, &src, src_r, SK_BLEND_SRC_OVER, 255, clip);
    char got[32], *o = got;
    for (int j = 0; j < 3; j++) {
        for (int i = 0; i < 5; i++)
            *o++ = (char)('0' + (sk_image_row(&dst, j)[i] & 255));
        *o++ = '/';
    }
    *o = 0;
    CHECK_STR(got, want);
    sk_image_free(&src);
    sk_image_free(&dst);
}

static void test_blit_clips_at_every_edge(void)
{
    sk_rect all = sk_rect_make(0, 0, 3, 2);
    check_blit(1, 1, all, NULL, "00000/01230/04560/");
    check_blit(-2, 0, all, NULL, "30000/60000/00000/");
    check_blit(3, -1, all, NULL, "00045/00000/00000/");
    check_blit(4, 2, all, NULL, "00000/00000/00001/");
    check_blit(5, 0, all, NULL, "00000/00000/00000/");
    check_blit(INT_MIN, INT_MIN, all, NULL, "00000/00000/00000/");
    /* Part of the source; a part reaching outside the source keeps its
     * corner at (x, y), so what is inside lands where it would. */
    check_blit(0, 0, sk_rect_make(1, 1, 2, 1), NULL, "56000/00000/00000/");
    check_blit(0, 0, sk_rect_make(-1, -1, 3, 3), NULL, "00000/01200/04500/");
    check_blit(0, 0, sk_rect_make(2, 0, 5, 5), NULL, "30000/60000/00000/");
    check_blit(0, 0, sk_rect_make(3, 0, 5, 5), NULL, "00000/00000/00000/");
    sk_rect clip = sk_rect_make(1, 1, 2, 1);
    check_blit(0, 0, all, &clip, "00000/05600/00000/");
    check_blit(2, 0, all, &clip, "00000/00400/00000/");
}

static void test_fill_mask_clips_at_every_edge(void)
{
    /* #.#  ..#  #..  with a fourth byte to each row the stride skips. */
    static const uint8_t a[] = {255, 0, 255, 0, 0, 0, 255, 255, 255, 0, 0, 0};
    sk_mask m = {a, 3, 3, 4};
    struct {
        int x, y;
        bool clip;
        const char *want;
    } cases[] = {
        {1, 0, false, ".#.#../...#../.#..../....../"},
        {-1, 0, false, ".#..../.#..../....../....../"},
        {4, 2, false, "....../....../....#./....../"},
        {0, -2, false, "#...../....../....../....../"},
        {5, 3, false, "....../....../....../.....#/"},
        {6, 0, false, "....../....../....../....../"},
        {0, 0, true, "....../..#.../....../....../"},
    };
    for (size_t i = 0; i < SK_LEN(cases); i++) {
        sk_image im;
        sk_image_init(&im, 6, 4);
        sk_rect clip = sk_rect_make(1, 1, 4, 2);
        sk_fill_mask(&im, cases[i].x, cases[i].y, &m, SK_RGB(9, 9, 9), cases[i].clip ? &clip : NULL);
        char got[64];
        picture(&im, got);
        CHECK_STR(got, cases[i].want);
        sk_image_free(&im);
    }
}

/* Coverage scales the colour's alpha: half a mask of half red. */
static void test_fill_mask_coverage(void)
{
    static const uint8_t a[] = {0, 128, 255};
    sk_mask m = {a, 3, 1, 3};
    sk_image im;
    sk_image_init(&im, 3, 1);
    sk_fill_mask(&im, 0, 0, &m, SK_RGBA(255, 0, 0, 128), NULL);
    CHECK_INT(im.px[0], 0);
    /* mul(128, 128) = 64.25 */
    CHECK_INT(im.px[1], 0x40400000u);
    CHECK_INT(im.px[2], 0x80800000u);
    sk_image_free(&im);
}

/* Scaled to its own size, either filter is a plain blit. */
static void test_scaled_identity(void)
{
    sk_image src, a, b;
    sk_image_init(&src, 7, 5);
    for (int i = 0; i < 35; i++)
        src.px[i] = sk_premul(SK_RGBA(i * 7, 255 - i * 5, i * 3, 100 + i * 4));
    for (int f = 0; f < 2; f++) {
        sk_image_init(&a, 9, 8);
        sk_image_init(&b, 9, 8);
        sk_blit(&a, 1, 2, &src, sk_image_bounds(&src), SK_BLEND_SRC_OVER, 200, NULL);
        sk_blit_scaled(&b, sk_rect_make(1, 2, 7, 5), &src, sk_image_bounds(&src), (sk_filter)f, SK_BLEND_SRC_OVER,
                       200, NULL);
        CHECK(memcmp(a.px, b.px, 9 * 8 * sizeof *a.px) == 0);
        sk_image_free(&a);
        sk_image_free(&b);
    }
    sk_image_free(&src);
}

/* Nearest at 3x: each pixel a 3 by 3 block. */
static void test_scaled_nearest(void)
{
    sk_image src, dst;
    make_src(&src);
    sk_image_init(&dst, 9, 6);
    sk_blit_scaled(&dst, sk_image_bounds(&dst), &src, sk_image_bounds(&src), SK_FILTER_NEAREST, SK_BLEND_SRC, 255,
                   NULL);
    int bad = 0;
    for (int y = 0; y < 6; y++)
        for (int x = 0; x < 9; x++)
            bad += sk_image_row(&dst, y)[x] != src.px[(y / 3) * 3 + x / 3];
    CHECK_INT(bad, 0);
    sk_image_free(&src);
    sk_image_free(&dst);
}

/* Bilinear, by hand. Black and white, two pixels, to four: the centres
 * 0.5, 1.5, 2.5, 3.5 map to 0.25, 0.75, 1.25, 1.75, sampled half a pixel
 * before, at -0.25 (the edge), 0.25, 0.75 and 1.25 (the edge again):
 * 0, 255 * 64 / 256 = 63.75, 255 * 192 / 256 = 191.25, 255. Down, four
 * pixels to two: centres 1 and 3, sampled at 0.5 and 2.5, halfway. */
static void test_scaled_bilinear(void)
{
    sk_image src, dst;
    sk_image_init(&src, 2, 1);
    src.px[0] = 0xff000000u;
    src.px[1] = 0xffffffffu;
    sk_image_init(&dst, 4, 1);
    sk_blit_scaled(&dst, sk_image_bounds(&dst), &src, sk_image_bounds(&src), SK_FILTER_BILINEAR, SK_BLEND_SRC, 255,
                   NULL);
    CHECK_INT(dst.px[0], 0xff000000u);
    CHECK_INT(dst.px[1], 0xff404040u);
    CHECK_INT(dst.px[2], 0xffbfbfbfu);
    CHECK_INT(dst.px[3], 0xffffffffu);
    sk_image_free(&src);

    sk_image_init(&src, 4, 1);
    src.px[0] = 0xff000000u;
    src.px[1] = 0xff646464u;
    src.px[2] = 0xff0a0a0au;
    src.px[3] = 0xff141414u;
    sk_image down;
    sk_image_init(&down, 2, 1);
    sk_blit_scaled(&down, sk_image_bounds(&down), &src, sk_image_bounds(&src), SK_FILTER_BILINEAR, SK_BLEND_SRC, 255,
                   NULL);
    CHECK_INT(down.px[0], 0xff323232u); /* (0 + 100) / 2 */
    CHECK_INT(down.px[1], 0xff0f0f0fu); /* (10 + 20) / 2 */
    sk_image_free(&src);
    sk_image_free(&dst);
    sk_image_free(&down);
}

/* A clipped scaled blit draws exactly the part of the whole one inside
 * the clip, and a destination off the image maps as if it were not. */
static void test_scaled_clipped(void)
{
    sk_image src, whole, part;
    sk_image_init(&src, 5, 4);
    for (int i = 0; i < 20; i++)
        src.px[i] = sk_premul(SK_RGBA(i * 13, i * 11, 255 - i * 9, 120 + i * 6));
    for (int f = 0; f < 2; f++) {
        sk_image_init(&whole, 40, 30);
        sk_image_init(&part, 40, 30);
        sk_rect dr = sk_rect_make(-7, -3, 51, 37);
        sk_rect clip = sk_rect_make(3, 4, 30, 9);
        sk_blit_scaled(&whole, dr, &src, sk_image_bounds(&src), (sk_filter)f, SK_BLEND_SRC_OVER, 255, NULL);
        sk_blit_scaled(&part, dr, &src, sk_image_bounds(&src), (sk_filter)f, SK_BLEND_SRC_OVER, 255, &clip);
        int bad = 0;
        for (int y = 0; y < 30; y++)
            for (int x = 0; x < 40; x++) {
                bool in = x >= 3 && x < 33 && y >= 4 && y < 13;
                bad += sk_image_row(&part, y)[x] != (in ? sk_image_row(&whole, y)[x] : 0);
            }
        CHECK_INT(bad, 0);
        sk_image_free(&whole);
        sk_image_free(&part);
    }
    sk_image_free(&src);
}

/* src_r reaching beyond src repeats the edge of what is inside; empty
 * rectangles draw nothing; huge ones do not overflow. */
static void test_scaled_edges(void)
{
    sk_image src, dst;
    make_src(&src);
    sk_image_init(&dst, 8, 8);
    sk_blit_scaled(&dst, sk_image_bounds(&dst), &src, sk_rect_make(-2, -2, 7, 6), SK_FILTER_NEAREST, SK_BLEND_SRC,
                   255, NULL);
    CHECK_INT(dst.px[0] & 255, 1);      /* left of and above the source: its corner */
    CHECK_INT(dst.px[63] & 255, 6);     /* past it: the far corner */
    CHECK_INT(dst.px[3 * 8 + 3] & 255, 2);
    memset(dst.px, 0, 64 * sizeof *dst.px);
    sk_blit_scaled(&dst, sk_rect_make(0, 0, 0, 8), &src, sk_image_bounds(&src), SK_FILTER_BILINEAR, SK_BLEND_SRC, 255,
                   NULL);
    sk_blit_scaled(&dst, sk_image_bounds(&dst), &src, sk_rect_make(0, 0, 3, 0), SK_FILTER_BILINEAR, SK_BLEND_SRC, 255,
                   NULL);
    sk_blit_scaled(&dst, sk_image_bounds(&dst), &src, sk_rect_make(5, 5, 3, 3), SK_FILTER_BILINEAR, SK_BLEND_SRC, 255,
                   NULL);
    for (int i = 0; i < 64; i++)
        CHECK_INT(dst.px[i], 0);
    sk_blit_scaled(&dst, sk_rect_make(INT_MIN, INT_MIN, INT_MAX, INT_MAX), &src, sk_rect_make(0, 0, INT_MAX, 2),
                   SK_FILTER_BILINEAR, SK_BLEND_SRC, 255, NULL);
    sk_blit_scaled(&dst, sk_rect_make(-5, -5, INT_MAX, INT_MAX), &src, sk_rect_make(INT_MIN, 0, INT_MAX, 2),
                   SK_FILTER_BILINEAR, SK_BLEND_SRC, 255, NULL);
    sk_blit_scaled(&dst, sk_rect_make(-5, -5, INT_MAX, INT_MAX), &src, sk_image_bounds(&src), SK_FILTER_NEAREST,
                   SK_BLEND_SRC, 255, NULL);
    CHECK_INT(dst.px[0] & 255, 1);
    sk_image_free(&src);
    sk_image_free(&dst);
}

/* A small picture scaled up, nearest and bilinear, and down, in a
 * strip: crisp blocks, smooth ramps, a softened copy. */
static void test_golden_scaled(void)
{
    sk_image src, im;
    sk_image_init(&src, 8, 8);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            sk_color c = (x + y) % 2 ? SK_RGB(240, 200, 40) : SK_RGB(30, 60, 160);
            if (x == 0 || y == 0 || x == 7 || y == 7)
                c = SK_RGBA(200, 30, 30, 160);
            if (x >= 3 && x <= 4 && y >= 3 && y <= 4)
                c = SK_RGBA(255, 255, 255, 60);
            sk_image_row(&src, y)[x] = sk_premul(c);
        }
    sk_image_init(&im, 200, 72);
    sk_fill_rect(&im, sk_image_bounds(&im), SK_RGB(235, 235, 235), SK_BLEND_SRC, NULL);
    sk_blit_scaled(&im, sk_rect_make(4, 4, 64, 64), &src, sk_image_bounds(&src), SK_FILTER_NEAREST, SK_BLEND_SRC_OVER,
                   255, NULL);
    sk_blit_scaled(&im, sk_rect_make(72, 4, 64, 64), &src, sk_image_bounds(&src), SK_FILTER_BILINEAR,
                   SK_BLEND_SRC_OVER, 255, NULL);
    sk_blit_scaled(&im, sk_rect_make(140, 4, 56, 21), &src, sk_image_bounds(&src), SK_FILTER_BILINEAR,
                   SK_BLEND_SRC_OVER, 255, NULL);
    sk_blit_scaled(&im, sk_rect_make(140, 30, 5, 5), &src, sk_image_bounds(&src), SK_FILTER_BILINEAR,
                   SK_BLEND_SRC_OVER, 255, NULL);
    sk_blit_scaled(&im, sk_rect_make(150, 30, 46, 38), &src, sk_rect_make(2, 2, 4, 4), SK_FILTER_BILINEAR,
                   SK_BLEND_SRC_OVER, 160, NULL);
    CHECK_GOLDEN("scaled", &im, 0);
    sk_image_free(&src);
    sk_image_free(&im);
}

const sk_test sk_suite_gfx_draw[] = {
    T(test_fill_rect_clips_at_every_edge),
    T(test_blit_clips_at_every_edge),
    T(test_fill_mask_clips_at_every_edge),
    T(test_fill_mask_coverage),
    T(test_scaled_identity),
    T(test_scaled_nearest),
    T(test_scaled_bilinear),
    T(test_scaled_clipped),
    T(test_scaled_edges),
    T(test_golden_scaled),
    {0},
};
