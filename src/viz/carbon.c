/* Carbon mode (decision 0015, issue #144), app/viz/carbon.js: the
 * neutral carbon atom, C I, on NIST's levels and lines, from
 * docs/research/carbon-atom.md, the photon from
 * docs/research/carbon-photon.md, and the owner's answers of 2026-10-15
 * on #144. One site holds an ensemble of N_ATOMS atoms, one standing
 * for many. An energy field, driven by the session's activity, excites
 * them, and each jumps by Monte Carlo at NIST's rates on the event
 * clock: instantaneous jumps at random times, a quantum trajectory per
 * atom. Every jump sends a front out from the site on the light clock,
 * painted by one M component's radiation pattern, its amplitude from h
 * nu and 1/r, collapsing to one detected direction at the pane's edge,
 * which lights the spectrum. Visible fronts are drawn in their hue,
 * ultraviolet ones sharpen the picture and infrared ones refract it as
 * heat haze, by one pass over the atom's pixels (see warp). One atom of
 * the ensemble is drawn: its cloud, Monte Carlo stippling of |psi|^2 for
 * its M_L about the chosen axis z, flowing about the axis as Madelung's
 * probability current says for m != 0, the core from the Bunge 1993
 * orbitals, the outer electron from the research's quantum-defect model,
 * labelled a model; its jumps snap the cloud and flash the jump's
 * transition density as the source. Beside it the level diagram, every
 * level at its energy, the ensemble's populations, the drawn atom's
 * level lit and its last jump an arrow, the ground term's fine structure
 * in an inset. Along the bottom the spectrum. Signals: see react. The
 * pointer: see pointer. */
#include "carbon.h"

#include "internal.h"

#include <stdio.h>

/* The event clock: every NIST rate divided by EVENT, one factor, so the
 * ratios between levels stay true. The light clock: light at c / LIGHT.
 * The ground term 3P is where a cascade rests. */
#define EVENT SK_CARBON_EVENT
#define LIGHT SK_CARBON_LIGHT
/* The speed of light (exact, SI) and the atomic unit of time (CODATA
 * 2022), seconds. */
#define C_LIGHT 299792458.0
#define AU_TIME 2.4188843265864e-17
/* The ensemble, and its field: a hot discharge's electron collisions
 * into any radiating level above the present one, by statistical
 * weight, at up to FIELD_MAX a second per atom at full activity. */
#define N_ATOMS 160
#define FIELD_MAX 1.2
/* Boltzmann's constant over hc, cm^-1 per K, and the gas's
 * temperature for the ground term's thermal populations. */
#define K_CM 0.6950348004
#define T_GAS 300
/* Points per electron, core and outer, and the share drawn afresh each
 * 60th of a second. */
#define PTS_CORE 230
#define PTS_OUTER 1300
#define SEETHE 0.012
/* A Madelung angular velocity above this, radians a second on screen,
 * is clipped. */
#define SPIN_MAX 3
/* At most FRONTS in flight; the drawn amplitude 1 at R0 bohr for a 538
 * nm front; the trailing crests; the points and seconds of the source's
 * flash. */
#define FRONTS 500
#define R0 8
#define NU_REF (1 / 538.18)
#define CRESTS 3
#define SOURCE_PTS 700
#define SOURCE_T 0.4
/* The view's radius, bohr. */
#define VIEW 24

#define NL SK_CARBON_LEVELS
#define NLINES SK_CARBON_LINES
#define LV(i) (&sk_carbon_levels[i])
#define LN(k) (&sk_carbon_lines[k])

/* Text outside ASCII, in UTF-8. */
#define S_TO " \xe2\x86\x92 "
#define S_TAU "\xcf\x84"
#define S_TIMES "\xc3\x97"
#define S_A0 "a\xe2\x82\x80"
#define S_PSI "\xcf\x88"
#define S_DELTA "\xce\x94"
#define S_THETA "\xce\x98"
#define S_DOTS "\xe2\x80\xa6"
#define S_MU "\xc2\xb5"
#define S_ODD "\xe1\xb5\x92"
#define S_SQ "\xc2\xb2"
#define S_INV "\xe2\x81\xbb\xc2\xb9"

static const char *superscript(char d)
{
    switch (d) {
    case '1':
        return "\xc2\xb9";
    case '2':
        return "\xc2\xb2";
    case '3':
        return "\xc2\xb3";
    case '4':
        return "\xe2\x81\xb4";
    default:
        return "\xe2\x81\xb5";
    }
}

/* ---- The levels ---- */

bool sk_carbon_ground(int i)
{
    return strcmp(LV(i)->conf, "2s2.2p2") == 0 && strcmp(LV(i)->term, "3P") == 0;
}

double sk_carbon_tau_screen(int i)
{
    return sk_carbon_ground(i) || !LV(i)->tau ? INFINITY : LV(i)->tau * EVENT;
}

double sk_carbon_light(void)
{
    return C_LIGHT / LIGHT / (sk_carbon_bohr_nm * 1e-9);
}

/* One atomic unit of time on the light clock, seconds on screen. */
#define CLOCK_AU (AU_TIME * LIGHT)

static int L_of(int i)
{
    switch (LV(i)->term[1]) {
    case 'S':
        return 0;
    case 'P':
        return 1;
    case 'D':
        return 2;
    default:
        return 3;
    }
}

/* A line's wavelength as NIST lists it: air between 200 and 2000 nm,
 * else vacuum. */
static double wl(int k)
{
    return LN(k)->air ? LN(k)->air : LN(k)->vac;
}

static bool visible(int k)
{
    return LN(k)->band == SK_CARBON_VISIBLE && LN(k)->rgb != SK_CARBON_NO_RGB;
}

static sk_color line_color(int k)
{
    return 0xff000000u | LN(k)->rgb;
}

/* ---- Orbitals ---- */

static double fact(int n)
{
    return n <= 1 ? 1 : n * fact(n - 1);
}

/* R_nl(r) of the RHF basis: sum_j c_j N_j r^(n_j - 1) exp(-zeta_j r),
 * N_j = (2 zeta_j)^(n_j + 1/2) / sqrt((2 n_j)!). */
static double slater_s(int col, double r)
{
    double s = 0;
    for (int j = 0; j < 7; j++) {
        const double *row = sk_carbon_rhf_s[j];
        int n = (int)row[0];
        double z = row[1];
        s += row[col] * sk_vm_pow(2 * z, n + 0.5) / sqrt(fact(2 * n)) * sk_vm_pow(r, n - 1) * sk_vm_exp(-z * r);
    }
    return s;
}

static double slater_p(double r)
{
    double s = 0;
    for (int j = 0; j < 5; j++) {
        const double *row = sk_carbon_rhf_p[j];
        int n = (int)row[0];
        double z = row[1];
        s += row[2] * sk_vm_pow(2 * z, n + 0.5) / sqrt(fact(2 * n)) * sk_vm_pow(r, n - 1) * sk_vm_exp(-z * r);
    }
    return s;
}

/* The generalized Laguerre polynomial L_k^(a)(x), by the recurrence. */
static double laguerre(int k, double a, double x)
{
    if (k == 0)
        return 1;
    double l0 = 1, l1 = 1 + a - x;
    for (int j = 1; j < k; j++) {
        double l2 = ((2 * j + 1 + a - x) * l1 - (j + a) * l0) / (j + 1);
        l0 = l1;
        l1 = l2;
    }
    return l1;
}

double sk_carbon_radial_p(int key, double r)
{
    if (key == SK_ORB_1S)
        return r * slater_s(2, r);
    if (key == SK_ORB_2S)
        return r * slater_s(3, r);
    if (key == SK_ORB_2P)
        return r * slater_p(r);
    const sk_carbon_outer *t = &sk_carbon_outer_e[key];
    return sk_vm_pow(r, t->lambda + 1) * sk_vm_exp(-r / t->n_star) * laguerre(t->nr, 2 * t->lambda + 1, 2 * r / t->n_star);
}

static double reach(int key)
{
    return key >= 0 ? 6 * sk_carbon_outer_e[key].mean_r + 20 : key == SK_ORB_1S ? 8 : 20;
}

#define TABLE_N 4096
#define TABLE_M 1024

/* A radial distribution as an inverse-CDF table: q[i] is the radius
 * below which a share i / M of it lies. dens(r) is the density per unit
 * r. Built once, on a grid dense near the nucleus. */
static void radial_table(double (*dens)(const int *ud, double r), const int *ud, double rmax, double *q)
{
    static double r[TABLE_N + 1], c[TABLE_N + 1];
    double prev = 0;
    r[0] = c[0] = 0;
    for (int k = 1; k <= TABLE_N; k++) {
        r[k] = rmax * ((double)k / TABLE_N) * ((double)k / TABLE_N);
        double v = dens(ud, r[k]);
        c[k] = c[k - 1] + 0.5 * (v + prev) * (r[k] - r[k - 1]);
        prev = v;
    }
    int k = 1;
    for (int i = 0; i <= TABLE_M; i++) {
        double u = c[TABLE_N] * i / TABLE_M;
        while (k < TABLE_N && c[k] < u)
            k++;
        double lo = c[k - 1], hi = c[k];
        q[i] = r[k - 1] + (hi > lo ? (u - lo) / (hi - lo) : 0) * (r[k] - r[k - 1]);
    }
}

/* A radius drawn from the table, for u uniform on [0, 1). */
static double draw_r(const double *q, double u)
{
    double x = u * TABLE_M;
    int i = SK_MIN(TABLE_M - 1, (int)floor(x));
    return q[i] + (x - i) * (q[i + 1] - q[i]);
}

double sk_carbon_mean_r(const double *q)
{
    double s = 0;
    for (int i = 0; i < TABLE_M; i++)
        s += (q[i] + q[i + 1]) / 2;
    return s / TABLE_M;
}

static double density(const int *key, double r)
{
    double p = sk_carbon_radial_p(*key, r);
    return p * p;
}

/* The tables, made on first use: the three core orbitals, then the
 * outer terms. Only the UI thread draws, so no lock. */
static double *tables[3 + SK_CARBON_OUTER];

const double *sk_carbon_table(int key)
{
    int i = key >= 0 ? 3 + key : -key - 1;
    if (!tables[i]) {
        tables[i] = sk_xmalloc((TABLE_M + 1) * sizeof *tables[i]);
        radial_table(density, &key, reach(key), tables[i]);
    }
    return tables[i];
}

/* The electrons of a level by orbital. 2s2.2p.nl: the C II core 1s2
 * 2s2 2p with one outer electron of the model. 2s.2p3: a 2s electron
 * raised into 2p, in Bunge's ground orbitals. */
typedef struct {
    int key, count, l;
    bool outer;
    char nl[4];
} shell;

static int outer_index(int level)
{
    char key[64];
    snprintf(key, sizeof key, "%s %s", LV(level)->conf, LV(level)->term);
    for (int i = 0; i < SK_CARBON_OUTER; i++)
        if (strcmp(sk_carbon_outer_e[i].key, key) == 0)
            return i;
    return -1;
}

static int shells(int level, shell out[4])
{
    const char *conf = LV(level)->conf;
    out[0] = (shell){SK_ORB_1S, 2, 0, false, ""};
    if (strcmp(conf, "2s2.2p2") == 0) {
        out[1] = (shell){SK_ORB_2S, 2, 0, false, ""};
        out[2] = (shell){SK_ORB_2P, 2, 1, false, ""};
        return 3;
    }
    if (strcmp(conf, "2s.2p3") == 0) {
        out[1] = (shell){SK_ORB_2S, 1, 0, false, ""};
        out[2] = (shell){SK_ORB_2P, 3, 1, false, ""};
        return 3;
    }
    out[1] = (shell){SK_ORB_2S, 2, 0, false, ""};
    out[2] = (shell){SK_ORB_2P, 1, 1, false, ""};
    const char *nl = strrchr(conf, '.') + 1;
    shell o = {outer_index(level), 1, 0, true, ""};
    snprintf(o.nl, sizeof o.nl, "%s", nl);
    const char *spdf = "spdf", *at = strchr(spdf, nl[strlen(nl) - 1]);
    o.l = at ? (int)(at - spdf) : 0;
    out[3] = o;
    return 4;
}

/* The size of a level: the mean radius of its widest orbital, bohr. */
static double size_of(int level)
{
    shell sh[4];
    int n = shells(level, sh);
    double m = 0;
    for (int i = 0; i < n; i++) {
        double r = sh[i].outer                ? sk_carbon_outer_e[sh[i].key].mean_r
                   : sh[i].key == SK_ORB_1S ? sk_carbon_mean_r_1s
                   : sh[i].key == SK_ORB_2S ? sk_carbon_mean_r_2s
                                             : sk_carbon_mean_r_2p;
        m = SK_MAX(m, r);
    }
    return m;
}

