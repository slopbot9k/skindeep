/* The panes of the visualization window (decision 0015), as
 * app/viz/layout.js lays them out: one per tab, in a grid of equal
 * panes whose column count gives each pane the longest shorter side,
 * and each pane's mode drop-down. The window draws with it and reads
 * the presses with it, so neither measures text. In points, the CSS
 * pixels of the JavaScript page; the window's size is whole points. */
#ifndef SK_VIZ_LAYOUT_H
#define SK_VIZ_LAYOUT_H

#include "../gfx/image.h"

typedef struct {
    int cols, rows;
} sk_viz_grid;

/* The grid of n panes in a w by h window. */
sk_viz_grid sk_viz_grid_of(int n, int w, int h);
/* Pane i of n, in whole points: the panes tile the window. */
sk_rect sk_viz_pane_rect(int i, int n, int w, int h);
/* The pane at (x, y), or -1 for none. */
int sk_viz_pane_at(double x, double y, int n, int w, int h);

/* A drop-down's geometry: a button at the pane's top left and, while it
 * is open, a row per mode below the button in as many columns as the
 * window's height needs, over the other panes. */
#define SK_VIZ_MENU_PAD 4
#define SK_VIZ_MENU_BUTTON_W 150
#define SK_VIZ_MENU_BUTTON_H 20
#define SK_VIZ_MENU_ROW 18
#define SK_VIZ_MENU_ITEM 160
#define SK_VIZ_MENU_MAX 32

typedef struct {
    sk_rect button;
    sk_rect items[SK_VIZ_MENU_MAX];
    int n_items;
} sk_viz_menu;

/* The drop-down of the pane at r, for m modes (at most
 * SK_VIZ_MENU_MAX), in a w by h window. */
void sk_viz_menu_of(sk_rect r, int m, int w, int h, sk_viz_menu *out);

/* What a press hits. */
typedef struct {
    int pane;
    int item;    /* the row of the open drop-down, or -1 */
    bool button; /* the pane's button */
} sk_viz_hit;

/* What a press at (x, y) hits, of n panes in a w by h window, each with
 * m modes, pane open's drop-down open (-1 for none): a row of the open
 * drop-down, or a pane's button. False for anything else. */
bool sk_viz_press(double x, double y, int n, int m, int w, int h, int open, sk_viz_hit *hit);

#endif
