/* Go mode (decision 0015), app/viz/go.js: a 13 by 13 board on which
 * every event of the pane's signal places a stone, black and white in
 * turn, by the rules of Go: a group with no liberty is captured, a move
 * that leaves its own group none is suicide and refused, and a single
 * stone just captured may not be retaken at once (ko). The move is
 * picked by the event: a prompt opens on a big point away from the
 * stones, a tool call answers near the last move and rings in its
 * class's colour, a stop hunts for captures, a subagent plays two
 * moves. Neither side fills its own eye, so groups with two live. When
 * neither side has a move, or the board is nearly full, it is counted
 * and cleared. While the agent idles the board shades the territory
 * each side holds. A click on a point plays it for the side to move, if
 * the rules allow. */
#include "go.h"

#include "internal.h"

/* ---- The rules ---- */

void sk_go_init(sk_go_board *b, int n)
{
    memset(b, 0, sizeof *b);
    b->n = SK_MAX(2, SK_MIN(n, SK_GO_MAX));
    b->ko = -1;
}

void sk_go_clear(sk_go_board *b)
{
    memset(b->cells, 0, sizeof b->cells);
    b->ko = -1;
}

int sk_go_adj(const sk_go_board *b, int i, int adj[4])
{
    int n = b->n, x = i % n, y = i / n, k = 0;
    if (x > 0)
        adj[k++] = i - 1;
    if (x < n - 1)
        adj[k++] = i + 1;
    if (y > 0)
        adj[k++] = i - n;
    if (y < n - 1)
        adj[k++] = i + n;
    return k;
}

void sk_go_group_at(const sk_go_board *b, int i, sk_go_group *g)
{
    uint8_t seen[SK_GO_MAX * SK_GO_MAX] = {0}, lib[SK_GO_MAX * SK_GO_MAX] = {0};
    int c = b->cells[i];
    g->n_stones = 0;
    g->n_libs = 0;
    g->stones[g->n_stones++] = i;
    seen[i] = 1;
    for (int k = 0; k < g->n_stones; k++) {
        int adj[4], na = sk_go_adj(b, g->stones[k], adj);
        for (int a = 0; a < na; a++) {
            int j = adj[a];
            if (b->cells[j] == SK_GO_EMPTY) {
                if (!lib[j]) {
                    lib[j] = 1;
                    g->n_libs++;
                }
            } else if (b->cells[j] == c && !seen[j]) {
                seen[j] = 1;
                g->stones[g->n_stones++] = j;
            }
        }
    }
}

int sk_go_play(sk_go_board *b, int i, int color, int *taken)
{
    if (b->cells[i] != SK_GO_EMPTY || i == b->ko)
        return -1;
    int other = 3 - color, n = 0;
    b->cells[i] = (int8_t)color;
    int adj[4], na = sk_go_adj(b, i, adj);
    sk_go_group g;
    for (int a = 0; a < na; a++) {
        int j = adj[a];
        if (b->cells[j] != other)
            continue;
        sk_go_group_at(b, j, &g);
        if (g.n_libs == 0)
            for (int s = 0; s < g.n_stones; s++) {
                b->cells[g.stones[s]] = SK_GO_EMPTY;
                taken[n++] = g.stones[s];
            }
    }
    sk_go_group_at(b, i, &g);
    if (g.n_libs == 0) {
        b->cells[i] = SK_GO_EMPTY;
        return -1;
    }
    b->ko = n == 1 && g.n_stones == 1 && g.n_libs == 1 ? taken[0] : -1;
    return n;
}

bool sk_go_eye(const sk_go_board *b, int i, int color)
{
    if (b->cells[i] != SK_GO_EMPTY)
        return false;
    int adj[4], na = sk_go_adj(b, i, adj);
    for (int a = 0; a < na; a++)
        if (b->cells[adj[a]] != color)
            return false;
    return true;
}

