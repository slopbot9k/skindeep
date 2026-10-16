/* The paint of decision 0037, a feature at a time, by the pixels it
 * leaves: boxes snapped to pixels, borders by side and rounded,
 * gradients, images, shadows, text, groups (opacity, filters,
 * clip-path, blend modes), overflow clips, z-order, turned layers, the
 * widgets' SVG; and a picture of them together at 1x and 2x. */
#include "../golden.h"
#include "../test.h"
#include "util.h"

#include <string.h>

#define RECT(cls, l, t, w, h)                                                                                         \
    "{\"class\": \"" cls "\", \"rect\": {\"left\": " #l ", \"top\": " #t ", \"width\": " #w ", \"height\": " #h "}}"

#define RED 0xffff0000u
#define GREEN 0xff00ff00u
#define BLUE 0xff0000ffu
#define CLEAR 0u

static unsigned alpha(sk_color c)
{
    return c >> 24;
}

/* Paints r of the skin at scale, and gives the pixel at (x, y) of it. */
static sk_color pixel(const sk_image *im, int x, int y)
{
    return sk_tskin_at(im, x, y);
}

static void test_boxes_snap_to_pixels(void)
{
    sk_tskin s;
    TSKIN(&s, "[{\"class\": \"a\", \"rect\": {\"left\": 10.4, \"top\": 20.6, \"width\": 30, \"height\": 10}}]", NULL,
          ".a { background: #f00 }\n", NULL);
    sk_image im;
    sk_tskin_paint(&s, (sk_frect){0, 0, 60, 40}, 1, &im);
    /* 10.4..40.4 across and 20.6..30.6 down, rounded at each edge. */
    CHECK_COLOR(pixel(&im, 10, 21), RED, 0);
    CHECK_COLOR(pixel(&im, 39, 30), RED, 0);
    CHECK_COLOR(pixel(&im, 9, 21), CLEAR, 0);
    CHECK_COLOR(pixel(&im, 40, 21), CLEAR, 0);
    CHECK_COLOR(pixel(&im, 10, 20), CLEAR, 0);
    CHECK_COLOR(pixel(&im, 10, 31), CLEAR, 0);
    sk_image_free(&im);
    /* At 2x: 20.8..80.8, 41.2..61.2. */
    sk_tskin_paint(&s, (sk_frect){0, 0, 60, 40}, 2, &im);
    CHECK_COLOR(pixel(&im, 21, 41), RED, 0);
    CHECK_COLOR(pixel(&im, 20, 41), CLEAR, 0);
    CHECK_COLOR(pixel(&im, 80, 60), RED, 0);
    CHECK_COLOR(pixel(&im, 81, 60), CLEAR, 0);
    sk_image_free(&im);
    sk_tskin_close(&s);
}

static void test_border_sides_and_radius(void)
{
    sk_tskin s;
    TSKIN(&s, "[" RECT("b", 0, 0, 40, 40) ", " RECT("r", 50, 0, 40, 40) ", " RECT("e", 100, 0, 60, 30) "]", NULL,
          ".b { border: 4px solid; border-color: #f00 #0f0 #00f #ff0 }\n"
          ".r { background: #00f; border-radius: 10px }\n"
          ".e { background: #00f; border-radius: 20px / 10px }\n",
          NULL);
    sk_image im;
    sk_tskin_paint(&s, (sk_frect){0, 0, 170, 40}, 1, &im);
    CHECK_COLOR(pixel(&im, 20, 1), RED, 0);
    CHECK_COLOR(pixel(&im, 38, 20), GREEN, 0);
    CHECK_COLOR(pixel(&im, 20, 38), BLUE, 0);
    CHECK_COLOR(pixel(&im, 1, 20), 0xffffff00u, 0);
    CHECK_COLOR(pixel(&im, 20, 20), CLEAR, 0);
    /* The sides meet on the corners' diagonals. */
    CHECK_COLOR(pixel(&im, 3, 0), RED, 0);
    CHECK_COLOR(pixel(&im, 0, 3), 0xffffff00u, 0);
    /* A rounded corner leaves its corner out, its middle in. */
    CHECK(alpha(pixel(&im, 50, 0)) == 0);
    CHECK(alpha(pixel(&im, 52, 2)) < 64);
    CHECK_COLOR(pixel(&im, 70, 20), BLUE, 0);
    CHECK_COLOR(pixel(&im, 50, 20), BLUE, 0);
    /* An elliptical corner, 20 across and 10 down: the edges run
     * straight past it. */
    CHECK(alpha(pixel(&im, 104, 1)) < 40);
    CHECK_COLOR(pixel(&im, 130, 0), BLUE, 0);
    CHECK_COLOR(pixel(&im, 100, 15), BLUE, 0);
    CHECK(alpha(pixel(&im, 100, 3)) < 40);
    sk_image_free(&im);
    sk_tskin_close(&s);
}