bool sk_carbon_mover(int up, int low, int *from, int *lf, int *to, int *lt)
{
    shell su[4], sl[4];
    int nu = shells(up, su), nlow = shells(low, sl);
    const shell *ou = nu == 4 ? &su[3] : NULL, *ol = nlow == 4 ? &sl[3] : NULL;
    bool low_ground_conf = strcmp(LV(low)->conf, "2s2.2p2") == 0;
    if (ou && ol) {
        *from = ou->key, *lf = ou->l, *to = ol->key, *lt = ol->l;
        return true;
    }
    if (ou && low_ground_conf) {
        *from = ou->key, *lf = ou->l, *to = SK_ORB_2P, *lt = 1;
        return true;
    }
    if (strcmp(LV(up)->conf, "2s.2p3") == 0 && low_ground_conf) {
        *from = SK_ORB_2P, *lf = 1, *to = SK_ORB_2S, *lt = 0;
        return true;
    }
    if (strcmp(LV(up)->conf, "2s2.2p2") == 0 && low_ground_conf) {
        *from = SK_ORB_2P, *lf = 1, *to = SK_ORB_2P, *lt = 1;
        return true;
    }
    return false;
}

/* ---- Radiation patterns ---- */

/* The angular distribution of one M component of a line's radiation
 * about the axis, at cos(Theta) = c. E1 and M1: sin^2 for Delta M = 0,
 * (1 + cos^2) / 2 for +-1. E2 (Jackson ch. 9): sin^2 cos^2 for 0, 1 - 3
 * cos^2 + 4 cos^4 for +-1, 1 - cos^4 for +-2. Each sums over its M to a
 * constant: an unoriented atom radiates isotropically. */
double sk_carbon_pattern(int type, int q, double c)
{
    double c2 = c * c;
    int a = abs(q);
    if (type == SK_CARBON_E2)
        return a == 0 ? (1 - c2) * c2 : a == 1 ? 1 - 3 * c2 + 4 * c2 * c2 : 1 - c2 * c2;
    return a == 0 ? 1 - c2 : (1 + c2) / 2;
}

double sk_carbon_pattern_max(int type, int q)
{
    static const double e2[3] = {0.25, 2, 1};
    return type == SK_CARBON_E2 ? e2[SK_MIN(2, abs(q))] : 1;
}

static const char *pattern_name(int type, int q)
{
    int a = abs(q);
    if (type == SK_CARBON_E2)
        return a == 0 ? "sin" S_SQ S_THETA "cos" S_SQ S_THETA
               : a == 1 ? "1-3cos" S_SQ S_THETA "+4cos\xe2\x81\xb4" S_THETA
                        : "1-cos\xe2\x81\xb4" S_THETA;
    return a == 0 ? "sin" S_SQ S_THETA : "(1+cos" S_SQ S_THETA ")/2";
}

void sk_carbon_detect_dir(int type, int q, sk_rng *rand, double out[3])
{
    double top = sk_carbon_pattern_max(type, q);
    for (int i = 0; i < 1000; i++) {
        double z = 2 * sk_rng_next(rand) - 1;
        if (sk_rng_next(rand) * top <= sk_carbon_pattern(type, q, z)) {
            double ph = SK_TAU * sk_rng_next(rand), s = sqrt(1 - z * z);
            out[0] = s * sk_vm_cos(ph);
            out[1] = s * sk_vm_sin(ph);
            out[2] = z;
            return;
        }
    }
    out[0] = 1;
    out[1] = out[2] = 0;
}

/* The unnormalized associated Legendre function P_l^m(cos theta), m >=
 * 0. */
static double legendre(int l, int m, double c)
{
    double s = sqrt(SK_MAX(0, 1 - c * c));
    if (l == 0)
        return 1;
    if (l == 1)
        return m == 0 ? c : s;
    if (l == 2)
        return m == 0 ? (3 * c * c - 1) / 2 : m == 1 ? 3 * s * c : 3 * s * s;
    switch (m) {
    case 0:
        return (5 * c * c * c - 3 * c) / 2;
    case 1:
        return 1.5 * (5 * c * c - 1) * s;
    case 2:
        return 15 * c * s * s;
    default:
        return 15 * s * s * s;
    }
}

/* The m of the upper and lower orbitals for component q = m - m', the
 * smallest that the l allow. */
static void m_pair(int lf, int lt, int q, int *m, int *mt)
{
    bool have = false;
    *m = *mt = 0;
    for (int a = -lf; a <= lf; a++) {
        int b = a - q;
        if (abs(b) > lt)
            continue;
        if (!have || abs(a) + abs(b) < abs(*m) + abs(*mt)) {
            *m = a;
            *mt = b;
            have = true;
        }
    }
}

/* The source tables, by the pair of orbitals, made on first use. */
typedef struct {
    int from, to;
    double q[TABLE_M + 1];
} source_table;

static double product(const int *pair, double r)
{
    return fabs(sk_carbon_radial_p(pair[0], r) * sk_carbon_radial_p(pair[1], r));
}

/* Points of the transition density psi_f* psi_i of a jump, its source:
 * the radial part |P_f P_i| as a table, the angular part P_lf^|m| P_lt^|m'|
 * cos(q phi), the real part at one instant, by rejection. Each point
 * carries its sign: x, y, z, sign, n of them into pts. */
static void source(int from, int lf, int to, int lt, int q, int n, sk_rng *rand, float *pts)
{
    static SK_VEC(source_table *) cache;
    source_table *t = NULL;
    for (size_t i = 0; i < cache.len; i++)
        if (cache.ptr[i]->from == from && cache.ptr[i]->to == to)
            t = cache.ptr[i];
    if (!t) {
        t = sk_xmalloc(sizeof *t);
        t->from = from;
        t->to = to;
        int pair[2] = {from, to};
        radial_table(product, pair, SK_MAX(reach(from), reach(to)), t->q);
        sk_vec_push(&cache, t);
    }
    int m, mt;
    m_pair(lf, lt, q, &m, &mt);
    double top = 0;
    for (int i = 0; i <= 200; i++)
        top = SK_MAX(top, fabs(legendre(lf, abs(m), -1 + i / 100.0) * legendre(lt, abs(mt), -1 + i / 100.0)));
    for (int k = 0; k < n; k++) {
        double c = 0, ph = 0, a = 0;
        for (int i = 0; i < 200; i++) {
            c = 2 * sk_rng_next(rand) - 1;
            ph = SK_TAU * sk_rng_next(rand);
            a = legendre(lf, abs(m), c) * legendre(lt, abs(mt), c) * sk_vm_cos(q * ph);
            if (sk_rng_next(rand) * top <= fabs(a))
                break;
        }
        double r = draw_r(t->q, sk_rng_next(rand)), s = sqrt(1 - c * c);
        pts[4 * k] = (float)(r * s * sk_vm_cos(ph));
        pts[4 * k + 1] = (float)(r * s * sk_vm_sin(ph));
        pts[4 * k + 2] = (float)(r * c);
        double sign = a * sk_carbon_radial_p(from, r) * sk_carbon_radial_p(to, r);
        pts[4 * k + 3] = sign < 0 ? -1.0f : 1.0f;
    }
}

/* Each level's lines down, and its E1 lines up. */
static int *downs[NL], n_downs[NL], *ups[NL], n_ups[NL];

static void graph(void)
{
    if (downs[0] || n_downs[1])
        return;
    for (int k = 0; k < NLINES; k++) {
        n_downs[LN(k)->u]++;
        if (LN(k)->type == SK_CARBON_E1)
            n_ups[LN(k)->l]++;
    }
    for (int i = 0; i < NL; i++) {
        downs[i] = sk_xmalloc((size_t)SK_MAX(1, n_downs[i]) * sizeof *downs[i]);
        ups[i] = sk_xmalloc((size_t)SK_MAX(1, n_ups[i]) * sizeof *ups[i]);
        n_downs[i] = n_ups[i] = 0;
    }
    for (int k = 0; k < NLINES; k++) {
        downs[LN(k)->u][n_downs[LN(k)->u]++] = k;
        if (LN(k)->type == SK_CARBON_E1)
            ups[LN(k)->l][n_ups[LN(k)->l]++] = k;
    }
}

int sk_carbon_decay(int i, double u)
{
    graph();
    double s = 0;
    for (int j = 0; j < n_downs[i]; j++) {
        s += LN(downs[i][j])->branch;
        if (u < s)
            return downs[i][j];
    }
    return n_downs[i] ? downs[i][n_downs[i] - 1] : -1;
}

/* A pick from n items by weight, for u uniform on [0, 1). */
static int pick(const int *items, const double *weights, int n, double u)
{
    double total = 0;
    for (int i = 0; i < n; i++)
        total += weights[i];
    double x = u * total;
    for (int i = 0; i < n; i++) {
        x -= weights[i];
        if (x < 0)
            return items[i];
    }
    return items[n - 1];
}

int sk_carbon_thermal(double u)
{
    int items[NL], n = 0;
    double w[NL];
    for (int i = 0; i < NL; i++)
        if (sk_carbon_ground(i)) {
            items[n] = i;
            w[n++] = LV(i)->g * sk_vm_exp(-LV(i)->E / (K_CM * T_GAS));
        }
    return pick(items, w, n, u);
}

/* The E1 absorptions from level i, as line indices sorted by the upper
 * level's energy, into out; their count. */
static int absorptions(int i, int *out)
{
    graph();
    int n = n_ups[i];
    memcpy(out, ups[i], (size_t)n * sizeof *out);
    /* Stable, as JavaScript's sort is. */
    for (int a = 1; a < n; a++)
        for (int b = a; b > 0 && LV(LN(out[b - 1])->u)->E > LV(LN(out[b])->u)->E; b--) {
            int t = out[b];
            out[b] = out[b - 1];
            out[b - 1] = t;
        }
    return n;
}

/* The radiating levels above level i, those with an allowed decay:
 * where the field's collisions send an atom. */
static int targets(int i, int *out)
{
    int n = 0;
    for (int k = 0; k < NL; k++)
        if (LV(k)->tau && LV(k)->tau < 1e-6 && LV(k)->E > LV(i)->E)
            out[n++] = k;
    return n;
}

/* ---- Names and numbers ---- */

static void conf_name(const char *conf, char *out, size_t n)
{
    if (strcmp(conf, "2s2.2p2") == 0) {
        snprintf(out, n, "2p" S_SQ);
        return;
    }
    if (strcmp(conf, "2s.2p3") == 0) {
        snprintf(out, n, "2s2p\xc2\xb3");
        return;
    }
    /* conf.replace('2s2.', '').replace('.', '') */
    const char *s = strncmp(conf, "2s2.", 4) == 0 ? conf + 4 : conf;
    size_t k = 0;
    bool dropped = false;
    for (; *s && k + 1 < n; s++) {
        if (*s == '.' && !dropped) {
            dropped = true;
            continue;
        }
        out[k++] = *s;
    }
    out[k] = 0;
}

void sk_carbon_level_name(int i, char *out, size_t n)
{
    const sk_carbon_level *lv = LV(i);
    char conf[32];
    conf_name(lv->conf, conf, sizeof conf);
    bool odd = lv->term[strlen(lv->term) - 1] == '*';
    snprintf(out, n, "%s %s%c%s\xe2\x82%c", conf, superscript(lv->term[0]), lv->term[1], odd ? S_ODD : "",
             (char)(0x80 + lv->J));
}

/* JavaScript's toFixed. */
static void fixed(double x, int d, char *out, size_t n)
{
    snprintf(out, n, "%.*f", d, x);
}

/* JavaScript's toPrecision(3), for positive x. */
static void precision3(double x, char *out, size_t n)
{
    int e = 0;
    while (x >= sk_vm_pow(10, e + 1))
        e++;
    while (x < sk_vm_pow(10, e) && e > -30)
        e--;
    /* Rounded to three digits it may reach the next power. */
    double r = sk_vm_round(x / sk_vm_pow(10, e - 2));
    if (r >= 1000)
        e++;
    if (e >= 3 || e < -6) {
        snprintf(out, n, "%.2fe%s%d", x / sk_vm_pow(10, e), e < 0 ? "-" : "+", abs(e));
        return;
    }
    snprintf(out, n, "%.*f", SK_MAX(0, 2 - e), x);
}

static void wl_text(double nm, char *out, size_t n)
{
    /* Below 100000 with at most two decimals: a few characters, so the
     * unit always fits after them in out. */
    char v[20];
    if (nm < 1000) {
        fixed(nm, 1, v, sizeof v);
        snprintf(out, n, "%s nm", v);
    } else if (nm < 100000) {
        fixed(nm / 1000, nm < 10000 ? 2 : 1, v, sizeof v);
        snprintf(out, n, "%s " S_MU "m", v);
    } else {
        snprintf(out, n, "%.0f " S_MU "m", sk_vm_round(nm / 1000));
    }
}

static void time_text(double s, char *out, size_t n)
{
    char v[32];
    if (s < 1e-6) {
        precision3(s * 1e9, v, sizeof v);
        snprintf(out, n, "%s ns", v);
    } else if (s < 1e-3) {
        precision3(s * 1e6, v, sizeof v);
        snprintf(out, n, "%s " S_MU "s", v);
    } else if (s < 1) {
        precision3(s * 1e3, v, sizeof v);
        snprintf(out, n, "%s ms", v);
    } else if (s < 120) {
        precision3(s, v, sizeof v);
        snprintf(out, n, "%s s", v);
    } else if (s < 86400) {
        snprintf(out, n, "%.0f min", sk_vm_round(s / 60));
    } else {
        snprintf(out, n, "%.0f d", sk_vm_round(s / 86400));
    }
}

