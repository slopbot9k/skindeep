/* SVG through skinc's decoder: any bytes parsed by nanosvg and, when
 * the checks pass them, drawn by its rasterizer at the SVG's own size,
 * at twice it, and squeezed into a box of another shape. The input is
 * copied to a buffer of its own size. What must hold: the size the SVG
 * gives is within the limits, and each image drawn is the size asked
 * for and premultiplied. */
#include "../../skinc/media.h"
#include "fuzz.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

const char *const sk_fuzz_dict[] = {
    "<svg xmlns='http://www.w3.org/2000/svg' ", "width='", "height='", "viewBox='", "preserveAspectRatio='",
    "<path d='", "<rect ", "<circle ", "<ellipse ", "<line ", "<polyline ", "<polygon points='", "<g ", "</g>",
    "<defs>", "<linearGradient id='g' ", "<radialGradient id='r' ", "<stop offset='", "stop-color='",
    "gradientUnits='userSpaceOnUse' ", "gradientTransform='", "url(#g)", "url(#r)", "fill='", "stroke='",
    "stroke-width='", "stroke-dasharray='", "stroke-dashoffset='", "stroke-linejoin='round' ",
    "stroke-linecap='square' ", "stroke-miterlimit='", "fill-rule='evenodd' ", "opacity='", "transform='",
    "matrix(", "rotate(", "scale(", "translate(", "skewX(", "<style>", ".a{fill:red}", "class='a' ",
    "style='", " M", " L", " C", " Q", " S", " T", " A", " H", " V", " Z", "1e38", "-1e38", "1e-38", "nan",
    "inf", "0.0001", "99999", "%", "em", "mm", "#fff", "rgb(", "/>", "'>", "'", NULL,
};

static void check(const sk_svg *s, float w, float h, float scale)
{
    sk_image im;
    if (!sk_svg_render(s, w, h, scale, 1u << 16, &im, NULL)) {
        if (im.px || im.w || im.h)
            abort();
        return;
    }
    if (im.w != (int)round((double)w * scale) || im.h != (int)round((double)h * scale))
        abort();
    for (int y = 0; y < im.h; y++)
        for (int x = 0; x < im.w; x++) {
            sk_px p = sk_image_row(&im, y)[x];
            uint32_t a = p >> 24;
            if (((p >> 16) & 255) > a || ((p >> 8) & 255) > a || (p & 255) > a)
                abort();
        }
    sk_image_free(&im);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint8_t *exact = malloc(size ? size : 1);
    if (!exact)
        abort();
    if (size)
        memcpy(exact, data, size);
    sk_svg *s = sk_svg_parse(exact, size, NULL);
    free(exact);
    if (!s)
        return 0;
    float w, h;
    sk_svg_size(s, &w, &h);
    if (!(w > 0 && h > 0 && w <= SK_MEDIA_MAX_SIDE && h <= SK_MEDIA_MAX_SIDE))
        abort();
    /* Drawn small, so a run stays quick: at most 256 a side. */
    float k = fminf(1.0f, 128.0f / fmaxf(w, h));
    check(s, w * k, h * k, 1);
    check(s, w * k, h * k, 2);
    check(s, w * k * 1.5f + 1, h * k * 0.5f + 1, 1);
    sk_svg_free(s);
    return 0;
}
