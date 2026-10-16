/* The neutral carbon atom's data for the Carbon mode (decision 0015,
 * issue #144): app/viz/carbon-data.js as C, the slim copy that
 * scripts/research/carbon_slim.js writes from
 * docs/research/carbon-atom-data.json. Energies in cm^-1 (E) and eV,
 * tau in s, wavelengths in nm (Ritz, vacuum and standard air), A in
 * s^-1, f the absorption oscillator strength, radii in bohr.
 *
 * Sources: NIST ASD 5.12 for the levels, lines, A and branching; Haris
 * and Kramida 2017 for the ionization limit; Bunge, Barrientos and Bunge
 * 1993 for the ground orbitals; CIE 1931 and CSS Color 4 for the
 * visible lines' colours. */
#ifndef SK_VIZ_CARBON_DATA_H
#define SK_VIZ_CARBON_DATA_H

#include "../base/base.h"

typedef struct {
    const char *id, *conf, *term; /* term: 2S+1, L, and * when odd */
    int J, g;
    bool odd;
    double E, eV;
    double tau; /* 0 for the ground level, which never decays */
} sk_carbon_level;

enum { SK_CARBON_E1, SK_CARBON_M1, SK_CARBON_E2 };
enum { SK_CARBON_VUV, SK_CARBON_UV, SK_CARBON_VISIBLE, SK_CARBON_IR };
#define SK_CARBON_NO_RGB 0xff000000u

typedef struct {
    int u, l; /* the upper and lower levels' indices */
    double vac, air; /* air 0 where NIST lists none */
    double A, f, branch;
    int type, band;
    uint32_t rgb; /* 0xRRGGBB, or SK_CARBON_NO_RGB outside the visible */
} sk_carbon_line;

typedef struct {
    const char *series;
    int n;
    double E;
} sk_carbon_rydberg;

/* The outer electron's quantum-defect model, by conf and term. */
typedef struct {
    const char *key;
    int n, l;
    double n_star;
    int nr;
    double lambda, mean_r;
} sk_carbon_outer;

/* The subshell occupations n_m of a term's M_L components: the 2p
 * subshell's for m = -1, 0, 1, and the outer subshell's for m = -l to
 * l (2p2 terms have none). */
typedef struct {
    const char *key;
    int L;
    int n_o;
    double p[4][3];
    double o[4][7];
} sk_carbon_angular;

#define SK_CARBON_LEVELS 68
#define SK_CARBON_LINES 559
#define SK_CARBON_RYDBERG 12
#define SK_CARBON_OUTER 30
#define SK_CARBON_ANGULAR 10

extern const sk_carbon_level sk_carbon_levels[SK_CARBON_LEVELS];
extern const sk_carbon_line sk_carbon_lines[SK_CARBON_LINES];
extern const sk_carbon_rydberg sk_carbon_rydberg_e[SK_CARBON_RYDBERG];
extern const sk_carbon_outer sk_carbon_outer_e[SK_CARBON_OUTER];
extern const sk_carbon_angular sk_carbon_angular_e[SK_CARBON_ANGULAR];
/* Bunge's RHF basis: rows of n, zeta and the 1s and 2s coefficients;
 * rows of n, zeta and the 2p coefficient. */
extern const double sk_carbon_rhf_s[7][4];
extern const double sk_carbon_rhf_p[5][3];
extern const double sk_carbon_mean_r_1s, sk_carbon_mean_r_2s, sk_carbon_mean_r_2p;
/* The C II limit, cm^-1 and eV, and the bohr in nm. */
extern const double sk_carbon_limit, sk_carbon_limit_ev, sk_carbon_bohr_nm;

#endif
