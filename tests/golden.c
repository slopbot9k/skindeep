#include "golden.h"

#include "test.h"

#include <stdlib.h>
#include <sys/stat.h>

static bool write_file(const char *path, const uint8_t *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    bool ok = fwrite(data, 1, n, f) == n;
    return fclose(f) == 0 && ok;
}

static void save(const char *path, const sk_image *im)
{
    size_t n = 0;
    uint8_t *q = sk_qoi_encode(im, &n);
    if (!q || !write_file(path, q, n))
        fprintf(stderr, "    cannot write %s\n", path);
    free(q);
}

static int channel_diff(sk_px a, sk_px b)
{
    int worst = 0;
    for (int s = 0; s < 32; s += 8) {
        int d = abs((int)((a >> s) & 255) - (int)((b >> s) & 255));
        worst = SK_MAX(worst, d);
    }
    return worst;
}

void sk_test_golden(const char *file, int line, const char *name, const sk_image *im, int tolerance)
{
    sk_buf path = {0};
    sk_buf_printf(&path, "tests/golden/%s.qoi", name);
    const char *update = getenv("SK_GOLDEN_UPDATE");
    if (update && *update == '1') {
        save(path.ptr, im);
        printf("    wrote %s\n", path.ptr);
        sk_buf_free(&path);
        return;
    }
    size_t n = 0;
    char *data = sk_test_read(path.ptr, &n);
    sk_image want = {0};
    bool have = data && sk_qoi_decode((const uint8_t *)data, n, (size_t)1 << 26, &want);
    free(data);
    long bad = 0;
    int worst = 0, bx = -1, by = -1;
    if (have && want.w == im->w && want.h == im->h) {
        for (int y = 0; y < im->h; y++)
            for (int x = 0; x < im->w; x++) {
                int d = channel_diff(sk_image_row(im, y)[x], sk_image_row(&want, y)[x]);
                if (d > tolerance) {
                    if (!bad)
                        bx = x, by = y;
                    bad++;
                }
                worst = SK_MAX(worst, d);
            }
    }
    if (!have || want.w != im->w || want.h != im->h || bad) {
        mkdir("build", 0755);
        mkdir("build/golden-out", 0755);
        sk_buf out = {0};
        sk_buf_printf(&out, "build/golden-out/%s.qoi", name);
        save(out.ptr, im);
        if (!have)
            sk_test_fail(file, line, "no golden image %s (SK_GOLDEN_UPDATE=1 writes it); drew %s", path.ptr, out.ptr);
        else if (want.w != im->w || want.h != im->h)
            sk_test_fail(file, line, "%s is %dx%d, drew %dx%d into %s", path.ptr, want.w, want.h, im->w, im->h,
                         out.ptr);
        else
            sk_test_fail(file, line, "%s: %ld pixels off by more than %d (worst %d, first at %d,%d); drew %s",
                         path.ptr, bad, tolerance, worst, bx, by, out.ptr);
        sk_buf_free(&out);
    }
    sk_image_free(&want);
    sk_buf_free(&path);
}
