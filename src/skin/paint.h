/* The paint of a skin (decision 0037): a laid-out tree drawn into an
 * image at a scale, as Chromium paints the CSS of the subset. skinc
 * paints its layers with it and the runtime what changes, so the two
 * draw alike.
 *
 *   sk_paint_env env = {&layout, &fonts, &images, glyphs, NULL};
 *   sk_paint(&im, &env, root, 2, 0, 0);
 *
 * What it paints: background colours, images and linear and radial
 * gradients (repeating too) with their size, position, repeat, origin,
 * clip and blend modes; borders of any width and colour per side with
 * elliptical radii; border images in nine slices; outlines; box shadows
 * outside and inset, blurred and spread; text in its colour with its
 * shadows and decorations; opacity, mix-blend-mode and isolation;
 * filters (blur, drop-shadow, brightness, contrast, saturate,
 * grayscale, sepia, invert, opacity, hue-rotate); polygon clip-paths;
 * overflow clips, rounded; transforms, a turned or skewed subtree drawn
 * into a layer and resampled; and the SVG the widgets draw: arcs, lines
 * and circles filled and stroked with caps, joins and dashes (a dash of
 * -sk-value() the node's value long), text and images. Painting order is CSS 2.1's Appendix E: stacking contexts by
 * z-index, positioned boxes in tree order above the flow.
 *
 * Blur is Skia's (src/skin/blur.h): a true Gaussian below a sigma of
 * 2, three box blurs from there. */
#ifndef SK_SKIN_PAINT_H
#define SK_SKIN_PAINT_H

#include "layout.h"

/* The images styles and SVG name, by their pack index: an image's own
 * size in CSS px, and pixels to draw it w by h device pixels (the
 * file's own pixels, which the painter scales, or an SVG drawn at that
 * size), premultiplied. Either may be NULL, or fail: the image is then
 * not drawn. */
typedef struct {
    bool (*size)(void *data, uint32_t source, float *w, float *h);
    const sk_image *(*pixels)(void *data, uint32_t source, int w, int h);
    void *data;
} sk_pimages;

/* Blurred shadows kept from one paint to the next of one caller, so a
 * part of a frame painted again blurs nothing it blurred before: a box
 * shadow's blurred corner patch, by its shape, offset and blur. At most
 * max_bytes, the least recently used going first. One thread's. */
typedef struct sk_paint_cache sk_paint_cache;

sk_paint_cache *sk_paint_cache_new(size_t max_bytes);
void sk_paint_cache_free(sk_paint_cache *c);

typedef struct {
    const sk_layout *layout;
    const sk_lfonts *fonts;
    const sk_pimages *images; /* may be NULL */
    sk_glyph_cache *glyphs;   /* the caller's, the thread's own */
    sk_paint_cache *cache;    /* may be NULL: blur every time */
} sk_paint_env;

/* Paints box root and its subtree into dst, at scale device pixels to
 * the CSS px, the skin area's point (ox, oy) at dst's top left. root is
 * painted as a stacking context of its own. dst is drawn over, not
 * cleared. */
void sk_paint(sk_image *dst, const sk_paint_env *env, uint32_t root, float scale, float ox, float oy);
/* The same with the skin area's origin at device pixel (-px, -py) of
 * dst: a part of a frame painted again lands on the whole pixels the
 * frame's first paint put it on, whatever the scale. */
void sk_paint_px(sk_image *dst, const sk_paint_env *env, uint32_t root, float scale, int px, int py);

#endif
