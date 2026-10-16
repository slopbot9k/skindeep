#include "../../src/text/text.h"
#include "../golden.h"
#include "../test.h"

#include <math.h>

#define INK SK_RGB(24, 24, 32)
#define PAPER SK_RGB(255, 255, 255)

static const char pangram[] = "The quick brown fox jumps over the lazy dog.";

static float advance_of(sk_embedded_font which, uint32_t cp, float px)
{
    const sk_font *f = sk_font_embedded(which);
    return sk_font_advance(f, sk_font_glyph(f, cp), px);
}

static float measure(sk_fontset *set, int style, float px, const char *s)
{
    return sk_measure_text(set, style, px, s, strlen(s));
}

static void paper(sk_image *im, int w, int h)
{
    sk_image_init(im, w, h);
    sk_fill_rect(im, sk_image_bounds(im), PAPER, SK_BLEND_SRC, NULL);
}

/* One line at the pen, its baseline below *top by the face's ascent;
 * *top moves down a line. */
static void line(sk_image *im, sk_glyph_cache *c, sk_fontset *set, int style, float px, float *top, const char *s)
{
    sk_font_vmetrics v;
    sk_font_vmetrics_at(sk_fontset_primary(set, style), px, &v);
    float baseline = *top + v.ascent;
    sk_draw_text(im, c, set, style, px, 8, baseline, s, strlen(s), INK, NULL);
    *top += ceilf(v.ascent + v.descent + v.line_gap);
}

static void test_measure(void)
{
    sk_fontset *mono = sk_fontset_new(NULL, NULL, true), *sans = sk_fontset_new(NULL, NULL, false);
    float m = advance_of(SK_FONT_MONO, 'm', 13);
    CHECK(measure(mono, 0, 13, "hello") == 5 * m);
    CHECK(measure(mono, SK_STYLE_BOLD, 13, "hello") == 5 * m);
    CHECK(measure(mono, 0, 13, "") == 0);
    CHECK(sk_measure_text(mono, 0, 13, NULL, 0) == 0);
    CHECK(measure(mono, 0, 0, "hello") == 0);
    CHECK(measure(mono, 0, NAN, "hello") == 0);
    /* Kerned within the face. */
    float a = advance_of(SK_FONT_SANS, 'A', 40), v = advance_of(SK_FONT_SANS, 'V', 40);
    float av = measure(sans, 0, 40, "AV");
    CHECK(av < a + v - 1);
    const sk_font *f = sk_font_embedded(SK_FONT_SANS);
    CHECK(av == a + sk_font_kern(f, sk_font_glyph(f, 'A'), sk_font_glyph(f, 'V'), 40) + v);
    /* Monospaced faces do not kern. */
    CHECK(measure(mono, 0, 40, "AV") == 2 * advance_of(SK_FONT_MONO, 'A', 40));
    sk_fontset_free(mono);
    sk_fontset_free(sans);
}

static void test_controls_and_bad_bytes(void)
{
    sk_fontset *sans = sk_fontset_new(NULL, NULL, false);
    float ab = measure(sans, 0, 13, "ab");
    CHECK(measure(sans, 0, 13, "a\tb\n\r\x01\x7f") == ab);
    CHECK(measure(sans, 0, 13, "a\xC2\x85\xC2\x9F" "b") == ab); /* C1 */
    float bad = measure(sans, 0, 13, "\xEF\xBF\xBD");
    CHECK(bad > 0);
    CHECK(measure(sans, 0, 13, "\xff") == bad);
    CHECK(measure(sans, 0, 13, "\xE2\x82") == bad); /* cut short */
    CHECK(measure(sans, 0, 13, "\xED\xA0\x80") == 3 * bad);
    /* A character nothing has draws the missing-glyph box. */
    CHECK(measure(sans, 0, 13, "\xF4\x8F\xBF\xBD") == advance_of(SK_FONT_SANS, 0x10FFFD, 13));
    CHECK(measure(sans, 0, 13, "\xF4\x8F\xBF\xBD") > 0);
    sk_fontset_free(sans);
}

static bool images_equal(const sk_image *a, const sk_image *b, int dx)
{
    for (int y = 0; y < a->h; y++)
        for (int x = SK_MAX(0, -dx); x < a->w && x + dx < b->w; x++)
            if (sk_image_row(a, y)[x] != sk_image_row(b, y)[x + dx])
                return false;
    return true;
}

