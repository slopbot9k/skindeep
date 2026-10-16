/* Filled paths: coverage against exact areas, the fill rules, clipping,
 * transforms, and coordinates no shape should have. */
#include "../../src/gfx/draw.h"
#include "../golden.h"
#include "../test.h"

#include <math.h>

/* The path filled in opaque white over nothing: each pixel's alpha is
 * its coverage. */
static sk_image cover(const sk_path *p, const sk_xform *m, sk_fill_rule rule, int w, int h, const sk_rect *clip)
{
    sk_image im;
    sk_image_init(&im, w, h);
    sk_fill_path(&im, p, m, rule, SK_RGB(255, 255, 255), clip);
    return im;
}

static int at(const sk_image *im, int x, int y)
{
    return (int)(sk_image_row(im, y)[x] >> 24);
}

static double total(const sk_image *im)
{
    double sum = 0;
    for (int y = 0; y < im->h; y++)
        for (int x = 0; x < im->w; x++)
            sum += at(im, x, y);
    return sum / 255;
}

static void test_integer_rect(void)
{
    sk_path *p = sk_path_new();
    sk_path_rect(p, 2, 3, 4, 2);
    sk_image im = cover(p, NULL, SK_FILL_NONZERO, 8, 8, NULL);
    int bad = 0;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            bad += at(&im, x, y) != (x >= 2 && x < 6 && y >= 3 && y < 5 ? 255 : 0);
    CHECK_INT(bad, 0);
    /* Every pixel is white or nothing: the colour is the coverage too. */
    CHECK_INT(im.px[3 * 8 + 2], 0xffffffffu);
    sk_image_free(&im);
    sk_path_free(p);
}

/* A half-covered pixel is 128 (127.5 rounded), a quarter 64 (63.75), three
 * quarters 191 (191.25). */
static void test_fractional_edges(void)
{
    sk_path *p = sk_path_new();
    sk_path_rect(p, 0.5f, 1, 2, 1);
    sk_image im = cover(p, NULL, SK_FILL_NONZERO, 4, 3, NULL);
    CHECK_INT(at(&im, 0, 1), 128);
    CHECK_INT(at(&im, 1, 1), 255);
    CHECK_INT(at(&im, 2, 1), 128);
    CHECK_INT(at(&im, 3, 1), 0);
    CHECK_INT(at(&im, 1, 0), 0);
    CHECK_INT(at(&im, 1, 2), 0);
    sk_image_free(&im);

    sk_path_clear(p);
    sk_path_rect(p, 0.25f, 0.5f, 1, 1);
    im = cover(p, NULL, SK_FILL_NONZERO, 3, 3, NULL);
    CHECK_INT(at(&im, 0, 0), 96); /* 0.75 * 0.5 = 0.375: 95.6 */
    CHECK_INT(at(&im, 1, 0), 32); /* 0.25 * 0.5 = 0.125: 31.9 */
    CHECK_INT(at(&im, 0, 1), 96);
    CHECK_INT(at(&im, 1, 1), 32);
    CHECK_INT(at(&im, 2, 1), 0);
    sk_image_free(&im);

    sk_path_clear(p);
    sk_path_rect(p, 0.25f, 0, 1, 1);
    im = cover(p, NULL, SK_FILL_NONZERO, 2, 1, NULL);
    CHECK_INT(at(&im, 0, 0), 191);
    CHECK_INT(at(&im, 1, 0), 64);
    sk_image_free(&im);
    sk_path_free(p);
}

/* A right triangle whose long side runs corner to corner of the pixels
 * it crosses: those are half covered, the ones inside whole. */
static void test_triangle(void)
{
    sk_path *p = sk_path_new();
    sk_path_move(p, 0, 0);
    sk_path_line(p, 4, 0);
    sk_path_line(p, 0, 4);
    sk_path_close(p);
    sk_image im = cover(p, NULL, SK_FILL_NONZERO, 5, 5, NULL);
    int bad = 0;
    for (int y = 0; y < 5; y++)
        for (int x = 0; x < 5; x++)
            bad += at(&im, x, y) != (x + y <= 2 ? 255 : x + y == 3 ? 128 : 0);
    CHECK_INT(bad, 0);
    sk_image_free(&im);
    sk_path_free(p);
}

