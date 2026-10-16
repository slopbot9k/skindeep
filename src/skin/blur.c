/* Gaussian blur, as Skia blurs CSS shadows and filters for Chromium.
 *
 * A CSS blur radius r is a standard deviation of r / 2. Below a sigma
 * of 2 the kernel is the true Gaussian, sampled at whole pixels out to
 * 3 sigma and normalised. From 2 up, three box blurs in each direction
 * stand for it, as SVG's feGaussianBlur and Skia do: a box d wide, d =
 * floor(sigma * 3 sqrt(2 pi) / 4 + 1/2); for an odd d three boxes
 * centred on the pixel, for an even d two boxes d wide centred half a
 * pixel left and half a pixel right of it and a third d + 1 wide
 * centred on it. Between the passes values keep 8 more bits than a
 * pixel, so only the last pass rounds to 8 bits.
 *
 * The passes across run along each row; the passes down run a row at a
 * time too, a running sum per column, so both read memory in order. */
#include "blur.h"

#include <math.h>
#include <string.h>

/* The three boxes for sigma: each pass's width and how far before the
 * pixel its window starts. */
typedef struct {
    int width[3], left[3];
} boxes;

static boxes plan(float sigma)
{
    int d = (int)floor(sigma * 3.0 * sqrt(2.0 * M_PI) / 4.0 + 0.5);
    if (d < 1)
        d = 1;
    boxes b;
    if (d & 1) {
        for (int i = 0; i < 3; i++) {
            b.width[i] = d;
            b.left[i] = d / 2;
        }
    } else {
        b.width[0] = d;
        b.left[0] = d / 2;
        b.width[1] = d;
        b.left[1] = d / 2 - 1;
        b.width[2] = d + 1;
        b.left[2] = d / 2;
    }
    return b;
}

/* A Gaussian kernel of sigma, 1 + 2 radius weights summing to 1. */
static float *kernel(float sigma, int *radius)
{
    int r = (int)ceilf(3 * sigma);
    if (r < 1)
        r = 1;
    float *k = sk_xmalloc((size_t)(2 * r + 1) * sizeof *k);
    double sum = 0;
    for (int i = -r; i <= r; i++) {
        double v = exp(-(double)i * i / (2.0 * sigma * sigma));
        k[i + r] = (float)v;
        sum += v;
    }
    for (int i = 0; i <= 2 * r; i++)
        k[i] = (float)(k[i] / sum);
    *radius = r;
    return k;
}

/* 2^32 over a box's width, to divide by a multiply. */
static uint64_t inverse(int width)
{
    return ((uint64_t)1 << 32) / (uint64_t)width;
}

static uint32_t scaled(uint64_t sum, uint64_t inv)
{
    return (uint32_t)((sum * inv + ((uint64_t)1 << 31)) >> 32);
}

/* One box pass along a row of n: out[i] is the mean of src[i - left
 * .. i - left + width - 1], values beyond the row counting as 0. */
static void box_row(const uint32_t *src, uint32_t *out, int n, int width, int left)
{
    uint64_t sum = 0, inv = inverse(width);
    for (int k = 0; k < width - left && k < n; k++)
        sum += src[k];
    for (int i = 0; i < n; i++) {
        out[i] = scaled(sum, inv);
        int add = i + width - left, drop = i - left;
        if (add < n)
            sum += src[add];
        if (drop >= 0)
            sum -= src[drop];
    }
}

/* The same down the columns of a w by h plane, a row at a time. */
static void box_down(const uint32_t *src, uint32_t *out, int w, int h, int width, int left, uint64_t *sums)
{
    uint64_t inv = inverse(width);
    memset(sums, 0, (size_t)w * sizeof *sums);
    for (int k = 0; k < width - left && k < h; k++) {
        const uint32_t *r = src + (size_t)k * (size_t)w;
        for (int x = 0; x < w; x++)
            sums[x] += r[x];
    }
    for (int y = 0; y < h; y++) {
        uint32_t *o = out + (size_t)y * (size_t)w;
        for (int x = 0; x < w; x++)
            o[x] = scaled(sums[x], inv);
        int add = y + width - left, drop = y - left;
        if (add < h) {
            const uint32_t *r = src + (size_t)add * (size_t)w;
            for (int x = 0; x < w; x++)
                sums[x] += r[x];
        }
        if (drop >= 0) {
            const uint32_t *r = src + (size_t)drop * (size_t)w;
            for (int x = 0; x < w; x++)
                sums[x] -= r[x];
        }
    }
}

/* The true kernel along a row, and down the columns. */
static void gauss_row(const uint32_t *src, uint32_t *out, int n, const float *k, int r)
{
    for (int i = 0; i < n; i++) {
        float acc = 0;
        int lo = i - r < 0 ? 0 : i - r, hi = i + r >= n ? n - 1 : i + r;
        for (int j = lo; j <= hi; j++)
            acc += k[j - i + r] * (float)src[j];
        out[i] = (uint32_t)(acc + 0.5f);
    }
}

