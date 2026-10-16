/* The procedural modes' rules and physics, as test/viz-modes.test.js
 * checks go.js, gravity.js and fern.js: Go's captures, suicide, ko,
 * eyes and territory; an orbit's energy and a merger's momentum; the
 * fern's maps and its strand. */
#include "../../src/core/bus.h"
#include "../../src/viz/fern.h"
#include "../../src/viz/go.h"
#include "../../src/viz/gravity.h"
#include "../test.h"

#include <math.h>

/* Go on a 5 by 5 board. */
#define AT(x, y) ((y) * 5 + (x))
#define B SK_GO_BLACK
#define W SK_GO_WHITE

static sk_go_board board(const int (*stones)[3], int n)
{
    sk_go_board b;
    sk_go_init(&b, 5);
    for (int i = 0; i < n; i++)
        b.cells[AT(stones[i][1], stones[i][2])] = (int8_t)stones[i][0];
    return b;
}

static void test_go_capture(void)
{
    static const int one[][3] = {{W, 1, 1}, {B, 0, 1}, {B, 1, 0}, {B, 2, 1}};
    sk_go_board b = board(one, 4);
    int taken[25];
    CHECK_INT(sk_go_play(&b, AT(1, 2), B, taken), 1);
    CHECK_INT(taken[0], AT(1, 1));
    CHECK_INT(b.cells[AT(1, 1)], 0);
    static const int two[][3] = {{W, 1, 1}, {W, 2, 1}, {B, 0, 1}, {B, 3, 1}, {B, 1, 0}, {B, 2, 0}, {B, 1, 2}};
    b = board(two, 7);
    CHECK_INT(sk_go_play(&b, AT(2, 2), B, taken), 2);
    CHECK((taken[0] == AT(1, 1) && taken[1] == AT(2, 1)) || (taken[0] == AT(2, 1) && taken[1] == AT(1, 1)));
    sk_go_group g;
    sk_go_group_at(&b, AT(2, 2), &g);
    CHECK_INT(g.n_libs, 6);
}

static void test_go_suicide(void)
{
    static const int corner[][3] = {{B, 1, 0}, {B, 0, 1}};
    sk_go_board b = board(corner, 2);
    int taken[25];
    CHECK_INT(sk_go_play(&b, AT(0, 0), W, taken), -1);
    CHECK_INT(b.cells[AT(0, 0)], 0);
    static const int cap[][3] = {{W, 1, 0}, {W, 0, 1}, {B, 2, 0}, {B, 1, 1}, {B, 0, 2}};
    b = board(cap, 5);
    sk_go_board c = b;
    CHECK_INT(sk_go_play(&c, AT(0, 0), W, taken), -1);
    CHECK_INT(sk_go_play(&b, AT(0, 0), B, taken), 2);
    CHECK_INT(sk_go_play(&b, AT(0, 0), B, taken), -1);
}

static void test_go_ko(void)
{
    static const int ko[][3] = {{B, 1, 0}, {B, 0, 1}, {B, 1, 2}, {W, 1, 1}, {W, 2, 0}, {W, 3, 1}, {W, 2, 2}};
    sk_go_board b = board(ko, 7);
    int taken[25];
    CHECK_INT(sk_go_play(&b, AT(2, 1), B, taken), 1);
    CHECK_INT(taken[0], AT(1, 1));
    CHECK_INT(sk_go_play(&b, AT(1, 1), W, taken), -1);
    CHECK_INT(sk_go_play(&b, AT(4, 4), W, taken), 0);
    CHECK_INT(sk_go_play(&b, AT(0, 4), B, taken), 0);
    CHECK_INT(sk_go_play(&b, AT(1, 1), W, taken), 1);
    CHECK_INT(taken[0], AT(2, 1));
}

static void test_go_choose(void)
{
    static const int one[][3] = {{W, 1, 1}, {B, 0, 1}, {B, 1, 0}, {B, 2, 1}};
    sk_go_board b = board(one, 4);
    CHECK_INT(sk_go_choose(&b, B, SK_EV_STOP, -1, NULL), AT(1, 2));
    sk_go_board full = board(NULL, 0);
    for (int y = 0; y < 5; y++)
        for (int x = 0; x < 5; x++)
            if ((x + y) % 2 || x == 2)
                full.cells[AT(x, y)] = B;
    for (int k = 0; k < 25; k++)
        if (!full.cells[k])
            CHECK(sk_go_eye(&full, k, B));
    sk_rng r;
    sk_rng_seed(&r, 3);
    CHECK_INT(sk_go_choose(&full, B, SK_EV_TOOL, -1, &r), -1);
    sk_go_board wall = board(NULL, 0);
    for (int y = 0; y < 5; y++) {
        wall.cells[AT(1, y)] = B;
        wall.cells[AT(3, y)] = W;
    }
    int8_t own[25];
    sk_go_territory(&wall, INFINITY, own);
    CHECK(own[AT(0, 2)] == B && own[AT(1, 2)] == B && own[AT(2, 2)] == 0 && own[AT(3, 2)] == W && own[AT(4, 2)] == W);
    sk_go_territory(&wall, 4, own);
    CHECK(own[AT(0, 2)] == 0 && own[AT(1, 2)] == B && own[AT(2, 2)] == 0 && own[AT(3, 2)] == W && own[AT(4, 2)] == 0);
}