/* A circle's area, short by what its chords cut off: under 0.2 pixel
 * deep, so under 2% here. */
static void test_circle_area(void)
{
    sk_path *p = sk_path_new();
    sk_path_ellipse(p, 2, 2, 40, 40);
    sk_image im = cover(p, NULL, SK_FILL_NONZERO, 44, 44, NULL);
    double area = total(&im), want = 3.14159265 * 400;
    CHECK(area < want && area > want * 0.98);
    /* Symmetric to the pixel. */
    int bad = 0;
    for (int y = 0; y < 44; y++)
        for (int x = 0; x < 44; x++)
            bad += abs(at(&im, x, y) - at(&im, 43 - x, y)) > 1 || abs(at(&im, x, y) - at(&im, y, x)) > 1;
    CHECK_INT(bad, 0);
    sk_image_free(&im);
    sk_path_free(p);
}

/* A five-pointed star drawn in one stroke crosses itself: its middle
 * winds twice, so nonzero fills it and even-odd leaves it empty. */
static sk_path *star(float dx)
{
    static const float v[5][2] = {{24, 4}, {35.76f, 40.18f}, {4.98f, 17.82f}, {43.02f, 17.82f}, {12.24f, 40.18f}};
    sk_path *p = sk_path_new();
    sk_path_move(p, v[0][0] + dx, v[0][1]);
    for (int i = 1; i < 5; i++)
        sk_path_line(p, v[i][0] + dx, v[i][1]);
    sk_path_close(p);
    return p;
}

static void test_star_rules(void)
{
    sk_path *p = star(0);
    sk_image nz = cover(p, NULL, SK_FILL_NONZERO, 48, 48, NULL);
    sk_image eo = cover(p, NULL, SK_FILL_EVENODD, 48, 48, NULL);
    for (int y = 21; y <= 26; y++)
        for (int x = 21; x <= 26; x++) {
            CHECK_INT(at(&nz, x, y), 255);
            CHECK_INT(at(&eo, x, y), 0);
        }
    /* A point and an arm wind once. */
    CHECK_INT(at(&nz, 23, 10), 255);
    CHECK_INT(at(&eo, 23, 10), 255);
    CHECK_INT(at(&nz, 10, 19), 255);
    CHECK_INT(at(&eo, 10, 19), 255);
    CHECK_INT(at(&nz, 2, 2), 0);
    /* Outside the middle the rules agree. */
    int differ = 0;
    for (int y = 0; y < 48; y++)
        for (int x = 0; x < 48; x++)
            differ += at(&nz, x, y) != at(&eo, x, y);
    CHECK(differ > 100 && differ < 220);
    sk_image_free(&nz);
    sk_image_free(&eo);
    sk_path_free(p);
}

/* Two overlapping squares: wound the same way the overlap winds twice,
 * wound opposite ways it winds not at all. */
static void test_windings(void)
{
    sk_path *same = sk_path_new(), *opposite = sk_path_new();
    sk_path_rect(same, 2, 2, 8, 8);
    sk_path_rect(same, 6, 2, 8, 8);
    sk_path_rect(opposite, 2, 2, 8, 8);
    sk_path_move(opposite, 6, 2);
    sk_path_line(opposite, 6, 10);
    sk_path_line(opposite, 14, 10);
    sk_path_line(opposite, 14, 2);
    sk_path_close(opposite);
    sk_image a = cover(same, NULL, SK_FILL_NONZERO, 16, 12, NULL);
    sk_image b = cover(same, NULL, SK_FILL_EVENODD, 16, 12, NULL);
    sk_image c = cover(opposite, NULL, SK_FILL_NONZERO, 16, 12, NULL);
    CHECK_INT(at(&a, 7, 5), 255);
    CHECK_INT(at(&b, 7, 5), 0);
    CHECK_INT(at(&c, 7, 5), 0);
    CHECK_INT(at(&a, 3, 5), 255);
    CHECK_INT(at(&b, 12, 5), 255);
    CHECK_INT(at(&c, 12, 5), 255);
    sk_image_free(&a);
    sk_image_free(&b);
    sk_image_free(&c);
    sk_path_free(same);
    sk_path_free(opposite);
}