int sk_go_choose(const sk_go_board *board, int color, int kind, int last, sk_rng *rand)
{
    int n = board->n, best = -1, stones = 0;
    double top = -INFINITY;
    for (int i = 0; i < n * n; i++)
        if (board->cells[i])
            stones++;
    int taken[SK_GO_MAX * SK_GO_MAX];
    sk_go_group own, g;
    for (int i = 0; i < n * n; i++) {
        if (board->cells[i] != SK_GO_EMPTY || sk_go_eye(board, i, color))
            continue;
        sk_go_board b = *board;
        int nt = sk_go_play(&b, i, color, taken);
        if (nt < 0)
            continue;
        int x = i % n, y = i / n;
        int line = SK_MIN(SK_MIN(x, y), SK_MIN(n - 1 - x, n - 1 - y));
        sk_go_group_at(&b, i, &own);
        double s = (rand ? sk_rng_next(rand) : 0) * 6 + 40 * nt;
        if (own.n_libs == 1 && !nt)
            s -= 30;
        else if (own.n_libs == 2)
            s -= 4;
        int adj[4], na = sk_go_adj(board, i, adj);
        for (int a = 0; a < na; a++) {
            int j = adj[a], c = board->cells[j];
            if (c == color) {
                sk_go_group_at(board, j, &g);
                if (g.n_libs == 1)
                    s += 18;
            }
            if (c == 3 - color) {
                int l = 0;
                if (b.cells[j]) {
                    sk_go_group_at(&b, j, &g);
                    l = g.n_libs;
                }
                s += l == 1 ? 12 : 2;
            }
        }
        if (stones < n)
            s += line == 2 || line == 3 ? 8 : line < 1 ? -10 : 0;
        else if (line == 0)
            s -= 3;
        int near = 0;
        if (last >= 0) {
            int lx = last % n;
            near = SK_MAX(abs(lx - x), abs((last - lx) / n - y));
        }
        if (kind == SK_EV_PROMPT) {
            int far = 9;
            for (int j = 0; j < n * n; j++)
                if (board->cells[j])
                    far = SK_MIN(far, SK_MAX(abs(j % n - x), abs(j / n - y)));
            s += 3 * SK_MIN(far, 4) + (line >= 2 ? 4 : 0);
        } else if (kind == SK_EV_STOP) {
            s += 30 * nt;
        } else if (last >= 0) {
            s += SK_MAX(0, 8 - 3 * near);
        }
        if (s > top) {
            top = s;
            best = i;
        }
    }
    return best;
}

void sk_go_territory(const sk_go_board *b, double max, int8_t *own)
{
    int n = b->n;
    uint8_t seen[SK_GO_MAX * SK_GO_MAX] = {0};
    int region[SK_GO_MAX * SK_GO_MAX];
    memset(own, 0, (size_t)(n * n));
    for (int i = 0; i < n * n; i++) {
        if (b->cells[i]) {
            own[i] = b->cells[i];
            continue;
        }
        if (seen[i])
            continue;
        int len = 0, touch = 0;
        region[len++] = i;
        seen[i] = 1;
        for (int k = 0; k < len; k++) {
            int adj[4], na = sk_go_adj(b, region[k], adj);
            for (int a = 0; a < na; a++) {
                int j = adj[a], c = b->cells[j];
                if (c)
                    touch |= c;
                else if (!seen[j]) {
                    seen[j] = 1;
                    region[len++] = j;
                }
            }
        }
        if ((touch == SK_GO_BLACK || touch == SK_GO_WHITE) && len <= max)
            for (int k = 0; k < len; k++)
                own[region[k]] = (int8_t)touch;
    }
}

/* ---- The mode ---- */

#define N 13
#define MAX_ANIM 512

typedef struct {
    int i, color;
    double t, hue; /* hue < 0: none, a stone with no ring */
} anim;

