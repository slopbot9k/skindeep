/* Paths: any path through any transform, filled under either rule,
 * stroked with any cap, join and width, masked, into a small image
 * framed by canary pixels. Nothing is written outside the image, every
 * pixel drawn stays premultiplied, and a mask holds what the fill
 * draws.
 *
 * The input: the image's size, a clip, a transform, the rule, the
 * stroke, then path operations to the end. A coordinate is one byte,
 * a third of a pixel from -21, or one of the bytes 0xf0..0xfa: NaN, the
 * infinities, huge and tiny values, or four bytes of a float. */
#include "../../src/gfx/draw.h"
#include "fuzz.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

const char *const sk_fuzz_dict[] = {
    "\xf0", "\xf1", "\xf2", "\xf3", "\xf4", "\xf5\xf6", "\xf7", "\xf8", "\xf9", "\xfa\xff\xff\x7f\x7f",
    "\xfa\x01\x01\x80\xff", "\x03\x40\x40\x80\x80\x80\x40\x40\x80", NULL,
};

#define PAD 4
#define CANARY 0x5a5a5a5au

typedef struct {
    const uint8_t *p;
    size_t n;
} reader;

static uint8_t byte(reader *r)
{
    if (!r->n)
        return 0;
    r->n--;
    return *r->p++;
}

static float coord(reader *r)
{
    uint8_t b = byte(r);
    switch (b) {
    case 0xf0:
        return NAN;
    case 0xf1:
        return INFINITY;
    case 0xf2:
        return -INFINITY;
    case 0xf3:
        return 1e30f;
    case 0xf4:
        return -1e30f;
    case 0xf5:
        return FLT_MAX;
    case 0xf6:
        return -FLT_MAX;
    case 0xf7:
        return 1e-30f;
    case 0xf8:
        return 4194304.0f;
    case 0xf9:
        return -5e6f;
    case 0xfa: {
        uint32_t bits = 0;
        for (int i = 0; i < 4; i++)
            bits |= (uint32_t)byte(r) << (8 * i);
        float f;
        memcpy(&f, &bits, sizeof f);
        return f;
    }
    default:
        return ((float)b - 64) / 3;
    }
}

/* The frame round the image is untouched, and every pixel in it is
 * premultiplied. */
static void check(const sk_image *canvas, int w, int h)
{
    for (int y = 0; y < canvas->h; y++)
        for (int x = 0; x < canvas->w; x++) {
            sk_px p = sk_image_row(canvas, y)[x];
            bool inside = x >= PAD && x < PAD + w && y >= PAD && y < PAD + h;
            if (!inside && p != CANARY)
                abort();
            uint32_t a = p >> 24;
            if (inside && (((p >> 16) & 255) > a || ((p >> 8) & 255) > a || (p & 255) > a))
                abort();
        }
}

/* n coordinates, read in order: the order of arguments in a call is the
 * compiler's to choose. */
