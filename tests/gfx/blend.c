/* Blend modes: values worked by hand from draw.h's formulas, and the
 * drawing functions against a plain reference of the same arithmetic,
 * channel by channel, on random premultiplied pixels. */
#include "../../src/gfx/draw.h"
#include "../golden.h"
#include "../test.h"

/* ---- The reference, one channel at a time ---- */

static uint32_t div255(uint32_t t)
{
    t += 128;
    return (t + (t >> 8)) >> 8;
}

static uint32_t ch(sk_px p, int c)
{
    return (p >> (8 * c)) & 255;
}

static sk_px ref_scale(sk_px p, uint32_t k)
{
    sk_px out = 0;
    for (int c = 0; c < 4; c++)
        out |= div255(ch(p, c) * k) << (8 * c);
    return out;
}

static sk_px ref_blend(sk_blend b, sk_px s, sk_px d)
{
    int32_t sa = (int32_t)ch(s, 3), da = (int32_t)ch(d, 3);
    if (b == SK_BLEND_SRC)
        return s;
    sk_px out = 0;
    for (int c = 0; c < 4; c++) {
        int32_t x = (int32_t)ch(s, c), y = (int32_t)ch(d, c), t = 0;
        uint32_t v;
        if (b == SK_BLEND_ADD) {
            v = (uint32_t)SK_MIN(x + y, 255);
        } else if (c == 3 || b == SK_BLEND_SRC_OVER) {
            v = (uint32_t)x + div255((uint32_t)(y * (255 - sa)));
        } else {
            switch (b) {
            case SK_BLEND_MULTIPLY:
                t = x * (255 - da) + y * (255 - sa) + x * y;
                break;
            case SK_BLEND_SCREEN:
                t = 255 * (x + y) - x * y;
                break;
            case SK_BLEND_OVERLAY:
                t = x * (255 - da) + y * (255 - sa) + (2 * y <= da ? 2 * x * y : sa * da - 2 * (da - y) * (sa - x));
                break;
            case SK_BLEND_DARKEN:
                t = 255 * (x + y) - SK_MAX(x * da, y * sa);
                break;
            default:
                t = 255 * (x + y) - SK_MIN(x * da, y * sa);
                break;
            }
            v = div255((uint32_t)t);
        }
        out |= v << (8 * c);
    }
    return out;
}

/* ---- By hand ---- */

#define PX(a, r, g, b) ((sk_px)((uint32_t)(a) << 24 | (uint32_t)(r) << 16 | (uint32_t)(g) << 8 | (uint32_t)(b)))

static sk_px one(sk_blend b, sk_px s, sk_px d, uint8_t opacity)
{
    sk_image src, dst;
    sk_image_init(&src, 1, 1);
    sk_image_init(&dst, 1, 1);
    src.px[0] = s;
    dst.px[0] = d;
    sk_blit(&dst, 0, 0, &src, sk_rect_make(0, 0, 1, 1), b, opacity, NULL);
    sk_px out = dst.px[0];
    sk_image_free(&src);
    sk_image_free(&dst);
    return out;
}

/* Both opaque: the separable modes are their plain formulas on 0..255,
 * mul(s, d) for multiply and so on. s = (200, 100, 50), d = (100, 150,
 * 250); 20000 / 255 = 78.4, 15000 / 255 = 58.8, 12500 / 255 = 49.0. */
