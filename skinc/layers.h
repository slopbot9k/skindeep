/* skinc's layer pass (decision 0037's "What is pre-rendered and what
 * the runtime paints"): the subtrees of a pack the runtime would paint
 * the same at every window size and split position, laid out with the
 * runtime's own code and painted with its painter at 1x and 2x, as the
 * LAYR records and QOI images of the pack. */
#ifndef SKINC_LAYERS_H
#define SKINC_LAYERS_H

#include "../src/base/base.h"
#include "../src/skin/pack.h"

/* A layer made: its LAYR record's fields and its QOI image, malloc'd. */
typedef struct {
    uint32_t el, style;
    uint8_t kind, scale;
    float x, y, ox, oy;
    uint16_t w, h;
    uint16_t slice[4];
    uint8_t *qoi;
    size_t len;
} sk_wlayer;

typedef SK_VEC(sk_wlayer) sk_wlayers;

/* What the pass found, for the tests and skinc -v: per element, the
 * kinds of layer it got as bits (1 << sk_layer_kind), 0 for none. */
typedef struct {
    uint8_t *kinds;
    uint32_t n;
} sk_layer_summary;

/* The layers of the n bytes of a pack that has none yet and passed the
 * loader, in the order of their elements, each element's by style
 * (none first), kind and scale. The same pack gives the same layers.
 * summary may be NULL. False with why in err when the runtime cannot
 * take the pack. */
bool sk_layers_make(const uint8_t *pack, size_t n, sk_wlayers *out, sk_layer_summary *summary, char *err,
                    size_t err_len);
void sk_wlayers_free(sk_wlayers *l);
void sk_layer_summary_free(sk_layer_summary *s);

#endif
