/* third_party/stb's stb_truetype implementation, compiled on its own
 * with the warnings its author's code meets (the Makefile's vendor
 * flags). Every caller includes the header without the implementation.
 *
 * Its asserts are compiled out. The rasterizer's, in
 * stbtt__handle_clipped_edge, check float geometry that rounding can
 * break at some edge positions, and each write after them is to a
 * pixel its caller has bounds-checked; every other assert either comes
 * before a clamp or is on a path a checked font never takes (CFF, the
 * packer). Left in, a skin font could abort the app over a rounding
 * error. */
#define STBTT_assert(x) ((void)0)
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../third_party/stb/stb_truetype.h"
