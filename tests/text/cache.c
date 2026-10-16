#include "../../src/text/internal.h"
#include "../test.h"

#include <math.h>
#include <stdlib.h>

static sk_glyph_ref ref_of(sk_embedded_font which, uint32_t cp, int synth)
{
    const sk_font *f = sk_font_embedded(which);
    sk_glyph_ref r = {f, sk_font_glyph(f, cp), synth};
    return r;
}

/* A copy of a glyph that outlives the cache's next call. */
typedef struct {
    int w, h, left, top;
    uint8_t *a;
} saved;

static saved save(const sk_glyph *g)
{
    saved s = {g->mask.w, g->mask.h, g->left, g->top, NULL};
    s.a = sk_xmalloc((size_t)SK_MAX(1, s.w * s.h));
    for (int y = 0; y < s.h; y++)
        memcpy(s.a + y * s.w, g->mask.a + (size_t)y * (size_t)g->mask.stride, (size_t)s.w);
    return s;
}

static bool same(const saved *s, const sk_glyph *g)
{
    if (s->w != g->mask.w || s->h != g->mask.h || s->left != g->left || s->top != g->top)
        return false;
    for (int y = 0; y < s->h; y++)
        if (memcmp(s->a + y * s->w, g->mask.a + (size_t)y * (size_t)g->mask.stride, (size_t)s->w) != 0)
            return false;
    return true;
}

static int ink(const sk_glyph *g)
{
    int n = 0;
    for (int i = 0; i < g->mask.w * g->mask.h; i++)
        n += g->mask.a[i];
    return n;
}

/* The left edge of the ink in row y of the mask, relative to the pen. */
static int left_ink(const sk_glyph *g, int row)
{
    for (int x = 0; x < g->mask.w; x++)
        if (g->mask.a[row * g->mask.stride + x] > 127)
            return g->left + x;
    return INT32_MAX;
}

static void test_glyph_box(void)
{
    sk_glyph_cache *c = sk_glyph_cache_new(1 << 20);
    const sk_font *f = sk_font_embedded(SK_FONT_SANS);
    sk_font_vmetrics v;
    sk_font_vmetrics_at(f, 20, &v);
    const sk_glyph *g = sk_glyph_get(c, ref_of(SK_FONT_SANS, 'H', 0), 20, 0);
    /* An H sits on the baseline and stands a cap height above it. */
    CHECK(g->mask.w > 8 && g->mask.w < 16);
    CHECK_INT(g->top + g->mask.h, 0);
    CHECK(g->top <= -14 && g->top >= -16);
    CHECK(g->left >= 0 && g->left <= 3);
    CHECK(-g->top < v.ascent);
    CHECK_INT(g->mask.stride, g->mask.w);
    /* A g descends below it. */
    g = sk_glyph_get(c, ref_of(SK_FONT_SANS, 'g', 0), 20, 0);
    CHECK(g->top + g->mask.h > 2);
    /* Spaces have no ink and no mask. */
    g = sk_glyph_get(c, ref_of(SK_FONT_SANS, ' ', 0), 20, 0);
    CHECK(g->mask.w == 0 && g->mask.h == 0);
    sk_glyph_cache_free(c);
}

static void test_bad_sizes(void)
{
    sk_glyph_cache *c = sk_glyph_cache_new(1 << 20);
    sk_glyph_ref r = ref_of(SK_FONT_MONO, 'A', 0);
    CHECK_INT(sk_glyph_get(c, r, 0, 0)->mask.w, 0);
    CHECK_INT(sk_glyph_get(c, r, -5, 0)->mask.w, 0);
    CHECK_INT(sk_glyph_get(c, r, NAN, 0)->mask.w, 0);
    CHECK_INT(sk_glyph_get(c, r, INFINITY, 0)->mask.w, 0);
    CHECK_INT(sk_glyph_get(c, r, 0.001f, 0)->mask.w, 0);
    r.glyph = 1u << 30;
    CHECK_INT(sk_glyph_get(c, r, 13, 0)->mask.w, 0);
    r.font = NULL;
    CHECK_INT(sk_glyph_get(c, r, 13, 0)->mask.w, 0);
    CHECK_INT(sk_text_cache_count(c), 0);
    sk_glyph_cache_free(c);
}

