/* The Quantum, Gravity 3D and Carbon modes' physics, as
 * test/viz-modes.test.js, viz-gravity3d.test.js and viz-carbon.test.js
 * check quantum.js, gravity3d.js and carbon.js. */
#include "../../src/base/json.h"
#include "../../src/viz/carbon.h"
#include "../../src/viz/gravity3d.h"
#include "../../src/viz/quantum.h"
#include "../test.h"

#include <math.h>

static void test_quantum(void)
{
    sk_wave w;
    CHECK(sk_wave_init(&w, 64, 32, 0.5));
    sk_wave_packet(&w, 16, 16, 4, 5, 1.1, 0.2, 1);
    double V[64 * 32];
    for (int i = 0; i < 64 * 32; i++)
        V[i] = i % 64 >= 30 && i % 64 < 33 && abs(i / 64 - 16) > 3 ? 4 : 0.3 * sin(i);
    sk_wave_potential(&w, V);
    double n0 = sk_wave_norm(&w);
    CHECK(fabs(n0 - 1) < 5e-3);
    for (int k = 0; k < 200; k++)
        sk_wave_step(&w);
    CHECK(fabs(sk_wave_norm(&w) - n0) < 1e-9);
    sk_wave_free(&w);

    sk_wave f;
    CHECK(sk_wave_init(&f, 128, 32, 0.5));
    sk_wave_packet(&f, 16, 16, 4, 4, 1, 0, 1);
    for (int k = 0; k < 40; k++)
        sk_wave_step(&f);
    double m = 0;
    for (int i = 0; i < 128 * 32; i++)
        m += (i % 128) * (f.re[i] * f.re[i] + f.im[i] * f.im[i]);
    m /= sk_wave_norm(&f);
    CHECK(fabs(m - 36) < 0.5);
    /* A measurement draws a point where the wave is. */
    int i = sk_wave_sample(&f, 0.5);
    CHECK(abs(i % 128 - 36) < 6);
    sk_wave_free(&f);
    CHECK(!sk_wave_init(&f, 100, 32, 0.5));
}

static sk_g3_body hole(double chi, double jx, double jy, double jz)
{
    sk_g3_body h;
    memset(&h, 0, sizeof h);
    h.m = 900;
    h.hole = true;
    h.chi = chi;
    h.jx = jx;
    h.jy = jy;
    h.jz = jz;
    h.r = sk_g3_horizon(&h);
    return h;
}

/* A body of mass m on an orbit at k r_s, at scale times the circular
 * speed, inclined by inc. */
static sk_g3_body orbiter(const sk_g3_body *h, double k, double scale, double m, double inc)
{
    double r = k * sk_g3_rs(900), v = sk_g3_circular(h, r) * scale;
    sk_g3_body b;
    memset(&b, 0, sizeof b);
    b.x = r;
    b.vy = v * cos(inc);
    b.vz = v * sin(inc);
    b.m = m;
    b.r = 1;
    return b;
}

static double period(double k)
{
    sk_g3_body h = hole(0, 0, 0, 1);
    double r = k * sk_g3_rs(900);
    return 2 * M_PI * r / sk_g3_circular(&h, r);
}

static void momentum(const sk_g3_bodies *bs, double p[3])
{
    p[0] = p[1] = p[2] = 0;
    for (size_t i = 0; i < bs->len; i++) {
        p[0] += bs->ptr[i].m * bs->ptr[i].vx;
        p[1] += bs->ptr[i].m * bs->ptr[i].vy;
        p[2] += bs->ptr[i].m * bs->ptr[i].vz;
    }
}

