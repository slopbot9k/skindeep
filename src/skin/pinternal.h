/* What the painter's files share and paint.h does not declare: the
 * canvas a paint goes to, the one compositing loop every fill runs
 * through, shapes as coverage, and the box decorations decor.c paints. */
#ifndef SK_SKIN_PINTERNAL_H
#define SK_SKIN_PINTERNAL_H

#include "../gfx/draw.h"
#include "blur.h"
#include "paint.h"

#include <math.h>

/* Where paint goes: an image, the mapping from the skin area's CSS px
 * to its pixels, and the clip, a rectangle of the image and, when the
 * clip is not a rectangle, its coverage (clip.w by clip.h). */
typedef struct {
    sk_image *im;
    sk_xform base;
    sk_rect clip;
    uint8_t *mask;
} canvas;

typedef struct {
    const sk_paint_env *env;
    const sk_layout *l;
    const sk_lnode *nodes;
    SK_VEC(uint8_t *) masks; /* clip masks to free */
} painter;

/* ---- Pixels ---- */

/* x * y / 255 rounded: src/gfx/draw.h's mul. */
static inline uint32_t pt_mul(uint32_t x, uint32_t y)
{
    uint32_t t = x * y + 128;
    return (t + (t >> 8)) >> 8;
}

/* p scaled by k, channel by channel. */
static inline sk_px pt_scale(sk_px p, uint32_t k)
{
    uint32_t rb = (p & 0x00ff00ffu) * k + 0x00800080u;
    uint32_t ag = ((p >> 8) & 0x00ff00ffu) * k + 0x00800080u;
    rb = ((rb + ((rb >> 8) & 0x00ff00ffu)) >> 8) & 0x00ff00ffu;
    ag = (ag + ((ag >> 8) & 0x00ff00ffu)) & 0xff00ff00u;
    return rb | ag;
}

static inline sk_px pt_over(sk_px s, sk_px d)
{
    return s + pt_scale(d, 255 - (s >> 24));
}

/* A CSS colour, not premultiplied, times alpha a (0 to 1), as a
 * premultiplied pixel. */
sk_px pt_color(sk_css_color c, float a);
/* d under s by blend mode b, as src/gfx/draw.h composites. */
sk_px pt_blend(sk_blend b, sk_px s, sk_px d);
sk_blend pt_mix(uint8_t mix);

/* ---- Filling ---- */

/* A source of pixels: a colour, or a row function. */
typedef struct pt_shader pt_shader;
struct pt_shader {
    sk_px solid;
    void (*row)(const pt_shader *s, int x, int y, int n, sk_px *out);
    const void *data;
};

/* r of the canvas under sh, each pixel's coverage cov (r.w by r.h,
 * stride cs; NULL covers all), opacity 0..255 and the clip applied,
 * by blend. */
void pt_fill(canvas *cv, sk_rect r, const uint8_t *cov, int cs, const pt_shader *sh, sk_blend blend,
             uint32_t opacity);
/* The same over an image's pixels: src at (x, y) of the canvas. */
void pt_fill_image(canvas *cv, int x, int y, const sk_image *src, const uint8_t *cov, int cs, sk_blend blend,
                   uint32_t opacity);
/* src mapped by m, its pixels to the canvas's, sampled bilinearly,
 * transparent beyond its edges. */
void pt_blit_affine(canvas *cv, const sk_image *src, const sk_xform *m, sk_blend blend, uint32_t opacity);

/* ---- Shapes ---- */

/* A rectangle with elliptical corners (SK_TOP_LEFT ...), in pixels. */
typedef struct {
    float x, y, w, h;
    float rx[4], ry[4];
} pt_rrect;

bool pt_rrect_square(const pt_rrect *r);
void pt_rrect_path(sk_path *p, const pt_rrect *r);
/* r grown by d each side (shrunk when d is negative), the radii as
 * CSS grows a shadow's: by d, less for a radius smaller than d. */
pt_rrect pt_rrect_spread(const pt_rrect *r, float dx, float dy);
/* The device rectangle that holds a float rectangle. */
sk_rect pt_round_out(sk_frect r);
/* The coverage of p mapped by m over r: r.w by r.h bytes. */
uint8_t *pt_cover(const sk_path *p, const sk_xform *m, sk_rect r, sk_fill_rule rule);
/* The coverage of a rounded rectangle over r. */
uint8_t *pt_cover_rrect(const pt_rrect *rr, sk_rect r);

/* ---- Boxes ---- */

/* The mapping of box i's CSS px to the canvas's pixels. */
sk_xform pt_map(const painter *P, const canvas *cv, uint32_t i);
static inline bool pt_square_map(const sk_xform *m)
{
    return fabsf(m->b) < 1e-6f && fabsf(m->c) < 1e-6f;
}
/* A CSS rectangle as pixels under an axis-aligned m, snapped to whole
 * pixels as Chromium snaps a box. */
sk_frect pt_snap(const sk_xform *m, sk_frect r);
/* A box's border, padding or content box as a rounded rectangle in
 * pixels: its border-radius, less the borders and padding inside. */
pt_rrect pt_box_rrect(const painter *P, uint32_t i, const sk_xform *m, uint8_t which);

/* decor.c: a box's own paint, in CSS's order. */
void pt_shadows(painter *P, canvas *cv, uint32_t i, bool inset);
void pt_background(painter *P, canvas *cv, uint32_t i);
void pt_border(painter *P, canvas *cv, uint32_t i);
void pt_outline(painter *P, canvas *cv, uint32_t i);
/* An image source drawn over a device rectangle dst, no further than
 * cov_r, through the coverage cov over cov_r when it is not NULL;
 * nearest for pixelated. */
void pt_image(painter *P, canvas *cv, uint32_t source, sk_rect dst, const uint8_t *cov, sk_rect cov_r,
              bool pixelated, sk_blend blend, uint32_t opacity);

#endif
