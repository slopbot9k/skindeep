/* Strokes (draw.h): the outline of a path at a width, as a path that
 * fills with nonzero. Curves are flattened first. An open subpath
 * becomes one closed outline: out along one side, a cap, back along the
 * other, the other cap. A closed one becomes two, one a side, the inner
 * reversed. On the inside of a turn a side passes through the vertex,
 * so every piece of an outline winds the same way and overlaps add up
 * under nonzero instead of cancelling. */
#include "internal.h"

#include <float.h>
#include <string.h>

typedef struct {
    double x, y;
} pt;

typedef struct {
    sk_path *out;
    double hw; /* half the width */
    sk_cap cap;
    sk_join join;
    double limit;
    /* The subpath, no point twice in a row, and which points lie inside
     * a flattened curve, where the join is always round. */
    SK_VEC(pt) p;
    SK_VEC(uint8_t) smooth;
    /* The subpath has a segment, if only one of no length: a lone move
     * has none, and draws no dot. */
    bool segment;
} stroker;

static pt add(pt a, pt b)
{
    return (pt){a.x + b.x, a.y + b.y};
}

static pt sub(pt a, pt b)
{
    return (pt){a.x - b.x, a.y - b.y};
}

static pt mul(pt a, double k)
{
    return (pt){a.x * k, a.y * k};
}

/* a turned a quarter, from x toward y. */
static pt perp(pt a)
{
    return (pt){-a.y, a.x};
}

static double dot(pt a, pt b)
{
    double xx = a.x * b.x;
    double yy = a.y * b.y;
    return xx + yy;
}

/* Positive when b turns from a toward perp(a). */
static double cross(pt a, pt b)
{
    double xy = a.x * b.y;
    double yx = a.y * b.x;
    return xy - yx;
}

/* The direction from a to b, which differ. */
static pt unit(pt a, pt b)
{
    pt d = sub(b, a);
    return mul(d, 1 / sk_gfx_hypot(d.x, d.y));
}

/* A float, infinite past float's range rather than undefined. */
static float to_float(double v)
{
    return v > FLT_MAX ? INFINITY : v < -FLT_MAX ? -INFINITY : (float)v;
}

static void move_to(stroker *s, pt a)
{
    sk_path_move(s->out, to_float(a.x), to_float(a.y));
}

static void line_to(stroker *s, pt a)
{
    sk_path_line(s->out, to_float(a.x), to_float(a.y));
}

/* The arc around c from c + a to c + b, a quarter turn or less, as a
 * cubic: its control points lie along the tangents, 4/3 tan(angle / 4)
 * of the radius out, worked out from the angle's cosine and sine. */
static void arc90(stroker *s, pt c, pt a, pt b)
{
    double r2 = s->hw * s->hw;
    double cs = dot(a, b) / r2, sn = cross(a, b) / r2;
    double t2 = fabs(sn) / (1 + cs);
    double k = 4.0 / 3 * t2 / (1 + sqrt(1 + t2 * t2));
    double turn = sn < 0 ? -k : k;
    pt c1 = add(add(c, a), mul(perp(a), turn)), c2 = sub(add(c, b), mul(perp(b), turn)), e = add(c, b);
    sk_path_cubic(s->out, to_float(c1.x), to_float(c1.y), to_float(c2.x), to_float(c2.y), to_float(e.x),
                  to_float(e.y));
}

/* The arc around c from c + a to c + b, less than half a turn, the short
 * way, in pieces of a quarter turn at most. */
static void arc(stroker *s, pt c, pt a, pt b)
{
    if (dot(a, b) >= 0) {
        arc90(s, c, a, b);
        return;
    }
    pt m = add(a, b);
    m = mul(m, s->hw / sk_gfx_hypot(m.x, m.y));
    arc90(s, c, a, m);
    arc90(s, c, m, b);
}

/* p offset to the left of direction d, as perp turns. */
static pt side_of(const stroker *s, pt p, pt d)
{
    return add(p, mul(perp(d), s->hw));
}

/* From p's offset along din to its offset along dout, the outline at
 * hand standing at the first. */