static void test_transform(void)
{
    sk_path *p = sk_path_new();
    sk_path_rect(p, 0, 0, 1, 1);
    sk_xform scale = {3, 0, 0, 3, 2, 1};
    sk_image im = cover(p, &scale, SK_FILL_NONZERO, 8, 6, NULL);
    int bad = 0;
    for (int y = 0; y < 6; y++)
        for (int x = 0; x < 8; x++)
            bad += at(&im, x, y) != (x >= 2 && x < 5 && y >= 1 && y < 4 ? 255 : 0);
    CHECK_INT(bad, 0);
    sk_image_free(&im);

    /* A quarter turn: x' = 10 - y, y' = x. */
    sk_path_clear(p);
    sk_path_rect(p, 0, 0, 4, 2);
    sk_xform turn = {0, 1, -1, 0, 10, 0};
    im = cover(p, &turn, SK_FILL_NONZERO, 12, 6, NULL);
    bad = 0;
    for (int y = 0; y < 6; y++)
        for (int x = 0; x < 12; x++)
            bad += at(&im, x, y) != (x >= 8 && x < 10 && y < 4 ? 255 : 0);
    CHECK_INT(bad, 0);
    sk_image_free(&im);
    sk_path_free(p);
}

/* Off the image, beyond the clip, out to 1e30: the visible part is
 * drawn and nothing else; NaN and infinity draw nothing at all. */
static void test_clipping_and_huge(void)
{
    sk_path *p = sk_path_new();
    sk_path_rect(p, -5, -5, 10, 10);
    sk_image im = cover(p, NULL, SK_FILL_NONZERO, 8, 8, NULL);
    CHECK_INT(at(&im, 4, 4), 255);
    CHECK_INT(at(&im, 5, 4), 0);
    CHECK_INT(at(&im, 0, 0), 255);
    sk_image_free(&im);

    sk_rect clip = sk_rect_make(2, 3, 3, 2);
    im = cover(p, NULL, SK_FILL_NONZERO, 8, 8, &clip);
    int bad = 0;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            bad += at(&im, x, y) != (x >= 2 && x < 5 && y >= 3 && y < 5 ? 255 : 0);
    CHECK_INT(bad, 0);
    sk_image_free(&im);

    sk_path_clear(p);
    sk_path_rect(p, -1e30f, -1e30f, 2e30f, 2e30f);
    im = cover(p, NULL, SK_FILL_NONZERO, 8, 8, NULL);
    CHECK_INT(total(&im), 64);
    sk_image_free(&im);

    /* A triangle reaching 1e30 to the right covers every row it spans. */
    sk_path_clear(p);
    sk_path_move(p, 0, 0);
    sk_path_line(p, 1e30f, 0);
    sk_path_line(p, 0, 8);
    sk_path_close(p);
    im = cover(p, NULL, SK_FILL_NONZERO, 8, 8, NULL);
    CHECK_INT(total(&im), 64);
    sk_image_free(&im);

    /* Curves with huge control points end, and stay inside the image. */
    sk_path_clear(p);
    sk_path_move(p, 1, 1);
    sk_path_cubic(p, 3e38f, -3e38f, -3e38f, 3e38f, 6, 6);
    sk_path_quad(p, 1e20f, 1e20f, 1, 6);
    sk_path_close(p);
    im = cover(p, NULL, SK_FILL_EVENODD, 8, 8, NULL);
    sk_image_free(&im);

    float bad_values[] = {NAN, INFINITY, -INFINITY};
    for (size_t i = 0; i < SK_LEN(bad_values); i++) {
        sk_path_clear(p);
        sk_path_rect(p, 1, 1, 5, 5);
        sk_path_line(p, bad_values[i], 3);
        im = cover(p, NULL, SK_FILL_NONZERO, 8, 8, NULL);
        CHECK_INT(total(&im), 0);
        sk_image_free(&im);
    }
    sk_path_clear(p);
    sk_path_rect(p, 1, 1, 5, 5);
    sk_xform nan = {NAN, 0, 0, 1, 0, 0};
    im = cover(p, &nan, SK_FILL_NONZERO, 8, 8, NULL);
    CHECK_INT(total(&im), 0);
    sk_image_free(&im);
    sk_path_free(p);
}

