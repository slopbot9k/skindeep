/* The panes and their drop-downs (layout.h): test/viz-layout.test.js
 * and viz-modes.test.js's checks of layout.js, and the mode table. */
#include "../../src/viz/layout.h"
#include "../../src/viz/viz.h"
#include "../test.h"

static bool within(sk_rect b, int W, int H)
{
    return b.x >= 0 && b.y >= 0 && b.x + b.w <= W && b.y + b.h <= H;
}

static double cx(sk_rect b)
{
    return b.x + b.w / 2.0;
}

static double cy(sk_rect b)
{
    return b.y + b.h / 2.0;
}

static void test_dropdown(void)
{
    static const int sizes[][2] = {{720, 460}, {320, 200}, {1200, 760}, {333, 701}};
    int m = SK_VIZ_MODES;
    for (size_t s = 0; s < SK_LEN(sizes); s++) {
        int W = sizes[s][0], H = sizes[s][1];
        for (int n = 1; n <= 6; n++)
            for (int i = 0; i < n; i++) {
                sk_rect r = sk_viz_pane_rect(i, n, W, H);
                sk_viz_menu d;
                sk_viz_menu_of(r, m, W, H, &d);
                CHECK_INT(d.n_items, m);
                sk_rect b = d.button;
                CHECK(b.x >= r.x && b.y >= r.y && b.x + b.w <= r.x + r.w && b.y + b.h <= r.y + r.h);
                for (int k = 0; k < m; k++) {
                    sk_rect row = d.items[k];
                    CHECK(within(row, W, H));
                    for (int o = k + 1; o < m; o++) {
                        sk_rect other = d.items[o];
                        CHECK(row.x + row.w <= other.x || other.x + other.w <= row.x || row.y + row.h <= other.y ||
                              other.y + other.h <= row.y);
                    }
                    sk_viz_hit hit;
                    CHECK(sk_viz_press(cx(row), cy(row), n, m, W, H, i, &hit));
                    CHECK(hit.pane == i && hit.item == k && !hit.button);
                }
                sk_viz_hit hit;
                CHECK(sk_viz_press(cx(b), cy(b), n, m, W, H, -1, &hit));
                CHECK(hit.pane == i && hit.button && hit.item == -1);
                /* Closed, a row's place is the pane's own, not the
                 * drop-down's. */
                sk_rect below = d.items[m - 1];
                CHECK(!sk_viz_press(cx(below), cy(below), n, m, W, H, -1, &hit) || hit.button);
            }
    }
    sk_viz_hit hit;
    CHECK(!sk_viz_press(-1, 5, 2, m, 720, 460, -1, &hit));
}

static void test_panes(void)
{
    sk_viz_grid g = sk_viz_grid_of(1, 720, 460);
    CHECK(g.cols == 1 && g.rows == 1);
    g = sk_viz_grid_of(2, 720, 460);
    CHECK(g.cols == 2 && g.rows == 1);
    g = sk_viz_grid_of(2, 400, 900);
    CHECK(g.cols == 1 && g.rows == 2);
    static const int sizes[][2] = {{720, 460}, {333, 701}};
    for (size_t s = 0; s < SK_LEN(sizes); s++) {
        int W = sizes[s][0], H = sizes[s][1];
        for (int n = 1; n <= 7; n++) {
            long area = 0;
            for (int i = 0; i < n; i++) {
                sk_rect r = sk_viz_pane_rect(i, n, W, H);
                area += (long)r.w * r.h;
                CHECK(within(r, W, H));
                CHECK_INT(sk_viz_pane_at(r.x, r.y, n, W, H), i);
                CHECK_INT(sk_viz_pane_at(r.x + r.w - 1, r.y + r.h - 1, n, W, H), i);
                CHECK_INT(sk_viz_pane_at(cx(r), cy(r), n, W, H), i);
            }
            g = sk_viz_grid_of(n, W, H);
            CHECK(area <= (long)W * H && (g.cols * g.rows != n || area == (long)W * H));
        }
    }
    CHECK_INT(sk_viz_pane_at(-1, 5, 2, 720, 460), -1);
}

static void test_modes(void)
{
    static const char *const ids[] = {"drift", "rings",  "plasma", "stardust",  "whirlpool", "scope",
                                      "analyzer", "fire", "superscope", "dotplane", "fern", "go",
                                      "zoom", "gravity", "gravity3d", "quantum", "carbon"};
    CHECK_INT(SK_LEN(ids), SK_VIZ_MODES);
    for (int i = 0; i < SK_VIZ_MODES; i++) {
        CHECK_STR(sk_viz_modes[i].id, ids[i]);
        CHECK_INT(sk_viz_mode_find(ids[i]), i);
        for (int k = i + 1; k < SK_VIZ_MODES; k++)
            CHECK(strcmp(sk_viz_modes[i].label, sk_viz_modes[k].label) != 0);
        CHECK_INT(sk_viz_modes[i].kind, i < 5 ? SK_VIZ_MILK : SK_VIZ_CANVAS);
    }
    CHECK_INT(sk_viz_mode_find("nope"), -1);
    CHECK_STR(sk_viz_modes[sk_viz_scope_mode()].id, "scope");
    /* A butterchurn mode has no renderer here. */
    CHECK(sk_vmode_new(0, 1) == NULL);
    CHECK(sk_vmode_new(-1, 1) == NULL);
    CHECK(sk_vmode_new(SK_VIZ_MODES, 1) == NULL);
}

const sk_test sk_suite_viz_layout[] = {T(test_dropdown), T(test_panes), T(test_modes), {0}};
