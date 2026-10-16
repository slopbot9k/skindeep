#include "draw.h"

/* x * y / 255, rounded: draw.h's mul. */
static inline uint32_t mul(uint32_t x, uint32_t y)
{
    uint32_t t = x * y + 128;
    return (t + (t >> 8)) >> 8;
}

/* A premultiplied pixel scaled by k, channel by channel. */
static inline sk_px scale(sk_px p, uint32_t k)
{
    return mul(p >> 24, k) << 24 | mul((p >> 16) & 255, k) << 16 | mul((p >> 8) & 255, k) << 8 | mul(p & 255, k);
}

static inline sk_px over(sk_px src, sk_px dst)
{
    uint32_t k = 255 - (src >> 24);
    if (k == 0)
        return src;
    if (k == 255)
        return dst;
    return src + scale(dst, k);
}

static inline sk_px blend(sk_blend b, sk_px src, sk_px dst)
{
    switch (b) {
    case SK_BLEND_SRC_OVER:
        return over(src, dst);
    case SK_BLEND_SRC:
        return src;
    default:
        sk_die("blend mode %d is not drawn yet", (int)b);
    }
}

/* r clipped to the image and the clip. */
static sk_rect clipped(const sk_image *dst, sk_rect r, const sk_rect *clip)
{
    r = sk_rect_intersect(r, sk_image_bounds(dst));
    return clip ? sk_rect_intersect(r, *clip) : r;
}

void sk_fill_rect(sk_image *dst, sk_rect r, sk_color c, sk_blend b, const sk_rect *clip)
{
    r = clipped(dst, r, clip);
    if (sk_rect_empty(r))
        return;
    sk_px p = sk_premul(c);
    for (int y = r.y; y < r.y + r.h; y++) {
        sk_px *row = sk_image_row(dst, y);
        for (int x = r.x; x < r.x + r.w; x++)
            row[x] = blend(b, p, row[x]);
    }
}

void sk_blit(sk_image *dst, int x, int y, const sk_image *src, sk_rect src_r, sk_blend b, uint8_t opacity,
             const sk_rect *clip)
{
    src_r = sk_rect_intersect(src_r, sk_image_bounds(src));
    sk_rect r = clipped(dst, sk_rect_make(x, y, src_r.w, src_r.h), clip);
    if (sk_rect_empty(r))
        return;
    int sx = src_r.x + (r.x - x), sy = src_r.y + (r.y - y);
    for (int j = 0; j < r.h; j++) {
        const sk_px *s = sk_image_row(src, sy + j) + sx;
        sk_px *d = sk_image_row(dst, r.y + j) + r.x;
        for (int i = 0; i < r.w; i++)
            d[i] = blend(b, opacity == 255 ? s[i] : scale(s[i], opacity), d[i]);
    }
}

void sk_fill_mask(sk_image *dst, int x, int y, const sk_mask *m, sk_color c, const sk_rect *clip)
{
    sk_rect r = clipped(dst, sk_rect_make(x, y, m->w, m->h), clip);
    if (sk_rect_empty(r))
        return;
    sk_px p = sk_premul(c);
    for (int j = r.y; j < r.y + r.h; j++) {
        const uint8_t *a = m->a + (size_t)(j - y) * (size_t)m->stride + (r.x - x);
        sk_px *d = sk_image_row(dst, j);
        for (int i = 0; i < r.w; i++)
            if (a[i])
                d[r.x + i] = over(a[i] == 255 ? p : scale(p, a[i]), d[r.x + i]);
    }
}