static void test_opaque_on_opaque(void)
{
    sk_px s = PX(255, 200, 100, 50), d = PX(255, 100, 150, 250);
    CHECK_INT(one(SK_BLEND_SRC_OVER, s, d, 255), s);
    CHECK_INT(one(SK_BLEND_SRC, s, d, 255), s);
    CHECK_INT(one(SK_BLEND_MULTIPLY, s, d, 255), PX(255, 78, 59, 49));
    /* s + d - mul(s, d): 300 - 78, 250 - 59, 300 - 49. */
    CHECK_INT(one(SK_BLEND_SCREEN, s, d, 255), PX(255, 222, 191, 251));
    /* R: 2 d <= 255, so 2 s d = 40000, / 255 = 156.9. G: 65025 - 2 * 105
     * * 155 = 32475, / 255 = 127.4. B: 65025 - 2 * 5 * 205 = 62975, / 255
     * = 246.96. */
    CHECK_INT(one(SK_BLEND_OVERLAY, s, d, 255), PX(255, 157, 127, 247));
    CHECK_INT(one(SK_BLEND_DARKEN, s, d, 255), PX(255, 100, 100, 50));
    CHECK_INT(one(SK_BLEND_LIGHTEN, s, d, 255), PX(255, 200, 150, 250));
    CHECK_INT(one(SK_BLEND_ADD, s, d, 255), PX(255, 255, 250, 255));
}

/* s = (100, 50, 0) at alpha 128 over d = (60, 200, 255) opaque. Every
 * mode but SRC and ADD ends opaque: 128 + mul(255, 127) = 255. */
static void test_translucent_on_opaque(void)
{
    sk_px s = PX(128, 100, 50, 0), d = PX(255, 60, 200, 255);
    /* 100 + mul(60, 127) = 100 + 29.9; 50 + mul(200, 127) = 50 + 99.6;
     * mul(255, 127) = 127. */
    CHECK_INT(one(SK_BLEND_SRC_OVER, s, d, 255), PX(255, 130, 150, 127));
    CHECK_INT(one(SK_BLEND_SRC, s, d, 255), s);
    /* d 127 + s d: 7620 + 6000 = 13620 (53.4); 25400 + 10000 = 35400
     * (138.8); 32385 + 0 (127). */
    CHECK_INT(one(SK_BLEND_MULTIPLY, s, d, 255), PX(255, 53, 139, 127));
    /* 255 (s + d) - s d: 40800 - 6000 = 34800 (136.5); 63750 - 10000 =
     * 53750 (210.8); 65025. */
    CHECK_INT(one(SK_BLEND_SCREEN, s, d, 255), PX(255, 136, 211, 255));
    /* d 127 + x. R: 2 * 60 <= 255, so x = 2 s d: 7620 + 12000 = 19620
     * (76.9). G: x = 128 * 255 - 2 * 55 * 78 = 24060, and 25400 + 24060
     * = 49460 (193.96). B: x = 32640 - 0, and 32385 + 32640 = 65025. */
    CHECK_INT(one(SK_BLEND_OVERLAY, s, d, 255), PX(255, 77, 194, 255));
    /* 255 (s + d) - max(s da, d sa): 40800 - 25500 = 15300 (60); 63750 -
     * 25600 = 38150 (149.6); 65025 - 32640 = 32385 (127). */
    CHECK_INT(one(SK_BLEND_DARKEN, s, d, 255), PX(255, 60, 150, 127));
    /* 255 (s + d) - min(s da, d sa): 40800 - 7680 = 33120 (129.9); 63750
     * - 12750 = 51000 (200); 65025. */
    CHECK_INT(one(SK_BLEND_LIGHTEN, s, d, 255), PX(255, 130, 200, 255));
    CHECK_INT(one(SK_BLEND_ADD, s, d, 255), PX(255, 160, 250, 255));
}

/* Both translucent. s = (64, 0, 128) at 128, d = (50, 100, 0) at 100.
 * Alpha: 128 + mul(100, 127) = 128 + 49.8. Multiply: s 155 + d 127 + s
 * d = 9920 + 6350 + 3200 = 19470 (76.4); 12700 (49.8); 19840 (77.8). */
