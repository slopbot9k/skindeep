/* app/renderer/splits.js in C (decision 0035): a split is one edge of
 * the terminal, its size the terminal rect's offset on that edge. A
 * split grows past its default only as far as the skin area, on its
 * axis, is no smaller than at the initial window less the growth: the
 * validator's two layouts then hold at every size. A followed rect key
 * takes its split's change. The runtime lays its splits out with these. */
#ifndef SKINC_SPLITS_H
#define SKINC_SPLITS_H

#include "skin.h"

/* The rect key index (sk_pack_rect's order) of an edge, and of the
 * size an edge's split may stretch. */
int sk_split_edge_key(uint8_t edge);
int sk_split_size_key(uint8_t edge);
bool sk_split_horizontal(uint8_t edge);

/* range(): split s's min, max and default in a skin area laid out at
 * W x H. */
void sk_split_range(const sk_skin *skin, const sk_pack_split *s, double W, double H, double *min, double *max,
                    double *def);
/* sizes(): each split's size, saved[i] (NaN for none, or not finite)
 * held within its range. */
void sk_split_sizes(const sk_skin *skin, const double *saved, double W, double H, double *out);
/* rect(): r with each followed key moved by its split's change. */
sk_pack_rect sk_split_rect(const sk_skin *skin, const sk_pack_rect *r, const double *at);
/* terminal(): the terminal's rect at those sizes. */
sk_pack_rect sk_split_terminal(const sk_skin *skin, const double *at);
/* movable(): a split may move its own edge, or the size of a rect
 * anchored there. */
bool sk_split_movable(uint8_t edge, int key, const sk_pack_rect *r);

#endif
