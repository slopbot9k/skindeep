#include "../../src/ui/internal.h"
#include "../test.h"

/* Every box drawing and block character, U+2500 to U+259F, the eighths
 * and checkers of U+1FB70 to U+1FB97 that CustomGlyphs.ts defines, and
 * the Powerline symbols it does. */
static void test_coverage(void)
{
    for (uint32_t cp = 0x2500; cp <= 0x259F; cp++)
        CHECK(sk_ui_custom_kind_of(cp) != SK_UI_CUSTOM_NONE);
    int boxes = 0, blocks = 0, patterns = 0;
    for (uint32_t cp = 0x2500; cp <= 0x259F; cp++) {
        sk_ui_custom_kind k = sk_ui_custom_kind_of(cp);
        boxes += k == SK_UI_CUSTOM_BOX;
        blocks += k == SK_UI_CUSTOM_BLOCK;
        patterns += k == SK_UI_CUSTOM_PATTERN;
    }
    CHECK_INT(boxes, 128);
    CHECK_INT(blocks, 29);
    CHECK_INT(patterns, 3);
    for (uint32_t cp = 0x1FB70; cp <= 0x1FB8B; cp++)
        CHECK(sk_ui_custom_kind_of(cp) == SK_UI_CUSTOM_BLOCK);
    for (uint32_t cp = 0x1FB95; cp <= 0x1FB97; cp++)
        CHECK(sk_ui_custom_kind_of(cp) == SK_UI_CUSTOM_BLOCK);
    for (uint32_t cp = 0xE0A0; cp <= 0xE0A2; cp++)
        CHECK(sk_ui_custom_kind_of(cp) == SK_UI_CUSTOM_POWERLINE);
    for (uint32_t cp = 0xE0B0; cp <= 0xE0BF; cp++)
        CHECK(sk_ui_custom_kind_of(cp) == SK_UI_CUSTOM_POWERLINE);
    const uint32_t none[] = {'A', ' ', 0x24FF, 0x25A0, 0x1FB6F, 0x1FB8C, 0x1FB94, 0x1FB98, 0xE0A3, 0xE0AF, 0xE0C0};
    for (size_t i = 0; i < SK_LEN(none); i++)
        CHECK(sk_ui_custom_kind_of(none[i]) == SK_UI_CUSTOM_NONE);
}

/* The glyphs of cps drawn side by side into cells w by h, white on
 * transparent, as the view's cells meet. */
static sk_image row_of(const uint32_t *cps, int n, int w, int h, double scale)
{
    sk_image im;
    sk_image_init(&im, n * w, h);
    for (int i = 0; i < n; i++)
        CHECK(sk_termview_custom_glyph(&im, sk_rect_make(i * w, 0, w, h), cps[i], SK_RGB(255, 255, 255), scale));
    return im;
}

static int alpha(const sk_image *im, int x, int y)
{
    return (int)(sk_image_row(im, y)[x] >> 24);
}

/* How many pixels of row y (or column x when col) are fully covered,
 * and how many are covered at all. */
static void count_line(const sk_image *im, int at, bool col, int *full, int *any)
{
    *full = *any = 0;
    int n = col ? im->h : im->w;
    for (int i = 0; i < n; i++) {
        int a = col ? alpha(im, at, i) : alpha(im, i, at);
        *full += a == 255;
        *any += a > 0;
    }
}

/* At 1x a normal line is one whole pixel through the middle of the
 * cell, rounded to the half pixel: row 8 and column 4 of 8 by 16; bold
 * is three. Lines meet across cells with no gap. */
static void test_lines_1x(void)
{
    const uint32_t row[] = {0x2500, 0x252C, 0x2500, 0x253C, 0x2500};
    sk_image im = row_of(row, 5, 8, 16, 1);
    int full, any;
    count_line(&im, 8, false, &full, &any);
    CHECK_INT(full, 40);
    for (int y = 0; y < 16; y++)
        if (y != 8)
            for (int x = 0; x < 40; x++)
                if (x != 12 && x != 28)
                    CHECK_INT(alpha(&im, x, y), 0);
    /* ┬ hangs from the line to the bottom, ┼ spans the cell. */
    count_line(&im, 12, true, &full, &any);
    CHECK_INT(full, 8);
    CHECK_INT(any, 8);
    count_line(&im, 28, true, &full, &any);
    CHECK_INT(full, 16);
    sk_image_free(&im);

    const uint32_t bold[] = {0x2501, 0x2501};
    im = row_of(bold, 2, 8, 16, 1);
    for (int y = 0; y < 16; y++) {
        count_line(&im, y, false, &full, &any);
        CHECK_INT(full, y >= 7 && y <= 9 ? 16 : 0);
        CHECK_INT(any, full);
    }
    sk_image_free(&im);

    /* │ stacked: column 4, unbroken. */
    sk_image col;
    sk_image_init(&col, 8, 48);
    for (int i = 0; i < 3; i++)
        sk_termview_custom_glyph(&col, sk_rect_make(0, i * 16, 8, 16), 0x2502, SK_RGB(255, 255, 255), 1);
    count_line(&col, 4, true, &full, &any);
    CHECK_INT(full, 48);
    count_line(&col, 3, true, &full, &any);
    CHECK_INT(any, 0);
    sk_image_free(&col);
}