/* x as 1.95e7: two decimals, trailing zeros and a bare point dropped. */
static void sci(double x, char *out, size_t n)
{
    int e = 0;
    while (x >= sk_vm_pow(10, e + 1))
        e++;
    while (x < sk_vm_pow(10, e) && e > -300)
        e--;
    char v[32];
    fixed(x / sk_vm_pow(10, e), 2, v, sizeof v);
    size_t k = strlen(v);
    while (k > 0 && v[k - 1] == '0')
        v[--k] = 0;
    if (k > 0 && v[k - 1] == '.')
        v[--k] = 0;
    snprintf(out, n, "%se%d", v, e);
}

/* ---- M_L and the ensemble ---- */

/* The subshell occupations n_m of level lv's component M_L = M: the 2p
 * subshell's for m = -1, 0, 1, and the outer subshell's for m = -l to l
 * (n_o of them, 0 for none). False where the table has none (2s.2p3),
 * which is then drawn round. */
static bool occupations(int level, int M, double p[3], double o[7], int *n_o)
{
    const sk_carbon_level *lv = LV(level);
    char key[16];
    if (strcmp(lv->conf, "2s2.2p2") == 0) {
        snprintf(key, sizeof key, "2p2 %s", lv->term);
    } else if (strcmp(lv->conf, "2s.2p3") != 0) {
        shell sh[4];
        shells(level, sh);
        snprintf(key, sizeof key, "l%d L%d", sh[3].l, L_of(level));
    } else {
        return false;
    }
    const sk_carbon_angular *e = NULL;
    for (int i = 0; i < SK_CARBON_ANGULAR; i++)
        if (strcmp(sk_carbon_angular_e[i].key, key) == 0)
            e = &sk_carbon_angular_e[i];
    if (!e || abs(M) > e->L)
        return false;
    int a = abs(M);
    *n_o = e->n_o;
    for (int i = 0; i < 3; i++)
        p[i] = e->p[a][M < 0 ? 2 - i : i];
    for (int i = 0; i < e->n_o; i++)
        o[i] = e->o[a][M < 0 ? e->n_o - 1 - i : i];
    return true;
}

typedef struct {
    int lv, M;
} atom;

/* ---- The cloud ---- */

/* The largest of P_l^m(cos theta)^2, for rejection. */
static double leg_top(int l, int m)
{
    static double tops[4][4];
    static bool have[4][4];
    if (!have[l][m]) {
        double t = 0;
        for (int i = 0; i <= 400; i++) {
            double v = legendre(l, m, -1 + i / 200.0);
            t = SK_MAX(t, v * v);
        }
        tops[l][m] = t;
        have[l][m] = true;
    }
    return tops[l][m];
}

/* What a cloud's points are drawn from, by shell. */
typedef struct {
    int key, l;
    bool outer, has_occ;
    int n_occ;
    double occ[7];
} kind;

/* The electron density of a level's M_L component as points, in bohr:
 * Monte Carlo stippling of |psi|^2. */
typedef struct {
    float *pts;
    int8_t *m;
    uint8_t *kinds;
    kind kind[4];
    int n, outer_from;
    double debt;
    bool round;
} cloud;

static void place_point(cloud *c, int k, sk_rng *rand)
{
    const kind *e = &c->kind[c->kinds[k]];
    double r = draw_r(sk_carbon_table(e->key), sk_rng_next(rand));
    double z = 2 * sk_rng_next(rand) - 1;
    int m = 0;
    if (e->has_occ) {
        int items[7];
        for (int i = 0; i < e->n_occ; i++)
            items[i] = i;
        m = pick(items, e->occ, e->n_occ, sk_rng_next(rand)) - e->l;
        double top = leg_top(e->l, abs(m));
        for (int i = 0; i < 200; i++) {
            z = 2 * sk_rng_next(rand) - 1;
            double v = legendre(e->l, abs(m), z);
            if (sk_rng_next(rand) * top <= v * v)
                break;
        }
    }
    double ph = SK_TAU * sk_rng_next(rand), s = sqrt(1 - z * z);
    c->pts[3 * k] = (float)(r * s * sk_vm_cos(ph));
    c->pts[3 * k + 1] = (float)(r * s * sk_vm_sin(ph));
    c->pts[3 * k + 2] = (float)(r * z);
    c->m[k] = (int8_t)m;
}

static void cloud_free(cloud *c)
{
    free(c->pts);
    free(c->m);
    free(c->kinds);
    memset(c, 0, sizeof *c);
}

static void cloud_make(cloud *c, int level, int M, sk_rng *rand)
{
    cloud_free(c);
    double p[3], o[7];
    int n_o = 0;
    bool occ = occupations(level, M, p, o, &n_o);
    shell sh[4];
    int ns = shells(level, sh);
    int n = 0;
    for (int i = 0; i < ns; i++)
        n += sh[i].count * (sh[i].outer ? PTS_OUTER : PTS_CORE);
    c->pts = sk_xmalloc((size_t)n * 3 * sizeof *c->pts);
    c->m = sk_xmalloc((size_t)n);
    c->kinds = sk_xmalloc((size_t)n);
    c->n = n;
    c->outer_from = n;
    c->round = !occ;
    int k = 0;
    for (int i = 0; i < ns; i++) {
        kind *e = &c->kind[i];
        e->key = sh[i].key;
        e->outer = sh[i].outer;
        e->l = sh[i].outer ? sh[i].l : sh[i].key == SK_ORB_2P ? 1 : 0;
        e->has_occ = false;
        if (occ && (e->outer || sh[i].key == SK_ORB_2P)) {
            e->has_occ = !e->outer || n_o > 0;
            e->n_occ = e->outer ? n_o : 3;
            memcpy(e->occ, e->outer ? o : p, (size_t)e->n_occ * sizeof *e->occ);
        }
        int count = sh[i].count * (sh[i].outer ? PTS_OUTER : PTS_CORE);
        if (sh[i].outer)
            c->outer_from = k;
        for (int j = 0; j < count; j++, k++) {
            c->kinds[k] = (uint8_t)i;
            place_point(c, k, rand);
        }
    }
}

static void cloud_step(cloud *c, double dt, sk_rng *rand)
{
    c->debt += c->n * SEETHE * dt * 60;
    for (; c->debt >= 1; c->debt--)
        place_point(c, (int)floor(sk_rng_next(rand) * c->n), rand);
    float *p = c->pts;
    for (int k = 0; k < c->n; k++) {
        int m = c->m[k];
        if (!m)
            continue;
        double x = p[3 * k], y = p[3 * k + 1];
        double w = sk_vm_clamp(m / SK_MAX(1e-6, x * x + y * y) / CLOCK_AU, -SPIN_MAX, SPIN_MAX) * dt;
        double cs = sk_vm_cos(w), sn = sk_vm_sin(w);
        p[3 * k] = (float)(x * cs - y * sn);
        p[3 * k + 1] = (float)(x * sn + y * cs);
    }
}

/* ---- The fronts ---- */

/* A front's pattern as one of five kinds: E1 and M1 with |q| 0 or 1, E2
 * with |q| 0, 1 or 2. */
static int kind_of(int type, int q)
{
    return type == SK_CARBON_E2 ? 2 + abs(q) : SK_MIN(1, abs(q));
}

static const int KINDS[5][2] = {{SK_CARBON_E1, 0}, {SK_CARBON_E1, 1}, {SK_CARBON_E2, 0}, {SK_CARBON_E2, 1},
                                {SK_CARBON_E2, 2}};
/* The drawn amplitude's share in the direction with cos(Theta) = c, as
 * a table of 257 steps of c from -1 to 1. */
static float SHAPES[5][257];

static void make_shapes(void)
{
    if (SHAPES[0][0] || SHAPES[0][128])
        return;
    for (int k = 0; k < 5; k++)
        for (int i = 0; i < 257; i++) {
            double c = -1 + i / 128.0;
            SHAPES[k][i] = (float)sqrt(SK_MAX(0, sk_carbon_pattern(KINDS[k][0], KINDS[k][1], c) /
                                                     sk_carbon_pattern_max(KINDS[k][0], KINDS[k][1])));
        }
}

static double shape(int k, double c)
{
    return SHAPES[k][(int)sk_vm_round((c + 1) * 128)];
}

typedef struct {
    int k, q, kind;
    double t, amp, gap;
    bool vis, uv;
} front;

static front front_make(int k, int q, double t)
{
    front f = {k, q, kind_of(LN(k)->type, q), t, sqrt((1 / wl(k)) / NU_REF), 4.5 * sk_vm_pow(wl(k) / 119, 0.35),
               visible(k), wl(k) < 380};
    return f;
}

static double front_radius(const front *f, double now)
{
    return sk_carbon_light() * (now - f->t);
}

/* ---- The pane ---- */

typedef struct {
    double x, y, w, h;
} box;

typedef struct {
    box atom, diag, spec;
    double cap_x, cap_y;
    bool has_ens;
    double ens_x, ens_y;
    bool narrow, small;
    /* the diagram's frame */
    double top, bot, x0, cw;
    bool has_inset;
    double ix, iy, iw, ih, inner, span;
    double base; /* the spectrum's axis */
} regions;

#define SMALL 240

static void parts(double w, double top, double spec_y, box *atom, box *diag)
{
    box body = {0, top, w, spec_y - top - 6};
    if (w >= 1.15 * body.h) {
        double aw = sk_vm_round(SK_MIN(w * 0.52, body.h * 1.25));
        *atom = (box){0, body.y, aw, body.h};
        *diag = (box){aw, body.y, w - aw - 6, body.h};
        return;
    }
    double ah = sk_vm_round(body.h * 0.55);
    *atom = (box){0, body.y, w, ah};
    *diag = (box){0, body.y + ah, w - 6, body.h - ah};
}

/* The pane's parts, in points: the atom, the level diagram, the
 * spectrum and the captions' places. The top 24 points hold the mode's
 * drop-down at the left, the bottom 18 its label. An atom smaller than
 * SMALL a side has no room for captions over its cloud: a band of two
 * rows above it holds the jump's caption and the ensemble's. */
static void make_regions(regions *R, double w, double h)
{
    memset(R, 0, sizeof *R);
    R->narrow = w < 480;
    double bottom = 18, sh = sk_vm_round(SK_MAX(34, SK_MIN(84, h * 0.17)));
    R->spec = (box){8, h - bottom - sh, w - 16, sh};
    double top = R->narrow ? 44 : 26;
    parts(w, top, R->spec.y, &R->atom, &R->diag);
    R->small = SK_MIN(R->atom.w, R->atom.h) < SMALL;
    if (R->small)
        parts(w, top + 12, R->spec.y, &R->atom, &R->diag);
    R->cap_x = R->narrow ? 8 : 162;
    R->cap_y = R->narrow ? 36 : 14;
    R->has_ens = R->small;
    R->ens_x = 8;
    R->ens_y = R->narrow ? 48 : 32;
}

static bool inside(const box *b, double x, double y)
{
    return x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h;
}

static const char *const COLS[5] = {"1even", "1odd", "3even", "3odd", "5odd"};
static const char *const COL_NAME[5] = {"singlet", "singlet" S_ODD, "triplet", "triplet" S_ODD, "quintet" S_ODD};

static int col_of(int level)
{
    char key[8];
    snprintf(key, sizeof key, "%c%s", LV(level)->term[0], LV(level)->odd ? "odd" : "even");
    for (int c = 0; c < 5; c++)
        if (strcmp(COLS[c], key) == 0)
            return c;
    return -1;
}

/* A text's ink box in the pane's points, where its anchor is. */
typedef struct {
    char t[160];
    double gx, gy, x, y, w, h;
} tbox;

typedef SK_VEC(tbox) tboxes;

/* A drawing surface of the pane: an image, its canvas, its width in
 * points and where it sits on the pane. */
typedef struct {
    sk_image im;
    sk_vc vc;
    double w, ox, oy;
} surface;

typedef struct {
    double x, y, z, t;
    sk_color fill;
} detection;

typedef struct {
    sk_vmode base;
    surface diag, spec, grain;
    regions R;
    int ow, oh;
    double counts[NLINES], flash[NLINES];
    double now;
    atom atoms[N_ATOMS];
    double entered;
    bool have_last;
    int last_k, last_q;
    double last_waited, last_t;
    char cause[160];
    double cause_t;
    SK_VEC(front) fronts;
    SK_VEC(detection) dots;
    bool have_src, src_pts;
    float src[4 * SOURCE_PTS];
    double src_t;
    cloud cl;
    double field;
    SK_VEC(double) born;
    double yaw, pitch, held;
    bool pressed, moved;
    double px0, py0, pyaw, ppitch;
    int clicks, turns, excitations, photons, detected;
    tboxes still, said;
    /* The atom's frame on screen this frame. */
    double fcx, fcy, fk, fedge, upz, B[8];
    /* The warp's tables, and the atom's pixels before it. */
    float *ws, *wd;
    uint8_t *won;
    int wn;
    sk_px *wsrc;
    size_t wsrc_n;
} carbon;

