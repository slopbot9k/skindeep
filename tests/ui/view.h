/* The terminal view tests' tools: a terminal and a view of it in the
 * embedded DejaVu Sans Mono 14 px, the font of the Electron app, drawn
 * into an image the grid's size. Only the embedded faces are used, so
 * every machine draws the same pixels. */
#ifndef SK_TEST_UI_VIEW_H
#define SK_TEST_UI_VIEW_H

#include "../../src/ui/termview.h"
#include "../test.h"

typedef struct {
    sk_fontset *fonts;
    sk_glyph_cache *cache;
    sk_termview_style style;
    sk_termview *v;
    sk_term *t;
    sk_image im;
    int cols, rows, cw, ch;
} tv;

void tv_open(tv *x, int cols, int rows, double scale);
void tv_close(tv *x);
void tv_feed(tv *x, const char *s);
/* Draws into x->im with the terminal locked, as the UI does. */
sk_rect tv_draw(tv *x, const sk_termview_state *st, bool all);
/* The focused state: cursor shown, no link. */
sk_termview_state tv_focused(void);
/* The colour at device pixel (px, py), 0xRRGGBB. */
uint32_t tv_rgb(const tv *x, int px, int py);
/* How many pixels of the rectangle are not rgb. */
int tv_count_not(const tv *x, sk_rect r, uint32_t rgb);

#endif
