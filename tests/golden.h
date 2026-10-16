/* Golden images (decision 0036): a test draws into memory and compares
 * the image with tests/golden/NAME.qoi. A pixel may differ from the
 * golden one by tolerance in each channel, since float rounding may
 * differ between machines; every other difference fails the test, which
 * writes what it drew to build/golden-out/NAME.qoi to look at.
 *
 * SK_GOLDEN_UPDATE=1 in the environment writes the golden images
 * instead: once, when a test is new or its picture changed on purpose,
 * after looking at what it drew. */
#ifndef SK_TEST_GOLDEN_H
#define SK_TEST_GOLDEN_H

#include "../src/gfx/image.h"

#define CHECK_GOLDEN(name, im, tolerance) sk_test_golden(__FILE__, __LINE__, (name), (im), (tolerance))
void sk_test_golden(const char *file, int line, const char *name, const sk_image *im, int tolerance);
/* The same where up to ppm pixels in a million may differ by more than
 * tolerance: a whole skin, whose blurs, glyphs and resampled images
 * may round a little differently on another machine. */
#define CHECK_GOLDEN_NEAR(name, im, tolerance, ppm)                                                                    \
    sk_test_golden_near(__FILE__, __LINE__, (name), (im), (tolerance), (ppm))
void sk_test_golden_near(const char *file, int line, const char *name, const sk_image *im, int tolerance, long ppm);

#endif
