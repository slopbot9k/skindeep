/* The visualizer (decision 0015) as the C port draws it (decision 0036):
 * the modes of app/viz/modes.js, and a renderer for each Canvas2D mode
 * that draws a pane's picture from its synthesiser (synth.h) and
 * answers the pointer. The five butterchurn modes, kind SK_VIZ_MILK,
 * are drawn elsewhere; the page showed the scope for one it could not
 * draw.
 *
 * A renderer keeps its picture from frame to frame, as a canvas keeps
 * its pixels: some modes fade the last frame rather than clear it. It
 * draws in points, the CSS pixels of the JavaScript page, at the pane's
 * scale, so the picture is the same at 1x and 2x, sharper. Its random
 * numbers come from its seed, and its frame from the synthesiser, dt and
 * the pointer alone, so the same inputs draw the same pixels on every
 * machine. */
#ifndef SK_VIZ_VIZ_H
#define SK_VIZ_VIZ_H

#include "layout.h"
#include "synth.h"

typedef enum { SK_VIZ_MILK, SK_VIZ_CANVAS } sk_viz_kind;

typedef struct {
    const char *id;    /* what the menu and the record use */
    const char *label; /* what the menu shows */
    sk_viz_kind kind;
} sk_viz_mode_info;

/* Every mode, in the order the space bar cycles them and the menu lists
 * them. */
#define SK_VIZ_MODES 17
extern const sk_viz_mode_info sk_viz_modes[SK_VIZ_MODES];
/* The index of the mode with this id, or -1. */
int sk_viz_mode_find(const char *id);
/* The scope's index: what a butterchurn mode shows when it cannot be
 * drawn. */
int sk_viz_scope_mode(void);

/* The pointer's kinds: the left button's press, a move (pressed or
 * not) and the release. */
enum { SK_VIZ_DOWN, SK_VIZ_MOVE, SK_VIZ_UP };

typedef struct sk_vmode sk_vmode;

/* A renderer for mode, a Canvas2D one; NULL for a butterchurn mode or
 * an index that is no mode. seed starts its random numbers. */
sk_vmode *sk_vmode_new(int mode, uint64_t seed);
void sk_vmode_free(sk_vmode *m);
int sk_vmode_index(const sk_vmode *m);
/* One frame, dt seconds after the last, from s (stepped already), for a
 * pane w by h points at scale: the picture, round(w scale) by round(h
 * scale) pixels, valid until the next call. A new size starts the
 * picture black, as a canvas resized is cleared. */
const sk_image *sk_vmode_draw(sk_vmode *m, sk_synth *s, double dt, int w, int h, double scale);
/* The pointer at (x, y), points from the pane's top left, down while
 * the button is held: false when the mode has no answer of its own, and
 * the caller tones the synthesiser instead (sk_synth_touch), as the
 * page did. */
bool sk_vmode_pointer(sk_vmode *m, int kind, double x, double y, bool down);
bool sk_vmode_has_pointer(const sk_vmode *m);

#endif
