/* Each tab's visualization in the tab strip (decision 0015,
 * 2026-10-15), as app/renderer/tabviz.js draws it: not the pane's mode,
 * which is unreadable at this size, but a band of the session's
 * loudness that scrolls right to left, a column every 125 ms, so a tab
 * holds the last 5.5 s. A column's height is the level the pane's
 * synthesiser eases from the same numbers, an event lights its column
 * in the scope's colour for its kind or tool class, and while the agent
 * works the band shimmers. Done, its busy state not 1 and the tab quiet
 * 2 s, the band greys and fades to a quarter and a flat green line runs
 * through it, the dashboard's colour for waiting; a tab with no busy
 * state also needs no token rate.
 *
 * The bands step from the feed's frames (vizfeed.h), the window's own
 * numbers and events, whether the window is open or not. A band is
 * drawn anew only as a column closes, and a done band whose line has
 * settled not at all. gui.c's tab strip reserves each band's rect as
 * app.css's `#tabs .viz` has it: flex 0 1 44px, at least 22px wide,
 * 16px high, on #000 with corners of 2px. A narrower rect narrows the
 * columns, as the canvas was stretched.
 *
 * Every call is made on the UI thread. */
#ifndef SK_APP_TABVIZ_H
#define SK_APP_TABVIZ_H

#include "../gfx/image.h"
#include "vizfeed.h"

/* The band in points, a column per point. */
#define SK_TABVIZ_W 44
#define SK_TABVIZ_MIN_W 22
#define SK_TABVIZ_H 16
#define SK_TABVIZ_RADIUS 2
#define SK_TABVIZ_COL_MS 125

typedef struct sk_tabviz sk_tabviz;

/* seed: the shimmer's random numbers. */
sk_tabviz *sk_tabviz_new(uint64_t seed);
void sk_tabviz_free(sk_tabviz *t);

/* Only these tabs keep a band, every window's tabs with one (a CLI or
 * a monitor tab, not the dashboard); a new one starts empty. */
void sk_tabviz_keep(sk_tabviz *t, const int *ids, size_t n);
/* A frame of the feed: each band's targets, and its events. */
void sk_tabviz_feed(sk_tabviz *t, const sk_vizfeed_msg *m);
/* dt seconds on, at most 0.1: the levels eased into the open column,
 * which closes every 125 ms. True when a band changed, so that the
 * strip is drawn again. */
bool sk_tabviz_step(sk_tabviz *t, double dt);
/* Tab id's band over r of dst (pixels), at the window's scale, clipped
 * to clip when it is not NULL. A tab with no band draws nothing. */
void sk_tabviz_draw(sk_tabviz *t, int id, sk_image *dst, sk_rect r, double scale, const sk_rect *clip);

/* For tests: how done the band is, 0 to 1 (-1 for no band), and column
 * x of 44, the oldest first: its height 0 to 1, its hue, and whether an
 * event lit it. */
double sk_tabviz_done(const sk_tabviz *t, int id);
bool sk_tabviz_column(const sk_tabviz *t, int id, int x, double *amp, double *hue, bool *lit);

#endif