static double rnd(carbon *c)
{
    return sk_rng_next(&c->base.rng);
}

/* ---- The ensemble's state ---- */

/* The drawn atom changed: its cloud snaps to the new level's. */
static void redraw(carbon *c, const char *why)
{
    cloud_make(&c->cl, c->atoms[0].lv, c->atoms[0].M, &c->base.rng);
    c->entered = c->now;
    if (why) {
        snprintf(c->cause, sizeof c->cause, "%s", why);
        c->cause_t = c->now;
    }
}

/* Atom i to level k by a collision, its M_L drawn anew. */
static void lift(carbon *c, int i, int k, const char *why)
{
    atom *a = &c->atoms[i];
    if (k < 0 || k == a->lv)
        return;
    int L = L_of(k);
    a->lv = k;
    a->M = (int)floor(rnd(c) * (2 * L + 1)) - L;
    c->excitations++;
    if (i == 0)
        redraw(c, why);
}

/* A pick among levels by statistical weight. */
static int pick_by_g(carbon *c, const int *set, int n)
{
    double w[NL];
    for (int i = 0; i < n; i++)
        w[i] = LV(set[i])->g;
    return pick(set, w, n, rnd(c));
}

/* The field's collision: any radiating level above, by weight g. */
static void excite(carbon *c, int i)
{
    int t[NL], n = targets(c->atoms[i].lv, t);
    if (n)
        lift(c, i, pick_by_g(c, t, n), "the field: electron collision");
}

/* A front reaches the pane's edge: it is found in one direction, drawn
 * from its pattern, and its line lights. on_screen false: it is counted
 * alone, the oldest dropped past FRONTS. */
static void detect(carbon *c, const front *f, bool on_screen)
{
    c->counts[f->k]++;
    c->flash[f->k] = 1;
    c->detected++;
    if (!on_screen)
        return;
    double dir[3];
    sk_carbon_detect_dir(LN(f->k)->type, f->q, &c->base.rng, dir);
    const double *B = c->B;
    double x = dir[0] * B[0] + dir[1] * B[1], y = dir[0] * B[2] + dir[1] * B[3] + dir[2] * B[4];
    double z = dir[0] * B[5] + dir[1] * B[6] + dir[2] * B[7];
    detection d = {c->fcx + x * c->fedge, c->fcy - y * c->fedge, z, c->now,
                   f->vis ? line_color(f->k) : SK_RGB(0xaa, 0xb3, 0xc5)};
    sk_vec_push(&c->dots, d);
    if (c->dots.len > 240)
        SK_VIZ_SHIFT(&c->dots, 1);
}

/* Atom i jumps down by line k, and its front leaves. */
static void jump(carbon *c, int i, int k)
{
    atom *a = &c->atoms[i];
    const sk_carbon_line *ln = LN(k);
    /* The component q = M_upper - M_lower, uniform among those the
     * multipole's rank and the lower level's L allow. */
    int rank = ln->type == SK_CARBON_E2 ? 2 : 1, L = L_of(ln->l), ok[5], n = 0;
    for (int q = -rank; q <= rank; q++)
        if (abs(a->M - q) <= L)
            ok[n++] = q;
    int q = n ? ok[(int)floor(rnd(c) * n)] : 0;
    a->M -= q;
    a->lv = ln->l;
    c->photons++;
    sk_vec_push(&c->born, c->now);
    sk_vec_push(&c->fronts, front_make(k, q, c->now));
    if (c->fronts.len > FRONTS) {
        front old = c->fronts.ptr[0];
        SK_VIZ_SHIFT(&c->fronts, 1);
        detect(c, &old, false);
    }
    if (i == 0) {
        int from, lf, to, lt;
        c->src_pts = sk_carbon_mover(ln->u, ln->l, &from, &lf, &to, &lt);
        if (c->src_pts)
            source(from, lf, to, lt, q, SOURCE_PTS, &c->base.rng, c->src);
        c->have_src = true;
        c->src_t = c->now;
        c->have_last = true;
        c->last_k = k;
        c->last_q = q;
        c->last_waited = c->now - c->entered;
        c->last_t = c->now;
        redraw(c, NULL);
    }
}

/* The ensemble by Gillespie's method over dt of the event clock: each
 * atom's decay at 1 / tau on screen competing with the field's
 * collisions at rate. */
static void ensemble_step(carbon *c, double dt, double rate)
{
    int t[NL];
    for (int i = 0; i < N_ATOMS; i++) {
        double left = dt;
        for (int n = 0; n < 64; n++) {
            atom *a = &c->atoms[i];
            double tau = sk_carbon_tau_screen(a->lv);
            double down = isinf(tau) ? 0 : 1 / tau;
            double up = rate > 0 && targets(a->lv, t) ? rate : 0, total = down + up;
            if (total <= 0)
                break;
            double w = -sk_vm_log(1 - rnd(c)) / total;
            if (w > left)
                break;
            left -= w;
            if (rnd(c) * total < down)
                jump(c, i, sk_carbon_decay(a->lv, rnd(c)));
            else
                excite(c, i);
        }
    }
}

/* A share of the ensemble, always the drawn atom among it. */
static bool some(carbon *c, int i, double share)
{
    return i == 0 || rnd(c) < share;
}

static void collide_into(carbon *c, const char *const *confs, int n_confs, double share, const char *why)
{
    int set[NL], n = 0;
    for (int k = 0; k < NL; k++)
        for (int j = 0; j < n_confs; j++)
            if (strcmp(LV(k)->conf, confs[j]) == 0) {
                set[n++] = k;
                break;
            }
    for (int i = 0; i < N_ATOMS; i++)
        if (some(c, i, share))
            lift(c, i, pick_by_g(c, set, n), why);
}

/* A photon absorbed from the present level, from a lamp of flat
 * spectral energy density: among the absorptions in the band of five by
 * the upper level's energy, or among all for band -1. */
static void absorb(carbon *c, int i, int band, const char *why)
{
    int all[NLINES], n = absorptions(c->atoms[i].lv, all);
    if (!n) {
        if (i == 0) {
            char name[64];
            sk_carbon_level_name(c->atoms[0].lv, name, sizeof name);
            snprintf(c->cause, sizeof c->cause, "%s: no E1 line up from %s", why, name);
            c->cause_t = c->now;
        }
        return;
    }
    const int *set = all;
    int m = n;
    if (band >= 0 && band < 5) {
        int a = band * n / 5, b = (band + 1) * n / 5;
        if (b > a) {
            set = all + a;
            m = b - a;
        }
    }
    double w[NLINES];
    for (int j = 0; j < m; j++)
        w[j] = LN(set[j])->f * wl(set[j]);
    int k = pick(set, w, m, rnd(c));
    char text[160], wv[32];
    wl_text(wl(k), wv, sizeof wv);
    snprintf(text, sizeof text, "%s: absorbed %s", why, wv);
    lift(c, i, LN(k)->u, text);
}

/* The session's events, each a pulse on a share of the ensemble, over
 * the field: see decision 0015's table. */
static void react(carbon *c, const sk_viz_event *e)
{
    if (e->kind == SK_EV_PROMPT) {
        static const char *const confs[] = {"2s2.2p.4d", "2s2.2p.5s"};
        collide_into(c, confs, 2, 0.25, "prompt: electron collision");
    } else if (e->kind == SK_EV_STOP) {
        static const char *const confs[] = {"2s2.2p.4p"};
        collide_into(c, confs, 1, 0.25, "stop: electron collision");
    } else if (e->kind == SK_EV_TOOL) {
        for (int i = 0; i < N_ATOMS; i++)
            if (some(c, i, 0.15))
                absorb(c, i, e->cls >= 0 && e->cls < 5 ? e->cls : -1, "tool call");
    } else if (e->kind == SK_EV_AGENT) {
        int quintet = -1;
        for (int k = 0; k < NL; k++)
            if (strcmp(LV(k)->id, "2s2p3_5So_2") == 0)
                quintet = k;
        for (int i = 0; i < N_ATOMS; i++)
            if (some(c, i, 0.1))
                lift(c, i, quintet, "subagent: spin-exchange collision");
    } else if (e->kind == SK_EV_COMPACT) {
        for (int i = 0; i < N_ATOMS; i++)
            lift(c, i, sk_carbon_thermal(rnd(c)), "compaction: quenched by a collision, no light");
    } else if (e->kind == SK_EV_OUTPUT) {
        for (int i = 0; i < N_ATOMS; i++)
            if (some(c, i, 0.3) && sk_carbon_ground(c->atoms[i].lv))
                lift(c, i, sk_carbon_thermal(rnd(c)), "output: a thermal collision");
    }
}

/* The field's strength from the session's activity: busy, the output
 * and the token rate, nothing while idle. */
static double strength(const sk_synth_levels *L)
{
    return sk_vm_clamp(0.45 * L->busy + 0.35 * SK_MIN(1, L->bass) + 0.2 * SK_MIN(1, L->treble), 0, 1) * (1 - L->idle);
}

/* ---- Text ---- */

/* t's ink box drawn at (x, y) on a surface w points wide at (ox, oy) of
 * the pane: a text that would run off its surface is moved in. */
static tbox box_of(sk_vc *g, double w, const char *t, double x, double y, double ox, double oy)
{
    sk_vc_metrics m;
    sk_vc_measure(g, t, &m);
    double l = m.left, r = m.right;
    if (x - l < 1)
        x = 1 + l;
    else if (x + r > w - 1)
        x = SK_MAX(1 + l, w - 1 - r);
    tbox b;
    snprintf(b.t, sizeof b.t, "%s", t);
    b.gx = x;
    b.gy = y;
    b.x = ox + x - l;
    b.y = oy + y - m.ascent;
    b.w = l + r;
    b.h = m.ascent + m.descent;
    return b;
}

/* A text placed only where it fits overlaps no text placed before it,
 * with a point to spare. */
static bool fits(const carbon *c, const tbox *b)
{
    const tboxes *lists[2] = {&c->still, &c->said};
    for (int l = 0; l < 2; l++)
        for (size_t i = 0; i < lists[l]->len; i++) {
            const tbox *o = &lists[l]->ptr[i];
            if (b->x < o->x + o->w + 1 && o->x < b->x + b->w + 1 && b->y < o->y + o->h + 1 && o->y < b->y + b->h + 1)
                return false;
        }
    return true;
}

static void write_box(sk_vc *g, const tbox *b, tboxes *list, sk_color fill)
{
    sk_vc_text(g, b->t, b->gx, b->gy, fill);
    sk_vec_push(list, *b);
}

static void say(sk_vc *g, double w, const char *t, double x, double y, double ox, double oy, tboxes *list,
                sk_color fill)
{
    tbox b = box_of(g, w, t, x, y, ox, oy);
    write_box(g, &b, list, fill);
}

/* t at the first of the places where it fits, or not at all; whether
 * it drew. */
static bool place(carbon *c, sk_vc *g, const char *t, const double (*at)[2], int n, sk_color fill)
{
    for (int i = 0; i < n; i++) {
        tbox b = box_of(g, c->base.w, t, at[i][0], at[i][1], 0, 0);
        if (fits(c, &b)) {
            write_box(g, &b, &c->said, fill);
            return true;
        }
    }
    return false;
}

/* t cut with an ellipsis to fit w points, or as it is. */
static void clip_text(sk_vc *g, const char *t, double w, char *out, size_t n)
{
    sk_vc_metrics m;
    sk_vc_measure(g, t, &m);
    snprintf(out, n, "%s", t);
    if (m.width <= w)
        return;
    size_t len = strlen(out);
    char tmp[512];
    for (;;) {
        /* Characters, not bytes. */
        size_t chars = 0;
        for (size_t i = 0; i < len; i++)
            chars += (out[i] & 0xc0) != 0x80;
        snprintf(tmp, sizeof tmp, "%.*s" S_DOTS, (int)len, out);
        sk_vc_measure(g, tmp, &m);
        if (chars <= 4 || m.width <= w)
            break;
        do
            len--;
        while (len > 0 && (out[len] & 0xc0) == 0x80);
        out[len] = 0;
    }
    snprintf(out + len, n - len, S_DOTS);
}

/* ---- Drawing ---- */

static double y_of(const carbon *c, double E)
{
    return c->R.bot - (c->R.bot - c->R.top) * E / sk_carbon_limit;
}

static double col_x(const carbon *c, int col)
{
    return c->R.x0 + col * c->R.cw;
}

/* The pane's background. */
#define BACKGROUND SK_RGB(0x03, 0x05, 0x0a)

/* An opaque surface onto the pane at (x, y) points, replacing what is
 * there, within the clip. */