static void test_draw_positions(void)
{
    sk_fontset *sans = sk_fontset_new(NULL, NULL, false);
    sk_glyph_cache *c = sk_glyph_cache_new(1 << 20);
    sk_image a, b, q;
    paper(&a, 200, 30);
    paper(&b, 200, 30);
    paper(&q, 200, 30);
    const char *s = "Hello, world";
    float w = sk_draw_text(&a, c, sans, 0, 15, 10, 20, s, strlen(s), INK, NULL);
    CHECK(w == measure(sans, 0, 15, s));
    /* A whole pixel to the right is the same picture moved. */
    CHECK(sk_draw_text(&b, c, sans, 0, 15, 11, 20, s, strlen(s), INK, NULL) == w);
    CHECK(images_equal(&a, &b, 1));
    /* A quarter pixel is not; less than an eighth rounds to none. */
    CHECK(sk_draw_text(&q, c, sans, 0, 15, 10.25f, 20, s, strlen(s), INK, NULL) == w);
    CHECK(!images_equal(&a, &q, 0));
    sk_image h1, h2;
    paper(&h1, 30, 30);
    paper(&h2, 30, 30);
    sk_draw_text(&h1, c, sans, 0, 15, 10, 20, "H", 1, INK, NULL);
    sk_draw_text(&h2, c, sans, 0, 15, 10.1f, 20.3f, "H", 1, INK, NULL);
    CHECK(images_equal(&h1, &h2, 0));
    sk_image_free(&h1);
    sk_image_free(&h2);
    /* Only inside the clip. */
    sk_fill_rect(&q, sk_image_bounds(&q), PAPER, SK_BLEND_SRC, NULL);
    sk_rect clip = sk_rect_make(40, 0, 30, 30);
    sk_draw_text(&q, c, sans, 0, 15, 10, 20, s, strlen(s), INK, &clip);
    bool outside = false, inside = false;
    for (int y = 0; y < q.h; y++)
        for (int x = 0; x < q.w; x++) {
            bool inked = sk_image_row(&q, y)[x] != sk_premul(PAPER);
            if (x >= 40 && x < 70)
                inside |= inked;
            else
                outside |= inked;
            if (x >= 40 && x < 70 && sk_image_row(&q, y)[x] != sk_image_row(&a, y)[x])
                outside = true;
        }
    CHECK(inside && !outside);
    /* Far away or off the image, it is measured and nothing is drawn. */
    sk_fill_rect(&q, sk_image_bounds(&q), PAPER, SK_BLEND_SRC, NULL);
    CHECK(sk_draw_text(&q, c, sans, 0, 15, -1e30f, 20, s, strlen(s), INK, NULL) == w);
    CHECK(sk_draw_text(&q, c, sans, 0, 15, 10, 1e30f, s, strlen(s), INK, NULL) == w);
    CHECK(sk_draw_text(&q, c, sans, 0, 15, 10, -500, s, strlen(s), INK, NULL) == w);
    CHECK(sk_draw_text(&q, c, sans, 0, 15, INFINITY, 20, s, strlen(s), INK, NULL) == w);
    sk_image blank;
    paper(&blank, 200, 30);
    CHECK(images_equal(&q, &blank, 0));
    sk_image_free(&blank);
    sk_image_free(&a);
    sk_image_free(&b);
    sk_image_free(&q);
    sk_glyph_cache_free(c);
    sk_fontset_free(sans);
}

static void test_golden_pangram(void)
{
    sk_fontset *mono = sk_fontset_new(NULL, NULL, true), *sans = sk_fontset_new(NULL, NULL, false);
    sk_glyph_cache *c = sk_glyph_cache_new(1 << 20);
    sk_image im;
    paper(&im, 720, 124);
    float top = 6;
    line(&im, c, mono, 0, 13, &top, "The quick brown fox jumps over the lazy dog. 0123456789 {}[]()<>|/\\~");
    line(&im, c, sans, 0, 13, &top, "The quick brown fox jumps over the lazy dog. 0123456789 {}[]()<>|/\\~");
    line(&im, c, mono, 0, 26, &top, pangram);
    line(&im, c, sans, 0, 26, &top, pangram);
    /* Beyond ASCII, a bad byte (U+FFFD) and a character no face has. */
    line(&im, c, mono, 0, 13, &top,
         "\xD0\x96\xD0\xB6 \xCE\xA9 \xE2\x89\xA0 \xE2\x86\x92 \xE2\x9C\x93 \xE2\x94\x80\xE2\x94\xBC "
         "bad:\xff missing:\xF4\x8F\xBF\xBD tab:\there");
    CHECK_GOLDEN("text_pangram", &im, 2);
    sk_image_free(&im);
    sk_glyph_cache_free(c);
    sk_fontset_free(mono);
    sk_fontset_free(sans);
}