static void turn(stroker *s, pt p, pt din, pt dout, bool smooth)
{
    double c = cross(din, dout), k = 1 + dot(din, dout);
    pt end = side_of(s, p, dout);
    if (c > 0 || (c == 0 && k > 1)) {
        /* The inside of the turn, or straight on. */
        if (c > 0)
            line_to(s, p);
        line_to(s, end);
        return;
    }
    pt a = mul(perp(din), s->hw), b = mul(perp(dout), s->hw);
    sk_join j = s->join;
    if (smooth) {
        /* Inside a curve the offset turns round, but where the turn is
         * slight a miter strays from the arc by hw (1 / cos(turn / 2) -
         * 1), which is hw (sqrt(2 / k) - 1): within the flattening's
         * tolerance, the miter's one point will do. */
        if (k > 0 && s->hw * (sqrt(2 / k) - 1) <= SK_GFX_TOLERANCE) {
            line_to(s, add(p, mul(add(a, b), 1 / k)));
            line_to(s, end);
            return;
        }
        j = SK_JOIN_ROUND;
    }
    if (j == SK_JOIN_ROUND) {
        if (c == 0) {
            /* Straight back: a half turn round the front. */
            pt front = mul(din, s->hw);
            arc90(s, p, a, front);
            arc90(s, p, front, b);
        } else {
            arc(s, p, a, b);
        }
        return;
    }
    /* SVG's limit: the miter's length over the width is 1 / sin of half
     * the angle between the segments, and its square is 2 / k. */
    if (j == SK_JOIN_MITER && k * s->limit * s->limit >= 2)
        line_to(s, add(p, mul(add(a, b), 1 / k)));
    line_to(s, end);
}

/* The end at p of a segment running along d, from its left side to its
 * right. */
static void end_cap(stroker *s, pt p, pt d)
{
    pt n = mul(perp(d), s->hw), e = mul(d, s->hw);
    switch (s->cap) {
    case SK_CAP_ROUND:
        arc90(s, p, n, e);
        arc90(s, p, e, mul(n, -1));
        break;
    case SK_CAP_SQUARE:
        line_to(s, add(add(p, n), e));
        line_to(s, add(sub(p, n), e));
        line_to(s, sub(p, n));
        break;
    default:
        line_to(s, sub(p, n));
        break;
    }
}

/* Point i of n, walking forward or back. */
static pt at(const stroker *s, size_t n, size_t i, bool back)
{
    return s->p.ptr[back ? n - 1 - i : i];
}

static bool smooth_at(const stroker *s, size_t n, size_t i, bool back)
{
    return s->smooth.ptr[back ? n - 1 - i : i];
}

/* One side of an open subpath of n points, from its first point's offset
 * to its last's. */
static void side(stroker *s, size_t n, bool back)
{
    pt d = unit(at(s, n, 0, back), at(s, n, 1, back));
    for (size_t i = 1; i < n; i++) {
        pt p = at(s, n, i, back);
        line_to(s, side_of(s, p, d));
        if (i + 1 < n) {
            pt next = unit(p, at(s, n, i + 1, back));
            turn(s, p, d, next, smooth_at(s, n, i, back));
            d = next;
        }
    }
}

/* One side of a closed subpath of n points, as a closed outline. */
static void ring(stroker *s, size_t n, bool back)
{
    pt p0 = at(s, n, 0, back), d0 = unit(p0, at(s, n, 1, back)), d = d0;
    move_to(s, side_of(s, p0, d0));
    for (size_t i = 1; i <= n; i++) {
        pt p = at(s, n, i % n, back);
        line_to(s, side_of(s, p, d));
        pt next = i == n ? d0 : unit(p, at(s, n, (i + 1) % n, back));
        turn(s, p, d, next, smooth_at(s, n, i % n, back));
        d = next;
    }
    sk_path_close(s->out);
}

/* A subpath of no length: a dot for round and square caps, as SVG has
 * it, square to the axes. */
static void dot_at(stroker *s, pt p)
{
    double r = s->hw;
    if (s->cap == SK_CAP_ROUND) {
        pt q[4] = {{r, 0}, {0, r}, {-r, 0}, {0, -r}};
        move_to(s, add(p, q[0]));
        for (int i = 0; i < 4; i++)
            arc90(s, p, q[i], q[(i + 1) % 4]);
        sk_path_close(s->out);
    } else if (s->cap == SK_CAP_SQUARE) {
        move_to(s, (pt){p.x - r, p.y - r});
        line_to(s, (pt){p.x + r, p.y - r});
        line_to(s, (pt){p.x + r, p.y + r});
        line_to(s, (pt){p.x - r, p.y + r});
        sk_path_close(s->out);
    }
}

