/* Strokes: the outline's geometry, read back as coverage. */
#include "../../src/gfx/draw.h"
#include "../golden.h"
#include "../test.h"

#include <math.h>

/* p stroked and filled in opaque white over nothing, so each pixel's
 * alpha is its coverage. */
static sk_image stroked(const sk_path *p, float width, sk_cap cap, sk_join join, float limit, int w, int h)
{
    sk_path *out = sk_path_new();
    sk_path_stroke(out, p, width, cap, join, limit);
    sk_image im;
    sk_image_init(&im, w, h);
    sk_fill_path(&im, out, NULL, SK_FILL_NONZERO, SK_RGB(255, 255, 255), NULL);
    sk_path_free(out);
    return im;
}

static int at(const sk_image *im, int x, int y)
{
    return (int)(sk_image_row(im, y)[x] >> 24);
}

static double total(const sk_image *im)
{
    double sum = 0;
    for (int i = 0; i < im->w * im->h; i++)
        sum += im->px[i] >> 24;
    return sum / 255;
}

static sk_path *segment(float x0, float y0, float x1, float y1)
{
    sk_path *p = sk_path_new();
    sk_path_move(p, x0, y0);
    sk_path_line(p, x1, y1);
    return p;
}

/* A line 4 wide from (10, 10) to (30, 10): butt ends where it does,
 * square ends half the width past, round ends in half circles. */
static void test_caps(void)
{
    sk_path *p = segment(10, 10, 30, 10);
    sk_image im = stroked(p, 4, SK_CAP_BUTT, SK_JOIN_MITER, 4, 40, 20);
    int bad = 0;
    for (int y = 0; y < 20; y++)
        for (int x = 0; x < 40; x++)
            bad += at(&im, x, y) != (x >= 10 && x < 30 && y >= 8 && y < 12 ? 255 : 0);
    CHECK_INT(bad, 0);
    sk_image_free(&im);

    im = stroked(p, 4, SK_CAP_SQUARE, SK_JOIN_MITER, 4, 40, 20);
    bad = 0;
    for (int y = 0; y < 20; y++)
        for (int x = 0; x < 40; x++)
            bad += at(&im, x, y) != (x >= 8 && x < 32 && y >= 8 && y < 12 ? 255 : 0);
    CHECK_INT(bad, 0);
    sk_image_free(&im);

    /* 80 plus a circle of radius 2, 12.57, less what the chords cut off:
     * within 0.2 of the arc, they cut at most 2/3 of 0.2 times its
     * length, 1.68. */
    im = stroked(p, 4, SK_CAP_ROUND, SK_JOIN_MITER, 4, 40, 20);
    CHECK(total(&im) < 92.57 && total(&im) > 92.57 - 1.68);
    CHECK_INT(at(&im, 9, 9), 255);
    CHECK_INT(at(&im, 7, 9), 0);
    CHECK_INT(at(&im, 30, 10), 255);
    sk_image_free(&im);
    sk_path_free(p);
}

/* A right-angled turn, 4 wide, from (10, 10) right to (30, 10) and down
 * to (30, 30). The outer corner is the square from (30, 8) to (32, 10):
 * a miter fills it, a bevel cuts it on the diagonal from (30, 8) to (32,
 * 10), a round join by a quarter circle about (30, 10). */
static void test_joins(void)
{
    sk_path *p = segment(10, 10, 30, 10);
    sk_path_line(p, 30, 30);
    sk_image miter = stroked(p, 4, SK_CAP_BUTT, SK_JOIN_MITER, 4, 40, 40);
    sk_image bevel = stroked(p, 4, SK_CAP_BUTT, SK_JOIN_BEVEL, 4, 40, 40);
    sk_image rounded = stroked(p, 4, SK_CAP_BUTT, SK_JOIN_ROUND, 4, 40, 40);
    CHECK_INT(at(&miter, 31, 8), 255);
    CHECK_INT(at(&miter, 30, 8), 255);
    CHECK_INT(at(&bevel, 31, 8), 0);
    CHECK_INT(at(&bevel, 30, 8), 128);
    CHECK_INT(at(&bevel, 31, 9), 128);
    CHECK_INT(at(&bevel, 30, 9), 255);
    /* The quarter circle's part of the far pixel: the integral of
     * sqrt(4 - x^2) - 1 over 1..sqrt 3, 0.315, so 80, less the chord's
     * cut, up to 0.2 deep. */
    CHECK(at(&rounded, 31, 8) > 40 && at(&rounded, 31, 8) <= 80);
    CHECK_INT(at(&rounded, 30, 9), 255);
    /* Inside the turn all three agree: nothing past the inner corner. */
    CHECK_INT(at(&miter, 27, 13), 0);
    CHECK_INT(at(&bevel, 28, 12), 255);
    CHECK(fabs(total(&miter) - (total(&bevel) + 2)) < 0.01);
    sk_image_free(&miter);
    sk_image_free(&bevel);
    sk_image_free(&rounded);
    sk_path_free(p);
}