/* Drawn whole or a row or a column at a time, the pixels are the same:
 * bands of rows meet without seams, and an edge left of the clip still
 * counts for the pixels right of it. */
static void test_bands_and_clips_agree(void)
{
    sk_path *p = sk_path_new();
    sk_path_move(p, 10.3f, 5.1f);
    sk_path_line(p, 610.7f, 5.6f);
    sk_path_line(p, 650.2f, 155.4f);
    sk_path_quad(p, 300, 250, 50.3f, 155.9f);
    sk_path_close(p);
    sk_path_ellipse(p, 200.5f, 40.25f, 300, 70);
    sk_image whole = cover(p, NULL, SK_FILL_EVENODD, 700, 200, NULL), parts;
    sk_image_init(&parts, 700, 200);
    for (int y = 0; y < 200; y++) {
        sk_rect row = sk_rect_make(0, y, 700, 1);
        sk_fill_path(&parts, p, NULL, SK_FILL_EVENODD, SK_RGB(255, 255, 255), &row);
    }
    CHECK(memcmp(whole.px, parts.px, 700 * 200 * sizeof *parts.px) == 0);
    memset(parts.px, 0, 700 * 200 * sizeof *parts.px);
    for (int x = 0; x < 700; x += 7) {
        sk_rect col = sk_rect_make(x, 0, 7, 200);
        sk_fill_path(&parts, p, NULL, SK_FILL_EVENODD, SK_RGB(255, 255, 255), &col);
    }
    CHECK(memcmp(whole.px, parts.px, 700 * 200 * sizeof *parts.px) == 0);
    sk_image_free(&whole);
    sk_image_free(&parts);
    sk_path_free(p);
}

/* A mask holds the coverage the fill draws. */
static void test_mask_matches_fill(void)
{
    sk_path *p = star(-3);
    sk_path_ellipse(p, 20.5f, 20.5f, 17, 9);
    sk_xform m = {0.9f, 0.2f, -0.1f, 1.1f, 2.5f, -1.25f};
    for (int rule = 0; rule < 2; rule++) {
        sk_image im = cover(p, &m, (sk_fill_rule)rule, 41, 37, NULL);
        uint8_t *mask = sk_path_mask(p, &m, (sk_fill_rule)rule, 41, 37);
        int bad = 0;
        for (int i = 0; i < 41 * 37; i++)
            bad += mask[i] != im.px[i] >> 24;
        CHECK_INT(bad, 0);
        free(mask);
        sk_image_free(&im);
    }
    CHECK(sk_path_mask(p, NULL, SK_FILL_NONZERO, 0, 5) == NULL);
    sk_path_free(p);
}

/* An open subpath fills as if closed; a line with no subpath starts
 * one; after a close a line starts again where the closed one did. */