static void blit_opaque(sk_vc *g, const sk_image *im, double x, double y)
{
    double px, py;
    sk_vc_apply(g, x, y, &px, &py);
    sk_blit(g->im, (int)floor(px + 0.5), (int)floor(py + 0.5), im, sk_image_bounds(im), SK_BLEND_SRC, 255,
            &g->st.clip);
}

static void surface_make(surface *s, double w, double h, double scale, double ox, double oy)
{
    sk_image_free(&s->im);
    sk_image_init(&s->im, (int)floor(SK_MAX(1, w) * scale + 0.5), (int)floor(SK_MAX(1, h) * scale + 0.5));
    if (!s->vc.path)
        sk_vc_init(&s->vc, &s->im, scale);
    sk_vc_target(&s->vc, &s->im, scale);
    s->w = SK_MAX(1, w);
    s->ox = ox;
    s->oy = oy;
}

static void surface_free(surface *s)
{
    if (s->vc.path)
        sk_vc_free(&s->vc);
    sk_image_free(&s->im);
}

static void stroke_line(sk_vc *g, double x0, double y0, double x1, double y1, sk_color c)
{
    sk_vc_paint p = sk_vc_solid(c);
    sk_vc_begin(g);
    sk_vc_move(g, x0, y0);
    sk_vc_line(g, x1, y1);
    sk_vc_stroke(g, &p);
}

static void fill_box(sk_vc *g, double x, double y, double w, double h, sk_color c)
{
    sk_vc_paint p = sk_vc_solid(c);
    sk_vc_fill_rect(g, x, y, w, h, &p);
}

static void draw_diagram(carbon *c, double scale)
{
    const box *d = &c->R.diag;
    surface_make(&c->diag, d->w, d->h, scale, d->x, d->y);
    /* Drawn over the pane's background, which is all it ever lies on,
     * so the pane takes it as it is. */
    sk_vc_clear(&c->diag.vc, BACKGROUND);
    sk_vc *g = &c->diag.vc;
    double W = c->diag.w;
    bool small = d->w < 260 || d->h < 200;
    double left = small ? 30 : 46;
    c->R.top = 16;
    c->R.bot = d->h - (small ? 14 : 26);
    c->R.x0 = left;
    c->R.cw = (d->w - left - 6) / 5;
    sk_vc_font(g, 10);
    sk_vc_align_text(g, SK_VC_LEFT, SK_VC_MIDDLE);
    sk_color dim = SK_RGB(0x7d, 0x89, 0xa1);
    /* The ionization limit, dashed. */
    double dash54[2] = {5, 4};
    sk_vc_dash(g, dash54, 2);
    stroke_line(g, c->R.x0 - 4, c->R.top, d->w - 4, c->R.top, dim);
    sk_vc_dash(g, NULL, 0);
    sk_vc_align_text(g, SK_VC_RIGHT, SK_VC_MIDDLE);
    char t[128], v[32];
    fixed(sk_carbon_limit_ev, 2, v, sizeof v);
    snprintf(t, sizeof t, "%s%s eV", small ? "limit " : "ionization limit ", v);
    say(g, W, t, d->w - 6, c->R.top - 8, d->x, d->y, &c->still, dim);
    /* The energy axis in eV. */
    stroke_line(g, c->R.x0 - 6, c->R.top, c->R.x0 - 6, c->R.bot, SK_RGB(0x1f, 0x28, 0x38));
    double per_ev = sk_carbon_limit / sk_carbon_limit_ev;
    for (int e = 0; e <= 10; e += small ? 5 : 2) {
        snprintf(t, sizeof t, "%d%s", e, small ? "" : " eV");
        say(g, W, t, c->R.x0 - 9, y_of(c, e * per_ev), d->x, d->y, &c->still, dim);
    }
    sk_vc_align_text(g, SK_VC_CENTER, SK_VC_MIDDLE);
    if (!small)
        for (int col = 0; col < 5; col++)
            say(g, W, COL_NAME[col], col_x(c, col) + c->R.cw / 2, c->R.bot + 13, d->x, d->y, &c->still, dim);
    /* The Rydberg ladders, faint, toward the limit. */
    for (int i = 0; i < SK_CARBON_RYDBERG; i++) {
        const sk_carbon_rydberg *r = &sk_carbon_rydberg_e[i];
        int col = strncmp(r->series, "2pnp", 4) == 0 ? 2 : 3;
        double y = sk_vm_round(y_of(c, r->E)) + 0.5;
        stroke_line(g, col_x(c, col) + c->R.cw * 0.25, y, col_x(c, col) + c->R.cw * 0.65, y, SK_RGB(0x2b, 0x34, 0x46));
    }
    /* Every level, its fine structure within a point. */
    sk_vc_line_width(g, 1.5);
    for (int k = 0; k < NL; k++) {
        double y = sk_vm_round(y_of(c, LV(k)->E)) + 0.5;
        stroke_line(g, col_x(c, col_of(k)) + 3, y, col_x(c, col_of(k)) + c->R.cw - 5, y, SK_RGB(0x4d, 0x5b, 0x77));
    }
    sk_vc_line_width(g, 1);
    /* The ground term's fine structure, magnified, in the triplet
     * columns' empty band between 1.2 and 6 eV, where that band is tall
     * enough to part the labels of J = 0 and 1 by a row. */
    c->R.has_inset = false;
    double iy = sk_vm_round(y_of(c, 6 * per_ev));
    double ih = sk_vm_round(SK_MIN(y_of(c, 1.2 * per_ev) - iy, 96));
    if (d->h >= 140 && ih >= 64) {
        double ix = sk_vm_round(col_x(c, 2) + 4), iw = sk_vm_round(SK_MIN(c->R.cw * 2 - 8, 130));
        int gl[3], ng = 0;
        for (int k = 0; k < NL && ng < 3; k++)
            if (sk_carbon_ground(k))
                gl[ng++] = k;
        double span = LV(gl[ng - 1])->E, inner = ih - 30;
        c->R.has_inset = true;
        c->R.ix = ix;
        c->R.iy = iy;
        c->R.iw = iw;
        c->R.ih = ih;
        c->R.inner = inner;
        c->R.span = span;
        sk_vc_paint frame = sk_vc_solid(SK_RGB(0x2e, 0x3a, 0x50));
        sk_vc_stroke_rect(g, ix + 0.5, iy + 0.5, iw, ih, &frame);
        double dash23[2] = {2, 3};
        sk_vc_dash(g, dash23, 2);
        stroke_line(g, ix + iw / 2, iy + ih, col_x(c, 2) + c->R.cw / 2, y_of(c, 0) - 2, SK_RGB(0x2e, 0x3a, 0x50));
        sk_vc_dash(g, NULL, 0);
        sk_vc_align_text(g, SK_VC_LEFT, SK_VC_MIDDLE);
        double mag = inner / ((c->R.bot - c->R.top) * span / sk_carbon_limit);
        snprintf(t, sizeof t, "\xc2\xb3P " S_TIMES "%.0f", sk_vm_round(mag));
        say(g, W, t, ix + 4, iy + 9, d->x, d->y, &c->still, dim);
        for (int j = 0; j < ng; j++) {
            const sk_carbon_level *lv = LV(gl[j]);
            double y = sk_vm_round(iy + ih - 10 - inner * lv->E / span) + 0.5;
            sk_vc_line_width(g, 1.5);
            stroke_line(g, ix + 6, y, ix + iw * 0.45, y, SK_RGB(0x4d, 0x5b, 0x77));
            sk_vc_line_width(g, 1);
            if (iw > 90) {
                fixed(lv->E, 1, v, sizeof v);
                snprintf(t, sizeof t, "J=%d  %s cm" S_INV, lv->J, v);
            } else {
                snprintf(t, sizeof t, "J=%d", lv->J);
            }
            say(g, W, t, ix + iw * 0.45 + 4, y, d->x, d->y, &c->still, SK_RGB(0x8c, 0x97, 0xad));
        }
    }
}

/* The inset's height of a ground-term energy. */
static double inset_y(const carbon *c, double E)
{
    return c->R.iy + c->R.ih - 10 - c->R.inner * E / c->R.span;
}

#define LO 110.0
#define HI 10000.0

static double spec_x(const carbon *c, double nm)
{
    double x1 = c->R.spec.w - 46;
    if (nm > HI)
        return x1 + 26 + (sk_vm_log10(nm) - 5.5) * 24;
    return 6 + (x1 - 6) * sk_vm_log(nm / LO) / sk_vm_log(HI / LO);
}

/* The spectrum's axis: log wavelength from LO to HI nm, the visible
 * band marked, every line a faint tick in its colour or grey, and an
 * off-scale bin at the right for the ground term's far-infrared lines. */
static void draw_spectrum(carbon *c, double scale)
{
    const box *s = &c->R.spec;
    surface_make(&c->spec, s->w, s->h, scale, s->x, s->y);
    sk_vc *g = &c->spec.vc;
    double W = c->spec.w, base = s->h - 14;
    c->R.base = base;
    fill_box(g, 0, 0, s->w, s->h, SK_RGB(0x06, 0x0a, 0x12));
    fill_box(g, spec_x(c, 380), 2, spec_x(c, 780) - spec_x(c, 380), base - 2, sk_vc_rgba(255, 255, 255, 0.05));
    stroke_line(g, 4, base + 0.5, spec_x(c, HI) + 2, base + 0.5, SK_RGB(0x33, 0x40, 0x5a));
    sk_vc_font(g, 9);
    sk_color dim = SK_RGB(0x7d, 0x89, 0xa1);
    sk_vc_align_text(g, SK_VC_CENTER, SK_VC_TOP);
    static const double few_nm[2] = {200, 1000};
    static const char *const few[2] = {"200 nm", "1 " S_MU "m"};
    static const double many_nm[7] = {120, 200, 380, 780, 2000, 5000, 10000};
    static const char *const many[7] = {"120 nm", "200", "380", "780", "2 " S_MU "m", "5", "10"};
    bool narrow = s->w < 330;
    for (int i = 0; i < (narrow ? 2 : 7); i++) {
        double nm = narrow ? few_nm[i] : many_nm[i];
        fill_box(g, spec_x(c, nm), base, 1, 3, dim);
        say(g, W, narrow ? few[i] : many[i], spec_x(c, nm), base + 3, s->x, s->y, &c->still, dim);
    }
    say(g, W, "far IR", spec_x(c, 370000), base + 3, s->x, s->y, &c->still, dim);
    sk_vc_paint frame = sk_vc_solid(SK_RGB(0x33, 0x40, 0x5a));
    sk_vc_stroke_rect(g, sk_vm_round(spec_x(c, 370000)) - 16.5, 2.5, 33, base - 3, &frame);
    if (!narrow) {
        sk_vc_align_text(g, SK_VC_LEFT, SK_VC_TOP);
        say(g, W, "visible", spec_x(c, 380) + 2, 3, s->x, s->y, &c->still, dim);
    }
    for (int k = 0; k < NLINES; k++) {
        sk_vc_alpha(g, visible(k) ? 0.6 : 0.8);
        fill_box(g, sk_vm_round(spec_x(c, wl(k))), base - 4, 1, 4, visible(k) ? line_color(k) : SK_RGB(0x3d, 0x48, 0x60));
    }
    sk_vc_alpha(g, 1);
}

/* A faint fixed grain under the atom: the picture the invisible fronts
 * sharpen and refract, which on black would show nothing. */
static void draw_grain(carbon *c, double scale)
{
    const box *a = &c->R.atom;
    surface_make(&c->grain, a->w, a->h, scale, a->x, a->y);
    sk_image *im = &c->grain.im;
    for (int y = 0; y < im->h; y++) {
        sk_px *row = sk_image_row(im, y);
        for (int x = 0; x < im->w; x++) {
            double v = rnd(c) < 0.25 ? 8 + 16 * rnd(c) : 4 * rnd(c);
            /* A Uint8ClampedArray rounds, ties to even. */
            uint32_t r = (uint32_t)nearbyint(v * 0.8), g = (uint32_t)nearbyint(v * 0.9), b = (uint32_t)nearbyint(v * 1.2);
            row[x] = 0xff000000u | r << 16 | g << 8 | b;
        }
    }
}

static void resize(carbon *c, int w, int h, double scale)
{
    make_regions(&c->R, w, h);
    c->still.len = 0;
    draw_diagram(c, scale);
    draw_spectrum(c, scale);
    draw_grain(c, scale);
}

/* The atom's frame on screen this frame. Its z axis is the axis M_L and
 * the fronts' components are taken about. */
static void frame(carbon *c, int w, int h)
{
    const box *a = &c->R.atom;
    c->fcx = a->x + a->w / 2;
    c->fcy = a->y + a->h / 2;
    c->fk = SK_MAX(10, SK_MIN(a->w, a->h) / 2 - 6) / VIEW;
    c->fedge = SK_MIN(SK_MIN(c->fcx, c->fcy), SK_MIN(w - c->fcx, h - c->fcy));
    double cy = sk_vm_cos(c->yaw), sy = sk_vm_sin(c->yaw), cp = sk_vm_cos(c->pitch), sp = sk_vm_sin(c->pitch);
    double B[8] = {-sy, cy, -sp * cy, -sp * sy, cp, cp * cy, cp * sy, sp};
    memcpy(c->B, B, sizeof B);
    c->upz = cp;
}

