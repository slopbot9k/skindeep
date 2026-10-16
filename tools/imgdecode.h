/* PNG, JPEG and GIF files decoded with stb_image, for the tools and
 * tests that draw a skin before skinc's own decoders land in the
 * library. Every function of stb_image stays static to this file. */
#ifndef SK_TOOLS_IMGDECODE_H
#define SK_TOOLS_IMGDECODE_H

#include "../src/gfx/image.h"

/* The file at path as a premultiplied image; false when it is not an
 * image stb_image reads or is larger than 2^26 pixels. */
bool sk_tool_decode(const char *path, sk_image *out);

#endif