static void test_subpaths(void)
{
    sk_path *open = sk_path_new(), *closed = sk_path_new();
    sk_path_line(open, 1, 1); /* no subpath: a move */
    sk_path_line(open, 7, 1);
    sk_path_line(open, 1, 7);
    sk_path_move(closed, 1, 1);
    sk_path_line(closed, 7, 1);
    sk_path_line(closed, 1, 7);
    sk_path_close(closed);
    sk_image a = cover(open, NULL, SK_FILL_NONZERO, 8, 8, NULL), b = cover(closed, NULL, SK_FILL_NONZERO, 8, 8, NULL);
    CHECK(memcmp(a.px, b.px, 64 * sizeof *a.px) == 0);
    CHECK_INT(at(&a, 2, 2), 255);
    sk_image_free(&a);
    sk_image_free(&b);

    /* Closed, then on from its start: a second triangle sharing the
     * corner at (1, 1). */
    sk_path_line(closed, 1, 13);
    sk_path_line(closed, 7, 13);
    a = cover(closed, NULL, SK_FILL_NONZERO, 8, 14, NULL);
    CHECK_INT(at(&a, 2, 10), 255);
    CHECK_INT(at(&a, 6, 8), 0);
    sk_image_free(&a);
    sk_path_free(open);
    sk_path_free(closed);
}

/* Rounded rectangles clamp their radius; zero and NaN radii are plain
 * rectangles. */
static void test_round_rect(void)
{
    sk_path *a = sk_path_new(), *b = sk_path_new();
    sk_path_round_rect(a, 1, 1, 10, 6, 100);
    sk_path_round_rect(b, 1, 1, 10, 6, 3);
    sk_image ia = cover(a, NULL, SK_FILL_NONZERO, 12, 8, NULL), ib = cover(b, NULL, SK_FILL_NONZERO, 12, 8, NULL);
    CHECK(memcmp(ia.px, ib.px, 96 * sizeof *ia.px) == 0);
    CHECK(at(&ia, 1, 1) < 64);
    CHECK_INT(at(&ia, 6, 1), 255);
    sk_image_free(&ia);
    sk_image_free(&ib);
    sk_path_clear(a);
    sk_path_clear(b);
    sk_path_round_rect(a, 1, 1, 10, 6, NAN);
    sk_path_rect(b, 1, 1, 10, 6);
    ia = cover(a, NULL, SK_FILL_NONZERO, 12, 8, NULL);
    ib = cover(b, NULL, SK_FILL_NONZERO, 12, 8, NULL);
    CHECK(memcmp(ia.px, ib.px, 96 * sizeof *ia.px) == 0);
    sk_image_free(&ia);
    sk_image_free(&ib);
    sk_path_free(a);
    sk_path_free(b);
}

/* ---- Golden ---- */

static void test_golden_shapes(void)
{
    sk_image im;
    sk_image_init(&im, 240, 130);
    sk_fill_rect(&im, sk_image_bounds(&im), SK_RGB(250, 248, 240), SK_BLEND_SRC, NULL);
    sk_path *p = sk_path_new();
    sk_path_round_rect(p, 8.5f, 8.5f, 90, 44, 12);
    sk_fill_path(&im, p, NULL, SK_FILL_NONZERO, SK_RGB(40, 90, 170), NULL);
    sk_path_clear(p);
    sk_path_ellipse(p, 70.25f, 30.75f, 90, 50);
    sk_fill_path(&im, p, NULL, SK_FILL_NONZERO, SK_RGBA(220, 40, 40, 150), NULL);
    sk_path_clear(p);
    sk_path_rect(p, 170.5f, 6, 0.5f, 40);
    sk_path_rect(p, 175.25f, 6, 1.25f, 40);
    sk_path_rect(p, 180, 6, 2, 40);
    sk_path_ellipse(p, 190, 10, 1, 1);
    sk_path_ellipse(p, 195, 10, 3, 3);
    sk_path_ellipse(p, 202, 10, 6, 6);
    sk_path_ellipse(p, 212, 10, 12, 12);
    sk_fill_path(&im, p, NULL, SK_FILL_NONZERO, SK_RGB(20, 20, 20), NULL);
    sk_path_free(p);
    p = star(0);
    sk_xform at_left = {1, 0, 0, 1, 4, 76}, at_mid = {1, 0, 0, 1, 56, 76};
    sk_fill_path(&im, p, &at_left, SK_FILL_NONZERO, SK_RGB(200, 140, 0), NULL);
    sk_fill_path(&im, p, &at_mid, SK_FILL_EVENODD, SK_RGB(200, 140, 0), NULL);
    sk_path_clear(p);
    /* A square turned 30 degrees: cos 0.8660254, sin 0.5. */
    sk_path_rect(p, -18, -18, 36, 36);
    sk_xform turned = {0.8660254f, 0.5f, -0.5f, 0.8660254f, 140, 98};
    sk_fill_path(&im, p, &turned, SK_FILL_NONZERO, SK_RGBA(30, 140, 70, 200), NULL);
    sk_path_clear(p);
    sk_path_move(p, 175, 120);
    sk_path_line(p, 236, 60);
    sk_path_line(p, 230, 124);
    sk_path_close(p);
    sk_path_move(p, 188, 60);
    sk_path_line(p, 192, 124);
    sk_path_line(p, 190.5f, 124);
    sk_path_close(p);
    sk_fill_path(&im, p, NULL, SK_FILL_NONZERO, SK_RGB(120, 40, 160), NULL);
    sk_path_free(p);
    CHECK_GOLDEN("path-shapes", &im, 1);
    sk_image_free(&im);
}