typedef struct {
    sk_vmode base;
    sk_go_board board;
    int color, last, passes;
    bool dirty;
    int8_t owner[N * N];
    /* Animations: stones placed and taken. */
    SK_VEC(anim) placed;
    SK_VEC(anim) fallen;
    double ending;
    /* The points of stones still settling, which the painted board
     * leaves out. */
    bool moving[N * N];
    int n_moving;
    double size, step, gx, gy, gm;
    bool have_geo;
    /* The wood, the grid and the settled stones, repainted when a move
     * lands: the wood and grid kept as they were painted at this size,
     * and the settled stones each a copy of a stone painted once. */
    sk_image wood, grid, sprite[2];
    double sprite_c;
    sk_vc wvc;
    double tick;
    int clicks;
} go;

static void go_init(sk_vmode *m)
{
    go *g = (go *)m;
    sk_go_init(&g->board, N);
    g->color = SK_GO_BLACK;
    g->last = -1;
    g->dirty = true;
    sk_vc_init(&g->wvc, &g->wood, 1);
}

static void go_fini(sk_vmode *m)
{
    go *g = (go *)m;
    sk_vec_free(&g->placed);
    sk_vec_free(&g->fallen);
    sk_vc_free(&g->wvc);
    sk_image_free(&g->wood);
    sk_image_free(&g->grid);
    sk_image_free(&g->sprite[0]);
    sk_image_free(&g->sprite[1]);
}

/* Color's stone at i, which took the stones taken. */
static void place(go *g, int i, const int *taken, int nt, double hue, double t)
{
    g->passes = 0;
    anim a = {i, g->color, t, hue};
    if (g->placed.len < MAX_ANIM)
        sk_vec_push(&g->placed, a);
    for (int k = 0; k < nt; k++) {
        anim f = {taken[k], 3 - g->color, t, -1};
        if (g->fallen.len < MAX_ANIM)
            sk_vec_push(&g->fallen, f);
    }
    g->last = i;
    g->dirty = true;
}

/* The other side to move, the board counted when it is done. */
static void turn(go *g, double t)
{
    g->color = 3 - g->color;
    int stones = 0;
    for (int i = 0; i < N * N; i++)
        if (g->board.cells[i])
            stones++;
    if (g->passes >= 2 || stones > N * N * 0.8)
        g->ending = t;
}

static void move(go *g, int kind, int cls, double t)
{
    if (g->ending)
        return;
    int i = sk_go_choose(&g->board, g->color, kind, g->last, &g->base.rng);
    if (i < 0) {
        g->passes++;
    } else {
        int taken[N * N];
        int nt = sk_go_play(&g->board, i, g->color, taken);
        place(g, i, taken, SK_MAX(0, nt), kind == SK_EV_TOOL ? sk_viz_class_hue(cls) : -1, t);
    }
    turn(g, t);
}

/* The board's geometry for a w by h pane. */
static void layout(go *g, int w, int h)
{
    g->size = floor(SK_MIN(w, h) * 0.96);
    g->step = g->size / (N + 0.6);
    g->gx = floor((w - g->size) / 2);
    g->gy = floor((h - g->size) / 2);
    g->gm = g->step * 0.8;
    g->have_geo = true;
}

static void stone(sk_vc *c, double x, double y, double r, int color, double alpha)
{
    sk_vc_alpha(c, alpha);
    sk_vc_paint grad = sk_vc_radial(x - r * 0.35, y - r * 0.35, r * 0.1, x, y, r);
    if (color == SK_GO_BLACK) {
        sk_vc_add_stop(&grad, 0, SK_RGB(0x66, 0x66, 0x66));
        sk_vc_add_stop(&grad, 1, SK_RGB(0x08, 0x08, 0x08));
    } else {
        sk_vc_add_stop(&grad, 0, SK_RGB(0xff, 0xff, 0xff));
        sk_vc_add_stop(&grad, 1, SK_RGB(0xb8, 0xb4, 0xaa));
    }
    sk_vc_begin(c);
    sk_vc_arc(c, x, y, r, 0, SK_TAU, false);
    sk_vc_fill(c, &grad, SK_FILL_NONZERO);
    sk_vc_alpha(c, 1);
}

