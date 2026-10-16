/* What the skin suites share: a throwaway skin (tests/skinc/util.h)
 * compiled, laid out in a skin area of 1000 x 678, and painted, its
 * images drawn by the test. */
#ifndef SK_TESTS_SKIN_UTIL_H
#define SK_TESTS_SKIN_UTIL_H

#include "../../skinc/render.h"
#include "../skinc/util.h"

/* What a test's images are: each source a w by h image in colour, or a
 * checkerboard of two colours in cells cell px wide when cell is not 0. */
typedef struct {
    int w, h;
    sk_color a, b;
    int cell;
} sk_test_image;

typedef struct {
    sk_front f;
    sk_diags d;
    sk_render_tree t;
    sk_render_fonts fonts;
    sk_lfonts lfonts;
    sk_layout l;
    bool ok;
    sk_test_image image;
    sk_image pixels;
    sk_pimages images;
    sk_glyph_cache *glyphs;
} sk_tskin;

/* Compiles the skin of panels, widgets and css, every variable as st
 * says (NULL: 0), and lays it out. A skin that does not compile fails
 * the test with its messages; s->ok says so. */
#define TSKIN(s, panels, widgets, css, st) sk_tskin_open(__FILE__, __LINE__, (s), (panels), (widgets), (css), (st))
void sk_tskin_open(const char *file, int line, sk_tskin *s, const char *panels, const char *widgets, const char *css,
                   const sk_render_state *st);
void sk_tskin_close(sk_tskin *s);

/* Boxes: the first panel of a class, its ::before or ::after, a
 * widget's part by role (nth from 0). Never NULL: a missing box is a
 * zeroed one and fails the test. */
const sk_lbox *sk_tskin_panel(sk_tskin *s, const char *klass);
const sk_lbox *sk_tskin_pseudo(sk_tskin *s, const char *klass, bool after);
const sk_lbox *sk_tskin_part(sk_tskin *s, const char *widget, uint8_t role, int nth);
/* The style an element's box was laid out with. */
const sk_css_style *sk_tskin_style(const sk_tskin *s, const sk_lbox *b);

/* The skin area's CSS rectangle r painted at scale into a new image
 * over transparent, its images s->image. */
void sk_tskin_paint(sk_tskin *s, sk_frect r, float scale, sk_image *out);
/* A pixel of an image, unpremultiplied, 0xAARRGGBB. */
sk_color sk_tskin_at(const sk_image *im, int x, int y);

/* A rectangle with fields x, y, w, h near the values given. */
#define CHECK_RECT(r, X, Y, W, H) sk_check_rect(__FILE__, __LINE__, #r, (r), (X), (Y), (W), (H))
void sk_check_rect(const char *file, int line, const char *what, sk_frect r, float x, float y, float w, float h);
/* A float within 0.01. */
#define CHECK_NEAR(a, b) sk_check_near(__FILE__, __LINE__, #a, (double)(a), (double)(b), 0.01)
#define CHECK_WITHIN(a, b, eps) sk_check_near(__FILE__, __LINE__, #a, (double)(a), (double)(b), (eps))
void sk_check_near(const char *file, int line, const char *what, double a, double b, double eps);
/* A colour within tolerance per channel. */
#define CHECK_COLOR(c, want, tol) sk_check_color(__FILE__, __LINE__, #c, (c), (want), (tol))
void sk_check_color(const char *file, int line, const char *what, sk_color c, sk_color want, int tol);

#endif