static void project(const carbon *c, double x, double y, double z, double *px, double *py, double *ps)
{
    const double *B = c->B;
    double X = x * B[0] + y * B[1], Y = x * B[2] + y * B[3] + z * B[4], Z = x * B[5] + y * B[6] + z * B[7];
    double d = 4.0 * VIEW, s = d / (d - Z);
    *px = c->fcx + X * c->fk * s;
    *py = c->fcy - Y * c->fk * s;
    *ps = s;
}

/* The visible fronts, in their hue: each the limb of its sphere and of
 * CRESTS - 1 crests behind it, as arcs whose brightness is the drawn
 * amplitude in the arc's direction, cos(Theta) = sin(phi) upZ on the
 * limb. Arcs batched by colour and brightness, one path a batch. */
typedef struct {
    sk_color rgb;
    int b;
    double r, p0, p1;
} arc_piece;

static int by_batch(const void *x, const void *y)
{
    const arc_piece *a = x, *b = y;
    if (a->rgb != b->rgb)
        return a->rgb < b->rgb ? -1 : 1;
    return (a->b > b->b) - (a->b < b->b);
}

static void draw_fronts(carbon *c, sk_vc *g)
{
    const int N = 36;
    SK_VEC(arc_piece) arcs = {0};
    for (size_t i = 0; i < c->fronts.len; i++) {
        const front *f = &c->fronts.ptr[i];
        if (!f->vis)
            continue;
        double Rb = front_radius(f, c->now), R = Rb * c->fk;
        double a0 = SK_MIN(1, f->amp * R0 / SK_MAX(Rb, 0.1));
        for (int w = 0; w < CRESTS; w++) {
            double r = R - w * f->gap;
            if (r <= 1)
                break;
            double fade = a0 * sk_vm_exp(-w / 1.2);
            for (int j = 0; j < N; j++) {
                double p0 = SK_TAU * j / N, p1 = SK_TAU * (j + 1) / N;
                int b = (int)sk_vm_round(10 * fade * shape(f->kind, sk_vm_sin((p0 + p1) / 2) * c->upz));
                if (b <= 0)
                    continue;
                arc_piece p = {line_color(f->k), b, r, p0, p1};
                sk_vec_push(&arcs, p);
            }
        }
    }
    if (arcs.len)
        qsort(arcs.ptr, arcs.len, sizeof *arcs.ptr, by_batch);
    sk_vc_line_width(g, 1.6);
    for (size_t i = 0; i < arcs.len;) {
        size_t j = i;
        sk_vc_begin(g);
        while (j < arcs.len && arcs.ptr[j].rgb == arcs.ptr[i].rgb && arcs.ptr[j].b == arcs.ptr[i].b) {
            const arc_piece *p = &arcs.ptr[j];
            sk_vc_move(g, c->fcx + p->r * sk_vm_cos(p->p0), c->fcy - p->r * sk_vm_sin(p->p0));
            sk_vc_arc(g, c->fcx, c->fcy, p->r, -p->p0, -p->p1, true);
            j++;
        }
        sk_vc_alpha(g, arcs.ptr[i].b / 10.0);
        sk_vc_paint paint = sk_vc_solid(arcs.ptr[i].rgb);
        sk_vc_stroke(g, &paint);
        i = j;
    }
    sk_vc_alpha(g, 1);
    sk_vec_free(&arcs);
}

/* The invisible fronts as image transforms over the atom's pixels, in
 * one pass, since every front shares one centre: each front adds to a
 * table by whole radius, per pattern kind, and a pixel takes the tables
 * at its radius times each kind's shape in its direction. Ultraviolet
 * fronts sharpen the picture and lift its contrast across a band as wide
 * as the front's crest spacing. Infrared fronts refract it, shifting
 * pixels along the radius by a ripple of that spacing, as heat haze
 * does. Radii with no front are skipped. In the image's pixels: a
 * radius, a spacing or a shift of the JavaScript's points here is scale
 * times as many. */
static void warp(carbon *c, sk_image *im, double scale)
{
    const box *ab = &c->R.atom;
    int X0 = (int)floor(ab->x * scale + 0.5), Y0 = (int)floor(ab->y * scale + 0.5);
    int W = (int)floor(ab->w * scale + 0.5), H = (int)floor(ab->h * scale + 0.5);
    W = SK_MIN(W, im->w - X0);
    H = SK_MIN(H, im->h - Y0);
    bool some = false;
    for (size_t i = 0; i < c->fronts.len && !some; i++)
        some = !c->fronts.ptr[i].vis;
    if (!some || W < 4 || H < 4)
        return;
    /* The tables in blocks of B pixels, the scale's whole part: a
     * block is a point at 2x, a pixel at 1x or 1.5x. */
    int B = (int)SK_MAX(1, SK_MIN(4, floor(scale)));
    double u = scale / B;
    int rmax = (int)ceil(sqrt((double)W * W + (double)H * H) / B) + 4;
    if (c->wn < rmax) {
        free(c->ws);
        free(c->wd);
        free(c->won);
        c->ws = sk_xmalloc((size_t)rmax * 5 * sizeof *c->ws);
        c->wd = sk_xmalloc((size_t)rmax * 5 * sizeof *c->wd);
        c->won = sk_xmalloc((size_t)rmax);
        c->wn = rmax;
    }
    memset(c->ws, 0, (size_t)rmax * 5 * sizeof *c->ws);
    memset(c->wd, 0, (size_t)rmax * 5 * sizeof *c->wd);
    memset(c->won, 0, (size_t)rmax);
    int used = 0;
    for (size_t i = 0; i < c->fronts.len; i++) {
        const front *f = &c->fronts.ptr[i];
        if (f->vis)
            continue;
        double Rb = front_radius(f, c->now), R = Rb * c->fk * u;
        double a = SK_MIN(1.5, f->amp * R0 / SK_MAX(Rb, 0.1));
        double gap = f->gap * u, w = f->uv ? 0.8 * gap : 1.5 * gap;
        int lo = (int)SK_MAX(0, floor(R - 2.5 * w)), hi = (int)SK_MIN(rmax - 1, ceil(R + w));
        if (hi < lo)
            continue;
        used++;
        for (int r = lo; r <= hi; r++) {
            double x = (r - R) / w, env = sk_vm_exp(-x * x);
            if (f->uv)
                c->ws[f->kind * rmax + r] += (float)(a * env);
            else
                c->wd[f->kind * rmax + r] += (float)(4 * u * a * env * sk_vm_sin(SK_TAU * (r - R) / gap));
            /* Which kinds a radius holds, a bit each. */
            c->won[r] |= (uint8_t)(1 << f->kind);
        }
    }
    if (!used)
        return;
    size_t n = (size_t)W * (size_t)H;
    if (c->wsrc_n < n) {
        free(c->wsrc);
        c->wsrc = sk_xmalloc(n * sizeof *c->wsrc);
        c->wsrc_n = n;
    }
    sk_px *src = c->wsrc;
    for (int y = 0; y < H; y++)
        memcpy(src + (size_t)y * (size_t)W, sk_image_row(im, Y0 + y) + X0, (size_t)W * sizeof *src);
    double ox = c->fcx * scale - X0, oy = c->fcy * scale - Y0;
    /* warp.js works on the canvas's pixels, the pane's points. Here a
     * block of the scale's pixels a side stands for a point: the shift
     * and the sharpening are worked out once a block, at its centre, the
     * sharpening from the blocks' mean colours, as the JavaScript's from
     * its points', and each pixel of the block takes them: the shift
     * fetched pixel by pixel, the sharpening added. The blocks at the
     * edges are left as the JavaScript leaves its edge points. */
    int bw = W / B, bh = H / B;
    if (bw < 3 || bh < 3)
        return;
    size_t nb = (size_t)bw * (size_t)bh;
    float *mean = sk_xmalloc(nb * 3 * sizeof *mean);
    for (int by = 0; by < bh; by++)
        for (int bx = 0; bx < bw; bx++) {
            uint32_t sum[3] = {0, 0, 0};
            for (int y = by * B; y < by * B + B; y++)
                for (int x = bx * B; x < bx * B + B; x++) {
                    sk_px p = src[(size_t)y * (size_t)W + (size_t)x];
                    sum[0] += p & 255;
                    sum[1] += (p >> 8) & 255;
                    sum[2] += (p >> 16) & 255;
                }
            for (int ch = 0; ch < 3; ch++)
                mean[((size_t)by * (size_t)bw + (size_t)bx) * 3 + (size_t)ch] = (float)sum[ch] / (float)(B * B);
        }
    float inv_b = 1.0f / (float)B;
    for (int by = 1; by < bh - 1; by++) {
        int y0 = by * B, y1 = y0 + B;
        float dy = (float)((y0 + y1 - 1) / 2.0 - oy), dy2 = dy * dy;
        for (int bx = 1; bx < bw - 1; bx++) {
            int x0 = bx * B, x1 = x0 + B;
            float dx = (float)((x0 + x1 - 1) / 2.0 - ox), rr = sqrtf(dx * dx + dy2);
            int ri = (int)(rr * inv_b);
            if (ri >= rmax || !c->won[ri])
                continue;
            float rinv = rr > 0 ? 1 / rr : 0;
            int ci = (int)floorf((-dy * rinv * (float)c->upz + 1) * 128 + 0.5f);
            float s = 0, disp = 0;
            for (int k = 0, bits = c->won[ri]; bits; k++, bits >>= 1) {
                if (!(bits & 1))
                    continue;
                float f = SHAPES[k][ci];
                s += c->ws[k * rmax + ri] * f;
                disp += c->wd[k * rmax + ri] * f;
            }
            float rn = rr > 0 ? rinv : 1;
            /* The shift in whole pixels: round(x + u) is x + round(u)
             * for a whole x. */
            int ix = (int)floorf(dx * rn * disp * (float)B + 0.5f), iy = (int)floorf(dy * rn * disp * (float)B + 0.5f);
            /* What changes no pixel is skipped: a shift that rounds to
             * the pixel itself, and a sharpening too weak to move a
             * channel half a step, its most 0.5 4 255 + 0.35 255. */
            bool shift = ix || iy, sharpen = s >= 1 / 1200.0f;
            if (!shift && !sharpen)
                continue;
            int delta[3] = {0, 0, 0};
            if (sharpen) {
                float sf = s < 1 ? s : 1;
                const float *m0 = mean + ((size_t)by * (size_t)bw + (size_t)bx) * 3;
                size_t row = (size_t)bw * 3;
                for (int ch = 0; ch < 3; ch++) {
                    float p = m0[ch], lap = 4 * p - m0[ch - 3] - m0[ch + 3] - m0[(ptrdiff_t)ch - (ptrdiff_t)row] - m0[ch + row];
                    delta[ch] = (int)nearbyintf(sf * (0.5f * lap + 0.35f * p));
                }
            }
            for (int y = y0; y < y1; y++) {
                sk_px *d = sk_image_row(im, Y0 + y) + X0;
                const sk_px *from = src + (size_t)SK_MAX(0, SK_MIN(H - 1, y + iy)) * (size_t)W;
                for (int x = x0; x < x1; x++) {
                    sk_px out = d[x];
                    if (shift)
                        out = (out & 0xff000000u) | (from[SK_MAX(0, SK_MIN(W - 1, x + ix))] & 0xffffffu);
                    if (sharpen) {
                        uint32_t res = out & 0xff000000u;
                        for (int ch = 0, sh = 0; ch < 3; ch++, sh += 8) {
                            int v = (int)((out >> sh) & 255) + delta[ch];
                            res |= (uint32_t)(v < 0 ? 0 : v > 255 ? 255 : v) << sh;
                        }
                        out = res;
                    }
                    d[x] = out;
                }
            }
        }
    }
    free(mean);
}

