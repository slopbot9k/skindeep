/* third_party/stb's stb_image, PNG and JPEG only, compiled on its own
 * with the warnings its author's code meets (the Makefile's vendor
 * flags). Only skinc decodes a skin's images (decisions 0036 and
 * 0037): skinned never links it. */
#include "media_config.h"

#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb/stb_image.h"
