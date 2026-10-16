/* The visualizer's Canvas2D (canvas.h): its geometry read back as
 * pixels, and a scene of everything it draws against a golden image. */
#include "../../src/viz/canvas.h"
#include "../golden.h"
#include "../test.h"

#include <math.h>

static int alpha(const sk_image *im, int x, int y)
{
    return (int)(sk_image_row(im, y)[x] >> 24);
}

static int green(const sk_image *im, int x, int y)
{
    return (int)((sk_image_row(im, y)[x] >> 8) & 255);
}

/* A rectangle's edge pixels are covered by the area inside, at 1x and
 * at 2x, where the same points cover twice the pixels. */
static void test_rects(void)
{
    sk_image im;
    sk_image_init(&im, 20, 20);
    sk_vc vc;
    sk_vc_init(&vc, &im, 1);
    sk_vc_paint white = sk_vc_solid(SK_RGB(255, 255, 255));
    sk_vc_fill_rect(&vc, 2.5, 2, 3, 4.25, &white);
    CHECK_INT(alpha(&im, 2, 3), 128);
    CHECK_INT(alpha(&im, 3, 3), 255);
    CHECK_INT(alpha(&im, 5, 3), 128);
    CHECK_INT(alpha(&im, 3, 6), 64);
    CHECK_INT(alpha(&im, 6, 3), 0);
    sk_vc_square(&vc, 10, 10, 0.5, 0.5, SK_RGB(255, 255, 255), 1);
    CHECK_INT(alpha(&im, 10, 10), 64);
    /* globalAlpha and lighter. */
    sk_vc_clear(&vc, SK_RGB(0, 0, 0));
    sk_vc_op(&vc, SK_BLEND_ADD);
    sk_vc_alpha(&vc, 0.5);
    sk_vc_paint g = sk_vc_solid(SK_RGB(0, 200, 0));
    sk_vc_fill_rect(&vc, 0, 0, 4, 4, &g);
    sk_vc_fill_rect(&vc, 0, 0, 4, 4, &g);
    CHECK_INT(green(&im, 1, 1), 200);
    sk_vc_fill_rect(&vc, 0, 0, 4, 4, &g);
    CHECK_INT(green(&im, 1, 1), 255);
    sk_image_free(&im);

    sk_image_init(&im, 40, 40);
    sk_vc_target(&vc, &im, 2);
    sk_vc_fill_rect(&vc, 2.25, 2, 3, 4, &white);
    CHECK_INT(alpha(&im, 4, 5), 128);
    CHECK_INT(alpha(&im, 5, 5), 255);
    CHECK_INT(alpha(&im, 10, 5), 128);
    CHECK_INT(alpha(&im, 11, 5), 0);
    sk_vc_free(&vc);
    sk_image_free(&im);
}

/* A circle of radius 10 covers about 100 pi pixels; a dashed line half
 * its length. */
static void test_paths(void)
{
    sk_image im;
    sk_image_init(&im, 64, 64);
    sk_vc vc;
    sk_vc_init(&vc, &im, 1);
    sk_vc_paint white = sk_vc_solid(SK_RGB(255, 255, 255));
    sk_vc_begin(&vc);
    sk_vc_arc(&vc, 32, 32, 10, 0, SK_TAU, false);
    sk_vc_fill(&vc, &white, SK_FILL_NONZERO);
    double sum = 0;
    for (int i = 0; i < 64 * 64; i++)
        sum += (im.px[i] >> 24) / 255.0;
    CHECK(fabs(sum - M_PI * 100) < 1);
    sk_vc_clear(&vc, 0);
    double dash[2] = {4, 4};
    sk_vc_dash(&vc, dash, 2);
    sk_vc_line_width(&vc, 2);
    sk_vc_begin(&vc);
    sk_vc_move(&vc, 4, 10);
    sk_vc_line(&vc, 44, 10);
    sk_vc_stroke(&vc, &white);
    sum = 0;
    for (int i = 0; i < 64 * 64; i++)
        sum += (im.px[i] >> 24) / 255.0;
    CHECK(fabs(sum - 40) < 0.5);
    CHECK(alpha(&im, 5, 10) == 255 && alpha(&im, 9, 10) == 0 && alpha(&im, 13, 10) == 255);
    /* A clip to a circle keeps a fill inside it. */
    sk_vc_clear(&vc, 0);
    sk_vc_dash(&vc, NULL, 0);
    sk_vc_save(&vc);
    sk_vc_begin(&vc);
    sk_vc_arc(&vc, 32, 32, 8, 0, SK_TAU, false);
    sk_vc_clip(&vc, SK_FILL_NONZERO);
    sk_vc_fill_rect(&vc, 0, 0, 64, 64, &white);
    sk_vc_restore(&vc);
    CHECK(alpha(&im, 32, 32) == 255 && alpha(&im, 32, 20) == 0 && alpha(&im, 2, 2) == 0);
    sk_vc_fill_rect(&vc, 0, 0, 4, 4, &white);
    CHECK_INT(alpha(&im, 2, 2), 255);
    sk_vc_free(&vc);
    sk_image_free(&im);
}

