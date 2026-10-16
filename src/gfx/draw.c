#include "internal.h"

#include <string.h>

/* ---- Pixels ---- */

/* One channel of a separable mode, by draw.h's formula: s and d
 * premultiplied, sa and da their alphas. */
static inline uint32_t channel(sk_blend b, int32_t s, int32_t d, int32_t sa, int32_t da)
{
    int32_t t;
    switch (b) {
    case SK_BLEND_MULTIPLY:
        t = s * (255 - da) + d * (255 - sa) + s * d;
        break;
    case SK_BLEND_SCREEN:
        t = 255 * (s + d) - s * d;
        break;
    case SK_BLEND_OVERLAY:
        t = s * (255 - da) + d * (255 - sa) + (2 * d <= da ? 2 * s * d : sa * da - 2 * (da - d) * (sa - s));
        break;
    case SK_BLEND_DARKEN:
        t = 255 * (s + d) - SK_MAX(s * da, d * sa);
        break;
    default: /* SK_BLEND_LIGHTEN */
        t = 255 * (s + d) - SK_MIN(s * da, d * sa);
        break;
    }
    /* Only a colour above its alpha, which is not premultiplied, can
     * leave 0..255 * 255. */
    t = t < 0 ? 0 : SK_MIN(t, 255 * 255);
    return px_div255((uint32_t)t);
}

static sk_px blend_px(sk_blend b, sk_px s, sk_px d)
{
    switch (b) {
    case SK_BLEND_SRC:
        return s;
    case SK_BLEND_MULTIPLY:
    case SK_BLEND_SCREEN:
    case SK_BLEND_OVERLAY:
    case SK_BLEND_DARKEN:
    case SK_BLEND_LIGHTEN: {
        int32_t sa = (int32_t)(s >> 24), da = (int32_t)(d >> 24);
        sk_px out = (sa + px_mul((uint32_t)da, (uint32_t)(255 - sa))) << 24;
        for (int sh = 0; sh < 24; sh += 8)
            out |= channel(b, (int32_t)((s >> sh) & 255), (int32_t)((d >> sh) & 255), sa, da) << sh;
        return out;
    }
    case SK_BLEND_ADD: {
        sk_px out = 0;
        for (int sh = 0; sh < 32; sh += 8)
            out |= SK_MIN(((s >> sh) & 255) + ((d >> sh) & 255), 255u) << sh;
        return out;
    }
    default:
        return px_over(s, d);
    }
}

/* ---- Rows ---- */

/* The loops below are plain so the compiler can vectorize them; each
 * computes exactly what blend_px would. */

static void row_fill(sk_px *d, int n, sk_px p)
{
    for (int i = 0; i < n; i++)
        d[i] = p;
}

/* d[0..n) under the one pixel p. */
static void row_solid(sk_px *d, int n, sk_px p, sk_blend b)
{
    if (b == SK_BLEND_SRC || (b == SK_BLEND_SRC_OVER && p >> 24 == 255)) {
        row_fill(d, n, p);
    } else if (b == SK_BLEND_SRC_OVER) {
        if (!p)
            return;
        uint32_t k = 255 - (p >> 24);
        for (int i = 0; i < n; i++)
            d[i] = p + px_scale(d[i], k);
    } else {
        for (int i = 0; i < n; i++)
            d[i] = blend_px(b, p, d[i]);
    }
}

/* d[0..n) under s[0..n), the source scaled by k first. */
static void row_blend(sk_px *d, const sk_px *s, int n, sk_blend b, uint32_t k)
{
    if (b == SK_BLEND_SRC_OVER) {
        if (k == 255)
            for (int i = 0; i < n; i++)
                d[i] = px_over(s[i], d[i]);
        else if (k)
            for (int i = 0; i < n; i++)
                d[i] = px_over(px_scale(s[i], k), d[i]);
    } else if (b == SK_BLEND_SRC) {
        if (k == 255)
            memmove(d, s, (size_t)n * sizeof *d);
        else
            for (int i = 0; i < n; i++)
                d[i] = px_scale(s[i], k);
    } else {
        for (int i = 0; i < n; i++)
            d[i] = blend_px(b, k == 255 ? s[i] : px_scale(s[i], k), d[i]);
    }
}

void sk_gfx_span(sk_px *d, int n, sk_px p, uint32_t cover)
{
    row_solid(d, n, cover == 255 ? p : px_scale(p, cover), SK_BLEND_SRC_OVER);
}