static void test_g3_horizon(void)
{
    double RS = sk_g3_rs(900);
    sk_g3_body h = hole(0, 0, 0, 1);
    CHECK(fabs(sk_g3_horizon(&h) - RS) < 1e-9);
    h = hole(0.6, 0, 0, 1);
    CHECK(fabs(sk_g3_horizon(&h) - RS / 2 * 1.8) < 1e-9);
    h = hole(SK_G3_CHI_MAX, 0, 0, 1);
    CHECK(sk_g3_horizon(&h) > RS / 2 && sk_g3_horizon(&h) < 0.7 * RS);
    /* The circular orbits' angular momentum is least at 3 r_s. */
    h = hole(0, 0, 0, 1);
    double best = 0, least = INFINITY;
    for (double k = 2.2; k < 6; k += 0.001) {
        double r = k * RS, L = r * sk_g3_circular(&h, r);
        if (L < least) {
            least = L;
            best = k;
        }
    }
    CHECK(fabs(best - 3) < 0.002);
}

static void test_g3_orbits(void)
{
    double RS = sk_g3_rs(900);
    static const double stable[] = {3.3, 4, 6};
    for (size_t s = 0; s < SK_LEN(stable); s++) {
        double k = stable[s];
        sk_g3_bodies bs = {0};
        sk_g3_body h = hole(0, 0, 0, 1);
        sk_vec_push(&bs, h);
        sk_vec_push(&bs, orbiter(&h, k, 0.999, 1e-3, 0));
        double lo = INFINITY, hi = 0;
        for (double t = 0; t < 30 * period(k); t += 0.25) {
            sk_g3_step(&bs, 0.25, NULL, NULL);
            if (bs.len != 2)
                break;
            const sk_g3_body *a = &bs.ptr[0], *b = &bs.ptr[1];
            double d = sqrt((b->x - a->x) * (b->x - a->x) + (b->y - a->y) * (b->y - a->y) + (b->z - a->z) * (b->z - a->z)) / RS;
            lo = SK_MIN(lo, d);
            hi = SK_MAX(hi, d);
        }
        CHECK_INT(bs.len, 2);
        CHECK(lo > 3 && hi < k * 1.01);
        sk_vec_free(&bs);
    }
    static const double falling[] = {2.5, 2.9};
    for (size_t s = 0; s < SK_LEN(falling); s++) {
        double k = falling[s];
        sk_g3_bodies bs = {0};
        sk_g3_body h = hole(0, 0, 0, 1);
        sk_vec_push(&bs, h);
        sk_vec_push(&bs, orbiter(&h, k, 0.999, 1, 0));
        double p0[3], p1[3], t = 0;
        momentum(&bs, p0);
        while (bs.len == 2 && t < 30 * period(k)) {
            sk_g3_step(&bs, 0.25, NULL, NULL);
            t += 0.25;
        }
        CHECK_INT(bs.len, 1);
        CHECK(t < 5 * period(k));
        CHECK(bs.ptr[0].m == 901);
        momentum(&bs, p1);
        for (int i = 0; i < 3; i++)
            CHECK(fabs(p1[i] - p0[i]) < 1e-9);
        sk_vec_free(&bs);
    }
}

static void test_g3_swallow(void)
{
    double RS = sk_g3_rs(900);
    sk_g3_bodies bs = {0};
    sk_g3_body h = hole(0.5, 0, 0, 1), b;
    memset(&b, 0, sizeof b);
    b.x = 0.6 * RS;
    b.z = 0.1 * RS;
    b.vx = 1;
    b.vy = -2;
    b.vz = 0.5;
    b.m = 7;
    b.r = 1;
    sk_vec_push(&bs, h);
    sk_vec_push(&bs, b);
    double p0[3], p1[3];
    momentum(&bs, p0);
    sk_g3_events swallowed = {0};
    sk_g3_step(&bs, 0.25, NULL, &swallowed);
    CHECK_INT(bs.len, 1);
    CHECK_INT(swallowed.len, 1);
    CHECK(bs.ptr[0].m == 907);
    sk_g3_body grown = {0};
    grown.m = 907;
    grown.chi = bs.ptr[0].chi;
    CHECK(fabs(sk_g3_horizon(&bs.ptr[0]) - sk_g3_horizon(&grown)) < 1e-9);
    momentum(&bs, p1);
    for (int i = 0; i < 3; i++)
        CHECK(fabs(p1[i] - p0[i]) < 1e-9);
    /* A dust grain past it too. */
    sk_g3_dust dust = {0};
    sk_g3_grain g;
    memset(&g, 0, sizeof g);
    g.x = 0.5 * RS;
    sk_vec_push(&dust, g);
    int eaten[1] = {0};
    sk_g3_fall(&dust, &bs, 0.25, eaten);
    CHECK(dust.ptr[0].gone && eaten[0] == 1 && bs.ptr[0].m > 907);
    sk_vec_free(&dust);
    sk_vec_free(&swallowed);
    sk_vec_free(&bs);
}