static void test_translucent_on_translucent(void)
{
    sk_px s = PX(128, 64, 0, 128), d = PX(100, 50, 100, 0);
    CHECK_INT(one(SK_BLEND_MULTIPLY, s, d, 255), PX(178, 76, 50, 78));
    /* Plus-lighter clamps alpha too: 228, 114, 100, 128. */
    CHECK_INT(one(SK_BLEND_ADD, s, d, 255), PX(228, 114, 100, 128));
    CHECK_INT(one(SK_BLEND_ADD, PX(200, 200, 0, 0), PX(100, 100, 0, 0), 255), PX(255, 255, 0, 0));
}

/* Opacity scales the source first: white at 128 over black is 128 grey,
 * opaque; SRC at opacity 0 leaves nothing. */
static void test_opacity(void)
{
    CHECK_INT(one(SK_BLEND_SRC_OVER, 0xffffffffu, 0xff000000u, 128), 0xff808080u);
    CHECK_INT(one(SK_BLEND_SRC_OVER, 0xffffffffu, 0xff000000u, 0), 0xff000000u);
    CHECK_INT(one(SK_BLEND_SRC, 0xffffffffu, 0xff000000u, 0), 0);
    CHECK_INT(one(SK_BLEND_SRC, 0xffffffffu, 0xff000000u, 51), 0x33333333u);
    /* Multiply of white is the identity, whatever the opacity. */
    for (int k = 0; k < 256; k += 15)
        CHECK_INT(one(SK_BLEND_MULTIPLY, 0xffffffffu, 0xff336699u, (uint8_t)k), 0xff336699u);
}

/* ---- Fast paths against the reference ---- */

static uint64_t rng;

static uint32_t next(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (uint32_t)(rng >> 16);
}

/* A premultiplied pixel, alpha often 0 or 255 as in real images. */
static sk_px random_px(void)
{
    uint32_t pick = next() % 8, a = pick == 0 ? 0 : pick == 1 ? 255 : next() & 255;
    return a << 24 | (next() % (a + 1)) << 16 | (next() % (a + 1)) << 8 | next() % (a + 1);
}

static void random_image(sk_image *im, int w, int h)
{
    sk_image_init(im, w, h);
    for (int i = 0; i < w * h; i++)
        im->px[i] = random_px();
}

static const sk_blend modes[] = {SK_BLEND_SRC_OVER, SK_BLEND_SRC,    SK_BLEND_MULTIPLY, SK_BLEND_SCREEN,
                                 SK_BLEND_OVERLAY,  SK_BLEND_DARKEN, SK_BLEND_LIGHTEN,  SK_BLEND_ADD};

/* sk_blit, every mode at every opacity, on rows long enough for the
 * vectorized loops and their tails. */
static void test_blit_matches_reference(void)
{
    rng = 0x9e3779b97f4a7c15ull;
    sk_image src, dst0, dst;
    random_image(&src, 37, 3);
    random_image(&dst0, 37, 3);
    sk_image_init(&dst, 37, 3);
    long bad = 0;
    for (size_t m = 0; m < SK_LEN(modes); m++)
        for (int k = 0; k < 256; k++) {
            memcpy(dst.px, dst0.px, 37 * 3 * sizeof *dst.px);
            sk_blit(&dst, 0, 0, &src, sk_image_bounds(&src), modes[m], (uint8_t)k, NULL);
            for (int i = 0; i < 37 * 3; i++) {
                sk_px want = ref_blend(modes[m], ref_scale(src.px[i], (uint32_t)k), dst0.px[i]);
                if (dst.px[i] != want && bad++ < 5)
                    sk_test_fail(__FILE__, __LINE__, "mode %d opacity %d: %08x over %08x is %08x, not %08x",
                                 (int)modes[m], k, src.px[i], dst0.px[i], dst.px[i], want);
            }
        }
    CHECK_INT(bad, 0);
    sk_image_free(&src);
    sk_image_free(&dst0);
    sk_image_free(&dst);
}