/* ---- Rectangles, images, masks ---- */

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
    for (int y = r.y; y < r.y + r.h; y++)
        row_solid(sk_image_row(dst, y) + r.x, r.w, p, b);
}

void sk_blit(sk_image *dst, int x, int y, const sk_image *src, sk_rect src_r, sk_blend b, uint8_t opacity,
             const sk_rect *clip)
{
    sk_rect s = sk_rect_intersect(src_r, sk_image_bounds(src));
    if (sk_rect_empty(s))
        return;
    /* Where the part of src_r inside src lands: src_r's corner is at
     * (x, y). Beyond INT_MAX it lands on no image. */
    long long dx = (long long)x + ((long long)s.x - src_r.x), dy = (long long)y + ((long long)s.y - src_r.y);
    if (dx > INT32_MAX || dy > INT32_MAX)
        return;
    sk_rect r = clipped(dst, sk_rect_make((int)dx, (int)dy, s.w, s.h), clip);
    if (sk_rect_empty(r))
        return;
    int sx = s.x + (r.x - (int)dx), sy = s.y + (r.y - (int)dy);
    for (int j = 0; j < r.h; j++)
        row_blend(sk_image_row(dst, r.y + j) + r.x, sk_image_row(src, sy + j) + sx, r.w, b, opacity);
}

void sk_fill_mask(sk_image *dst, int x, int y, const sk_mask *m, sk_color c, const sk_rect *clip)
{
    sk_rect r = clipped(dst, sk_rect_make(x, y, m->w, m->h), clip);
    sk_px p = sk_premul(c);
    if (sk_rect_empty(r) || !p)
        return;
    for (int j = r.y; j < r.y + r.h; j++) {
        const uint8_t *a = m->a + (size_t)(j - y) * (size_t)m->stride + (r.x - x);
        sk_px *d = sk_image_row(dst, j) + r.x;
        /* No test for coverage 0, which leaves d as it is: a glyph's
         * zeros and ones mispredict, and without the branch the loop
         * vectorizes. */
        for (int i = 0; i < r.w; i++)
            d[i] = px_over(px_scale(p, a[i]), d[i]);
    }
}

/* ---- Scaled images ---- */

/* (a (256 - f) + b f + 128) / 256 rounded down, channel by channel, two
 * channels to a multiply, as a 256 + (b - a) f + 128. A lane's (b - a) f
 * may be negative and borrow from the lane above, but arithmetic mod
 * 2^32 is linear, so the word ends as each lane's true sum, which lies
 * in 0..255 * 256 + 128 and fits its lane. */
static inline sk_px lerp_px(sk_px a, sk_px b, uint32_t f)
{
    uint32_t a0 = a & 0x00ff00ffu, a1 = (a >> 8) & 0x00ff00ffu;
    uint32_t rb = ((b & 0x00ff00ffu) - a0) * f + (a0 << 8) + 0x00800080u;
    uint32_t ag = (((b >> 8) & 0x00ff00ffu) - a1) * f + (a1 << 8) + 0x00800080u;
    return ((rb >> 8) & 0x00ff00ffu) | (ag & 0xff00ff00u);
}

/* Where pixel j of a destination n wide samples a source m wide, from
 * the source's start, and in *f the weight of the pixel after it. The
 * centre j + 1/2 maps to (j + 1/2) m / n, which is q + rem / 2n, from
 * integers alone: nearest takes pixel q, and bilinear samples half a
 * pixel before the centre, in 256ths rounded down. */
static long long sample(long long j, int n, int m, sk_filter filter, uint32_t *f)
{
    uint64_t num = (uint64_t)(2 * j + 1) * (uint64_t)m, den = 2 * (uint64_t)n;
    uint64_t q = num / den, rem = num % den;
    *f = 0;
    if (filter == SK_FILTER_NEAREST)
        return (long long)q;
    /* A pixel more than the sample, so it is never negative. */
    uint64_t u = q * 256 + rem * 256 / den + 128;
    *f = (uint32_t)(u & 255);
    return (long long)(u >> 8) - 1;
}

static int clamp(long long i, int lo, int hi)
{
    return (int)SK_MIN(SK_MAX(i, (long long)lo), (long long)hi);
}

/* pad[k] for k in a..b is s[lo + k - 1] kept within lo..hi: a row with
 * its edge pixels repeated once more each side, so that a bilinear
 * sample's two pixels are always pad[k] and pad[k + 1]. */
