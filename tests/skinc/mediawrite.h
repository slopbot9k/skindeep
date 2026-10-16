/* Small PNG and WAV files made in code, for skinc's media tests and the
 * fuzz targets' seeds: a PNG's image data is zlib's stored blocks,
 * every row filtered with filter type row % 5 (none, sub, up, average,
 * Paeth), so the decoder undoes each. */
#ifndef SK_TESTS_SKINC_MEDIAWRITE_H
#define SK_TESTS_SKINC_MEDIAWRITE_H

#include "../../src/base/base.h"

/* A w x h PNG of colour type type (0 grey, 2 RGB, 3 palette, 4 grey
 * and alpha, 6 RGBA) and bit depth depth, whose rows are packed as PNG
 * packs them, each stride bytes of rows. plte (3 bytes an entry) and
 * trns may be NULL. Adam7 interlaced when interlace is set, which
 * takes a depth of 8 or 16. malloc'd, its size in *n. */
uint8_t *sk_test_png(int w, int h, int type, int depth, bool interlace, const uint8_t *rows, size_t stride,
                     const uint8_t *plte, size_t nplte, const uint8_t *trns, size_t ntrns, size_t *n);
/* A PNG of the signature and an IHDR alone, claiming w x h, RGBA. */
uint8_t *sk_test_png_header(uint32_t w, uint32_t h, size_t *n);

/* A WAV of format (1 PCM, 3 float, or anything to be refused), the
 * frames' bytes as given. */
uint8_t *sk_test_wav(unsigned format, unsigned channels, uint32_t rate, unsigned bits, const void *data,
                     size_t bytes, size_t *n);

#endif
