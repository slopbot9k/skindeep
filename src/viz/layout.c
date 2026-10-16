/* The panes and their drop-downs (layout.h), line for line
 * app/viz/layout.js. */
#include "layout.h"

#include "vmath.h"

#include <math.h>

sk_viz_grid sk_viz_grid_of(int n, int w, int h)
{
    sk_viz_grid best = {1, SK_MAX(1, n)};
    double side = -1;
    for (int cols = 1; cols <= SK_MAX(1, n); cols++) {
        int rows = (n + cols - 1) / cols;
        double s = SK_MIN((double)w / cols, rows ? (double)h / rows : INFINITY);
        if (s > side) {
            side = s;
            best.cols = cols;
            best.rows = rows;
        }
    }
    return best;
}

sk_rect sk_viz_pane_rect(int i, int n, int w, int h)
{
    sk_viz_grid g = sk_viz_grid_of(n, w, h);
    int c = i % g.cols, r = i / g.cols;
    int x = (int)sk_vm_round((double)c * w / g.cols), y = (int)sk_vm_round((double)r * h / g.rows);
    int x1 = (int)sk_vm_round((double)(c + 1) * w / g.cols), y1 = (int)sk_vm_round((double)(r + 1) * h / g.rows);
    return sk_rect_make(x, y, x1 - x, y1 - y);
}

static bool inside(sk_rect b, double x, double y)
{
    return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

int sk_viz_pane_at(double x, double y, int n, int w, int h)
{
    for (int i = 0; i < n; i++)
        if (inside(sk_viz_pane_rect(i, n, w, h), x, y))
            return i;
    return -1;
}

void sk_viz_menu_of(sk_rect r, int m, int w, int h, sk_viz_menu *out)
{
    m = SK_MAX(0, SK_MIN(m, SK_VIZ_MENU_MAX));
    out->button = sk_rect_make(r.x + SK_VIZ_MENU_PAD, r.y + SK_VIZ_MENU_PAD,
                               SK_MAX(0, SK_MIN(SK_VIZ_MENU_BUTTON_W, r.w - 2 * SK_VIZ_MENU_PAD)),
                               SK_VIZ_MENU_BUTTON_H);
    int top = out->button.y + out->button.h + 2;
    int fit = (int)floor((double)(h - SK_VIZ_MENU_PAD - top) / SK_VIZ_MENU_ROW);
    int rows = SK_MAX(1, SK_MIN(m, fit));
    int cols = (m + rows - 1) / rows;
    int iw = cols ? (int)floor((double)(w - 2 * SK_VIZ_MENU_PAD) / cols) : SK_VIZ_MENU_ITEM;
    iw = SK_MAX(1, SK_MIN(SK_VIZ_MENU_ITEM, iw));
    int left = SK_MAX(SK_VIZ_MENU_PAD, SK_MIN(out->button.x, w - SK_VIZ_MENU_PAD - cols * iw));
    for (int i = 0; i < m; i++)
        out->items[i] = sk_rect_make(left + i / rows * iw, top + i % rows * SK_VIZ_MENU_ROW, iw, SK_VIZ_MENU_ROW);
    out->n_items = m;
}

bool sk_viz_press(double x, double y, int n, int m, int w, int h, int open, sk_viz_hit *hit)
{
    sk_viz_menu menu;
    if (open >= 0 && open < n) {
        sk_viz_menu_of(sk_viz_pane_rect(open, n, w, h), m, w, h, &menu);
        for (int k = 0; k < menu.n_items; k++)
            if (inside(menu.items[k], x, y)) {
                *hit = (sk_viz_hit){open, k, false};
                return true;
            }
    }
    int i = sk_viz_pane_at(x, y, n, w, h);
    if (i < 0)
        return false;
    sk_viz_menu_of(sk_viz_pane_rect(i, n, w, h), m, w, h, &menu);
    if (!inside(menu.button, x, y))
        return false;
    *hit = (sk_viz_hit){i, -1, true};
    return true;
}