static void test_gradients(void)
{
    sk_tskin s;
    TSKIN(&s,
          "[" RECT("l", 0, 0, 100, 10) ", " RECT("p", 0, 20, 100, 10) ", " RECT("c", 0, 40, 100, 100) ", " RECT(
              "s", 110, 0, 10, 20) "]",
          NULL,
          ".l { background: linear-gradient(to right, #ff0000, #0000ff) }\n"
          ".p { background: linear-gradient(to right, rgba(255, 0, 0, 1), rgba(0, 0, 255, 0)) }\n"
          ".c { background: radial-gradient(circle closest-side, #fff, #000) }\n"
          ".s { background: repeating-linear-gradient(to bottom, #000 0, #000 2px, #fff 2px, #fff 4px) }\n",
          NULL);
    sk_image im;
    sk_tskin_paint(&s, (sk_frect){0, 0, 130, 140}, 1, &im);
    CHECK_COLOR(pixel(&im, 0, 5), 0xfffe0001u, 2);
    CHECK_COLOR(pixel(&im, 99, 5), 0xff0100feu, 2);
    CHECK_COLOR(pixel(&im, 50, 5), 0xff7f0080u, 2);
    /* Premultiplied: red fades out, never through purple. */
    sk_color mid = pixel(&im, 50, 25);
    CHECK_WITHIN(alpha(mid), 128, 2);
    CHECK_COLOR(mid & 0xffffffu, 0xff0000u, 3);
    CHECK_COLOR(pixel(&im, 50, 90), 0xffffffffu, 8);
    CHECK_COLOR(pixel(&im, 50, 41), 0xff000000u, 12);
    CHECK_COLOR(pixel(&im, 1, 41), 0xff000000u, 0);
    /* Stripes two pixels tall, black then white. */
    static const sk_color STRIPE[8] = {0xff000000u, 0xff000000u, 0xffffffffu, 0xffffffffu,
                                       0xff000000u, 0xff000000u, 0xffffffffu, 0xffffffffu};
    for (int y = 0; y < 8; y++)
        CHECK_COLOR(pixel(&im, 115, y), STRIPE[y], 1);
    sk_image_free(&im);
    sk_tskin_close(&s);
}

static void test_background_images(void)
{
    sk_tskin s;
    TSKIN(&s, "[" RECT("n", 0, 0, 40, 20) ", " RECT("x", 0, 30, 40, 20) ", " RECT("k", 50, 0, 40, 20) "]", NULL,
          ".n { background: url(a.png) 5px 2px / 10px 10px no-repeat }\n"
          ".x { background: url(a.png) 5px 2px / 10px 10px repeat-x }\n"
          ".k { background: url(a.png) right 0 top 0 / auto 10px no-repeat }\n",
          NULL);
    s.image = (sk_test_image){4, 4, RED, BLUE, 0};
    sk_image im;
    sk_tskin_paint(&s, (sk_frect){0, 0, 100, 60}, 1, &im);
    /* One tile 10px square at 5, 2. */
    CHECK_COLOR(pixel(&im, 5, 2), RED, 0);
    CHECK_COLOR(pixel(&im, 14, 11), RED, 0);
    CHECK_COLOR(pixel(&im, 15, 2), CLEAR, 0);
    CHECK_COLOR(pixel(&im, 4, 2), CLEAR, 0);
    /* Tiles across the whole width, one row of them. */
    CHECK_COLOR(pixel(&im, 0, 35), RED, 0);
    CHECK_COLOR(pixel(&im, 39, 35), RED, 0);
    CHECK_COLOR(pixel(&im, 20, 31), CLEAR, 0);
    CHECK_COLOR(pixel(&im, 20, 43), CLEAR, 0);
    /* auto from the height and the image's ratio, at the right edge. */
    CHECK_COLOR(pixel(&im, 89, 0), RED, 0);
    CHECK_COLOR(pixel(&im, 80, 9), RED, 0);
    CHECK_COLOR(pixel(&im, 79, 0), CLEAR, 0);
    sk_image_free(&im);
    sk_tskin_close(&s);
}