static void pad_row(sk_px *pad, const sk_px *s, int a, int b, int lo, int hi)
{
    for (; a <= b && a < 1; a++)
        pad[a] = s[lo];
    int mid = SK_MIN(b, hi - lo + 1);
    if (a <= mid) {
        memcpy(pad + a, s + lo + a - 1, (size_t)(mid - a + 1) * sizeof *pad);
        a = mid + 1;
    }
    for (; a <= b; a++)
        pad[a] = s[hi];
}

void sk_blit_scaled(sk_image *dst, sk_rect dst_r, const sk_image *src, sk_rect src_r, sk_filter filter,
                    sk_blend b, uint8_t opacity, const sk_rect *clip)
{
    sk_rect in = sk_rect_intersect(src_r, sk_image_bounds(src));
    sk_rect r = clipped(dst, dst_r, clip);
    if (sk_rect_empty(in) || sk_rect_empty(r) || sk_rect_empty(src_r) || sk_rect_empty(dst_r))
        return;
    if (b == SK_BLEND_SRC_OVER && !opacity)
        return;
    if (filter != SK_FILTER_NEAREST)
        filter = SK_FILTER_BILINEAR;
    bool bilinear = filter == SK_FILTER_BILINEAR;
    int lo = in.x, hi = in.x + in.w - 1;
    /* Across: nearest's pixel, or bilinear's place in the padded row. */
    int *xi = sk_xreallocarray(NULL, (size_t)r.w, sizeof *xi);
    uint32_t *xf = sk_xreallocarray(NULL, (size_t)r.w, sizeof *xf);
    for (int j = 0; j < r.w; j++) {
        long long i = src_r.x + sample((long long)r.x + j - dst_r.x, dst_r.w, src_r.w, filter, &xf[j]);
        xi[j] = bilinear ? clamp(i - lo + 1, 0, in.w) : clamp(i, lo, hi);
    }
    sk_px *out = sk_xreallocarray(NULL, (size_t)r.w, sizeof *out);
    /* Bilinear keeps the two source rows it last interpolated across. */
    sk_px *pad = NULL, *h[2] = {NULL, NULL};
    int hy[2] = {-1, -1};
    if (bilinear) {
        pad = sk_xreallocarray(NULL, (size_t)in.w + 2, sizeof *pad);
        h[0] = sk_xreallocarray(NULL, (size_t)r.w, sizeof **h);
        h[1] = sk_xreallocarray(NULL, (size_t)r.w, sizeof **h);
    }
    for (int y = r.y; y < r.y + r.h; y++) {
        uint32_t fy;
        long long i = src_r.y + sample((long long)y - dst_r.y, dst_r.h, src_r.h, filter, &fy);
        int y0 = clamp(i, in.y, in.y + in.h - 1), y1 = clamp(i + 1, in.y, in.y + in.h - 1);
        sk_px *d = sk_image_row(dst, y) + r.x;
        if (!bilinear) {
            const sk_px *s = sk_image_row(src, y0);
            for (int j = 0; j < r.w; j++)
                out[j] = s[xi[j]];
            row_blend(d, out, r.w, b, opacity);
            continue;
        }
        /* Across each source row, then down between the two. A row not
         * kept goes where the other one needed is not. */
        for (int k = 0; k < 2; k++) {
            int want = k ? y1 : y0;
            if (hy[0] == want || hy[1] == want)
                continue;
            sk_px *hs = h[hy[0] == (k ? y0 : y1)];
            pad_row(pad, sk_image_row(src, want), xi[0], xi[r.w - 1] + 1, lo, hi);
            /* The samples gathered first: the lerps alone vectorize. */
            for (int j = 0; j < r.w; j++) {
                out[j] = pad[xi[j]];
                hs[j] = pad[xi[j] + 1];
            }
            for (int j = 0; j < r.w; j++)
                hs[j] = lerp_px(out[j], hs[j], xf[j]);
            hy[hy[0] == (k ? y0 : y1)] = want;
        }
        const sk_px *top = h[hy[0] == y0 ? 0 : 1], *bot = h[hy[0] == y1 ? 0 : 1];
        if (b == SK_BLEND_SRC_OVER && opacity == 255) {
            for (int j = 0; j < r.w; j++)
                d[j] = px_over(lerp_px(top[j], bot[j], fy), d[j]);
        } else {
            for (int j = 0; j < r.w; j++)
                out[j] = lerp_px(top[j], bot[j], fy);
            row_blend(d, out, r.w, b, opacity);
        }
    }
    free(pad);
    free(h[0]);
    free(h[1]);
    free(out);
    free(xi);
    free(xf);
}