/* The orbit's angular momentum turns about the spin axis at about 2 G J
 * / (c^2 r^3), the Lense-Thirring rate, J = chi M^2 / c. */
static void test_g3_frame_dragging(void)
{
    double k = 8, T = 10 * period(k), RS = sk_g3_rs(900), C = SK_G3_C;
    double lt = 2 * 0.9 * 900 * 900 / (C * C * C * pow(k * RS, 3)) * T;
    static const int senses[] = {1, -1, 0};
    for (size_t s = 0; s < SK_LEN(senses); s++) {
        int sense = senses[s];
        sk_g3_bodies bs = {0};
        sk_g3_body h = hole(sense ? 0.9 : 0, 0, 0, sense ? sense : 1);
        sk_vec_push(&bs, h);
        sk_vec_push(&bs, orbiter(&h, k, 1, 1e-3, M_PI / 3));
#define NODE()                                                               \
    atan2((bs.ptr[1].z - bs.ptr[0].z) * (bs.ptr[1].vx - bs.ptr[0].vx) -        \
              (bs.ptr[1].x - bs.ptr[0].x) * (bs.ptr[1].vz - bs.ptr[0].vz),     \
          (bs.ptr[1].y - bs.ptr[0].y) * (bs.ptr[1].vz - bs.ptr[0].vz) -        \
              (bs.ptr[1].z - bs.ptr[0].z) * (bs.ptr[1].vy - bs.ptr[0].vy))
        double last = NODE(), turned = 0;
        for (double t = 0; t < T; t += 0.25) {
            sk_g3_step(&bs, 0.25, NULL, NULL);
            double n = NODE();
            turned += atan2(sin(n - last), cos(n - last));
            last = n;
        }
#undef NODE
        if (!sense)
            CHECK(fabs(turned) < 1e-6);
        else
            CHECK(fabs(turned / (sense * lt) - 1) < 0.1);
        sk_vec_free(&bs);
    }
}

static void test_carbon_data(void)
{
    /* Every level's g is 2J + 1, every line's Ritz wavelength agrees
     * with NIST's to 2e-7, each level's branches sum to 1 and 1 / sum A
     * is its lifetime. */
    double worst = 0, branch[SK_CARBON_LEVELS] = {0}, A[SK_CARBON_LEVELS] = {0};
    for (int i = 0; i < SK_CARBON_LEVELS; i++)
        CHECK_INT(sk_carbon_levels[i].g, 2 * sk_carbon_levels[i].J + 1);
    for (int k = 0; k < SK_CARBON_LINES; k++) {
        const sk_carbon_line *l = &sk_carbon_lines[k];
        double nm = 1e7 / (sk_carbon_levels[l->u].E - sk_carbon_levels[l->l].E);
        worst = SK_MAX(worst, fabs(nm / l->vac - 1));
        branch[l->u] += l->branch;
        A[l->u] += l->A;
    }
    CHECK(worst < 2e-7);
    int decaying = 0;
    for (int i = 0; i < SK_CARBON_LEVELS; i++) {
        if (!A[i])
            continue;
        decaying++;
        CHECK(fabs(branch[i] - 1) < 1e-12);
        CHECK(fabs(1 / A[i] / sk_carbon_levels[i].tau - 1) < 1e-9);
    }
    CHECK_INT(decaying, SK_CARBON_LEVELS - 1);
    CHECK(fabs(sk_carbon_limit - 90820.348) < 1e-9);
    /* Every cascade, walked by the branching ratios, rests in 3P. */
    sk_rng r;
    sk_rng_seed(&r, 9);
    for (int run = 0; run < 200; run++) {
        int i = SK_CARBON_LEVELS - 1 - run % 30;
        for (int n = 0; n < 40 && !sk_carbon_ground(i); n++)
            i = sk_carbon_lines[sk_carbon_decay(i, sk_rng_next(&r))].l;
        CHECK(sk_carbon_ground(i));
        CHECK(isinf(sk_carbon_tau_screen(i)));
    }
    char name[64];
    sk_carbon_level_name(0, name, sizeof name);
    CHECK_STR(name, "2p\xc2\xb2 \xc2\xb3P\xe2\x82\x80");
}