static void test_border_image(void)
{
    sk_tskin s;
    TSKIN(&s, "[" RECT("i", 0, 0, 40, 40) "]", NULL,
          ".i { border: 10px solid; border-image: url(a.png) 2 fill / 10px stretch }\n", NULL);
    /* Cells of 2px, red and blue: corners red, edges blue, middle red. */
    s.image = (sk_test_image){6, 6, RED, BLUE, 2};
    sk_image im;
    sk_tskin_paint(&s, (sk_frect){0, 0, 40, 40}, 1, &im);
    CHECK_COLOR(pixel(&im, 5, 5), RED, 0);
    CHECK_COLOR(pixel(&im, 20, 5), BLUE, 0);
    CHECK_COLOR(pixel(&im, 35, 5), RED, 0);
    CHECK_COLOR(pixel(&im, 5, 20), BLUE, 0);
    CHECK_COLOR(pixel(&im, 20, 20), RED, 0);
    sk_image_free(&im);
    sk_tskin_close(&s);
}

static void test_box_shadows(void)
{
    sk_tskin s;
    TSKIN(&s,
          "[" RECT("o", 40, 40, 20, 20) ", " RECT("sp", 140, 40, 20, 20) ", " RECT("of", 240, 40, 20, 20) ", " RECT(
              "in", 300, 0, 40, 40) ", " RECT("il", 350, 0, 40, 40) "]",
          NULL,
          ".o { box-shadow: 0 0 10px #000 }\n.sp { box-shadow: 0 0 0 5px #000 }\n"
          ".of { box-shadow: 5px 5px 0 #000 }\n"
          ".in { box-shadow: inset 0 0 0 5px #000 }\n.il { box-shadow: inset 5px 0 0 #000 }\n",
          NULL);
    sk_image im;
    sk_tskin_paint(&s, (sk_frect){0, 0, 400, 100}, 1, &im);
    /* A blur fades out on both sides alike, and nothing is drawn under
     * the box itself. */
    unsigned l = alpha(pixel(&im, 35, 50)), r = alpha(pixel(&im, 64, 50));
    CHECK(l > 20 && l < 200);
    CHECK_WITHIN(l, r, 1);
    CHECK(alpha(pixel(&im, 39, 50)) > l);
    CHECK_INT(alpha(pixel(&im, 50, 50)), 0);
    CHECK_INT(alpha(pixel(&im, 20, 50)), 0);
    /* Spread without blur: 5px all round. */
    CHECK_COLOR(pixel(&im, 136, 50), 0xff000000u, 0);
    CHECK_COLOR(pixel(&im, 134, 50), CLEAR, 0);
    CHECK_COLOR(pixel(&im, 164, 64), 0xff000000u, 0);
    /* Offset: the shadow shows past the box's bottom right only. */
    CHECK_COLOR(pixel(&im, 262, 62), 0xff000000u, 0);
    CHECK_COLOR(pixel(&im, 241, 39), CLEAR, 0);
    /* Inset: inside the padding box. */
    CHECK_COLOR(pixel(&im, 302, 20), 0xff000000u, 0);
    CHECK_COLOR(pixel(&im, 320, 20), CLEAR, 0);
    CHECK_COLOR(pixel(&im, 352, 20), 0xff000000u, 0);
    CHECK_COLOR(pixel(&im, 388, 20), CLEAR, 0);
    sk_image_free(&im);
    sk_tskin_close(&s);
}

/* Pixels of im in [x0, x1) with a channel at least 128 where mask asks. */
static int count(const sk_image *im, int x0, int x1, sk_color mask)
{
    int n = 0;
    for (int y = 0; y < im->h; y++)
        for (int x = x0; x < x1 && x < im->w; x++) {
            sk_color c = pixel(im, x, y);
            if (alpha(c) > 128 && (c & mask) >= (mask & 0x80808080u))
                n++;
        }
    return n;
}

static void test_text_and_shadow(void)
{
    sk_tskin s;
    TSKIN(&s, "[" RECT("t", 0, 0, 100, 40) "]", NULL,
          ".t { font: 20px \"DejaVu Sans Mono\"; color: #f00; text-shadow: 30px 0 0 #00f }\n.t::after { content: \"W\" }\n",
          NULL);
    sk_image im;
    sk_tskin_paint(&s, (sk_frect){0, 0, 100, 40}, 1, &im);
    CHECK(count(&im, 0, 13, 0xff0000u) > 20);
    CHECK(count(&im, 30, 43, 0x0000ffu) > 20);
    CHECK_INT(count(&im, 14, 29, 0xffffffu), 0);
    sk_image_free(&im);
    sk_tskin_close(&s);
}