static void draw_atom(carbon *c, sk_vc *g, double dim, double scale)
{
    const box *a = &c->R.atom;
    sk_vc_save(g);
    sk_vc_begin(g);
    sk_vc_rect(g, a->x, a->y, a->w, a->h);
    sk_vc_clip(g, SK_FILL_NONZERO);
    blit_opaque(g, &c->grain.im, a->x, a->y);
    /* A faint equator and meridian, for depth. */
    double dash35[2] = {3, 5};
    sk_vc_dash(g, dash35, 2);
    sk_vc_paint ring = sk_vc_solid(SK_RGB(0x1d, 0x26, 0x38));
    for (int k = 0; k < 2; k++) {
        sk_vc_begin(g);
        for (int j = 0; j <= 64; j++) {
            double t = SK_TAU * j / 64, r = 0.92 * VIEW, px, py, ps;
            if (k)
                project(c, r * sk_vm_cos(t), 0, r * sk_vm_sin(t), &px, &py, &ps);
            else
                project(c, r * sk_vm_cos(t), r * sk_vm_sin(t), 0, &px, &py, &ps);
            if (j)
                sk_vc_line(g, px, py);
            else
                sk_vc_move(g, px, py);
        }
        sk_vc_stroke(g, &ring);
    }
    sk_vc_dash(g, NULL, 0);
    sk_vc_op(g, SK_BLEND_ADD);
    /* The drawn atom's cloud, dimmed while its jump's source flashes
     * over it, mint where psi_f* psi_i is positive, pink where
     * negative. */
    bool flashing = c->have_src && c->src_pts && c->now - c->src_t < SOURCE_T;
    double cd = flashing ? 0.45 * dim : dim;
    for (int i = 0; i < c->cl.n; i++) {
        double px, py, ps;
        project(c, c->cl.pts[3 * i], c->cl.pts[3 * i + 1], c->cl.pts[3 * i + 2], &px, &py, &ps);
        if (px < a->x || px >= a->x + a->w || py < a->y || py >= a->y + a->h)
            continue;
        double near = sk_vm_clamp((ps - 0.8) / 0.45, 0, 1);
        bool outer = i >= c->cl.outer_from;
        sk_vc_batch(g, outer ? SK_RGB(0xff, 0xcf, 0x7a) : SK_RGB(0xa9, 0xc8, 0xff),
                    (outer ? 0.4 + 0.5 * near : 0.3 + 0.45 * near) * cd, px, py, 1 + 1.6 * near);
    }
    if (flashing) {
        double s = 1 - (c->now - c->src_t) / SOURCE_T;
        for (int i = 0; i < SOURCE_PTS; i++) {
            double px, py, ps;
            project(c, c->src[4 * i], c->src[4 * i + 1], c->src[4 * i + 2], &px, &py, &ps);
            sk_vc_batch(g, c->src[4 * i + 3] > 0 ? SK_RGB(0x7d, 0xff, 0xd0) : SK_RGB(0xff, 0x8a, 0xd8),
                        (0.5 + 0.4 * sk_vm_clamp(ps - 0.8, 0, 1)) * s, px, py, 1.8);
        }
    }
    double px, py, ps;
    project(c, 0, 0, 0, &px, &py, &ps);
    sk_vc_batch(g, SK_RGB(255, 255, 255), 1, px, py, 2);
    sk_vc_flush(g);
    sk_vc_restore(g);
    /* The fronts in their hue over the pane, the detections, then the
     * invisible fronts' pass over the atom's pixels. */
    sk_vc_save(g);
    sk_vc_op(g, SK_BLEND_ADD);
    draw_fronts(c, g);
    size_t kept = 0;
    for (size_t i = 0; i < c->dots.len; i++)
        if (c->now - c->dots.ptr[i].t < 0.6)
            c->dots.ptr[kept++] = c->dots.ptr[i];
    c->dots.len = kept;
    for (size_t i = 0; i < c->dots.len; i++) {
        const detection *d = &c->dots.ptr[i];
        sk_vc_batch(g, d->fill, 1 - (c->now - d->t) / 0.6, d->x, d->y, 2.5 + 1.5 * (0.5 + 0.5 * d->z));
    }
    sk_vc_flush(g);
    sk_vc_restore(g);
    warp(c, g->im, scale);
}

static void labels(carbon *c, sk_vc *g)
{
    const box *a = &c->R.atom;
    const atom *ax = &c->atoms[0];
    /* The axis, dashed through the atom. */
    double tx, ty, ts, bx, by, bs;
    project(c, 0, 0, 1.15 * VIEW, &tx, &ty, &ts);
    project(c, 0, 0, -1.15 * VIEW, &bx, &by, &bs);
    double dash24[2] = {2, 4};
    sk_vc_dash(g, dash24, 2);
    stroke_line(g, tx, ty, bx, by, SK_RGB(0x5d, 0x67, 0x80));
    sk_vc_dash(g, NULL, 0);
    sk_vc_font(g, 10);
    sk_vc_align_text(g, SK_VC_LEFT, SK_VC_ALPHABETIC);
    if (c->R.small)
        return;
    /* The rows: a colour, a text, a swatch's colour or 0. */
    struct row {
        sk_color col;
        char t[200];
        sk_color swatch;
    } rows[10], rest[3];
    int nr = 0;
    char name[64], v1[32], v2[32];
    sk_carbon_level_name(ax->lv, name, sizeof name);
    shell sh[4];
    int ns = shells(ax->lv, sh);
    rows[nr] = (struct row){SK_RGB(0xe8, 0xee, 0xfc), "", 0};
    snprintf(rows[nr++].t, sizeof rows[0].t, "%s  M_L=%d%s", name, ax->M, c->cl.round ? ", round" : "");
    rows[nr] = (struct row){SK_RGB(0x8c, 0x97, 0xad), "", 0};
    if (sk_carbon_ground(ax->lv)) {
        snprintf(rows[nr++].t, sizeof rows[0].t, "ground term, at rest");
    } else {
        time_text(LV(ax->lv)->tau, v1, sizeof v1);
        time_text(sk_carbon_tau_screen(ax->lv), v2, sizeof v2);
        snprintf(rows[nr++].t, sizeof rows[0].t, S_TAU " %s, %s on the event clock", v1, v2);
    }
    if (ns == 4) {
        fixed(sk_carbon_outer_e[sh[3].key].mean_r, 1, v1, sizeof v1);
        rows[nr] = (struct row){SK_RGB(0xff, 0xcf, 0x7a), "", 0};
        snprintf(rows[nr++].t, sizeof rows[0].t, "%s cloud: a model, <r> %s " S_A0, sh[3].nl, v1);
    }
    if (c->now - c->cause_t < 4) {
        rows[nr] = (struct row){SK_RGB(0x9f, 0xb7, 0xe0), "", 0};
        snprintf(rows[nr++].t, sizeof rows[0].t, "%s", c->cause);
    }
    if (c->have_last && c->now - c->last_t < 3) {
        int k = c->last_k;
        const sk_carbon_line *ln = LN(k);
        static const char *const types[3] = {"E1", "M1", "E2"};
        rows[nr] = (struct row){SK_RGB(0xc9, 0xd3, 0xe6), "", visible(k) ? line_color(k) : SK_RGB(0xaa, 0xb3, 0xc5)};
        snprintf(rows[nr++].t, sizeof rows[0].t, "its photon " S_DELTA "M=%s%d %s, %s", c->last_q > 0 ? "+" : "",
                 c->last_q, types[ln->type], pattern_name(ln->type, c->last_q));
        rows[nr] = (struct row){SK_RGB(0x8c, 0x97, 0xad), "", 0};
        snprintf(rows[nr++].t, sizeof rows[0].t, "%s",
                 c->src_pts ? "source: " S_PSI "f*" S_PSI "i, + mint, - pink" : "source: a two-electron jump, not drawn");
        wl_text(wl(k), v1, sizeof v1);
        rows[nr] = (struct row){SK_RGB(0x8c, 0x97, 0xad), "", 0};
        snprintf(rows[nr++].t, sizeof rows[0].t, "%s = %.0f" S_TIMES " this atom: crest spacing a symbol", v1,
                 sk_vm_round(wl(k) / (size_of(ln->u) * sk_carbon_bohr_nm)));
    }
    char e1[32], e2[32], lt[32];
    sci(EVENT, e1, sizeof e1);
    sci(LIGHT, e2, sizeof e2);
    fixed(sk_carbon_light(), 1, lt, sizeof lt);
    rest[0] = (struct row){SK_RGB(0x7d, 0x89, 0xa1), "one site, 160 atoms: cloud of 1, fronts of all", 0};
    rest[1] = (struct row){SK_RGB(0x7d, 0x89, 0xa1), "", 0};
    snprintf(rest[1].t, sizeof rest[1].t, "events: NIST rates / %s, light: c / %s = %s " S_A0 "/s", e1, e2, lt);
    rest[2] = (struct row){SK_RGB(0x7d, 0x89, 0xa1), "", 0};
    snprintf(rest[2].t, sizeof rest[2].t, "field %.0f%%, %zu photons/s, %zu fronts", sk_vm_round(100 * c->field),
             c->born.len, c->fronts.len);
    /* A narrower atom keeps the level, its clock, its photon and the
     * rate. Each row, then the axis's name, then the scale bar, is
     * written where it overlaps no text before it, or left out. */
    struct row keep[13];
    int nk = 0;
    if (a->w < 320) {
        keep[nk++] = rows[0];
        keep[nk++] = rows[1];
        for (int i = 0; i < nr; i++)
            if (rows[i].swatch)
                keep[nk++] = rows[i];
        keep[nk++] = rest[2];
    } else {
        for (int i = 0; i < nr; i++)
            keep[nk++] = rows[i];
        for (int i = 0; i < 3; i++)
            keep[nk++] = rest[i];
    }
    double fit = a->w - 14;
    int j = 0;
    for (int i = 0; i < nk; i++) {
        double y = a->y + 11 + 12 * j, x = a->x + 8 + (keep[i].swatch ? 11 : 0);
        char t[512];
        clip_text(g, keep[i].t, fit - (x - a->x - 8), t, sizeof t);
        tbox b = box_of(g, c->base.w, t, x, y, 0, 0);
        if (!fits(c, &b))
            continue;
        if (keep[i].swatch)
            fill_box(g, a->x + 8, y - 7, 7, 7, keep[i].swatch);
        write_box(g, &b, &c->said, keep[i].col);
        j++;
    }
    double at[1][2] = {{tx + 4, SK_MAX(a->y + 10, ty)}};
    place(c, g, "z, chosen", at, 1, SK_RGB(0x7d, 0x89, 0xa1));
    /* A scale bar, drawn only with its label. */
    static const int steps[6] = {1, 2, 5, 10, 20, 50};
    int step = 50;
    for (int i = 0; i < 6; i++)
        if (steps[i] * c->fk >= a->w * 0.1) {
            step = steps[i];
            break;
        }
    double sbx = a->x + 10, sby = a->y + a->h - 6;
    char t[64];
    fixed(step * sk_carbon_bohr_nm, 2, v1, sizeof v1);
    snprintf(t, sizeof t, "%d " S_A0 " = %s nm", step, v1);
    double sat[1][2] = {{sbx, sby - 4}};
    if (place(c, g, t, sat, 1, SK_RGB(0x7d, 0x89, 0xa1)))
        fill_box(g, sbx, sby, step * c->fk, 1, SK_RGB(0x7d, 0x89, 0xa1));
}

