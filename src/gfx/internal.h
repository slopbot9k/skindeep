/* What the files of gfx share and draw.h does not declare: the pixel
 * arithmetic, the rows the drawing functions composite, the inside of a
 * path and the flattening of its curves. */
#ifndef SK_GFX_INTERNAL_H
#define SK_GFX_INTERNAL_H

#include "draw.h"

#include <math.h>

/* x * y / 255 rounded, for x * y up to 255 * 255: draw.h's mul. */
static inline uint32_t px_mul(uint32_t x, uint32_t y)
{
    uint32_t t = x * y + 128;
    return (t + (t >> 8)) >> 8;
}

/* t / 255 rounded, for t up to 255 * 255: px_mul's last step. */
static inline uint32_t px_div255(uint32_t t)
{
    t += 128;
    return (t + (t >> 8)) >> 8;
}

/* p scaled by k, channel by channel, two channels to a multiply. A
 * 16-bit lane holds x * k + 128 <= 65153 and then at most 65407, so no
 * carry crosses into the next lane and each channel is px_mul's. */
static inline sk_px px_scale(sk_px p, uint32_t k)
{
    uint32_t rb = (p & 0x00ff00ffu) * k + 0x00800080u;
    uint32_t ag = ((p >> 8) & 0x00ff00ffu) * k + 0x00800080u;
    rb = ((rb + ((rb >> 8) & 0x00ff00ffu)) >> 8) & 0x00ff00ffu;
    ag = (ag + ((ag >> 8) & 0x00ff00ffu)) & 0xff00ff00u;
    return rb | ag;
}

/* Source-over, draw.h's: src + mul(dst, 255 - src alpha). */
static inline sk_px px_over(sk_px s, sk_px d)
{
    return s + px_scale(d, 255 - (s >> 24));
}

/* d[0..n) under p covering cover (0..255) of each pixel, source-over:
 * the rows of a filled path. */
void sk_gfx_span(sk_px *d, int n, sk_px p, uint32_t cover);

/* ---- Paths ---- */

enum { SK_PATH_MOVE, SK_PATH_LINE, SK_PATH_QUAD, SK_PATH_CUBIC, SK_PATH_CLOSE };

/* Each subpath starts with a move. pts holds x, y pairs: one for a move
 * or a line, two for a quadratic, three for a cubic, none for a close. */
struct sk_path {
    SK_VEC(uint8_t) verbs;
    SK_VEC(float) pts;
    float sx, sy; /* where the last subpath began */
    bool open;    /* a subpath is open, so a line continues it */
    bool closed;  /* the last subpath was closed, so a line reopens it */
};

/* How far a chord may stray from its curve, in pixels. */
#define SK_GFX_TOLERANCE 0.2
/* At most this many chords to a curve, however large. */
#define SK_GFX_MAX_STEPS 1024

/* Chords for a curve whose second differences reach dd, a quadratic's
 * or the larger of a cubic's two: a chord over a parameter step h
 * strays at most |B''| h^2 / 8 from the curve, and |B''| is 2 dd for a
 * quadratic and at most 6 dd for a cubic. NaN and infinity get the most. */
static inline int sk_gfx_steps(double dd, bool cubic)
{
    double n = ceil(sqrt(dd * (cubic ? 0.75 : 0.25) / SK_GFX_TOLERANCE));
    if (!(n <= SK_GFX_MAX_STEPS))
        return SK_GFX_MAX_STEPS;
    return n < 1 ? 1 : (int)n;
}

/* a + (b - a) t in separate steps: no compiler fuses them into one
 * rounding, so every machine flattens curves to the same points. */
static inline double sk_gfx_lerp(double a, double b, double t)
{
    double d = b - a;
    d *= t;
    return a + d;
}

/* The length of (x, y), the same on every machine. */
static inline double sk_gfx_hypot(double x, double y)
{
    double xx = x * x;
    double yy = y * y;
    return sqrt(xx + yy);
}

#endif