static void gauss_down(const uint32_t *src, uint32_t *out, int w, int h, const float *k, int r, float *acc)
{
    for (int y = 0; y < h; y++) {
        memset(acc, 0, (size_t)w * sizeof *acc);
        int lo = y - r < 0 ? 0 : y - r, hi = y + r >= h ? h - 1 : y + r;
        for (int j = lo; j <= hi; j++) {
            float f = k[j - y + r];
            const uint32_t *s = src + (size_t)j * (size_t)w;
            for (int x = 0; x < w; x++)
                acc[x] += f * (float)s[x];
        }
        uint32_t *o = out + (size_t)y * (size_t)w;
        for (int x = 0; x < w; x++)
            o[x] = (uint32_t)(acc[x] + 0.5f);
    }
}

/* A w by h plane of 8.8 fixed-point values blurred in place; tmp holds
 * as many. */
static void blur_plane(uint32_t *v, uint32_t *tmp, int w, int h, float sigma)
{
    int m = w > h ? w : h;
    if (sigma >= 2) {
        boxes b = plan(sigma);
        uint32_t *row = sk_xmalloc((size_t)m * sizeof *row), *row2 = sk_xmalloc((size_t)m * sizeof *row2);
        for (int y = 0; y < h; y++) {
            uint32_t *p = v + (size_t)y * (size_t)w;
            box_row(p, row, w, b.width[0], b.left[0]);
            box_row(row, row2, w, b.width[1], b.left[1]);
            box_row(row2, p, w, b.width[2], b.left[2]);
        }
        uint64_t *sums = sk_xmalloc((size_t)w * sizeof *sums);
        box_down(v, tmp, w, h, b.width[0], b.left[0], sums);
        box_down(tmp, v, w, h, b.width[1], b.left[1], sums);
        box_down(v, tmp, w, h, b.width[2], b.left[2], sums);
        memcpy(v, tmp, (size_t)w * (size_t)h * sizeof *v);
        free(sums);
        free(row);
        free(row2);
        return;
    }
    int r;
    float *k = kernel(sigma, &r);
    uint32_t *row = sk_xmalloc((size_t)m * sizeof *row);
    for (int y = 0; y < h; y++) {
        uint32_t *p = v + (size_t)y * (size_t)w;
        gauss_row(p, row, w, k, r);
        memcpy(p, row, (size_t)w * sizeof *p);
    }
    float *acc = sk_xmalloc((size_t)w * sizeof *acc);
    gauss_down(v, tmp, w, h, k, r, acc);
    memcpy(v, tmp, (size_t)w * (size_t)h * sizeof *v);
    free(acc);
    free(row);
    free(k);
}

void sk_blur_a8(uint8_t *a, int w, int h, int stride, float sigma)
{
    sigma = fminf(sigma, SK_BLUR_MAX_SIGMA);
    if (!(sigma > 0.01f) || w <= 0 || h <= 0)
        return;
    size_t n = (size_t)w * (size_t)h;
    uint32_t *v = sk_xmalloc(n * sizeof *v), *tmp = sk_xmalloc(n * sizeof *tmp);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            v[(size_t)y * (size_t)w + (size_t)x] = (uint32_t)a[(size_t)y * (size_t)stride + (size_t)x] << 8;
    blur_plane(v, tmp, w, h, sigma);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t q = (v[(size_t)y * (size_t)w + (size_t)x] + 128) >> 8;
            a[(size_t)y * (size_t)stride + (size_t)x] = (uint8_t)(q > 255 ? 255 : q);
        }
    free(v);
    free(tmp);
}

void sk_blur_image(sk_image *im, float sigma)
{
    sigma = fminf(sigma, SK_BLUR_MAX_SIGMA);
    if (!(sigma > 0.01f) || im->w <= 0 || im->h <= 0)
        return;
    int w = im->w, h = im->h;
    size_t n = (size_t)w * (size_t)h;
    uint32_t *v = sk_xmalloc(n * sizeof *v), *tmp = sk_xmalloc(n * sizeof *tmp);
    uint32_t *out = sk_xcalloc(n, sizeof *out);
    for (int sh = 0; sh < 32; sh += 8) {
        for (int y = 0; y < h; y++) {
            const sk_px *row = sk_image_row(im, y);
            for (int x = 0; x < w; x++)
                v[(size_t)y * (size_t)w + (size_t)x] = ((row[x] >> sh) & 255) << 8;
        }
        blur_plane(v, tmp, w, h, sigma);
        for (size_t i = 0; i < n; i++) {
            uint32_t q = (v[i] + 128) >> 8;
            out[i] |= (q > 255 ? 255 : q) << sh;
        }
    }
    /* Rounding may leave a colour above its alpha: hold it there. */
    for (int y = 0; y < h; y++) {
        sk_px *row = sk_image_row(im, y);
        for (int x = 0; x < w; x++) {
            uint32_t p = out[(size_t)y * (size_t)w + (size_t)x], al = p >> 24;
            uint32_t r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
            r = r > al ? al : r;
            g = g > al ? al : g;
            b = b > al ? al : b;
            row[x] = al << 24 | r << 16 | g << 8 | b;
        }
    }
    free(v);
    free(tmp);
    free(out);
}

int sk_blur_reach(float sigma)
{
    sigma = fminf(sigma, SK_BLUR_MAX_SIGMA);
    if (!(sigma > 0.01f))
        return 0;
    if (sigma < 2)
        return (int)ceilf(3 * sigma);
    boxes b = plan(sigma);
    int r = 0;
    for (int i = 0; i < 3; i++)
        r += b.width[i] - 1 - b.left[i] > b.left[i] ? b.width[i] - 1 - b.left[i] : b.left[i];
    return r;
}