/* ═ and ║: two lines, at .5 -/+ yp and xp of the cell rounded to the
 * half pixel, yp being .15 of the width in the height's terms. */
static void test_double_lines(void)
{
    const uint32_t h[] = {0x2550};
    sk_image im = row_of(h, 1, 8, 16, 1);
    for (int y = 0; y < 16; y++) {
        int full, any;
        count_line(&im, y, false, &full, &any);
        CHECK_INT(full, y == 6 || y == 9 ? 8 : 0);
    }
    sk_image_free(&im);
    const uint32_t v[] = {0x2551};
    im = row_of(v, 1, 8, 16, 1);
    for (int x = 0; x < 8; x++) {
        int full, any;
        count_line(&im, x, true, &full, &any);
        CHECK_INT(full, x == 2 || x == 5 ? 16 : 0);
    }
    sk_image_free(&im);
}

/* At 2x a line is two pixels about a half pixel: half, whole, half. */
static void test_lines_2x(void)
{
    const uint32_t row[] = {0x2500, 0x2500};
    sk_image im = row_of(row, 2, 16, 32, 2);
    for (int x = 0; x < 32; x++) {
        CHECK_INT(alpha(&im, x, 15), 128);
        CHECK_INT(alpha(&im, x, 16), 255);
        CHECK_INT(alpha(&im, x, 17), 128);
        CHECK_INT(alpha(&im, x, 14), 0);
        CHECK_INT(alpha(&im, x, 18), 0);
    }
    sk_image_free(&im);
}

/* Blocks fill eighths of the cell; shades are patterns of device
 * pixels. */
static void test_blocks_and_shades(void)
{
    const uint32_t cps[] = {0x2588, 0x2580, 0x2590, 0x2581, 0x259A, 0x2591, 0x2592, 0x2593};
    sk_image im = row_of(cps, 8, 8, 16, 1);
    int n[8] = {0}, half_top = 0, right = 0;
    for (int c = 0; c < 8; c++)
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 8; x++) {
                int a = alpha(&im, c * 8 + x, y);
                CHECK(a == 0 || a == 255);
                n[c] += a == 255;
                if (c == 1 && a)
                    half_top += y < 8;
                if (c == 2 && a)
                    right += x >= 4;
            }
    CHECK_INT(n[0], 128);
    CHECK_INT(n[1], 64);
    CHECK_INT(half_top, 64);
    CHECK_INT(n[2], 64);
    CHECK_INT(right, 64);
    CHECK_INT(n[3], 16);
    CHECK_INT(n[4], 64);
    /* Light: 2 of 16; medium: 2 of 8; dark: 6 of 8. */
    CHECK_INT(n[5], 16);
    CHECK_INT(n[6], 32);
    CHECK_INT(n[7], 96);
    CHECK_INT(alpha(&im, 40, 0), 255);
    CHECK_INT(alpha(&im, 41, 0), 0);
    CHECK_INT(alpha(&im, 42, 2), 255);
    sk_image_free(&im);
}

/* A Powerline arrow stays in its cell: it is clipped to it. */
static void test_powerline(void)
{
    sk_image im;
    sk_image_init(&im, 24, 16);
    CHECK(sk_termview_custom_glyph(&im, sk_rect_make(8, 0, 8, 16), 0xE0B0, SK_RGB(255, 255, 255), 1));
    int inside = 0, outside = 0;
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 24; x++) {
            if (x >= 8 && x < 16)
                inside += alpha(&im, x, y) > 0;
            else
                outside += alpha(&im, x, y) > 0;
        }
    CHECK(inside > 40);
    CHECK_INT(outside, 0);
    /* Its point is at the middle of the right side. */
    CHECK_INT(alpha(&im, 8, 8), 255);
    CHECK_INT(alpha(&im, 15, 0), 0);
    CHECK_INT(alpha(&im, 15, 15), 0);
    sk_image_free(&im);
}

static void test_not_custom(void)
{
    sk_image im;
    sk_image_init(&im, 8, 16);
    CHECK(!sk_termview_custom_glyph(&im, sk_rect_make(0, 0, 8, 16), 'A', SK_RGB(255, 255, 255), 1));
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 8; x++)
            CHECK_INT(alpha(&im, x, y), 0);
    sk_image_free(&im);
}

const sk_test sk_suite_customglyph[] = {
    T(test_coverage),
    T(test_lines_1x),
    T(test_double_lines),
    T(test_lines_2x),
    T(test_blocks_and_shades),
    T(test_powerline),
    T(test_not_custom),
    {0},
};