/* Every kind of thing the modes draw, at 2x. */
static void test_scene(void)
{
    sk_image im;
    sk_image_init(&im, 400, 260);
    sk_vc vc;
    sk_vc_init(&vc, &im, 2);
    sk_vc_clear(&vc, SK_RGB(4, 6, 12));
    sk_vc_paint g = sk_vc_linear(0, 0, 160, 100);
    sk_vc_add_stop(&g, 0, SK_RGB(217, 168, 92));
    sk_vc_add_stop(&g, 1, SK_RGB(185, 130, 62));
    sk_vc_fill_rect(&vc, 4, 4, 70, 70, &g);
    sk_vc_paint stone = sk_vc_radial(30 - 6, 30 - 6, 2, 30, 30, 16);
    sk_vc_add_stop(&stone, 0, SK_RGB(0x66, 0x66, 0x66));
    sk_vc_add_stop(&stone, 1, SK_RGB(8, 8, 8));
    sk_vc_begin(&vc);
    sk_vc_arc(&vc, 30, 30, 16, 0, SK_TAU, false);
    sk_vc_fill(&vc, &stone, SK_FILL_NONZERO);
    sk_vc_paint glow = sk_vc_radial(110, 40, 0, 110, 40, 30);
    sk_vc_add_stop(&glow, 0, sk_vc_hsla(40, 100, 85, 1));
    sk_vc_add_stop(&glow, 0.35, sk_vc_hsla(40, 100, 60, 0.35));
    sk_vc_add_stop(&glow, 1, sk_vc_hsla(40, 100, 50, 0));
    sk_vc_op(&vc, SK_BLEND_ADD);
    sk_vc_fill_rect(&vc, 80, 10, 60, 60, &glow);
    sk_vc_op(&vc, SK_BLEND_SRC_OVER);
    sk_vc_save(&vc);
    sk_vc_translate(&vc, 175, 100);
    sk_vc_rotate(&vc, 0.4);
    sk_vc_line_width(&vc, 3);
    sk_vc_cap(&vc, SK_CAP_ROUND);
    sk_vc_begin(&vc);
    sk_vc_ellipse(&vc, 0, 0, 20, 8, 0, 0, SK_TAU * 0.8, false);
    sk_vc_paint p = sk_vc_solid(sk_vc_rgba(120, 200, 255, 0.8));
    sk_vc_stroke(&vc, &p);
    sk_vc_restore(&vc);
    sk_vc_begin(&vc);
    sk_vc_move(&vc, 150, 30);
    sk_vc_quad(&vc, 170, 10, 190, 30);
    sk_vc_cubic(&vc, 200, 50, 160, 70, 150, 30);
    p = sk_vc_solid(SK_RGB(150, 40, 30));
    sk_vc_fill(&vc, &p, SK_FILL_NONZERO);
    double dash[2] = {5, 4};
    sk_vc_dash(&vc, dash, 2);
    sk_vc_line_width(&vc, 1);
    sk_vc_begin(&vc);
    sk_vc_move(&vc, 4, 90);
    sk_vc_line(&vc, 150, 90);
    p = sk_vc_solid(SK_RGB(125, 137, 161));
    sk_vc_stroke(&vc, &p);
    sk_vc_dash(&vc, NULL, 0);
    sk_vc_line_width(&vc, 2);
    sk_vc_begin(&vc);
    for (int i = 0; i <= 60; i++) {
        double x = 4 + i * 2.4, y = 75 - 8 * sk_vm_sin(i * 0.3);
        if (i)
            sk_vc_line(&vc, x, y);
        else
            sk_vc_move(&vc, x, y);
    }
    sk_color c = sk_vc_hsl(140, 100, 60);
    p = sk_vc_solid(c);
    sk_vc_stroke_glow(&vc, &p, c, 12);
    sk_vc_font(&vc, 10);
    sk_vc_align_text(&vc, SK_VC_LEFT, SK_VC_MIDDLE);
    sk_vc_text(&vc, "2p\xc2\xb2 \xc2\xb3P\xe2\x82\x80  538.0 nm", 4, 100, SK_RGB(232, 238, 252));
    sk_vc_align_text(&vc, SK_VC_RIGHT, SK_VC_ALPHABETIC);
    sk_vc_alpha(&vc, 0.6);
    sk_vc_text(&vc, "right", 196, 124, SK_RGB(255, 207, 122));
    sk_vc_metrics m;
    sk_vc_measure(&vc, "Hg", &m);
    CHECK(m.width > 10 && m.width < 14 && m.ascent > 5 && m.ascent < 9 && m.descent > 1 && m.descent < 4);
    CHECK_GOLDEN("viz_canvas", &im, 2);
    sk_vc_free(&vc);
    sk_image_free(&im);
}

const sk_test sk_suite_viz_canvas[] = {T(test_rects), T(test_paths), T(test_scene), {0}};