static void finish(stroker *s, bool closed)
{
    size_t n = s->p.len;
    if (closed && n > 1 && s->p.ptr[0].x == s->p.ptr[n - 1].x && s->p.ptr[0].y == s->p.ptr[n - 1].y)
        n--;
    if (n == 1 && (s->segment || closed)) {
        dot_at(s, s->p.ptr[0]);
    } else if (n > 1 && closed) {
        ring(s, n, false);
        ring(s, n, true);
    } else if (n > 1) {
        pt first = s->p.ptr[0], last = s->p.ptr[n - 1];
        move_to(s, side_of(s, first, unit(first, s->p.ptr[1])));
        side(s, n, false);
        end_cap(s, last, unit(s->p.ptr[n - 2], last));
        side(s, n, true);
        end_cap(s, first, unit(s->p.ptr[1], first));
        sk_path_close(s->out);
    }
    s->p.len = s->smooth.len = 0;
    s->segment = false;
}

static void point(stroker *s, double x, double y, bool smooth)
{
    if (s->p.len) {
        pt *last = &s->p.ptr[s->p.len - 1];
        if (last->x == x && last->y == y) {
            if (!smooth)
                s->smooth.ptr[s->smooth.len - 1] = 0;
            return;
        }
    }
    sk_vec_push(&s->p, ((pt){x, y}));
    sk_vec_push(&s->smooth, smooth);
}

/* A curve from the last point, through n control points and to its end,
 * flattened as sk_fill_path flattens it, but before any transform. */
static void curve(stroker *s, const float *v, int n)
{
    double x[4] = {0}, y[4] = {0};
    x[0] = s->p.ptr[s->p.len - 1].x;
    y[0] = s->p.ptr[s->p.len - 1].y;
    for (int i = 1; i <= n; i++) {
        x[i] = v[2 * i - 2];
        y[i] = v[2 * i - 1];
    }
    double dd = sk_gfx_hypot(x[0] - 2 * x[1] + x[2], y[0] - 2 * y[1] + y[2]);
    if (n == 3)
        dd = fmax(dd, sk_gfx_hypot(x[1] - 2 * x[2] + x[3], y[1] - 2 * y[2] + y[3]));
    int steps = sk_gfx_steps(dd, n == 3);
    for (int i = 1; i < steps; i++) {
        double t = (double)i / steps, qx[4], qy[4];
        memcpy(qx, x, sizeof x);
        memcpy(qy, y, sizeof y);
        for (int level = n; level > 0; level--)
            for (int j = 0; j < level; j++) {
                qx[j] = sk_gfx_lerp(qx[j], qx[j + 1], t);
                qy[j] = sk_gfx_lerp(qy[j], qy[j + 1], t);
            }
        point(s, qx[0], qy[0], true);
    }
    point(s, x[n], y[n], false);
}

void sk_path_stroke(sk_path *out, const sk_path *p, float width, sk_cap cap, sk_join join, float miter_limit)
{
    if (!(width > 0) || !isfinite(width))
        return;
    for (size_t i = 0; i < p->pts.len; i++)
        if (!isfinite(p->pts.ptr[i]))
            return;
    stroker s = {out, width / 2.0, cap, join, miter_limit >= 1 ? miter_limit : 1, {0}, {0}, false};
    /* Stroking a path into itself reads a copy. */
    sk_path copy = {0};
    if (out == p) {
        for (size_t i = 0; i < p->verbs.len; i++)
            sk_vec_push(&copy.verbs, p->verbs.ptr[i]);
        for (size_t i = 0; i < p->pts.len; i++)
            sk_vec_push(&copy.pts, p->pts.ptr[i]);
        p = &copy;
    }
    const float *v = p->pts.ptr;
    for (size_t i = 0; i < p->verbs.len; i++) {
        switch (p->verbs.ptr[i]) {
        case SK_PATH_MOVE:
            finish(&s, false);
            point(&s, v[0], v[1], false);
            v += 2;
            break;
        case SK_PATH_LINE:
            s.segment = true;
            point(&s, v[0], v[1], false);
            v += 2;
            break;
        case SK_PATH_QUAD:
            s.segment = true;
            curve(&s, v, 2);
            v += 4;
            break;
        case SK_PATH_CUBIC:
            s.segment = true;
            curve(&s, v, 3);
            v += 6;
            break;
        default: /* SK_PATH_CLOSE */
            finish(&s, true);
            break;
        }
    }
    finish(&s, false);
    sk_vec_free(&s.p);
    sk_vec_free(&s.smooth);
    sk_vec_free(&copy.verbs);
    sk_vec_free(&copy.pts);
}