/* sk_fill_rect, every mode, with many colours. */
static void test_fill_rect_matches_reference(void)
{
    rng = 0x2545f4914f6cdd1dull;
    sk_image dst0, dst;
    random_image(&dst0, 29, 2);
    sk_image_init(&dst, 29, 2);
    long bad = 0;
    for (size_t m = 0; m < SK_LEN(modes); m++)
        for (int n = 0; n < 300; n++) {
            sk_color c = next() % 4 == 0 ? SK_RGB(next(), next(), next()) : (sk_color)next() << 8 | (next() & 255);
            memcpy(dst.px, dst0.px, 29 * 2 * sizeof *dst.px);
            sk_fill_rect(&dst, sk_image_bounds(&dst), c, modes[m], NULL);
            for (int i = 0; i < 29 * 2; i++)
                if (dst.px[i] != ref_blend(modes[m], sk_premul(c), dst0.px[i]))
                    bad++;
        }
    CHECK_INT(bad, 0);
    sk_image_free(&dst0);
    sk_image_free(&dst);
}

/* sk_fill_mask: coverage times the colour, source-over. */
static void test_fill_mask_matches_reference(void)
{
    rng = 0x5851f42d4c957f2dull;
    sk_image dst0, dst;
    random_image(&dst0, 23, 7);
    sk_image_init(&dst, 23, 7);
    uint8_t a[23 * 7];
    long bad = 0;
    for (int n = 0; n < 400; n++) {
        for (size_t i = 0; i < sizeof a; i++) {
            uint32_t pick = next() % 3;
            a[i] = (uint8_t)(pick == 0 ? 0 : pick == 1 ? 255 : next());
        }
        sk_color c = n % 3 ? (sk_color)next() << 8 | (next() & 255) : SK_RGB(next(), next(), next());
        memcpy(dst.px, dst0.px, sizeof a * sizeof *dst.px);
        sk_mask m = {a, 23, 7, 23};
        sk_fill_mask(&dst, 0, 0, &m, c, NULL);
        for (size_t i = 0; i < sizeof a; i++)
            if (dst.px[i] != ref_blend(SK_BLEND_SRC_OVER, ref_scale(sk_premul(c), a[i]), dst0.px[i]))
                bad++;
    }
    CHECK_INT(bad, 0);
    sk_image_free(&dst0);
    sk_image_free(&dst);
}

/* The bilinear sample of draw.h along one axis, from its words. */
static void ref_sample(int j, int n, int m, int *i, uint32_t *f)
{
    /* (j + 1/2) m / n - 1/2 in 256ths, rounded down: 256 ((2j + 1) m -
     * n) / 2n, floored, from a numerator that may be negative. */
    long long num = 256LL * ((2LL * j + 1) * m - n), den = 2LL * n;
    long long u = num >= 0 ? num / den : -((-num + den - 1) / den);
    *i = (int)(u >= 0 ? u / 256 : -((-u + 255) / 256));
    *f = (uint32_t)(u - 256LL * *i);
}

static sk_px ref_lerp(sk_px a, sk_px b, uint32_t f)
{
    sk_px out = 0;
    for (int c = 0; c < 4; c++)
        out |= ((ch(a, c) * (256 - f) + ch(b, c) * f + 128) >> 8) << (8 * c);
    return out;
}

static sk_px ref_at(const sk_image *im, int x, int y)
{
    x = SK_MIN(SK_MAX(x, 0), im->w - 1);
    y = SK_MIN(SK_MAX(y, 0), im->h - 1);
    return sk_image_row(im, y)[x];
}

/* sk_blit_scaled, both filters, up and down, every mode and a range of
 * opacities, against the mapping and arithmetic written out plainly. */