static void coords(reader *r, float *c, int n)
{
    for (int i = 0; i < n; i++)
        c[i] = coord(r);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    reader r = {data, size};
    float c[6];
    uint8_t b[4];
    int w = 1 + byte(&r) % 40;
    int h = 1 + byte(&r) % 40;
    sk_rect clip_r = {0, 0, 0, 0};
    const sk_rect *clip = NULL;
    switch (byte(&r) % 4) {
    case 1:
        for (int i = 0; i < 4; i++)
            b[i] = byte(&r);
        clip_r = sk_rect_make((int8_t)b[0], (int8_t)b[1], b[2] % 64, b[3] % 64);
        clip = &clip_r;
        break;
    case 2:
        clip_r = sk_rect_make(INT_MIN, INT_MIN + (int)byte(&r), INT_MAX, INT_MAX);
        clip = &clip_r;
        break;
    default:
        break;
    }
    sk_xform m = SK_XFORM_IDENTITY;
    const sk_xform *mp = NULL;
    switch (byte(&r) % 4) {
    case 1:
        coords(&r, c, 4);
        m = (sk_xform){c[0] / 8, 0, 0, c[1] / 8, c[2], c[3]};
        mp = &m;
        break;
    case 2:
        coords(&r, c, 6);
        m = (sk_xform){c[0] / 16, c[1] / 16, c[2] / 16, c[3] / 16, c[4], c[5]};
        mp = &m;
        break;
    default:
        break;
    }
    sk_fill_rule rule = byte(&r) & 1 ? SK_FILL_EVENODD : SK_FILL_NONZERO;
    sk_cap cap = (sk_cap)(byte(&r) % 3);
    sk_join join = (sk_join)(byte(&r) % 3);
    coords(&r, c, 2);
    float width = c[0] / 4, limit = c[1] / 4;
    b[0] = byte(&r);
    b[1] = byte(&r);
    sk_color color = SK_RGBA(b[0], 90, 200, b[1]);

    sk_path *p = sk_path_new();
    while (r.n) {
        switch (byte(&r) % 9) {
        case 0:
            coords(&r, c, 2);
            sk_path_move(p, c[0], c[1]);
            break;
        case 1:
            coords(&r, c, 2);
            sk_path_line(p, c[0], c[1]);
            break;
        case 2:
            coords(&r, c, 4);
            sk_path_quad(p, c[0], c[1], c[2], c[3]);
            break;
        case 3:
            coords(&r, c, 6);
            sk_path_cubic(p, c[0], c[1], c[2], c[3], c[4], c[5]);
            break;
        case 4:
            sk_path_close(p);
            break;
        case 5:
            coords(&r, c, 4);
            sk_path_rect(p, c[0], c[1], c[2], c[3]);
            break;
        case 6:
            coords(&r, c, 5);
            sk_path_round_rect(p, c[0], c[1], c[2], c[3], c[4]);
            break;
        case 7:
            coords(&r, c, 4);
            sk_path_ellipse(p, c[0], c[1], c[2], c[3]);
            break;
        default:
            coords(&r, c, 4);
            sk_path_line(p, c[0], c[1]);
            sk_path_line(p, c[2], c[3]);
            break;
        }
    }

    sk_image canvas;
    sk_image_init(&canvas, w + 2 * PAD, h + 2 * PAD);
    for (int i = 0; i < canvas.w * canvas.h; i++)
        canvas.px[i] = CANARY;
    sk_image view = sk_image_view(&canvas, sk_rect_make(PAD, PAD, w, h));
    for (int y = 0; y < h; y++)
        memset(sk_image_row(&view, y), 0, (size_t)w * sizeof(sk_px));

    /* The fill, in white, is the mask; then in colour. */
    sk_fill_path(&view, p, mp, rule, SK_RGB(255, 255, 255), NULL);
    uint8_t *mask = sk_path_mask(p, mp, rule, w, h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (mask[y * w + x] != sk_image_row(&view, y)[x] >> 24)
                abort();
    check(&canvas, w, h);
    sk_fill_path(&view, p, mp, rule, color, clip);
    check(&canvas, w, h);

    sk_path *out = sk_path_new();
    sk_path_stroke(out, p, width, cap, join, limit);
    sk_fill_path(&view, out, mp, SK_FILL_NONZERO, color, clip);
    check(&canvas, w, h);
    sk_path_stroke(p, p, width / 2, cap, join, limit);
    sk_fill_path(&view, p, mp, rule, color, clip);
    check(&canvas, w, h);

    sk_mask mk = {mask, w, h, w};
    sk_fill_mask(&view, (int8_t)(w * 7) % 9, (int8_t)(h * 5) % 7, &mk, color, clip);
    check(&canvas, w, h);

    free(mask);
    sk_path_free(out);
    sk_path_free(p);
    sk_image_free(&canvas);
    return 0;
}