/* A turn of about 143 degrees: the segments meet at 36.87 degrees, so
 * the miter is 1 / sin(18.43) = 3.16 widths long. Under a limit of 4 it
 * reaches (46.3, 20); under 3 it becomes a bevel. */
static void test_miter_limit(void)
{
    sk_path *p = segment(10, 30, 40, 20);
    sk_path_line(p, 10, 10);
    sk_image pointed = stroked(p, 4, SK_CAP_BUTT, SK_JOIN_MITER, 4, 50, 40);
    sk_image cut = stroked(p, 4, SK_CAP_BUTT, SK_JOIN_MITER, 3, 50, 40);
    CHECK(at(&pointed, 44, 19) > 100);
    CHECK_INT(at(&pointed, 47, 19), 0);
    CHECK_INT(at(&cut, 44, 19), 0);
    CHECK_INT(at(&cut, 42, 19), 0);
    sk_image bevel = stroked(p, 4, SK_CAP_BUTT, SK_JOIN_BEVEL, 4, 50, 40);
    CHECK(memcmp(cut.px, bevel.px, 50 * 40 * sizeof *cut.px) == 0);
    /* Below 1, as SVG allows none, is 1. */
    sk_image_free(&cut);
    cut = stroked(p, 4, SK_CAP_BUTT, SK_JOIN_MITER, 0.5f, 50, 40);
    CHECK(memcmp(cut.px, bevel.px, 50 * 40 * sizeof *cut.px) == 0);
    sk_image_free(&pointed);
    sk_image_free(&cut);
    sk_image_free(&bevel);
    sk_path_free(p);
}

/* A closed square: a ring with mitered corners all round, its start
 * included, and its middle empty. Caps do not apply. */
static void test_closed(void)
{
    sk_path *p = sk_path_new();
    sk_path_rect(p, 10, 10, 20, 20);
    sk_image im = stroked(p, 4, SK_CAP_ROUND, SK_JOIN_MITER, 4, 40, 40);
    int bad = 0;
    for (int y = 0; y < 40; y++)
        for (int x = 0; x < 40; x++) {
            bool outer = x >= 8 && x < 32 && y >= 8 && y < 32, inner = x >= 12 && x < 28 && y >= 12 && y < 28;
            bad += at(&im, x, y) != (outer && !inner ? 255 : 0);
        }
    CHECK_INT(bad, 0);
    sk_image_free(&im);
    /* Wound the other way, the same ring. */
    sk_path_clear(p);
    sk_path_move(p, 10, 10);
    sk_path_line(p, 10, 30);
    sk_path_line(p, 30, 30);
    sk_path_line(p, 30, 10);
    sk_path_close(p);
    sk_image back = stroked(p, 4, SK_CAP_ROUND, SK_JOIN_MITER, 4, 40, 40);
    im = stroked(p, 4, SK_CAP_BUTT, SK_JOIN_MITER, 4, 40, 40);
    CHECK(memcmp(im.px, back.px, 40 * 40 * sizeof *im.px) == 0);
    CHECK_INT(at(&im, 8, 8), 255);
    CHECK_INT(at(&im, 20, 20), 0);
    sk_image_free(&im);
    sk_image_free(&back);
    sk_path_free(p);
}

/* A circle of radius 10 stroked 2 wide: a ring of 2 pi 10 2. */
static void test_curves(void)
{
    sk_path *p = sk_path_new();
    sk_path_ellipse(p, 10, 10, 20, 20);
    sk_image im = stroked(p, 2, SK_CAP_BUTT, SK_JOIN_BEVEL, 4, 40, 40);
    CHECK(fabs(total(&im) - 125.66) < 2);
    CHECK_INT(at(&im, 20, 20), 0);
    CHECK_INT(at(&im, 20, 10), 255);
    sk_image_free(&im);
    /* A quadratic whose control point is on its chord is a line. */
    sk_path_clear(p);
    sk_path_move(p, 10, 10);
    sk_path_quad(p, 20, 10, 30, 10);
    im = stroked(p, 4, SK_CAP_BUTT, SK_JOIN_MITER, 4, 40, 20);
    sk_path *line = segment(10, 10, 30, 10);
    sk_image want = stroked(line, 4, SK_CAP_BUTT, SK_JOIN_MITER, 4, 40, 20);
    CHECK(memcmp(im.px, want.px, 40 * 20 * sizeof *im.px) == 0);
    sk_image_free(&im);
    sk_image_free(&want);
    sk_path_free(line);
    sk_path_free(p);
}

