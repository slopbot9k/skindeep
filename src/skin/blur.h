/* Gaussian blur of a standard deviation sigma in pixels, as Chromium's
 * Skia blurs shadows and filters: the true kernel below a sigma of 2,
 * three box blurs from 2 up (blur.c says how). A CSS blur radius r is
 * a sigma of r / 2. Values outside the buffer count as transparent. */
#ifndef SK_SKIN_BLUR_H
#define SK_SKIN_BLUR_H

#include "../gfx/image.h"

/* Skia's own limit: a larger sigma blurs as this one, so no blur spreads
 * a pixel further than about 1600 pixels. */
#define SK_BLUR_MAX_SIGMA 532.0f

/* An 8-bit coverage mask of w by h, in place. */
void sk_blur_a8(uint8_t *a, int w, int h, int stride, float sigma);
/* A premultiplied image, every channel, in place. */
void sk_blur_image(sk_image *im, float sigma);
/* How far, in whole pixels, the blur spreads a pixel. */
int sk_blur_reach(float sigma);

#endif
