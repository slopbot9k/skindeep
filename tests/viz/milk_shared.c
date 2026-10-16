/* The MilkDrop tests' runs and golden comparison (milk_shared.h). */
#include "milk_shared.h"

#include "../golden.h"
#include "../test.h"

#include <stdlib.h>

sk_image sk_milk_test_run(int preset, int frames, int w, int h, int scale, int blend_to, int blend_at)
{
    sk_milk *m = sk_milk_new(preset, 1);
    sk_image im;
    sk_image_init(&im, w * scale, h * scale);
    float samples[SK_MILK_SAMPLES];
    for (int n = 0; n < frames; n++) {
        if (n == blend_at && blend_to >= 0)
            sk_milk_load(m, blend_to, 0.5);
        sk_milk_demo_signal(n, samples);
        sk_milk_frame(m, samples, 1.0 / 60, &im, scale);
    }
    sk_milk_free(m);
    return im;
}

void sk_milk_test_frames(int preset, int w, int h, int scale, const int *at, int n, sk_image *out)
{
    sk_milk *m = sk_milk_new(preset, 1);
    sk_image im;
    sk_image_init(&im, w * scale, h * scale);
    float samples[SK_MILK_SAMPLES];
    for (int frame = 0, i = 0; i < n; frame++) {
        sk_milk_demo_signal(frame, samples);
        sk_milk_frame(m, samples, 1.0 / 60, &im, scale);
        for (; i < n && at[i] == frame + 1; i++) {
            sk_image_init(&out[i], im.w, im.h);
            memcpy(out[i].px, im.px, (size_t)im.w * (size_t)im.h * sizeof *im.px);
        }
    }
    sk_image_free(&im);
    sk_milk_free(m);
}

sk_image sk_milk_test_middle(const sk_image *im)
{
    return sk_image_view(im, sk_rect_make((im->w - 128) / 2, (im->h - 96) / 2, 128, 96));
}

double sk_milk_test_mean(const sk_image *im, sk_rect r, int shift)
{
    r = sk_rect_intersect(r, sk_image_bounds(im));
    double sum = 0;
    for (int y = r.y; y < r.y + r.h; y++)
        for (int x = r.x; x < r.x + r.w; x++)
            sum += (sk_image_row(im, y)[x] >> shift) & 255;
    return sk_rect_empty(r) ? 0 : sum / ((double)r.w * r.h);
}

/* Whether im is near enough tests/golden/NAME.qoi, as milk_shared.h
 * has it. */
static bool near(const char *name, const sk_image *im)
{
    sk_buf path = {0};
    sk_buf_printf(&path, "tests/golden/%s.qoi", name);
    size_t n = 0;
    char *data = sk_test_read(path.ptr, &n);
    sk_buf_free(&path);
    sk_image want = {0};
    bool ok = data && sk_qoi_decode((const uint8_t *)data, n, (size_t)1 << 24, &want);
    free(data);
    if (!ok || want.w != im->w || want.h != im->h) {
        sk_image_free(&want);
        return false;
    }
    long far = 0;
    for (int y = 0; y < im->h; y++)
        for (int x = 0; x < im->w; x++) {
            sk_px a = sk_image_row(im, y)[x], b = sk_image_row(&want, y)[x];
            for (int s = 0; s < 32; s += 8)
                if (abs((int)((a >> s) & 255) - (int)((b >> s) & 255)) > 8) {
                    far++;
                    break;
                }
        }
    sk_image_free(&want);
    return far * 500 <= (long)im->w * im->h;
}

void sk_milk_test_golden(const char *file, int line, const char *name, const sk_image *im)
{
    const char *update = getenv("SK_GOLDEN_UPDATE");
    if ((update && *update == '1') || !near(name, im))
        sk_test_golden(file, line, name, im, 8);
}