static double energy(const sk_grav_bodies *bs)
{
    const sk_grav_body *a = &bs->ptr[0], *b = &bs->ptr[1];
    double dx = a->x - b->x, dy = a->y - b->y, d = sqrt(dx * dx + dy * dy);
    return 0.5 * (a->m * (a->vx * a->vx + a->vy * a->vy) + b->m * (b->vx * b->vx + b->vy * b->vy)) -
           a->m * b->m / sqrt(d * d + 9);
}

static void test_gravity(void)
{
    sk_grav_bodies bs = {0};
    sk_grav_body sun = {0, 0, 0, 0, 1000, 1, 0, 0, 0, false, false, false, 0, 0};
    sk_grav_body planet = {100, 0, 0, sqrt(1000 / 100.0), 1, 1, 0, 0, 0, false, false, false, 0, 0};
    sk_vec_push(&bs, sun);
    sk_vec_push(&bs, planet);
    sk_grav_centre(&bs);
    double e0 = energy(&bs);
    for (int k = 0; k < 2000; k++)
        sk_grav_step(&bs, 0.25, NULL);
    CHECK(fabs(energy(&bs) / e0 - 1) < 1e-3);
    bs.len = 0;
    sk_grav_body a = {0, 0, 1, 0, 3, 1, 0, 0, 0, false, false, false, 0, 0};
    sk_grav_body b = {1, 0, -1, 2, 1, 1, 0, 0, 0, false, false, false, 0, 0};
    sk_vec_push(&bs, a);
    sk_vec_push(&bs, b);
    sk_grav_merges merged = {0};
    sk_grav_step(&bs, 0.01, &merged);
    CHECK_INT(bs.len, 1);
    CHECK_INT(merged.len, 1);
    CHECK(bs.ptr[0].m == 4);
    CHECK(fabs(bs.ptr[0].vx * 4 - 2) < 0.05 && fabs(bs.ptr[0].vy * 4 - 2) < 0.05);
    sk_vec_free(&bs);
    sk_vec_free(&merged);
}

typedef struct {
    double box[4];
} span;

static void extent(void *ud, double x, double y)
{
    span *s = ud;
    s->box[0] = SK_MIN(s->box[0], x);
    s->box[1] = SK_MAX(s->box[1], x);
    s->box[2] = SK_MIN(s->box[2], y);
    s->box[3] = SK_MAX(s->box[3], y);
}

static void test_fern_maps(void)
{
    for (int k = 0; k < SK_FERN_SPECIES; k++) {
        double sum = 0;
        for (int i = 0; i < 4; i++) {
            const double *m = sk_fern_species[k][i];
            sum += m[6];
            CHECK(fabs(m[0] * m[3] - m[1] * m[2]) < 1);
        }
        CHECK(fabs(sum - 1) < 1e-3);
        double u[4][7];
        sk_fern_unit(sk_fern_species[k], u);
        double a = u[1][0], b = u[1][1], c = u[1][2], d = u[1][3], e = u[1][4], f = u[1][5];
        CHECK(fabs(a - d) < 1e-12 && fabs(b + c) < 1e-12);
        double det = (1 - a) * (1 - a) + c * c;
        double tip[2] = {((1 - a) * e - c * f) / det, (c * e + (1 - a) * f) / det};
        CHECK(fabs(tip[0]) < 1e-9 && fabs(tip[1] - 1) < 1e-9);
        CHECK(fabs(u[0][4]) < 1e-9 && fabs(u[0][5] / (1 - u[0][3])) < 1e-9);
    }
    span s = {{INFINITY, -INFINITY, INFINITY, -INFINITY}};
    double cum[4] = {0.01, 0.86, 0.93, 1}, x = 0, y = 0;
    sk_rng r;
    sk_rng_seed(&r, 5);
    sk_fern_iterate(sk_fern_species[0], cum, 20000, &x, &y, &r, extent, &s);
    CHECK(s.box[0] > -2.3 && s.box[1] < 2.8 && s.box[2] >= 0 && s.box[3] < 10.1 && s.box[3] > 9.5);
}

static void test_fern_strand(void)
{
    double P[4 * 41];
    CHECK_INT(sk_fern_strand(0, 0, 0, 10, 1, 3, 0, NULL, NULL, P, 40), 41);
    CHECK(fabs(P[160]) < 1e-9 && fabs(P[161] + 10) < 1e-9 && P[163] == 1);
    static const double cases[][2] = {{0, 2.5}, {0.4, 1}, {0.8, -0.6}};
    for (size_t k = 0; k < SK_LEN(cases); k++) {
        double c = cases[k][0], T = cases[k][1];
        sk_fern_strand(0, 0, 0.3, 10, c, T, 0, NULL, NULL, P, 40);
        CHECK(fabs(P[162] - 0.3 - 2 * M_PI * T) < 0.05);
        double len = 0;
        for (int i = 1; i <= 40; i++)
            len += hypot(P[4 * i] - P[4 * i - 4], P[4 * i + 1] - P[4 * i - 3]);
        CHECK(fabs(len - 10) < 1e-9);
        for (int i = 1; i <= 40; i++)
            if (P[4 * i + 3] <= c)
                CHECK(fabs(P[4 * i + 2] - 0.3) < 1e-12);
    }
}

const sk_test sk_suite_viz_rules[] = {T(test_go_capture), T(test_go_suicide),  T(test_go_ko),
                                      T(test_go_choose),  T(test_gravity),     T(test_fern_maps),
                                      T(test_fern_strand), {0}};