/* The wood and the grid, and the two stones, at a new size. */
static void paint_base(go *g, double scale)
{
    double size = g->size, step = g->step, m = g->gm;
    int px = (int)floor(size * scale + 0.5);
    sk_image_free(&g->wood);
    sk_image_free(&g->grid);
    sk_image_init(&g->wood, px, px);
    sk_image_init(&g->grid, px, px);
    sk_vc *wg = &g->wvc;
    sk_vc_target(wg, &g->grid, scale);
    sk_vc_paint grad = sk_vc_linear(0, 0, size, size);
    sk_vc_add_stop(&grad, 0, SK_RGB(0xd9, 0xa8, 0x5c));
    sk_vc_add_stop(&grad, 1, SK_RGB(0xb9, 0x82, 0x3e));
    sk_vc_fill_rect(wg, 0, 0, size, size, &grad);
    sk_vc_paint grain = sk_vc_solid(sk_vc_rgba(120, 70, 20, 0.18));
    sk_vc_line_width(wg, 1);
    for (int k = 0; k < 26; k++) {
        sk_vc_begin(wg);
        for (int x = 0; x <= size; x += 8) {
            double y = k * size / 25 + 6 * sk_vm_sin(x / 60.0 + k * 1.7) + 3 * sk_vm_sin(x / 17.0 + k);
            if (x)
                sk_vc_line(wg, x, y);
            else
                sk_vc_move(wg, x, y);
        }
        sk_vc_stroke(wg, &grain);
    }
    sk_vc_paint ink = sk_vc_solid(SK_RGB(0x2a, 0x1a, 0x08));
    sk_vc_line_width(wg, SK_MAX(1, step / 22));
    sk_vc_begin(wg);
    for (int k = 0; k < N; k++) {
        double p = sk_vm_round(m + k * step) + 0.5;
        sk_vc_move(wg, m, p);
        sk_vc_line(wg, m + (N - 1) * step, p);
        sk_vc_move(wg, p, m);
        sk_vc_line(wg, p, m + (N - 1) * step);
    }
    sk_vc_stroke(wg, &ink);
    static const int hoshi[5][2] = {{3, 3}, {3, 9}, {9, 3}, {9, 9}, {6, 6}};
    for (int k = 0; k < 5; k++) {
        sk_vc_begin(wg);
        sk_vc_arc(wg, m + hoshi[k][0] * step, m + hoshi[k][1] * step, SK_MAX(1.5, step / 9), 0, SK_TAU, false);
        sk_vc_fill(wg, &ink, SK_FILL_NONZERO);
    }
    /* A stone of each colour about the centre of its image. */
    double r = step * 0.47;
    int side = (int)ceil(2 * r * scale) + 4;
    g->sprite_c = side / 2.0;
    for (int c = 0; c < 2; c++) {
        sk_image_free(&g->sprite[c]);
        sk_image_init(&g->sprite[c], side, side);
        sk_vc_target(wg, &g->sprite[c], scale);
        stone(wg, g->sprite_c / scale, g->sprite_c / scale, r, c ? SK_GO_WHITE : SK_GO_BLACK, 1);
    }
}