static int level(const char *id)
{
    for (int i = 0; i < SK_CARBON_LEVELS; i++)
        if (strcmp(sk_carbon_levels[i].id, id) == 0)
            return i;
    return -1;
}

/* carbon_data.c holds the research data's numbers as they are
 * (viz-carbon.test.js): each level's energies and lifetime and each
 * line's levels, rate and branching ratio, exactly, in their order, and
 * the ionization limit. */
static double num_at(const sk_json *v, const char *key)
{
    double d = NAN;
    sk_json_as_number(sk_json_get(v, key), &d);
    return d;
}

static bool str_is(const sk_json *v, const char *key, const char *want)
{
    sk_str s = {0};
    return sk_json_as_string(sk_json_get(v, key), &s) && sk_str_eq_c(s, want);
}

static void test_carbon_data_is_the_research_data(void)
{
    size_t len;
    char *text = sk_test_read("docs/research/carbon-atom-data.json", &len);
    CHECK(text != NULL);
    if (!text)
        return;
    sk_arena a;
    sk_arena_init(&a, 0);
    const sk_json *full = sk_json_parse(&a, text, len, NULL);
    const sk_json *levels = sk_json_get(full, "levels"), *lines = sk_json_get(full, "lines");
    CHECK(levels && levels->type == SK_JSON_ARRAY && levels->array.len == SK_CARBON_LEVELS);
    CHECK(lines && lines->type == SK_JSON_ARRAY && lines->array.len == SK_CARBON_LINES);
    for (int i = 0; levels && i < SK_CARBON_LEVELS && (size_t)i < levels->array.len; i++) {
        const sk_carbon_level *l = &sk_carbon_levels[i];
        const sk_json *f = sk_json_at(levels, (size_t)i);
        const sk_json *tau = sk_json_get(f, "tau_s");
        bool ok = str_is(f, "id", l->id) && num_at(f, "energy_cm") == l->E && num_at(f, "energy_eV") == l->eV &&
                  (tau && tau->type == SK_JSON_NULL ? l->tau == 0 : num_at(f, "tau_s") == l->tau);
        if (!ok)
            sk_test_fail(__FILE__, __LINE__, "level %d, %s, is not the research data's", i, l->id);
    }
    for (int k = 0; lines && k < SK_CARBON_LINES && (size_t)k < lines->array.len; k++) {
        const sk_carbon_line *l = &sk_carbon_lines[k];
        const sk_json *f = sk_json_at(lines, (size_t)k);
        bool ok = str_is(f, "upper", sk_carbon_levels[l->u].id) && str_is(f, "lower", sk_carbon_levels[l->l].id) &&
                  num_at(f, "A_s") == l->A && num_at(f, "branch") == l->branch;
        if (!ok)
            sk_test_fail(__FILE__, __LINE__, "line %d is not the research data's", k);
    }
    CHECK(num_at(sk_json_dig(full, "ionization", "limits", "2P1/2", NULL), "energy_cm") == sk_carbon_limit);
    sk_arena_free(&a);
    free(text);
}

