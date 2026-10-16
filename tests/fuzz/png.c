/* PNGs through skinc's decoder: any bytes after the signature, which
 * the target writes over the first eight, go through the size check
 * and stb_image. The input is copied to a buffer of its own size, so
 * a read past it is the sanitizer's to see. What must hold: an image
 * that comes back is within the size asked for, premultiplied, and
 * its pack QOI decodes to the same pixels. */
#include "../../skinc/media.h"
#include "fuzz.h"

#include <stdlib.h>
#include <string.h>

const char *const sk_fuzz_dict[] = {
    "IHDR", "PLTE", "tRNS", "IDAT", "IEND", "CgBI", "gAMA", "\x78\x01", "\x78\x9c", "\x00\x00\x00\x0d",
    "\x08\x06\x00\x00\x00", "\x10\x02\x00\x00\x01", "\x01\x00\x00\x00", "\xff\xff", NULL,
};

#define MAX_PIXELS (1u << 20)

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    size_t n = size > 8 ? size : 8;
    uint8_t *exact = malloc(n);
    if (!exact)
        abort();
    memcpy(exact, sig, 8);
    if (size > 8)
        memcpy(exact + 8, data + 8, size - 8);
    sk_image im;
    if (sk_media_image(exact, n, MAX_PIXELS, &im, NULL)) {
        if (im.w <= 0 || im.h <= 0 || (size_t)im.w * (size_t)im.h > MAX_PIXELS)
            abort();
        for (int y = 0; y < im.h; y++)
            for (int x = 0; x < im.w; x++) {
                sk_px p = sk_image_row(&im, y)[x];
                uint32_t a = p >> 24;
                if (((p >> 16) & 255) > a || ((p >> 8) & 255) > a || (p & 255) > a)
                    abort();
            }
        size_t qn = 0;
        uint8_t *q = sk_media_qoi(&im, &qn);
        sk_image back;
        if (!q || !sk_qoi_decode(q, qn, MAX_PIXELS, &back) || back.w != im.w || back.h != im.h)
            abort();
        for (int y = 0; y < im.h; y++)
            if (memcmp(sk_image_row(&back, y), sk_image_row(&im, y), (size_t)im.w * sizeof(sk_px)) != 0)
                abort();
        sk_image_free(&back);
        free(q);
        sk_image_free(&im);
    } else if (im.px || im.w || im.h) {
        abort();
    }
    free(exact);
    return 0;
}
