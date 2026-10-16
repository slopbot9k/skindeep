/* test/skin-splits.test.js's cases on the C splits.js. */
#include "../../skinc/splits.h"
#include "../test.h"

#include <math.h>
#include <string.h>

/* A skin of 1000 x 722 at first (a 700 px skin area), 800 x 522 at
 * least, its terminal 200 px from the right and 60 from the top. */
static sk_pack_split SPLITS[2] = {{"side", SK_SPLIT_RIGHT, 150, 400}, {"band", SK_SPLIT_TOP, 60, 140}};

static sk_skin skin(void)
{
    sk_skin s;
    memset(&s, 0, sizeof s);
    s.width = 1000;
    s.height = 722;
    s.min_width = 800;
    s.min_height = 522;
    /* left 10, top 60, right 200, bottom 10 */
    s.terminal_rect.has = 1 | 2 | 8 | 16;
    s.terminal_rect.v[0] = 10;
    s.terminal_rect.v[1] = 200;
    s.terminal_rect.v[3] = 60;
    s.terminal_rect.v[4] = 10;
    s.splits = SPLITS;
    s.nsplits = 2;
    return s;
}

static void sizes(double side, double band, double W, double H, double want_side, double want_band)
{
    sk_skin s = skin();
    double saved[2] = {side, band}, out[2];
    sk_split_sizes(&s, saved, W, H, out);
    if (out[0] != want_side || out[1] != want_band)
        sk_test_fail(__FILE__, __LINE__, "sizes(%g, %g) at %g x %g: %g, %g, want %g, %g", side, band, W, H, out[0],
                     out[1], want_side, want_band);
}

static void test_saved_within_min_and_max(void)
{
    sizes(NAN, NAN, 1000, 700, 200, 60);
    sizes(300, 100, 1000, 700, 300, 100);
    sizes(9000, 0, 1000, 700, 400, 60);
    sizes(INFINITY, NAN, 1000, 700, 200, 60);
}

static void test_growth_gives_way_below_the_initial_size(void)
{
    sizes(400, 140, 1000, 700, 400, 140);
    sizes(400, 140, 950, 680, 350, 120);
    sizes(400, 140, 800, 500, 200, 60);
    sizes(150, NAN, 800, 500, 150, 60);
    sizes(400, NAN, 1400, 900, 400, 60);
    sk_skin s = skin();
    double min, max, def;
    sk_split_range(&s, &SPLITS[0], 900, 700, &min, &max, &def);
    CHECK(min == 150 && max == 300 && def == 200);
}

static sk_pack_rect rect(unsigned has, const float v[6])
{
    sk_pack_rect r;
    memset(&r, 0, sizeof r);
    r.has = (uint8_t)has;
    memcpy(r.v, v, sizeof r.v);
    for (int i = 0; i < 6; i++)
        r.follow[i] = -1;
    return r;
}

static void test_followed_keys_move(void)
{
    sk_skin s = skin();
    double at[2] = {300, 100};
    /* { right: 12, top: 60, width: 180, bottom: 10 }, width follows side,
     * top follows band. */
    sk_pack_rect r = rect(2 | 4 | 8 | 16, (const float[6]){0, 12, 180, 60, 10, 0});
    r.follow[2] = 0;
    r.follow[3] = 1;
    sk_pack_rect m = sk_split_rect(&s, &r, at);
    CHECK(m.v[1] == 12 && m.v[3] == 100 && m.v[2] == 280 && m.v[4] == 10);
    /* { left: 8, top: 54, right: 194, bottom: 4 }, right follows side. */
    sk_pack_rect w = rect(1 | 2 | 8 | 16, (const float[6]){8, 194, 0, 54, 4, 0});
    w.follow[1] = 0;
    m = sk_split_rect(&s, &w, at);
    CHECK(m.v[0] == 8 && m.v[1] == 294 && m.v[3] == 54 && m.v[4] == 4);
    sk_pack_rect still = rect(1 | 4 | 8 | 32, (const float[6]){8, 0, 10, 4, 0, 10});
    m = sk_split_rect(&s, &still, at);
    CHECK(!memcmp(m.v, still.v, sizeof m.v));
    sk_pack_rect t = sk_split_terminal(&s, at);
    CHECK(t.v[0] == 10 && t.v[3] == 100 && t.v[1] == 300 && t.v[4] == 10);
}

static void test_movable(void)
{
    sk_pack_rect rw = rect(2 | 4, (const float[6]){0});
    sk_pack_rect lw = rect(1 | 4, (const float[6]){0});
    sk_pack_rect lr = rect(1 | 2, (const float[6]){0});
    sk_pack_rect bh = rect(16 | 32, (const float[6]){0});
    CHECK(sk_split_movable(SK_SPLIT_RIGHT, 1, &rw));
    CHECK(sk_split_movable(SK_SPLIT_RIGHT, 2, &rw));
    CHECK(!sk_split_movable(SK_SPLIT_RIGHT, 2, &lw));
    CHECK(!sk_split_movable(SK_SPLIT_RIGHT, 0, &lr));
    CHECK(sk_split_movable(SK_SPLIT_BOTTOM, 5, &bh));
    CHECK(!sk_split_movable(SK_SPLIT_TOP, 5, &bh));
}

const sk_test sk_suite_skinc_splits[] = {
    T(test_saved_within_min_and_max), T(test_growth_gives_way_below_the_initial_size), T(test_followed_keys_move),
    T(test_movable),                  {0},
};