static void test_golden_mono_styles(void)
{
    sk_fontset *mono = sk_fontset_new(NULL, NULL, true);
    sk_glyph_cache *c = sk_glyph_cache_new(1 << 20);
    sk_image im;
    paper(&im, 520, 88);
    float top = 4;
    line(&im, c, mono, SK_STYLE_REGULAR, 16, &top, "Book         sphinx of black quartz 0O1lI");
    line(&im, c, mono, SK_STYLE_BOLD, 16, &top, "Bold         sphinx of black quartz 0O1lI");
    line(&im, c, mono, SK_STYLE_ITALIC, 16, &top, "Oblique      sphinx of black quartz 0O1lI");
    line(&im, c, mono, SK_STYLE_BOLD | SK_STYLE_ITALIC, 16, &top, "Bold Oblique sphinx of black quartz 0O1lI");
    CHECK_GOLDEN("text_mono_styles", &im, 2);
    sk_image_free(&im);
    sk_glyph_cache_free(c);
    sk_fontset_free(mono);
}

/* VT323 has one face and DejaVu Sans no oblique: what they lack is
 * faked, bold by spreading the mask, italic by slanting the outline. */
static void test_golden_synthetic(void)
{
    const char *dirs[] = {"skins/phosphor/fonts", NULL};
    const char *vt[] = {"VT323", NULL};
    sk_fonts *fs = sk_fonts_scan(dirs);
    sk_fontset *set = sk_fontset_new(fs, vt, true), *sans = sk_fontset_new(NULL, NULL, false);
    sk_glyph_cache *c = sk_glyph_cache_new(1 << 20);
    sk_image im;
    paper(&im, 400, 136);
    float top = 4;
    line(&im, c, set, SK_STYLE_REGULAR, 22, &top, "VT323 regular Hamburgefonstiv");
    line(&im, c, set, SK_STYLE_BOLD, 22, &top, "VT323 bold Hamburgefonstiv");
    line(&im, c, set, SK_STYLE_ITALIC, 22, &top, "VT323 italic Hamburgefonstiv");
    line(&im, c, set, SK_STYLE_BOLD | SK_STYLE_ITALIC, 22, &top, "VT323 bold italic Hamburgefonstiv");
    line(&im, c, sans, SK_STYLE_ITALIC, 16, &top, "DejaVu Sans italic Hamburgefonstiv");
    line(&im, c, sans, SK_STYLE_BOLD | SK_STYLE_ITALIC, 16, &top, "DejaVu Sans bold italic Hamburgefonstiv");
    CHECK_GOLDEN("text_synthetic", &im, 2);
    sk_image_free(&im);
    sk_glyph_cache_free(c);
    sk_fontset_free(set);
    sk_fontset_free(sans);
    sk_fonts_free(fs);
}

/* "AV To" kerned, over the same letters set one by one, unkerned. */
static void test_golden_kerning(void)
{
    sk_fontset *sans = sk_fontset_new(NULL, NULL, false);
    sk_glyph_cache *c = sk_glyph_cache_new(1 << 20);
    sk_image im;
    paper(&im, 220, 120);
    const char *s = "AV To";
    float kerned = sk_draw_text(&im, c, sans, 0, 48, 8, 48, s, strlen(s), INK, NULL);
    float x = 8;
    for (const char *p = s; *p; p++)
        x += sk_draw_text(&im, c, sans, 0, 48, x, 106, p, 1, INK, NULL);
    CHECK(kerned < x - 8 - 4);
    CHECK_GOLDEN("text_kerning", &im, 2);
    sk_image_free(&im);
    sk_glyph_cache_free(c);
    sk_fontset_free(sans);
}

const sk_test sk_suite_textrun[] = {
    T(test_measure),
    T(test_controls_and_bad_bytes),
    T(test_draw_positions),
    T(test_golden_pangram),
    T(test_golden_mono_styles),
    T(test_golden_synthetic),
    T(test_golden_kerning),
    {0},
};
