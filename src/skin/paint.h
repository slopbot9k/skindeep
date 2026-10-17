/* The paint of a skin (decision 0037): a laid-out tree drawn into an
 * image at a scale, as Chromium paints the CSS of the subset. skinc
 * paints its layers with it and the runtime what changes, so the two
 * draw alike.
 *
 *   sk_paint_env env = {&layout, &fonts, &images, glyphs, NULL, NULL};
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

/* ---- Layers ---- */

/* What a layer stands in for when the painter meets its node. */
typedef enum {
    SK_PART_SUBTREE, /* STATIC, STATE, SPRITE: the node and all it holds */
    SK_PART_NINE,    /* NINE: its box shadows and border, over its background */
} sk_paint_part;

/* A layer as the painter composites it (decision 0037): premultiplied
 * pixels painted at scale device pixels to the CSS px, their top left
 * (x, y) CSS px from the border box of anchor, before that box's
 * transforms. The anchor is the node itself, or for an SVG shape or
 * group the svg that holds it.
 *
 * SUBTREE: what the node's group holds before its opacity and blend,
 * which the painter applies, and before the node's own transform, which
 * it applies too: a turned needle's or knob's group, an animated
 * element, is turned with its layer.
 *
 * NINE: the box drawn ref_w by ref_h CSS px; slice[] (SK_TOP, ...)
 * device pixels at each edge are kept, the middle row and column
 * stretched over a box of any size no smaller. */
typedef struct {
    const sk_image *im;
    uint8_t kind; /* sk_layer_kind */
    float scale;
    float x, y;
    uint32_t anchor;
    float ref_w, ref_h;
    uint16_t slice[4];
} sk_player;

typedef struct {
    /* The layer node paints as for part (sk_paint_part), where it is
     * drawn at k device pixels to the CSS px; NULL: paint it. Asked
     * again for a node, it must answer alike within one paint. */
    const sk_player *(*find)(void *data, uint32_t node, uint8_t part, float k);
    void *data;
} sk_players;

typedef struct {
    const sk_layout *layout;
    const sk_lfonts *fonts;
    const sk_pimages *images; /* may be NULL */
    sk_glyph_cache *glyphs;   /* the caller's, the thread's own */
    sk_paint_cache *cache;    /* may be NULL: blur every time */
    const sk_players *layers; /* may be NULL: paint every node */
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

/* What a layer of node i holds, painted into dst (drawn over, not
 * cleared): own maps the node's own space (its border box's CSS px, or
 * an SVG node's user units), before its own transform, to dst's pixels.
 * SUBTREE: the node and all it holds as its group holds them, filters
 * and clip-path applied, opacity and blend not. NINE: its box shadows
 * and border, outside and inset, without its background. */
void sk_paint_layer(sk_image *dst, const sk_paint_env *env, uint32_t i, uint8_t part, const sk_xform *own);
/* Whether node i starts a stacking context of its own inside root's,
 * as the painter decides it: what a layer of a node that does not
 * must leave alone. */
bool sk_paint_makes_context(const sk_layout *l, uint32_t i, uint32_t root);

#endif
