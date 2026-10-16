/* gfxbench: times one frame of a window composited in software at
 * Retina 2x, 3200 by 2000 pixels (decision 0036), step by step. A
 * step's time is the median of its runs, and so is the total's.
 *
 *   gfxbench [runs]      10 runs unless told
 *
 * The steps: the frame cleared; a full-frame translucent layer blitted
 * source-over; 4000 glyph masks of 10 by 20 in an opaque colour; 200
 * rounded rectangles of 240 by 48 (radius 12), built and filled as
 * paths, half opaque and half translucent; a translucent 1600 by 1000
 * image stretched bilinearly over the whole frame. */
#include "../src/gfx/draw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define W 3200
#define H 2000
#define STEPS 5
#define GLYPHS 4000
#define RECTS 200

static const char *const names[STEPS] = {"clear", "layer", "glyphs", "rounded rects", "scaled blit"};

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

static int by_value(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static double median(double *v, int n)
{
    qsort(v, (size_t)n, sizeof *v, by_value);
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

/* A gradient with its alpha between lo and hi. */
static void gradient(sk_image *im, int w, int h, int lo, int hi)
{
    sk_image_init(im, w, h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int a = lo + (hi - lo) * ((x + y) % 256) / 255;
            sk_image_row(im, y)[x] = sk_premul(SK_RGBA(x * 255 / w, y * 255 / h, (x ^ y) & 255, a));
        }
}

int main(int argc, char **argv)
{
    int runs = argc > 1 ? atoi(argv[1]) : 10;
    if (runs < 1 || runs > 1000) {
        fprintf(stderr, "usage: gfxbench [runs]\n");
        return 2;
    }
    sk_image frame, layer, bg;
    sk_image_init(&frame, W, H);
    gradient(&layer, W, H, 40, 220);
    gradient(&bg, W / 2, H / 2, 100, 180);

    /* Glyph-like masks: rings and bars, with edges of every coverage. */
    uint8_t *masks[8];
    sk_path *p = sk_path_new(), *outline = sk_path_new();
    for (int i = 0; i < 8; i++) {
        sk_path_clear(p);
        sk_path_clear(outline);
        sk_path_ellipse(p, 1.3f + 0.1f * (float)i, 3.2f, 7.4f - 0.2f * (float)i, 12.5f);
        sk_path_rect(p, 4.5f, 1.0f + (float)i, 1.6f, 18.0f - (float)i);
        sk_path_stroke(outline, p, 1.7f, SK_CAP_BUTT, SK_JOIN_MITER, 4);
        masks[i] = sk_path_mask(outline, NULL, SK_FILL_NONZERO, 10, 20);
    }

    double (*t)[STEPS + 1] = calloc((size_t)runs, sizeof *t);
    if (!t)
        return 1;
    sk_rect all = sk_image_bounds(&frame);
    for (int run = 0; run < runs; run++) {
        double t0 = now_ms();
        sk_fill_rect(&frame, all, 0, SK_BLEND_SRC, NULL);
        double t1 = now_ms();
        sk_blit(&frame, 0, 0, &layer, all, SK_BLEND_SRC_OVER, 255, NULL);
        double t2 = now_ms();
        for (int g = 0; g < GLYPHS; g++) {
            sk_mask m = {masks[g % 8], 10, 20, 10};
            sk_fill_mask(&frame, (g % 100) * 32 + 4, (g / 100) * 50 + 8, &m, SK_RGB(230, 230, 220), NULL);
        }
        double t3 = now_ms();
        for (int r = 0; r < RECTS; r++) {
            sk_path_clear(p);
            sk_path_round_rect(p, (float)(r % 10) * 320 + 20.5f, (float)(r / 10) * 100 + 20.25f, 240, 48, 12);
            sk_color c = r % 2 ? SK_RGB(40, 90, 160) : SK_RGBA(250, 250, 250, 90);
            sk_fill_path(&frame, p, NULL, SK_FILL_NONZERO, c, NULL);
        }
        double t4 = now_ms();
        sk_blit_scaled(&frame, all, &bg, sk_image_bounds(&bg), SK_FILTER_BILINEAR, SK_BLEND_SRC_OVER, 255, NULL);
        double t5 = now_ms();
        double step[STEPS] = {t1 - t0, t2 - t1, t3 - t2, t4 - t3, t5 - t4};
        memcpy(t[run], step, sizeof step);
        t[run][STEPS] = t5 - t0;
    }

    printf("gfxbench: %dx%d, median of %d runs\n", W, H, runs);
    double *col = malloc((size_t)runs * sizeof *col);
    if (!col)
        return 1;
    for (int s = 0; s <= STEPS; s++) {
        for (int run = 0; run < runs; run++)
            col[run] = t[run][s];
        printf("  %-14s %8.2f ms\n", s < STEPS ? names[s] : "total", median(col, runs));
    }
    /* Something of the frame, so no step is optimized away. */
    uint32_t sum = 0;
    for (int y = 0; y < H; y += 7)
        for (int x = 0; x < W; x += 13)
            sum = sum * 31 + sk_image_row(&frame, y)[x];
    printf("  (frame checksum %08x)\n", sum);

    free(col);
    free(t);
    for (int i = 0; i < 8; i++)
        free(masks[i]);
    sk_path_free(p);
    sk_path_free(outline);
    sk_image_free(&frame);
    sk_image_free(&layer);
    sk_image_free(&bg);
    return 0;
}