/* Zero-length subpaths: a dot under round and square caps, nothing under
 * butt; a lone move is no subpath at all. Repeated points change
 * nothing. */
static void test_degenerate(void)
{
    sk_path *p = segment(20, 20, 20, 20);
    /* A circle of radius 3, 28.27, less up to 2/3 of 0.2 of its 18.85
     * round. */
    sk_image im = stroked(p, 6, SK_CAP_ROUND, SK_JOIN_MITER, 4, 40, 40);
    CHECK(total(&im) < 28.28 && total(&im) > 28.27 - 2.52);
    sk_image_free(&im);
    im = stroked(p, 6, SK_CAP_SQUARE, SK_JOIN_MITER, 4, 40, 40);
    CHECK(fabs(total(&im) - 36) < 0.01);
    CHECK_INT(at(&im, 17, 17), 255);
    sk_image_free(&im);
    im = stroked(p, 6, SK_CAP_BUTT, SK_JOIN_MITER, 4, 40, 40);
    CHECK_INT(total(&im), 0);
    sk_image_free(&im);
    sk_path_clear(p);
    sk_path_move(p, 20, 20);
    sk_path_close(p);
    im = stroked(p, 6, SK_CAP_SQUARE, SK_JOIN_MITER, 4, 40, 40);
    CHECK(fabs(total(&im) - 36) < 0.01);
    sk_image_free(&im);
    sk_path_clear(p);
    sk_path_move(p, 20, 20);
    im = stroked(p, 6, SK_CAP_ROUND, SK_JOIN_MITER, 4, 40, 40);
    CHECK_INT(total(&im), 0);
    sk_image_free(&im);

    sk_path *plain = segment(5, 5, 30, 5), *repeats = segment(5, 5, 5, 5);
    sk_path_line(plain, 30, 30);
    sk_path_line(repeats, 30, 5);
    sk_path_line(repeats, 30, 5);
    sk_path_line(repeats, 30, 30);
    sk_path_line(repeats, 30, 30);
    for (int cap = 0; cap < 3; cap++) {
        sk_image a = stroked(plain, 3, (sk_cap)cap, SK_JOIN_ROUND, 4, 40, 40);
        sk_image b = stroked(repeats, 3, (sk_cap)cap, SK_JOIN_ROUND, 4, 40, 40);
        CHECK(memcmp(a.px, b.px, 40 * 40 * sizeof *a.px) == 0);
        sk_image_free(&a);
        sk_image_free(&b);
    }
    sk_path_free(plain);
    sk_path_free(repeats);
    sk_path_free(p);
}

/* Back on itself: the stroke of a there-and-back line is the line's,
 * with the far end capped by the turn. */
static void test_reversal(void)
{
    sk_path *p = segment(10, 10, 30, 10);
    sk_path_line(p, 10, 10);
    sk_image im = stroked(p, 4, SK_CAP_BUTT, SK_JOIN_ROUND, 4, 40, 20);
    CHECK_INT(at(&im, 20, 9), 255);
    CHECK_INT(at(&im, 30, 9), 255);
    CHECK_INT(at(&im, 9, 9), 0);
    sk_image_free(&im);
    im = stroked(p, 4, SK_CAP_BUTT, SK_JOIN_MITER, 4, 40, 20);
    CHECK_INT(at(&im, 30, 9), 0);
    CHECK_INT(at(&im, 29, 9), 255);
    sk_image_free(&im);
    sk_path_free(p);
}

/* Stroked into itself, a path gains its outline; widths that are not
 * positive and finite, and NaN points, stroke to nothing. */
