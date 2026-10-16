#include "image.h"

#include "../../third_party/qoi/qoi.h"

#include <string.h>

sk_px sk_premul(sk_color c)
{
    uint32_t a = c >> 24;
    if (a == 255)
        return c;
    if (a == 0)
        return 0;
    uint32_t r = ((c >> 16) & 255) * a, g = ((c >> 8) & 255) * a, b = (c & 255) * a;
    r = (r + 127) / 255;
    g = (g + 127) / 255;
    b = (b + 127) / 255;
    return a << 24 | r << 16 | g << 8 | b;
}

sk_color sk_unpremul(sk_px p)
{
    uint32_t a = p >> 24;
    if (a == 255 || a == 0)
        return a ? p : 0;
    uint32_t r = (((p >> 16) & 255) * 255 + a / 2) / a, g = (((p >> 8) & 255) * 255 + a / 2) / a,
             b = ((p & 255) * 255 + a / 2) / a;
    return a << 24 | SK_MIN(r, 255u) << 16 | SK_MIN(g, 255u) << 8 | SK_MIN(b, 255u);
}

sk_rect sk_rect_intersect(sk_rect a, sk_rect b)
{
    int x0 = SK_MAX(a.x, b.x), y0 = SK_MAX(a.y, b.y);
    int x1 = SK_MIN(a.x + a.w, b.x + b.w), y1 = SK_MIN(a.y + a.h, b.y + b.h);
    if (x1 <= x0 || y1 <= y0)
        return sk_rect_make(x0, y0, 0, 0);
    return sk_rect_make(x0, y0, x1 - x0, y1 - y0);
}

sk_rect sk_rect_union(sk_rect a, sk_rect b)
{
    if (sk_rect_empty(a))
        return b;
    if (sk_rect_empty(b))
        return a;
    int x0 = SK_MIN(a.x, b.x), y0 = SK_MIN(a.y, b.y);
    int x1 = SK_MAX(a.x + a.w, b.x + b.w), y1 = SK_MAX(a.y + a.h, b.y + b.h);
    return sk_rect_make(x0, y0, x1 - x0, y1 - y0);
}

void sk_image_init(sk_image *im, int w, int h)
{
    memset(im, 0, sizeof *im);
    if (w <= 0 || h <= 0)
        return;
    im->px = sk_xcalloc((size_t)w * (size_t)h, sizeof *im->px);
    im->w = w;
    im->h = h;
    im->stride = w;
    im->owned = true;
}

void sk_image_free(sk_image *im)
{
    if (im->owned)
        free(im->px);
    memset(im, 0, sizeof *im);
}

sk_image sk_image_view(const sk_image *im, sk_rect r)
{
    sk_image v = {0};
    r = sk_rect_intersect(r, sk_image_bounds(im));
    if (sk_rect_empty(r))
        return v;
    v.px = im->px + (size_t)r.y * (size_t)im->stride + (size_t)r.x;
    v.w = r.w;
    v.h = r.h;
    v.stride = im->stride;
    return v;
}

uint8_t *sk_qoi_encode(const sk_image *im, size_t *n)
{
    if (!im->w || !im->h)
        return NULL;
    uint8_t *rgba = sk_xmalloc((size_t)im->w * (size_t)im->h * 4);
    uint8_t *o = rgba;
    for (int y = 0; y < im->h; y++) {
        const sk_px *row = sk_image_row(im, y);
        for (int x = 0; x < im->w; x++) {
            sk_color c = sk_unpremul(row[x]);
            *o++ = (uint8_t)(c >> 16);
            *o++ = (uint8_t)(c >> 8);
            *o++ = (uint8_t)c;
            *o++ = (uint8_t)(c >> 24);
        }
    }
    qoi_desc d = {(unsigned)im->w, (unsigned)im->h, 4, QOI_SRGB};
    int len = 0;
    void *out = qoi_encode(rgba, &d, &len);
    free(rgba);
    if (!out)
        return NULL;
    *n = (size_t)len;
    return out;
}

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

bool sk_qoi_decode(const uint8_t *data, size_t n, size_t max_pixels, sk_image *out)
{
    memset(out, 0, sizeof *out);
    /* The header first: qoi_decode would allocate whatever it names. */
    if (n < 14 || n > (size_t)0x7fffffff || memcmp(data, "qoif", 4) != 0)
        return false;
    uint32_t w = be32(data + 4), h = be32(data + 8);
    if (!w || !h || w > 0x7fff || h > 0x7fff || (size_t)w * h > max_pixels)
        return false;
    qoi_desc d;
    uint8_t *rgba = qoi_decode(data, (int)n, &d, 4);
    if (!rgba)
        return false;
    sk_image_init(out, (int)d.width, (int)d.height);
    const uint8_t *p = rgba;
    for (int y = 0; y < out->h; y++) {
        sk_px *row = sk_image_row(out, y);
        for (int x = 0; x < out->w; x++, p += 4)
            row[x] = sk_premul(SK_RGBA(p[0], p[1], p[2], p[3]));
    }
    free(rgba);
    return true;
}
