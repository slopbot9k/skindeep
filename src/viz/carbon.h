/* The Carbon mode's physics (carbon.c), for its tests: carbon.js's
 * clocks, orbitals, radiation patterns, decays and level names. Levels
 * and lines are indices into carbon_data.h's tables; an orbital is
 * SK_ORB_1S, SK_ORB_2S, SK_ORB_2P or an index into its outer-electron
 * terms. */
#ifndef SK_VIZ_CARBON_H
#define SK_VIZ_CARBON_H

#include "carbon_data.h"
#include "vmath.h"

enum { SK_ORB_1S = -1, SK_ORB_2S = -2, SK_ORB_2P = -3 };

/* The event clock's factor and the light clock's. */
#define SK_CARBON_EVENT 1e6
#define SK_CARBON_LIGHT 1e17

/* A level is in the ground term 3P. */
bool sk_carbon_ground(int level);
/* A level's mean wait on the event clock, seconds: its lifetime times
 * SK_CARBON_EVENT, INFINITY in the ground term. */
double sk_carbon_tau_screen(int level);
/* Light on the light clock, bohr a second on screen. */
double sk_carbon_light(void);
/* P(r) = r R(r) of an orbital. */
double sk_carbon_radial_p(int orbital, double r);
/* An orbital's radius table: q[i] is the radius below which a share i
 * / 1024 of its density lies. Its mean radius. */
const double *sk_carbon_table(int orbital);
double sk_carbon_mean_r(const double *q);
/* A line's radiation pattern for component q at cos(Theta) = c, and
 * its largest value. */
double sk_carbon_pattern(int type, int q, double c);
double sk_carbon_pattern_max(int type, int q);
/* A unit vector drawn from a pattern about z. */
void sk_carbon_detect_dir(int type, int q, sk_rng *rand, double out[3]);
/* The decay from level i for u uniform on [0, 1): a line index, by the
 * branching ratios. */
int sk_carbon_decay(int level, double u);
/* The ground term's level after a thermal collision, for u uniform. */
int sk_carbon_thermal(double u);
/* The level's name, such as 2p3s 1P°1, into out. */
void sk_carbon_level_name(int level, char *out, size_t n);
/* The one electron a line moves, upper to lower: false for a jump of
 * two electrons. */
bool sk_carbon_mover(int up, int low, int *from, int *lf, int *to, int *lt);

#endif
