/* What the MilkDrop tests share (tests/viz/milk_*.c): a preset run on
 * the test signal, and a golden comparison fit for a picture fed back
 * into itself. */
#ifndef SK_TEST_MILK_SHARED_H
#define SK_TEST_MILK_SHARED_H

#include "../../src/viz/milk.h"

/* preset run for frames frames of 1/60 s on sk_milk_demo_signal with
 * seed 1, into a w by h image of points at scale; blend_to, if not -1,
 * is loaded at frame blend_at with a 0.5 s blend. The caller frees it. */
sk_image sk_milk_test_run(int preset, int frames, int w, int h, int scale, int blend_to, int blend_at);
/* The same run, out[i] a copy of the picture after at[i] frames, at
 * ascending. The caller frees them. */
void sk_milk_test_frames(int preset, int w, int h, int scale, const int *at, int n, sk_image *out);

/* The pane the golden runs draw: 320 by 240 points, a quarter of a
 * small window, where the presets look as they do in the app; their
 * golden images are the middle 128 by 96 pixels. Smaller, the dots and
 * lines each frame draws cover more of the pane and fill it. */
#define SK_MILK_TEST_W 320
#define SK_MILK_TEST_H 240
sk_image sk_milk_test_middle(const sk_image *im);

/* The mean of a channel over the rectangle r of im, 0..255: shift 16
 * for red, 8 for green, 0 for blue. */
double sk_milk_test_mean(const sk_image *im, sk_rect r, int shift);

/* im against tests/golden/NAME.qoi. Each frame rounds to 8 bits and
 * the next is drawn from it, so a last-bit difference in a machine's
 * sin can move a pixel a level or two, and very rarely a wave by a
 * pixel: a pixel may be off by 8 in a channel, and one pixel in 500 by
 * more. Anything else fails as CHECK_GOLDEN does, writing what was
 * drawn to build/golden-out/. */
#define CHECK_MILK_GOLDEN(name, im) sk_milk_test_golden(__FILE__, __LINE__, (name), (im))
void sk_milk_test_golden(const char *file, int line, const char *name, const sk_image *im);

#endif
