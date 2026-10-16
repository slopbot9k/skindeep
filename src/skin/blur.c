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
 * pixel, so only the last pass rounds to 8 bits. */
#include "blur.h"

#include <math.h>
#include <string.h>

/* The three boxes for sigma: each pass's width and how far left of the
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

/* One box pass over n values, src read with stride ss, out written
 * with stride os: out[i] is the mean of src[i - left .. i - left +
 * width - 1], values outside counting as 0. Inputs and outputs carry
 * shift extra bits: in << in_shift, out << out_shift. */
static void box_pass(const uint32_t *src, size_t ss, uint32_t *out, size_t os, int n, int width, int left)
{
    uint64_t sum = 0;
    /* The window for i = 0 is [-left, width - left). */
    for (int k = 0; k < width - left && k < n; k++)
        sum += src[(size_t)k * ss];
    uint64_t inv = ((uint64_t)1 << 32) / (uint64_t)width;
    for (int i = 0; i < n; i++) {
        out[(size_t)i * os] = (uint32_t)((sum * inv + ((uint64_t)1 << 31)) >> 32);
        int add = i + width - left, drop = i - left;
        if (add < n)
            sum += src[(size_t)add * ss];
        if (drop >= 0)
            sum -= src[(size_t)drop * ss];
    }
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

/* Blurs n lines of len values in v (8.8 fixed point), each line's
 * values step apart and lines line apart. */
static void blur_lines(uint32_t *v, int nlines, size_t line, int len, size_t step, float sigma, uint32_t *t0,
                       uint32_t *t1)
{
    if (sigma >= 2) {
        boxes b = plan(sigma);
        for (int l = 0; l < nlines; l++) {
            uint32_t *p = v + (size_t)l * line;
            box_pass(p, step, t0, 1, len, b.width[0], b.left[0]);
            box_pass(t0, 1, t1, 1, len, b.width[1], b.left[1]);
            box_pass(t1, 1, p, step, len, b.width[2], b.left[2]);
        }
        return;
    }
    int r;
    float *k = kernel(sigma, &r);
    for (int l = 0; l < nlines; l++) {
        uint32_t *p = v + (size_t)l * line;
        for (int i = 0; i < len; i++)
            t0[i] = p[(size_t)i * step];
        for (int i = 0; i < len; i++) {
            float acc = 0;
            int lo = i - r < 0 ? 0 : i - r, hi = i + r >= len ? len - 1 : i + r;
            for (int j = lo; j <= hi; j++)
                acc += k[j - i + r] * (float)t0[j];
            p[(size_t)i * step] = (uint32_t)(acc + 0.5f);
        }
    }
    free(k);
}

void sk_blur_a8(uint8_t *a, int w, int h, int stride, float sigma)
{
    if (!(sigma > 0.01f) || w <= 0 || h <= 0)
        return;
    size_t n = (size_t)w * (size_t)h;
    uint32_t *v = sk_xmalloc(n * sizeof *v);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            v[(size_t)y * (size_t)w + (size_t)x] = (uint32_t)a[(size_t)y * (size_t)stride + (size_t)x] << 8;
    int m = w > h ? w : h;
    uint32_t *t0 = sk_xmalloc((size_t)m * sizeof *t0), *t1 = sk_xmalloc((size_t)m * sizeof *t1);
    blur_lines(v, h, (size_t)w, w, 1, sigma, t0, t1);
    blur_lines(v, w, 1, h, (size_t)w, sigma, t0, t1);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t q = (v[(size_t)y * (size_t)w + (size_t)x] + 128) >> 8;
            a[(size_t)y * (size_t)stride + (size_t)x] = (uint8_t)(q > 255 ? 255 : q);
        }
    free(v);
    free(t0);
    free(t1);
}

void sk_blur_image(sk_image *im, float sigma)
{
    if (!(sigma > 0.01f) || im->w <= 0 || im->h <= 0)
        return;
    int w = im->w, h = im->h;
    size_t n = (size_t)w * (size_t)h;
    uint32_t *v = sk_xmalloc(n * sizeof *v);
    int m = w > h ? w : h;
    uint32_t *t0 = sk_xmalloc((size_t)m * sizeof *t0), *t1 = sk_xmalloc((size_t)m * sizeof *t1);
    uint32_t *out = sk_xcalloc(n, sizeof *out);
    for (int sh = 0; sh < 32; sh += 8) {
        for (int y = 0; y < h; y++) {
            const sk_px *row = sk_image_row(im, y);
            for (int x = 0; x < w; x++)
                v[(size_t)y * (size_t)w + (size_t)x] = ((row[x] >> sh) & 255) << 8;
        }
        blur_lines(v, h, (size_t)w, w, 1, sigma, t0, t1);
        blur_lines(v, w, 1, h, (size_t)w, sigma, t0, t1);
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
    free(t0);
    free(t1);
    free(out);
}

int sk_blur_reach(float sigma)
{
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
