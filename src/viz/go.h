/* Go's rules for the Go mode (go.c), apart from drawing, for its tests:
 * the board of app/viz/go.js's Board, choose and territory. Points are
 * y n + x. */
#ifndef SK_VIZ_GO_H
#define SK_VIZ_GO_H

#include "vmath.h"

#define SK_GO_MAX 19
enum { SK_GO_EMPTY, SK_GO_BLACK, SK_GO_WHITE };

typedef struct {
    int n;
    int8_t cells[SK_GO_MAX * SK_GO_MAX];
    int ko; /* the point a single stone was just captured from, or -1 */
} sk_go_board;

/* A group's stones and its liberties. */
typedef struct {
    int n_stones, n_libs;
    int stones[SK_GO_MAX * SK_GO_MAX];
} sk_go_group;

void sk_go_init(sk_go_board *b, int n);
void sk_go_clear(sk_go_board *b);
/* The neighbours of i into adj, up to four; returns how many. */
int sk_go_adj(const sk_go_board *b, int i, int adj[4]);
void sk_go_group_at(const sk_go_board *b, int i, sk_go_group *g);
/* color plays at i: the stones it captures into taken, their count, or
 * -1 for an illegal move, the point taken, the ko point or suicide. */
int sk_go_play(sk_go_board *b, int i, int color, int *taken);
/* An empty point every neighbour of which is color's. */
bool sk_go_eye(const sk_go_board *b, int i, int color);
/* The move color plays for an event of kind, or -1 for a pass: every
 * legal move but its own eyes scored, the best taken. last: the last
 * move's point or -1. rand: Math.random's place, or NULL for 0. */
int sk_go_choose(const sk_go_board *b, int color, int kind, int last, sk_rng *rand);
/* Area score: each point's owner, its stone's colour, or the one colour
 * an empty region of at most max points touches, else 0. */
void sk_go_territory(const sk_go_board *b, double max, int8_t *own);

#endif