/* The wood, the grid and the stones, redrawn when a move lands. */
static void paint(go *g, double scale)
{
    double step = g->step, m = g->gm;
    if (!g->grid.px || g->grid.w != (int)floor(g->size * scale + 0.5))
        paint_base(g, scale);
    sk_blit(&g->wood, 0, 0, &g->grid, sk_image_bounds(&g->grid), SK_BLEND_SRC, 255, NULL);
    for (int i = 0; i < N * N; i++) {
        if (!g->board.cells[i] || g->moving[i])
            continue;
        const sk_image *sp = &g->sprite[g->board.cells[i] == SK_GO_WHITE];
        double x = (m + (i % N) * step) * scale - g->sprite_c, y = (m + (i / N) * step) * scale - g->sprite_c;
        sk_blit(&g->wood, (int)floor(x + 0.5), (int)floor(y + 0.5), sp, sk_image_bounds(sp), SK_BLEND_SRC_OVER, 255,
                NULL);
    }
    /* A region larger than a quarter of the board is open ground, not
     * yet anyone's. */
    sk_go_territory(&g->board, N * N / 4.0, g->owner);
    g->dirty = false;
}

static void at(const go *g, int i, double *x, double *y)
{
    *x = g->gx + g->gm + (i % N) * g->step;
    *y = g->gy + g->gm + (i / N) * g->step;
}

#define PLACED_LIFE(a) (t - (a).t < ((a).hue < 0 ? 0.3 : 1.2))
#define FALLEN_LIFE(a) (t - (a).t < 0.8)