static void test_groups(void)
{
    sk_tskin s;
    TSKIN(&s,
          "[" RECT("op", 0, 0, 40, 40) ", " RECT("br", 50, 0, 20, 20) ", " RECT("gr", 80, 0, 20, 20) ", " RECT(
              "ds", 110, 0, 20, 20) ", " RECT("cp", 0, 50, 40, 40) ", " RECT("mx", 50, 50, 20, 20) ", " RECT(
              "under", 80, 50, 20, 20) ", " RECT("iso", 80, 50, 20, 20) "]",
          NULL,
          ".op { opacity: 0.5 }\n"
          ".op::before, .op::after { content: \"\"; position: absolute; top: 0; bottom: 0; width: 30px; background: #f00 }\n"
          ".op::before { left: 0 } .op::after { right: 0 }\n"
          ".br { background: #808080; filter: brightness(0.5) }\n"
          ".gr { background: #f00; filter: grayscale(1) }\n"
          ".ds { background: #f00; filter: drop-shadow(10px 0 0 #00f) }\n"
          ".cp { background: #0f0; clip-path: polygon(0 0, 100% 0, 0 100%) }\n"
          ".mx { background: #ff0 }\n"
          ".mx::after { content: \"\"; position: absolute; inset: 0; background: #0ff; mix-blend-mode: multiply }\n"
          ".under { background: #ff0 } .iso { z-index: 1 }\n"
          ".iso::after { content: \"\"; position: absolute; inset: 0; background: #0ff; mix-blend-mode: multiply }\n",
          NULL);
    sk_image im;
    sk_tskin_paint(&s, (sk_frect){0, 0, 150, 100}, 1, &im);
    /* A stacking context isolates a blend: nothing of its own under
     * the cyan, so cyan it stays. */
    CHECK_COLOR(pixel(&im, 90, 60), 0xff00ffffu, 0);
    /* Opacity groups: where the two overlap, still half. */
    CHECK_WITHIN(alpha(pixel(&im, 20, 20)), 128, 1);
    CHECK_WITHIN(alpha(pixel(&im, 5, 20)), 128, 1);
    CHECK_COLOR(pixel(&im, 60, 10), 0xff404040u, 1);
    CHECK_COLOR(pixel(&im, 90, 10), 0xff363636u, 1);
    CHECK_COLOR(pixel(&im, 120, 10), RED, 0);
    CHECK_COLOR(pixel(&im, 135, 10), BLUE, 0);
    CHECK_COLOR(pixel(&im, 5, 55), GREEN, 0);
    CHECK_INT(alpha(pixel(&im, 35, 85)), 0);
    CHECK_COLOR(pixel(&im, 60, 60), GREEN, 0);
    sk_image_free(&im);
    sk_tskin_close(&s);
}

static void test_clips_order_and_turns(void)
{
    sk_tskin s;
    TSKIN(&s, "[" RECT("ov", 0, 0, 40, 40) ", " RECT("z", 50, 0, 40, 40) ", " RECT("rt", 100, 0, 40, 40) "]", NULL,
          ".ov { overflow: hidden; border-radius: 20px }\n"
          ".ov::after { content: \"\"; position: absolute; inset: -10px; background: #f00 }\n"
          ".z::before, .z::after { content: \"\"; position: absolute; inset: 0 }\n"
          ".z::before { background: #f00; z-index: 2 } .z::after { background: #00f; z-index: 1 }\n"
          ".rt::after { content: \"\"; position: absolute; left: 0; top: 15px; width: 40px; height: 10px;\n"
          "  background: #0f0; transform: rotate(90deg) }\n",
          NULL);
    sk_image im;
    sk_tskin_paint(&s, (sk_frect){0, 0, 150, 50}, 1, &im);
    /* The round clip cuts the child's corners. */
    CHECK_INT(alpha(pixel(&im, 1, 1)), 0);
    CHECK_COLOR(pixel(&im, 20, 20), RED, 0);
    CHECK_COLOR(pixel(&im, 20, 1), RED, 0);
    /* z-index over tree order. */
    CHECK_COLOR(pixel(&im, 70, 20), RED, 0);
    /* Turned a quarter: tall, not wide. */
    CHECK_COLOR(pixel(&im, 120, 3), GREEN, 8);
    CHECK_INT(alpha(pixel(&im, 103, 20)), 0);
    sk_image_free(&im);
    sk_tskin_close(&s);
}