static void test_blit_scaled_matches_reference(void)
{
    rng = 0x123456789abcdefull;
    sk_image src;
    random_image(&src, 13, 9);
    static const int sizes[][2] = {{13, 9}, {40, 23}, {5, 4}, {26, 18}, {7, 30}, {1, 1}};
    long bad = 0;
    for (size_t z = 0; z < SK_LEN(sizes); z++) {
        int w = sizes[z][0], h = sizes[z][1];
        sk_image dst0, dst;
        random_image(&dst0, w, h);
        sk_image_init(&dst, w, h);
        for (int filter = 0; filter < 2; filter++)
            for (size_t m = 0; m < SK_LEN(modes); m++)
                for (int k = 0; k < 256; k += 51) {
                    memcpy(dst.px, dst0.px, (size_t)w * (size_t)h * sizeof *dst.px);
                    sk_blit_scaled(&dst, sk_image_bounds(&dst), &src, sk_image_bounds(&src), (sk_filter)filter,
                                   modes[m], (uint8_t)k, NULL);
                    for (int y = 0; y < h; y++)
                        for (int x = 0; x < w; x++) {
                            sk_px p;
                            if (filter == SK_FILTER_NEAREST) {
                                p = ref_at(&src, (int)(((2LL * x + 1) * src.w) / (2LL * w)),
                                           (int)(((2LL * y + 1) * src.h) / (2LL * h)));
                            } else {
                                int i, j;
                                uint32_t fx, fy;
                                ref_sample(x, w, src.w, &i, &fx);
                                ref_sample(y, h, src.h, &j, &fy);
                                sk_px top = ref_lerp(ref_at(&src, i, j), ref_at(&src, i + 1, j), fx);
                                sk_px bot = ref_lerp(ref_at(&src, i, j + 1), ref_at(&src, i + 1, j + 1), fx);
                                p = ref_lerp(top, bot, fy);
                            }
                            sk_px want = ref_blend(modes[m], ref_scale(p, (uint32_t)k), dst0.px[y * w + x]);
                            if (dst.px[y * w + x] != want && bad++ < 5)
                                sk_test_fail(__FILE__, __LINE__, "%dx%d filter %d mode %d opacity %d at %d,%d", w,
                                             h, filter, (int)modes[m], k, x, y);
                        }
                }
        sk_image_free(&dst0);
        sk_image_free(&dst);
    }
    CHECK_INT(bad, 0);
    sk_image_free(&src);
}

/* ---- Golden ---- */

/* Each mode, a band each, over a gradient of hue and alpha: the source
 * is a ramp of colours whose alpha falls from opaque to clear. */
static void test_golden_modes(void)
{
    enum { W = 192, BAND = 20 };
    sk_image im, src;
    sk_image_init(&im, W, BAND * (int)SK_LEN(modes));
    sk_image_init(&src, W, BAND);
    for (int x = 0; x < W; x++) {
        sk_color under = SK_RGBA(255 - x, x * 4 / 3 % 256, 90 + x / 3, 255 - (x < 32 ? 0 : (x - 32) / 2));
        sk_fill_rect(&im, sk_rect_make(x, 0, 1, im.h), under, SK_BLEND_SRC, NULL);
        for (int y = 0; y < BAND; y++)
            sk_image_row(&src, y)[x] = sk_premul(SK_RGBA(40 + y * 10, 230 - y * 9, x, 255 - y * 12));
    }
    for (size_t m = 0; m < SK_LEN(modes); m++)
        sk_blit(&im, 0, BAND * (int)m, &src, sk_image_bounds(&src), modes[m], 255, NULL);
    CHECK_GOLDEN("blend-modes", &im, 0);
    sk_image_free(&im);
    sk_image_free(&src);
}

const sk_test sk_suite_gfx_blend[] = {
    T(test_opaque_on_opaque),
    T(test_translucent_on_opaque),
    T(test_translucent_on_translucent),
    T(test_opacity),
    T(test_blit_matches_reference),
    T(test_fill_rect_matches_reference),
    T(test_fill_mask_matches_reference),
    T(test_blit_scaled_matches_reference),
    T(test_golden_modes),
    {0},
};
