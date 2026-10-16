/* Images in memory (decision 0036). A pixel is a uint32_t 0xAARRGGBB
 * with premultiplied alpha. On the little-endian machines both systems
 * run on, its bytes are B, G, R, A, which is what an X11 ZPixmap at
 * depth 24 or 32 and a CGImage of kCGImageAlphaPremultipliedFirst |
 * kCGBitmapByteOrder32Little take as they are, so a frame reaches the
 * screen without a conversion.
 *
 * Every drawing function works in device pixels. A window's logical
 * size times its scale is its image's size (Retina: scale 2). */
#ifndef SK_GFX_IMAGE_H
#define SK_GFX_IMAGE_H

#include "../base/base.h"

/* A premultiplied pixel. */
typedef uint32_t sk_px;
/* A colour as CSS writes it, not premultiplied: 0xAARRGGBB. */
typedef uint32_t sk_color;

#define SK_RGB(r, g, b) ((sk_color)(0xff000000u | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b)))
#define SK_RGBA(r, g, b, a)                                                  \
    ((sk_color)(((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b)))

/* The colour premultiplied, rounding to nearest. */
sk_px sk_premul(sk_color c);
/* The colour back, rounding to nearest; a transparent pixel is 0. */
sk_color sk_unpremul(sk_px p);

typedef struct {
    int x, y, w, h;
} sk_rect;

static inline sk_rect sk_rect_make(int x, int y, int w, int h)
{
    sk_rect r = {x, y, w, h};
    return r;
}
static inline bool sk_rect_empty(sk_rect r)
{
    return r.w <= 0 || r.h <= 0;
}
/* The overlap; empty (w or h 0) when there is none. */
sk_rect sk_rect_intersect(sk_rect a, sk_rect b);
/* The smallest rectangle holding both; an empty one counts for nothing. */
sk_rect sk_rect_union(sk_rect a, sk_rect b);

/* stride is in pixels, at least w. An image either owns its pixels or
 * views another's: a view is never freed. */
typedef struct {
    sk_px *px;
    int w, h, stride;
    bool owned;
} sk_image;

/* A w by h image, every pixel transparent. Dies when memory is out; w
 * or h of 0 makes an empty image with no pixels. */
void sk_image_init(sk_image *im, int w, int h);
/* Frees an owned image's pixels; a view is left alone. Zeroes *im. */
void sk_image_free(sk_image *im);
/* r of im, clipped to it, sharing its pixels. */
sk_image sk_image_view(const sk_image *im, sk_rect r);
static inline sk_px *sk_image_row(const sk_image *im, int y)
{
    return im->px + (size_t)y * (size_t)im->stride;
}
static inline sk_rect sk_image_bounds(const sk_image *im)
{
    return sk_rect_make(0, 0, im->w, im->h);
}

/* QOI (third_party/qoi), the image format of skin packs and of the
 * golden images the tests compare with. Stored unpremultiplied, RGBA.
 * Encode: a malloc'd buffer and its size in *n, or NULL. Decode: false
 * for anything that is not a QOI image of at most max_pixels. */
uint8_t *sk_qoi_encode(const sk_image *im, size_t *n);
bool sk_qoi_decode(const uint8_t *data, size_t n, size_t max_pixels, sk_image *out);

#endif