static void test_svg_arcs_and_dashes(void)
{
    sk_tskin s;
    TSKIN(&s, NULL,
          "[{\"id\": \"g\", \"type\": \"gauge\", \"bind\": \"context.used_pct\", \"rect\": {\"left\": 0, \"top\": 0, "
          "\"width\": 100, \"height\": 100}}]",
          ".w-gauge .track { stroke: #f00; stroke-dasharray: 25 25 }\n", NULL);
    sk_image im;
    sk_tskin_paint(&s, (sk_frect){0, 0, 100, 100}, 1, &im);
    /* The track, radius 43 about the middle, its dashes a quarter of
     * its length each: -135 to -67.5 degrees, then 0 to 67.5. */
    CHECK_COLOR(pixel(&im, 7, 57), RED, 0);
    CHECK_COLOR(pixel(&im, 71, 12), RED, 0);
    CHECK_INT(alpha(pixel(&im, 28, 12)), 0);
    CHECK_INT(alpha(pixel(&im, 92, 57)), 0);
    /* The fill at 0 of 100 draws nothing; the middle is empty. */
    CHECK_INT(alpha(pixel(&im, 50, 30)), 0);
    sk_image_free(&im);
    sk_tskin_close(&s);
}

/* Values far past any skin's: a shadow blurred and spread across the
 * whole area, text of 3000px, a transform that flattens a box. Painted
 * all the same, in bounded memory. */
static void test_extreme_values(void)
{
    sk_tskin s;
    TSKIN(&s, "[" RECT("x", 10, 10, 50, 50) ", " RECT("y", 100, 10, 50, 50) ", " RECT("z", 200, 10, 50, 50) "]",
          NULL,
          ".x { box-shadow: 0 0 100000px 100000px #f00, inset 0 0 90000px #00f; filter: blur(5000px) }\n"
          ".y { font-size: 3000px; text-shadow: 0 0 9000px #0f0 } .y::after { content: \"WWWW\" }\n"
          ".z { background: #fff; transform: scale(0, 1) rotate(45deg) }\n",
          NULL);
    sk_image im;
    sk_tskin_paint(&s, (sk_frect){0, 0, 300, 100}, 2, &im);
    CHECK_INT(im.w, 600);
    sk_image_free(&im);
    sk_tskin_close(&s);
}

static const char *FEATURES =
    "[" RECT("f1", 10, 10, 120, 60) ", " RECT("f2", 150, 10, 120, 60) ", " RECT("f3", 10, 90, 120, 60) ", " RECT(
        "f4", 150, 90, 120, 60) "]";

static const char *FEATURES_CSS =
    ".f1 { background: linear-gradient(135deg, #ffb070, #602080); border: 3px solid; border-radius: 12px 4px;\n"
    "  border-color: #fff #888 #222 #888; box-shadow: 0 4px 10px rgba(0, 0, 0, 0.7) }\n"
    ".f2 { background: radial-gradient(ellipse at 30% 40%, #8f8, #040 70%); border-radius: 30px / 20px;\n"
    "  box-shadow: inset 0 0 12px #000 }\n"
    ".f3 { background: #1a2030; font: bold 18px \"DejaVu Sans\"; color: #fe8; text-align: center;\n"
    "  text-shadow: 0 0 4px #f80 }\n.f3::after { content: \"Skin 0037\" }\n"
    ".f4 { background: repeating-linear-gradient(45deg, #333 0 4px, #555 4px 8px); opacity: 0.8;\n"
    "  outline: 2px solid #0cf; outline-offset: 2px }\n"
    ".f4::after { content: \"\"; position: absolute; left: 30px; top: 10px; width: 60px; height: 40px;\n"
    "  background: #c33; transform: rotate(-12deg); filter: drop-shadow(3px 3px 2px #000) }\n";

static void test_golden_features(void)
{
    sk_tskin s;
    TSKIN(&s, FEATURES, NULL, FEATURES_CSS, NULL);
    sk_image im;
    sk_tskin_paint(&s, (sk_frect){0, 0, 280, 160}, 1, &im);
    CHECK_GOLDEN("skin_features_1x", &im, 2);
    sk_image_free(&im);
    sk_tskin_paint(&s, (sk_frect){0, 0, 280, 160}, 2, &im);
    CHECK_GOLDEN("skin_features_2x", &im, 2);
    sk_image_free(&im);
    sk_tskin_close(&s);
}

const sk_test sk_suite_skin_paint[] = {
    T(test_boxes_snap_to_pixels),
    T(test_border_sides_and_radius),
    T(test_gradients),
    T(test_background_images),
    T(test_border_image),
    T(test_box_shadows),
    T(test_text_and_shadow),
    T(test_groups),
    T(test_clips_order_and_turns),
    T(test_svg_arcs_and_dashes),
    T(test_extreme_values),
    T(test_golden_features),
    {0},
};