static void test_carbon_clocks(void)
{
    static const char *const ids[] = {"2p4p_1P_1", "2p3s_1Po_1", "2s2p3_5So_2", "2p2_1D_2"};
    for (size_t i = 0; i < SK_LEN(ids); i++) {
        int k = level(ids[i]);
        CHECK(fabs(sk_carbon_tau_screen(k) / (sk_carbon_levels[k].tau * SK_CARBON_EVENT) - 1) < 1e-12);
    }
    double r = sk_carbon_tau_screen(level("2p4p_3D_3")) / sk_carbon_tau_screen(level("2p3s_3Po_2"));
    CHECK(r > 80 && r < 90);
    CHECK(fabs(sk_carbon_light() - 56.65) < 0.01);
}

static void test_carbon_orbitals(void)
{
    CHECK(fabs(sk_carbon_mean_r(sk_carbon_table(SK_ORB_1S)) / sk_carbon_mean_r_1s - 1) < 0.015);
    CHECK(fabs(sk_carbon_mean_r(sk_carbon_table(SK_ORB_2S)) / sk_carbon_mean_r_2s - 1) < 0.015);
    CHECK(fabs(sk_carbon_mean_r(sk_carbon_table(SK_ORB_2P)) / sk_carbon_mean_r_2p - 1) < 0.015);
    for (int k = 0; k < SK_CARBON_OUTER; k++)
        CHECK(fabs(sk_carbon_mean_r(sk_carbon_table(k)) / sk_carbon_outer_e[k].mean_r - 1) < 0.01);
    /* A two-electron jump has no one-electron source. */
    int from, lf, to, lt;
    CHECK(sk_carbon_mover(level("2p4p_1P_1"), level("2p3s_1Po_1"), &from, &lf, &to, &lt));
    CHECK(lf == 1 && lt == 0);
    CHECK(!sk_carbon_mover(level("2p4p_3P_2"), level("2s2p3_3Do_3"), &from, &lf, &to, &lt));
}

static void test_carbon_patterns(void)
{
    static const int types[3][2] = {{SK_CARBON_E1, 1}, {SK_CARBON_M1, 1}, {SK_CARBON_E2, 2}};
    static const double cs[6] = {-1, -0.5, 0, 0.3, 0.9, 1};
    for (int t = 0; t < 3; t++) {
        int type = types[t][0], rank = types[t][1];
        double sums[6];
        for (int j = 0; j < 6; j++) {
            double s = 0;
            for (int q = -rank; q <= rank; q++) {
                double norm = 0;
                for (int i = 0; i < 2000; i++)
                    norm += sk_carbon_pattern(type, q, -1 + (i + 0.5) / 1000) / 1000;
                s += sk_carbon_pattern(type, q, cs[j]) / norm;
                CHECK(sk_carbon_pattern(type, q, cs[j]) <= sk_carbon_pattern_max(type, q) + 1e-12);
            }
            sums[j] = s;
        }
        for (int j = 0; j < 6; j++)
            CHECK(fabs(sums[j] - sums[0]) < 1e-3);
    }
    /* Delta M = 0 of E1, sin^2, puts 0.6875 of its detections at |cos
     * Theta| < 1/2. */
    sk_rng r;
    sk_rng_seed(&r, 4);
    int near = 0, n = 20000;
    for (int i = 0; i < n; i++) {
        double d[3];
        sk_carbon_detect_dir(SK_CARBON_E1, 0, &r, d);
        near += fabs(d[2]) < 0.5;
    }
    CHECK(fabs((double)near / n - 0.6875) < 0.02);
    /* The ground term's thermal populations at 300 K: 0.128, 0.354,
     * 0.518. */
    int count[3] = {0, 0, 0};
    for (int i = 0; i < 30000; i++)
        count[sk_carbon_thermal(sk_rng_next(&r))]++;
    CHECK(fabs(count[0] / 30000.0 - 0.128) < 0.01 && fabs(count[2] / 30000.0 - 0.518) < 0.01);
}

const sk_test sk_suite_viz_physics[] = {T(test_quantum),         T(test_g3_horizon),  T(test_g3_orbits),
                                        T(test_g3_swallow),      T(test_g3_frame_dragging), T(test_carbon_data), T(test_carbon_data_is_the_research_data),
                                        T(test_carbon_clocks),   T(test_carbon_orbitals), T(test_carbon_patterns),
                                        {0}};