static void go_draw(sk_vmode *m, sk_synth *s, double dt)
{
    (void)dt;
    go *g = (go *)m;
    int w = m->w, h = m->h;
    if (w < 8 || h < 8)
        return;
    double t = s->t;
    g->tick = t;
    if (m->fresh || !g->have_geo) {
        layout(g, w, h);
        sk_image_free(&g->grid);
        g->dirty = true;
    }
    sk_vmode_events(m, s);
    for (size_t k = 0; k < m->ev.len; k++) {
        move(g, m->ev.ptr[k].kind, m->ev.ptr[k].cls, t);
        if (m->ev.ptr[k].kind == SK_EV_AGENT)
            move(g, m->ev.ptr[k].kind, m->ev.ptr[k].cls, t);
    }
    if (g->ending && t - g->ending > 4) {
        sk_go_clear(&g->board);
        g->ending = 0;
        g->last = -1;
        g->passes = 0;
        g->color = SK_GO_BLACK;
        g->placed.len = 0;
        g->fallen.len = 0;
        g->dirty = true;
    }
    /* A placed stone settles in 0.3 s, then joins the painted board. A
     * tool call's ring spreads for 1.2 s. */
    SK_VIZ_KEEP(&g->placed, PLACED_LIFE);
    bool now[N * N] = {0};
    int n_now = 0;
    for (size_t k = 0; k < g->placed.len; k++)
        if (t - g->placed.ptr[k].t < 0.3 && !now[g->placed.ptr[k].i]) {
            now[g->placed.ptr[k].i] = true;
            n_now++;
        }
    if (n_now != g->n_moving || memcmp(now, g->moving, sizeof now) != 0) {
        memcpy(g->moving, now, sizeof now);
        g->n_moving = n_now;
        g->dirty = true;
    }
    SK_VIZ_KEEP(&g->fallen, FALLEN_LIFE);
    if (g->dirty)
        paint(g, m->scale);

    sk_vc *c = &m->vc;
    double step = g->step, size = g->size;
    const sk_synth_levels *L = &s->level;
    sk_vc_paint p = sk_vc_solid(sk_vc_rgba(10 + 14 * L->busy, 8 + 8 * L->busy, 6, 1));
    sk_vc_fill_rect(c, 0, 0, w, h, &p);
    sk_vc_image(c, &g->wood, g->gx, g->gy, size, size, false);
    /* The lamp dims while the agent idles. */
    if (L->idle > 0.02) {
        p = sk_vc_solid(sk_vc_rgba(0, 0, 0, 0.35 * L->idle));
        sk_vc_fill_rect(c, g->gx, g->gy, size, size, &p);
    }
    /* Territory, as small squares, fading in while idle or counting. */
    double shade = SK_MAX(L->idle, g->ending ? SK_MIN(1, (t - g->ending) / 0.8) : 0);
    if (shade > 0.02) {
        double q = step * 0.36;
        for (int i = 0; i < N * N; i++) {
            if (!g->owner[i] || g->board.cells[i])
                continue;
            double x, y;
            at(g, i, &x, &y);
            p = sk_vc_solid(g->owner[i] == SK_GO_BLACK ? sk_vc_rgba(10, 10, 10, 0.75 * shade)
                                                       : sk_vc_rgba(250, 250, 245, 0.85 * shade));
            sk_vc_fill_rect(c, x - q / 2, y - q / 2, q, q, &p);
        }
    }
    for (size_t k = 0; k < g->fallen.len; k++) {
        const anim *f = &g->fallen.ptr[k];
        double x, y, a = 1 - (t - f->t) / 0.8;
        at(g, f->i, &x, &y);
        stone(c, x, y - (1 - a) * step * 0.6, step * 0.47 * (1 + 0.3 * (1 - a)), f->color, a);
    }
    for (size_t k = 0; k < g->placed.len; k++) {
        const anim *pl = &g->placed.ptr[k];
        double x, y, a = SK_MIN(1, (t - pl->t) / 0.3);
        at(g, pl->i, &x, &y);
        if (t - pl->t < 0.3 && g->board.cells[pl->i] == pl->color)
            stone(c, x, y, step * 0.47 * (1.4 - 0.4 * a), pl->color, a);
        if (pl->hue >= 0) {
            p = sk_vc_solid(sk_vc_hsla(pl->hue, 95, 60, SK_MAX(0, 1 - (t - pl->t) / 1.2)));
            sk_vc_line_width(c, SK_MAX(2, step / 8));
            sk_vc_begin(c);
            sk_vc_arc(c, x, y, step * (0.5 + 0.8 * (t - pl->t)), 0, SK_TAU, false);
            sk_vc_stroke(c, &p);
        }
    }
    if (g->last >= 0 && g->board.cells[g->last]) {
        double x, y;
        at(g, g->last, &x, &y);
        p = sk_vc_solid(g->board.cells[g->last] == SK_GO_BLACK ? sk_vc_rgba(255, 255, 255, 0.8)
                                                              : sk_vc_rgba(0, 0, 0, 0.7));
        sk_vc_line_width(c, SK_MAX(1, step / 14));
        sk_vc_begin(c);
        sk_vc_arc(c, x, y, step * (0.2 + 0.04 * sk_vm_sin(t * 4)), 0, SK_TAU, false);
        sk_vc_stroke(c, &p);
    }
    if (g->ending) {
        p = sk_vc_solid(sk_vc_rgba(0, 0, 0, SK_MAX(0, (t - g->ending - 3) / 1)));
        sk_vc_fill_rect(c, g->gx, g->gy, size, size, &p);
    }
}

/* A click within half a step of a point plays it, if it is legal. */
static void go_pointer(sk_vmode *m, int kind, double x, double y, bool down)
{
    (void)down;
    go *g = (go *)m;
    if (kind != SK_VIZ_DOWN || !g->have_geo || g->ending)
        return;
    double step = g->step, gm = g->gm;
    int gx = (int)sk_vm_round((x - g->gx - gm) / step), gy = (int)sk_vm_round((y - g->gy - gm) / step);
    if (gx < 0 || gy < 0 || gx >= N || gy >= N)
        return;
    double dx = x - g->gx - gm - gx * step, dy = y - g->gy - gm - gy * step;
    if (sqrt(dx * dx + dy * dy) > step / 2)
        return;
    int i = gy * N + gx, taken[N * N];
    int nt = sk_go_play(&g->board, i, g->color, taken);
    if (nt < 0)
        return;
    g->clicks++;
    place(g, i, taken, nt, -1, g->tick);
    turn(g, g->tick);
}

const sk_vmode_class sk_vmode_go = {sizeof(go), go_init, go_draw, go_pointer, go_fini};
