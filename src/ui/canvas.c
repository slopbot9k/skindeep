/* What the atlas and the custom glyphs ask of a 2D canvas (internal.h),
 * in device pixels: shapes as coverage, cut by a rectangle anti-aliased
 * as Chromium's clip() is, painted in a colour or a shade's pattern. */
#include "internal.h"

#include <math.h>
#include <string.h>

void sk_ui_canvas_init(sk_ui_canvas *cv, int w, int h, sk_color background)
{
    memset(cv, 0, sizeof *cv);
    sk_image_init(&cv->im, w, h);
    if (background)
        sk_fill_rect(&cv->im, sk_image_bounds(&cv->im), background, SK_BLEND_SRC, NULL);
    cv->fill.c = SK_RGB(0, 0, 0);
    cv->line_width = 1;
}

void sk_ui_canvas_free(sk_ui_canvas *cv)
{
    sk_image_free(&cv->im);
}

double sk_ui_js_round(double v)
{
    double f = floor(v);
    return v - f >= 0.5 ? f + 1 : f;
}

/* How much of pixels lo..lo+1 the span a..b covers. */
static double overlap(int lo, double a, double b)
{
    double o = fmin((double)lo + 1, b) - fmax((double)lo, a);
    return o > 0 ? o : 0;
}

/* The coverage of pixel (x, y) by a clip rectangle, 0..1. */
static double clip_at(const double *r, int x, int y)
{
    return overlap(x, r[0], r[2]) * overlap(y, r[1], r[3]);
}

void sk_ui_canvas_cover(sk_ui_canvas *cv, const uint8_t *cov, sk_ui_paint paint, const double *clip)
{
    int w = cv->im.w, h = cv->im.h;
    if (!cov || w <= 0 || h <= 0)
        return;
    const sk_ui_pattern *pat = paint.pattern;
    if (!pat && !clip && !cv->clipped) {
        sk_mask mask = {cov, w, h, w};
        sk_fill_mask(&cv->im, 0, 0, &mask, paint.c, NULL);
        return;
    }
    uint8_t *m = sk_xmalloc((size_t)w * (size_t)h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            size_t i = (size_t)y * (size_t)w + (size_t)x;
            double k = cov[i];
            if (k > 0 && cv->clipped)
                k *= clip_at(cv->clip, x, y);
            if (k > 0 && clip)
                k *= clip_at(clip, x, y);
            if (k > 0 && pat && !pat->bits[(y % pat->h) * pat->w + x % pat->w])
                k = 0;
            m[i] = (uint8_t)floor(k + 0.5);
        }
    sk_mask mask = {m, w, h, w};
    sk_fill_mask(&cv->im, 0, 0, &mask, paint.c, NULL);
    free(m);
}

void sk_ui_canvas_rect(sk_ui_canvas *cv, double x, double y, double w, double h, sk_ui_paint paint)
{
    /* On whole pixels, with nothing to cut it, a rectangle covers its
     * pixels wholly: filled as it is. */
    if (!paint.pattern && !cv->clipped && x == floor(x) && y == floor(y) && w == floor(w) && h == floor(h) &&
        fabs(x) < 1e6 && fabs(y) < 1e6 && w >= 0 && h >= 0 && w < 1e6 && h < 1e6) {
        sk_fill_rect(&cv->im, sk_rect_make((int)x, (int)y, (int)w, (int)h), paint.c, SK_BLEND_SRC_OVER, NULL);
        return;
    }
    sk_path *p = sk_path_new();
    sk_path_rect(p, (float)x, (float)y, (float)w, (float)h);
    sk_ui_canvas_fill(cv, p, paint, NULL);
    sk_path_free(p);
}

void sk_ui_canvas_fill(sk_ui_canvas *cv, const sk_path *p, sk_ui_paint paint, const double *clip)
{
    uint8_t *cov = sk_path_mask(p, NULL, SK_FILL_NONZERO, cv->im.w, cv->im.h);
    sk_ui_canvas_cover(cv, cov, paint, clip);
    free(cov);
}

void sk_ui_canvas_stroke(sk_ui_canvas *cv, const sk_path *p, double width, sk_ui_paint paint, const double *clip)
{
    sk_path *outline = sk_path_new();
    sk_path_stroke(outline, p, (float)width, SK_CAP_BUTT, SK_JOIN_MITER, 10);
    sk_ui_canvas_fill(cv, outline, paint, clip);
    sk_path_free(outline);
}