static void draw_diagram_now(carbon *c, sk_vc *g)
{
    const box *d = &c->R.diag;
    blit_opaque(g, &c->diag.im, d->x, d->y);
    double cw = c->R.cw;
    /* The ensemble's populations: each level's bar as bright as its
     * share of the atoms. */
    double pop[NL] = {0};
    for (int i = 0; i < N_ATOMS; i++)
        pop[c->atoms[i].lv]++;
    sk_vc_save(g);
    sk_vc_line_width(g, 2);
    for (int k = 0; k < NL; k++) {
        if (!pop[k])
            continue;
        sk_vc_alpha(g, SK_MIN(1, 0.25 + 3 * pop[k] / N_ATOMS));
        double y = sk_vm_round(d->y + y_of(c, LV(k)->E)) + 0.5, x = d->x + col_x(c, col_of(k));
        stroke_line(g, x + 3, y, x + 3 + (cw - 8) * SK_MIN(1, 0.2 + pop[k] / N_ATOMS * 2), y, SK_RGB(0x9f, 0xb7, 0xe0));
        if (c->R.has_inset && sk_carbon_ground(k)) {
            double yy = sk_vm_round(d->y + inset_y(c, LV(k)->E)) + 0.5;
            stroke_line(g, d->x + c->R.ix + 6, yy, d->x + c->R.ix + 6 + (c->R.iw * 0.45 - 6) * pop[k] / N_ATOMS, yy,
                        SK_RGB(0x9f, 0xb7, 0xe0));
        }
    }
    sk_vc_restore(g);
    int lv = c->atoms[0].lv;
    /* The drawn atom's last jump: an arrow from the upper level to the
     * lower, in its light's colour, or grey and dashed outside the
     * visible, its wavelength written after the level's name. */
    bool arrow = false;
    double ax1 = 0, ay1 = 0, ax2 = 0, ay2 = 0, aa = 0;
    sk_color afill = 0;
    char at_text[32] = "";
    if (c->have_last && c->now - c->last_t < 3) {
        const sk_carbon_line *ln = LN(c->last_k);
        double a = SK_MAX(0.3, 1 - (c->now - c->last_t) / 3);
        bool same = col_of(ln->u) == col_of(ln->l), vis = visible(c->last_k);
        double x1 = d->x + col_x(c, col_of(ln->u)) + cw * (same ? 0.35 : 0.5), y1 = d->y + y_of(c, LV(ln->u)->E);
        double x2 = d->x + col_x(c, col_of(ln->l)) + cw * (same ? 0.35 : 0.5), y2 = d->y + y_of(c, LV(ln->l)->E);
        sk_color col = vis ? line_color(c->last_k) : SK_RGB(0x9a, 0xa3, 0xb5);
        sk_vc_save(g);
        sk_vc_alpha(g, a);
        sk_vc_line_width(g, vis ? 2.2 : 1.5);
        if (!vis) {
            double dash[2] = {5, 4};
            sk_vc_dash(g, dash, 2);
        }
        stroke_line(g, x1, y1, x2, y2 - 1, col);
        sk_vc_dash(g, NULL, 0);
        double ang = sk_vm_atan2(y2 - y1, x2 - x1);
        sk_vc_begin(g);
        sk_vc_move(g, x2, y2);
        sk_vc_line(g, x2 - 8 * sk_vm_cos(ang - 0.4), y2 - 8 * sk_vm_sin(ang - 0.4));
        sk_vc_line(g, x2 - 8 * sk_vm_cos(ang + 0.4), y2 - 8 * sk_vm_sin(ang + 0.4));
        sk_vc_paint p = sk_vc_solid(col);
        sk_vc_fill(g, &p, SK_FILL_NONZERO);
        arrow = true;
        wl_text(wl(c->last_k), at_text, sizeof at_text);
        afill = col;
        aa = a;
        ax1 = x1;
        ay1 = y1;
        ax2 = x2;
        ay2 = y2;
        sk_vc_restore(g);
    }
    /* The drawn atom's level, lit, and in the inset its J. */
    double y = sk_vm_round(d->y + y_of(c, LV(lv)->E)) + 0.5, x = d->x + col_x(c, col_of(lv));
    sk_vc_line_width(g, 3);
    stroke_line(g, x + 1, y, x + cw - 3, y, SK_RGB(255, 255, 255));
    if (c->R.has_inset && sk_carbon_ground(lv)) {
        double yy = sk_vm_round(d->y + inset_y(c, LV(lv)->E)) + 0.5;
        sk_vc_line_width(g, 2.5);
        stroke_line(g, d->x + c->R.ix + 6, yy, d->x + c->R.ix + c->R.iw * 0.45, yy, SK_RGB(255, 255, 255));
    }
    sk_vc_line_width(g, 1);
    sk_vc_font(g, 10);
    char label[64];
    sk_carbon_level_name(lv, label, sizeof label);
    sk_vc_metrics m;
    sk_vc_measure(g, label, &m);
    bool right = x + cw + m.width + 4 < d->x + d->w;
    sk_vc_align_text(g, right ? SK_VC_LEFT : SK_VC_RIGHT, SK_VC_MIDDLE);
    double lat[2][2] = {{right ? x + cw : x - 2, y - 7}, {right ? x + cw : x - 2, y + 8}};
    place(c, g, label, lat, 2, SK_RGB(255, 255, 255));
    if (arrow && !c->R.small) {
        sk_vc_save(g);
        sk_vc_alpha(g, aa);
        sk_vc_align_text(g, SK_VC_LEFT, SK_VC_MIDDLE);
        sk_vc_measure(g, at_text, &m);
        double tw = m.width, places[10][2];
        static const double fs[5] = {0.5, 0.35, 0.65, 0.2, 0.8};
        for (int i = 0; i < 5; i++) {
            double mx = ax1 + (ax2 - ax1) * fs[i], my = ay1 + (ay2 - ay1) * fs[i];
            places[2 * i][0] = SK_MIN(d->x + d->w - tw - 2, mx + 6);
            places[2 * i][1] = my;
            places[2 * i + 1][0] = mx - 6 - tw;
            places[2 * i + 1][1] = my;
        }
        place(c, g, at_text, places, 10, afill);
        sk_vc_restore(g);
    }
}

static void draw_spectrum_now(carbon *c, sk_vc *g, double dt)
{
    const box *s = &c->R.spec;
    blit_opaque(g, &c->spec.im, s->x, s->y);
    double top = 1;
    for (int k = 0; k < NLINES; k++)
        top = SK_MAX(top, c->counts[k]);
    double room = c->R.base - 6;
    sk_vc_save(g);
    for (int k = 0; k < NLINES; k++) {
        if (!c->counts[k])
            continue;
        bool vis = visible(k);
        double x = sk_vm_round(s->x + spec_x(c, wl(k)));
        double hgt = 4 + (room - 4) * sqrt(c->counts[k] / top) * 0.75;
        sk_color col = vis ? line_color(k) : SK_RGB(0x8a, 0x94, 0xa8);
        sk_vc_alpha(g, 0.6 + 0.4 * c->flash[k]);
        fill_box(g, x - 1, s->y + c->R.base - hgt, vis ? 3 : 2, hgt, col);
        if (c->flash[k] > 0.02) {
            sk_vc_alpha(g, c->flash[k]);
            fill_box(g, x - 1, s->y + 3, vis ? 3 : 2, c->R.base - 3, col);
        }
        c->flash[k] *= sk_vm_exp(-dt / 0.4);
    }
    sk_vc_restore(g);
}

/* The captions above the atom, measured first so that no other text
 * takes their place, and drawn last, over the fronts. */
typedef struct {
    double px;
    sk_color fill;
    tbox b;
} caption_t;

static int captions(carbon *c, sk_vc *g, double w, caption_t *out)
{
    int n = 0;
    sk_vc_align_text(g, SK_VC_LEFT, SK_VC_MIDDLE);
    if (c->have_last) {
        const sk_carbon_line *ln = LN(c->last_k);
        char up[64], low[64], wv[32], a[32], tau[32], waited[32], t[400], cut[512];
        sk_carbon_level_name(ln->u, up, sizeof up);
        sk_carbon_level_name(ln->l, low, sizeof low);
        wl_text(wl(c->last_k), wv, sizeof wv);
        sci(ln->A, a, sizeof a);
        time_text(LV(ln->u)->tau, tau, sizeof tau);
        time_text(c->last_waited, waited, sizeof waited);
        static const char *const types[3] = {"", " M1", " E2"};
        snprintf(t, sizeof t, "%s" S_TO "%s  %s%s  A %s s" S_INV "  " S_TAU " %s  waited %s", up, low, wv, types[ln->type],
                 a, tau, waited);
        sk_vc_font(g, 11);
        clip_text(g, t, w - c->R.cap_x - 8, cut, sizeof cut);
        out[n++] = (caption_t){11, SK_RGB(0xc9, 0xd3, 0xe6), box_of(g, c->base.w, cut, c->R.cap_x, c->R.cap_y, 0, 0)};
    }
    if (c->R.has_ens) {
        sk_vc_font(g, 10);
        double room = w - c->R.ens_x - 8;
        static const char *const ens[3] = {"one site, 160 atoms: cloud of 1, fronts of all",
                                           "160 atoms: cloud of 1, fronts of all", "160 atoms, cloud of 1"};
        const char *t = ens[2];
        for (int i = 0; i < 3; i++) {
            sk_vc_metrics m;
            sk_vc_measure(g, ens[i], &m);
            if (m.width <= room) {
                t = ens[i];
                break;
            }
        }
        char cut[512];
        clip_text(g, t, room, cut, sizeof cut);
        out[n++] = (caption_t){10, SK_RGB(0x7d, 0x89, 0xa1), box_of(g, c->base.w, cut, c->R.ens_x, c->R.ens_y, 0, 0)};
    }
    for (int i = 0; i < n; i++)
        sk_vec_push(&c->said, out[i].b);
    return n;
}

static void caption(sk_vc *g, const caption_t *out, int n)
{
    sk_vc_align_text(g, SK_VC_LEFT, SK_VC_MIDDLE);
    for (int i = 0; i < n; i++) {
        sk_vc_font(g, out[i].px);
        sk_vc_text(g, out[i].b.t, out[i].b.gx, out[i].b.gy, out[i].fill);
    }
}

static void carbon_init(sk_vmode *m)
{
    carbon *c = (carbon *)m;
    graph();
    make_shapes();
    /* The ensemble at rest in the ground term, J by its thermal
     * populations, and the one atom drawn, atoms[0]. */
    for (int i = 0; i < N_ATOMS; i++)
        c->atoms[i] = (atom){sk_carbon_thermal(rnd(c)), 0};
    snprintf(c->cause, sizeof c->cause, "at rest in the ground term");
    c->cause_t = -10;
    c->yaw = 0.4;
    c->pitch = 0.32;
    c->held = -1e9;
    redraw(c, NULL);
}

static void carbon_fini(sk_vmode *m)
{
    carbon *c = (carbon *)m;
    surface_free(&c->diag);
    surface_free(&c->spec);
    surface_free(&c->grain);
    cloud_free(&c->cl);
    sk_vec_free(&c->fronts);
    sk_vec_free(&c->dots);
    sk_vec_free(&c->born);
    sk_vec_free(&c->still);
    sk_vec_free(&c->said);
    free(c->ws);
    free(c->wd);
    free(c->won);
    free(c->wsrc);
}

static void carbon_draw(sk_vmode *m, sk_synth *s, double dt)
{
    carbon *c = (carbon *)m;
    int w = m->w, h = m->h;
    if (w < 4 || h < 4)
        return;
    if (m->fresh || w != c->ow || h != c->oh) {
        c->ow = w;
        c->oh = h;
        resize(c, w, h, m->scale);
    }
    const sk_synth_levels *L = &s->level;
    c->now += dt;
    sk_vmode_events(m, s);
    for (size_t k = 0; k < m->ev.len; k++)
        react(c, &m->ev.ptr[k]);
    c->field = strength(L);
    ensemble_step(c, dt, FIELD_MAX * c->field);
    size_t old = 0;
    while (old < c->born.len && c->now - c->born.ptr[old] > 1)
        old++;
    SK_VIZ_SHIFT(&c->born, old);
    cloud_step(&c->cl, dt, &m->rng);
    if (c->now - c->held > 4)
        c->yaw += dt * 0.25 * (1 - 0.7 * L->idle);
    frame(c, w, h);
    /* Fronts that reached the pane's edge are detected. */
    size_t kept = 0;
    for (size_t i = 0; i < c->fronts.len; i++) {
        front f = c->fronts.ptr[i];
        if (front_radius(&f, c->now) * c->fk < c->fedge)
            c->fronts.ptr[kept++] = f;
        else
            detect(c, &f, true);
    }
    c->fronts.len = kept;
    sk_vc *g = &m->vc;
    c->said.len = 0;
    caption_t caps[2];
    int ncaps = captions(c, g, w, caps);
    sk_vc_op(g, SK_BLEND_SRC_OVER);
    sk_vc_alpha(g, 1);
    sk_vc_clear(g, BACKGROUND);
    draw_spectrum_now(c, g, dt);
    draw_diagram_now(c, g);
    draw_atom(c, g, 1 - 0.3 * L->idle, m->scale);
    labels(c, g);
    caption(g, caps, ncaps);
}

/* A drag across the atom turns the camera. A click on the atom shines a
 * lamp on the drawn atom: a photon absorbed from its present level. A
 * click on the level diagram lifts the drawn atom to the level nearest
 * it in that column by a collision. */
static void carbon_pointer(sk_vmode *m, int kind, double x, double y, bool down)
{
    carbon *c = (carbon *)m;
    if (!c->ow)
        return;
    if (kind == SK_VIZ_DOWN) {
        c->pressed = true;
        c->moved = false;
        c->px0 = x;
        c->py0 = y;
        c->pyaw = c->yaw;
        c->ppitch = c->pitch;
        return;
    }
    if (!c->pressed)
        return;
    double dx = x - c->px0, dy = y - c->py0;
    if (kind == SK_VIZ_MOVE && down) {
        if (!c->moved && sqrt(dx * dx + dy * dy) >= 5 && inside(&c->R.atom, c->px0, c->py0)) {
            c->moved = true;
            c->turns++;
        }
        if (c->moved) {
            c->yaw = c->pyaw - dx * 0.01;
            c->pitch = SK_MAX(-1.4, SK_MIN(1.4, c->ppitch + dy * 0.01));
            c->held = c->now;
        }
        return;
    }
    if (kind != SK_VIZ_UP)
        return;
    bool moved = c->moved;
    c->pressed = false;
    if (moved || sqrt(dx * dx + dy * dy) >= 5)
        return;
    c->clicks++;
    if (inside(&c->R.diag, x, y)) {
        const box *d = &c->R.diag;
        int col = (int)SK_MAX(0, SK_MIN(4, floor((x - d->x - c->R.x0) / c->R.cw)));
        int best = -1;
        double gap = INFINITY;
        for (int k = 0; k < NL; k++) {
            if (col_of(k) != col)
                continue;
            double e = fabs(d->y + y_of(c, LV(k)->E) - y);
            if (e < gap) {
                gap = e;
                best = k;
            }
        }
        if (best >= 0 && gap < 30)
            lift(c, 0, best, "click: electron collision");
    } else if (inside(&c->R.atom, x, y)) {
        absorb(c, 0, -1, "click: a lamp");
    }
}

const sk_vmode_class sk_vmode_carbon = {sizeof(carbon), carbon_init, carbon_draw, carbon_pointer, carbon_fini};