/* Each quarter pixel moves the ink right, a pixel after four. */
static void test_subpixel(void)
{
    sk_glyph_cache *c = sk_glyph_cache_new(1 << 20);
    sk_glyph_ref r = ref_of(SK_FONT_MONO, 'l', 0);
    double centre[4];
    for (int s = 0; s < 4; s++) {
        const sk_glyph *g = sk_glyph_get(c, r, 16, s);
        double sum = 0, wsum = 0;
        for (int y = 0; y < g->mask.h; y++)
            for (int x = 0; x < g->mask.w; x++) {
                sum += g->mask.a[y * g->mask.stride + x];
                wsum += g->mask.a[y * g->mask.stride + x] * (g->left + x + 0.5);
            }
        centre[s] = wsum / sum;
    }
    for (int s = 1; s < 4; s++) {
        double step = centre[s] - centre[s - 1];
        if (step < 0.15 || step > 0.35)
            sk_test_fail(__FILE__, __LINE__, "subpixel %d moves the ink %.3f px", s, step);
    }
    /* Sizes are kept to 1/64 px. */
    const sk_glyph *a = sk_glyph_get(c, r, 16.001f, 0);
    CHECK_INT(sk_text_cache_count(c), 4);
    CHECK(a == sk_glyph_get(c, r, 16, 0));
    sk_glyph_get(c, r, 16.02f, 0);
    CHECK_INT(sk_text_cache_count(c), 5);
    sk_glyph_cache_free(c);
}

static void test_synthetic_bold(void)
{
    sk_glyph_cache *c = sk_glyph_cache_new(1 << 20);
    for (int px = 8; px <= 48; px += 20) {
        saved plain = save(sk_glyph_get(c, ref_of(SK_FONT_SANS, 'l', 0), (float)px, 0));
        const sk_glyph *bold = sk_glyph_get(c, ref_of(SK_FONT_SANS, 'l', SK_STYLE_BOLD), (float)px, 0);
        /* Wider by px/16, at least a pixel, on the right; the same box
         * otherwise. */
        int grow = (int)ceilf(SK_MAX(1.0f, (float)px / 16.0f));
        CHECK_INT(bold->mask.w, plain.w + grow);
        CHECK_INT(bold->mask.h, plain.h);
        CHECK_INT(bold->left, plain.left);
        CHECK_INT(bold->top, plain.top);
        int plain_ink = 0;
        for (int i = 0; i < plain.w * plain.h; i++)
            plain_ink += plain.a[i];
        CHECK(ink(bold) > plain_ink);
        free(plain.a);
    }
    sk_glyph_cache_free(c);
}

static void test_synthetic_italic(void)
{
    sk_glyph_cache *c = sk_glyph_cache_new(1 << 20);
    const sk_glyph *g = sk_glyph_get(c, ref_of(SK_FONT_SANS, 'l', SK_STYLE_ITALIC), 40, 0);
    saved slanted = save(g);
    g = sk_glyph_get(c, ref_of(SK_FONT_SANS, 'l', 0), 40, 0);
    /* An upright stem: its left edge is the same at the top and the
     * bottom. Slanted, the top is 0.2 of its height further right, and
     * the foot stays on the baseline. */
    int h = g->mask.h;
    CHECK_INT(left_ink(g, 1), left_ink(g, h - 2));
    sk_glyph s = {{slanted.a, slanted.w, slanted.h, slanted.w}, slanted.left, slanted.top};
    CHECK_INT(s.top, g->top);
    CHECK_INT(s.top + s.mask.h, 0);
    int shift = left_ink(&s, 1) - left_ink(&s, h - 2);
    int want = (int)lroundf(0.2f * (float)(h - 3));
    if (abs(shift - want) > 1)
        sk_test_fail(__FILE__, __LINE__, "slanted by %d px over %d, want %d", shift, h - 3, want);
    CHECK(abs(left_ink(&s, h - 2) - left_ink(g, h - 2)) <= 1);
    free(slanted.a);
    sk_glyph_cache_free(c);
}