static void test_golden_curves(void)
{
    sk_image im;
    sk_image_init(&im, 240, 120);
    sk_path *p = sk_path_new();
    /* A heart of two cubics. */
    sk_path_move(p, 50, 104);
    sk_path_cubic(p, 10, 70, 6, 30, 30, 22);
    sk_path_cubic(p, 42, 18, 50, 28, 50, 36);
    sk_path_cubic(p, 50, 28, 58, 18, 70, 22);
    sk_path_cubic(p, 94, 30, 90, 70, 50, 104);
    sk_path_close(p);
    sk_fill_path(&im, p, NULL, SK_FILL_NONZERO, SK_RGB(210, 30, 60), NULL);
    sk_path_clear(p);
    /* An arch of quadratics. */
    sk_path_move(p, 104, 110);
    sk_path_quad(p, 134, -40, 164, 110);
    sk_path_line(p, 150, 110);
    sk_path_quad(p, 134, 20, 118, 110);
    sk_path_close(p);
    sk_fill_path(&im, p, NULL, SK_FILL_NONZERO, SK_RGB(30, 110, 200), NULL);
    sk_path_clear(p);
    /* A cubic that crosses itself, closed by its base: two lobes, wound
     * opposite ways, which even-odd fills alike. */
    sk_path_move(p, 172, 100);
    sk_path_cubic(p, 290, 0, 150, 0, 236, 100);
    sk_path_close(p);
    sk_fill_path(&im, p, NULL, SK_FILL_EVENODD, SK_RGBA(20, 150, 90, 220), NULL);
    sk_path_clear(p);
    /* A thin S, close to its flattening tolerance. */
    sk_path_move(p, 176, 10);
    sk_path_cubic(p, 236, 10, 176, 50, 236, 50);
    sk_path_line(p, 236, 52);
    sk_path_cubic(p, 174, 52, 234, 12, 176, 12);
    sk_path_close(p);
    sk_fill_path(&im, p, NULL, SK_FILL_NONZERO, SK_RGB(40, 40, 40), NULL);
    sk_path_free(p);
    CHECK_GOLDEN("path-curves", &im, 1);
    sk_image_free(&im);
}

const sk_test sk_suite_gfx_path[] = {
    T(test_integer_rect),
    T(test_fractional_edges),
    T(test_triangle),
    T(test_circle_area),
    T(test_star_rules),
    T(test_windings),
    T(test_transform),
    T(test_clipping_and_huge),
    T(test_bands_and_clips_agree),
    T(test_mask_matches_fill),
    T(test_subpaths),
    T(test_round_rect),
    T(test_golden_shapes),
    T(test_golden_curves),
    {0},
};
