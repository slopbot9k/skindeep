/* third_party/nanosvg's parser and rasterizer, compiled on their own
 * with the warnings their author's code meets (the Makefile's vendor
 * flags). Only skinc reads a skin's SVG files (decisions 0036 and
 * 0037): skinned never links them. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include "../third_party/nanosvg/nanosvg.h"
#include "../third_party/nanosvg/nanosvgrast.h"