static void test_odd_arguments(void)
{
    sk_path *p = segment(10, 10, 30, 10), *out = sk_path_new();
    sk_path_stroke(p, p, 4, SK_CAP_BUTT, SK_JOIN_MITER, 4);
    sk_image im;
    sk_image_init(&im, 40, 20);
    sk_fill_path(&im, p, NULL, SK_FILL_NONZERO, SK_RGB(255, 255, 255), NULL);
    CHECK_INT(at(&im, 20, 9), 255);
    CHECK_INT(at(&im, 20, 13), 0);
    sk_image_free(&im);
    sk_path_free(p);

    p = segment(10, 10, 30, 10);
    float widths[] = {0, -1, NAN, INFINITY};
    for (size_t i = 0; i < SK_LEN(widths); i++)
        sk_path_stroke(out, p, widths[i], SK_CAP_ROUND, SK_JOIN_ROUND, 4);
    sk_path_line(p, NAN, 3);
    sk_path_stroke(out, p, 2, SK_CAP_ROUND, SK_JOIN_ROUND, 4);
    sk_image_init(&im, 40, 20);
    sk_fill_path(&im, out, NULL, SK_FILL_NONZERO, SK_RGB(255, 255, 255), NULL);
    CHECK_INT(total(&im), 0);
    sk_image_free(&im);
    sk_path_free(p);
    sk_path_free(out);
}

/* ---- Golden ---- */

/* Every cap with every join on a zigzag; then closed shapes, curves and
 * hairlines. */
static void test_golden_strokes(void)
{
    sk_image im;
    sk_image_init(&im, 300, 220);
    sk_fill_rect(&im, sk_image_bounds(&im), SK_RGB(255, 255, 250), SK_BLEND_SRC, NULL);
    sk_path *zig = sk_path_new();
    sk_path_move(zig, 0, 30);
    sk_path_line(zig, 20, 0);
    sk_path_line(zig, 40, 30);
    sk_path_line(zig, 52, 6);
    sk_path_line(zig, 80, 10);
    for (int cap = 0; cap < 3; cap++)
        for (int join = 0; join < 3; join++) {
            sk_xform place = {1, 0, 0, 1, 14 + 96.0f * (float)join, 12 + 46.0f * (float)cap};
            sk_path *out = sk_path_new();
            sk_path_stroke(out, zig, 9, (sk_cap)cap, (sk_join)join, 4);
            sk_fill_path(&im, out, &place, SK_FILL_NONZERO, SK_RGBA(30, 70, 150, 200), NULL);
            sk_path_clear(out);
            sk_path_stroke(out, zig, 1, SK_CAP_BUTT, SK_JOIN_MITER, 4);
            sk_fill_path(&im, out, &place, SK_FILL_NONZERO, SK_RGB(250, 250, 250), NULL);
            sk_path_free(out);
        }
    sk_path_free(zig);

    sk_path *p = sk_path_new(), *out = sk_path_new();
    sk_path_round_rect(p, 12.5f, 160.5f, 60, 44, 10);
    sk_path_stroke(out, p, 3, SK_CAP_BUTT, SK_JOIN_MITER, 4);
    sk_path_clear(p);
    sk_path_move(p, 90, 200);
    sk_path_line(p, 110, 160);
    sk_path_line(p, 130, 200);
    sk_path_close(p);
    sk_path_stroke(out, p, 6, SK_CAP_BUTT, SK_JOIN_MITER, 10);
    sk_fill_path(&im, out, NULL, SK_FILL_NONZERO, SK_RGB(170, 40, 50), NULL);
    sk_path_clear(out);
    sk_path_clear(p);
    sk_path_move(p, 145, 205);
    sk_path_cubic(p, 160, 120, 200, 260, 215, 165);
    sk_path_quad(p, 240, 140, 230, 205);
    sk_path_stroke(out, p, 7, SK_CAP_ROUND, SK_JOIN_ROUND, 4);
    sk_fill_path(&im, out, NULL, SK_FILL_NONZERO, SK_RGBA(30, 140, 70, 190), NULL);
    sk_path_clear(out);
    for (int i = 0; i < 5; i++) {
        float w = 0.25f + 0.5f * (float)i, x = 250.5f + 9.0f * (float)i;
        sk_path_clear(p);
        sk_path_move(p, x, 160);
        sk_path_line(p, x + 6, 210);
        sk_path_stroke(out, p, w, SK_CAP_BUTT, SK_JOIN_MITER, 4);
    }
    sk_fill_path(&im, out, NULL, SK_FILL_NONZERO, SK_RGB(20, 20, 20), NULL);
    sk_path_free(p);
    sk_path_free(out);
    CHECK_GOLDEN("strokes", &im, 1);
    sk_image_free(&im);
}

const sk_test sk_suite_gfx_stroke[] = {
    T(test_caps),      T(test_joins),     T(test_miter_limit),   T(test_closed),          T(test_curves),
    T(test_degenerate), T(test_reversal), T(test_odd_arguments), T(test_golden_strokes), {0},
};
