/* JPEGs through skinc's decoder: any bytes after the start of image,
 * which the target writes over the first three, go through the frame
 * header's size check, the Exif orientation and stb_image. The input
 * is copied to a buffer of its own size, so a read past it is the
 * sanitizer's to see. What must hold: an image that comes back is
 * within the size asked for and premultiplied (opaque, a JPEG). */
#include "../../skinc/media.h"
#include "fuzz.h"

#include <stdlib.h>
#include <string.h>

const char *const sk_fuzz_dict[] = {
    "\xff\xc0", "\xff\xc1", "\xff\xc2", "\xff\xc4", "\xff\xdb", "\xff\xda", "\xff\xdd", "\xff\xd9", "\xff\xe0",
    "\xff\xe1", "\xff\xee", "\xff\xd0", "Exif\0\0", "MM\0*\0\0\0\x08", "II*\0\x08\0\0\0", "\x01\x12\0\x03",
    "\x12\x01\x03\0", "Adobe", "\x11\x00", "\x22\x00", NULL,
};

#define MAX_PIXELS (1u << 20)

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    size_t n = size > 3 ? size : 3;
    uint8_t *exact = malloc(n);
    if (!exact)
        abort();
    exact[0] = 0xff, exact[1] = 0xd8, exact[2] = 0xff;
    if (size > 3)
        memcpy(exact + 3, data + 3, size - 3);
    sk_image im;
    if (sk_media_image(exact, n, MAX_PIXELS, &im, NULL)) {
        if (im.w <= 0 || im.h <= 0 || (size_t)im.w * (size_t)im.h > MAX_PIXELS)
            abort();
        for (int y = 0; y < im.h; y++)
            for (int x = 0; x < im.w; x++)
                if (sk_image_row(&im, y)[x] >> 24 != 255)
                    abort();
        sk_image_free(&im);
    } else if (im.px || im.w || im.h) {
        abort();
    }
    free(exact);
    return 0;
}