/* A small cache keeps under its size by dropping the least recently
 * used, and draws a dropped glyph again exactly as before. */
static void test_eviction(void)
{
    const size_t max = 4096;
    sk_glyph_cache *c = sk_glyph_cache_new(max);
    sk_glyph_ref a = ref_of(SK_FONT_SANS, 'A', SK_STYLE_ITALIC | SK_STYLE_BOLD);
    saved first = save(sk_glyph_get(c, a, 24, 1));
    CHECK_INT(sk_text_cache_count(c), 1);
    for (uint32_t cp = 'a'; cp <= 'z'; cp++) {
        sk_glyph_get(c, ref_of(SK_FONT_SANS, cp, 0), 24, 0);
        CHECK(sk_text_cache_bytes(c) <= max);
    }
    /* Some went, the A first of them; the newest stays. */
    size_t n = sk_text_cache_count(c);
    CHECK(n > 1 && n < 27);
    sk_glyph_get(c, ref_of(SK_FONT_SANS, 'z', 0), 24, 0);
    CHECK_INT(sk_text_cache_count(c), n);
    CHECK(same(&first, sk_glyph_get(c, a, 24, 1)));
    /* Used between the others, it stays as they come and go. */
    for (uint32_t cp = 'a'; cp <= 'z'; cp++) {
        sk_glyph_get(c, ref_of(SK_FONT_SANS, cp, SK_STYLE_BOLD), 24, 2);
        const sk_glyph *g = sk_glyph_get(c, a, 24, 1);
        CHECK(same(&first, g));
    }
    free(first.a);

    /* A glyph larger than the whole cache is still returned whole, and
     * goes when the next comes. */
    sk_glyph_cache *tiny = sk_glyph_cache_new(64);
    const sk_glyph *big = sk_glyph_get(tiny, ref_of(SK_FONT_MONO, 'W', 0), 200, 0);
    CHECK(big->mask.w > 100 && ink(big) > 0);
    CHECK_INT(sk_text_cache_count(tiny), 1);
    sk_glyph_get(tiny, ref_of(SK_FONT_MONO, 'M', 0), 200, 0);
    CHECK_INT(sk_text_cache_count(tiny), 1);
    sk_glyph_cache_free(tiny);
    sk_glyph_cache_free(c);
}

/* Enough glyphs that the table grows, each found again afterwards. */
static void test_many(void)
{
    sk_glyph_cache *c = sk_glyph_cache_new((size_t)64 << 20);
    for (uint32_t cp = 0x21; cp < 0x7f; cp++)
        for (int s = 0; s < 4; s++)
            sk_glyph_get(c, ref_of(SK_FONT_MONO, cp, s), 13, s);
    size_t n = sk_text_cache_count(c);
    CHECK_INT(n, 94 * 4);
    for (uint32_t cp = 0x21; cp < 0x7f; cp++)
        for (int s = 0; s < 4; s++)
            CHECK(sk_glyph_get(c, ref_of(SK_FONT_MONO, cp, s), 13, s)->mask.w > 0);
    CHECK_INT(sk_text_cache_count(c), n);
    sk_glyph_cache_free(c);
}

const sk_test sk_suite_glyphcache[] = {
    T(test_glyph_box),
    T(test_bad_sizes),
    T(test_subpixel),
    T(test_synthetic_bold),
    T(test_synthetic_italic),
    T(test_eviction),
    T(test_many),
    {0},
};
