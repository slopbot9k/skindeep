/* The rasterizer and compositor (decision 0036), in software on the UI
 * thread: rectangles, images and coverage masks composited onto an
 * image, and paths filled with anti-aliasing. Phase 3 needs what the
 * terminal, the menus and the tab strip draw; the skin runtime of phase
 * 6 adds what its CSS paint needs on the same functions.
 *
 * Every function clips to the destination and, when clip is not NULL,
 * to *clip as well. Nothing is drawn outside both. Colours come as
 * sk_color (not premultiplied); results round to nearest, and the same
 * input draws the same pixels on every machine.
 *
 * The arithmetic, which every implementation keeps exactly, so golden
 * images stay the same however the loops are written: mul(x, y) is
 * x * y / 255 rounded, as t = x * y + 128, (t + (t >> 8)) >> 8. A
 * colour is premultiplied by sk_premul; coverage or opacity k scales a
 * premultiplied pixel channel by channel, mul(channel, k); source-over
 * is dst = src + mul(dst, 255 - src alpha), channel by channel. */
#ifndef SK_GFX_DRAW_H
#define SK_GFX_DRAW_H

#include "image.h"

/* How a source pixel meets the one under it. SRC_OVER is CSS's normal
 * compositing; SRC replaces; the rest are CSS's mix-blend-mode, which
 * skins use (phase 6), applied then composited source-over.
 *
 * The modes in integers, from W3C Compositing and Blending Level 1. s
 * and d are one channel of the source and the destination, both
 * premultiplied, and sa and da their alphas, all 0..255; div255(t) is
 * mul's rounding of t / 255: t += 128, (t + (t >> 8)) >> 8. MULTIPLY,
 * SCREEN, OVERLAY, DARKEN and LIGHTEN keep source-over's alpha, sa +
 * mul(da, 255 - sa), and make each colour channel div255(t) of
 *   MULTIPLY  s (255 - da) + d (255 - sa) + s d
 *   SCREEN    255 (s + d) - s d
 *   OVERLAY   s (255 - da) + d (255 - sa) + x, where x is 2 s d when
 *             2 d <= da, else sa da - 2 (da - d) (sa - s)
 *   DARKEN    255 (s + d) - max(s da, d sa)
 *   LIGHTEN   255 (s + d) - min(s da, d sa)
 * which are the spec's cs (1 - ab) + cb (1 - as) + as ab B(Cb, Cs)
 * scaled by 255 * 255 and rounded once. t is clamped to 0..255 * 255,
 * which only a pixel whose colour exceeds its alpha could leave. ADD,
 * plus-lighter, is min(255, s + d) in every channel, alpha too. An
 * opacity or a coverage k scales the source, mul(channel, k), first. */
typedef enum {
    SK_BLEND_SRC_OVER,
    SK_BLEND_SRC,
    SK_BLEND_MULTIPLY,
    SK_BLEND_SCREEN,
    SK_BLEND_OVERLAY,
    SK_BLEND_DARKEN,
    SK_BLEND_LIGHTEN,
    SK_BLEND_ADD, /* plus-lighter */
} sk_blend;

/* r filled with c, source-over (SRC when blend says so). */
void sk_fill_rect(sk_image *dst, sk_rect r, sk_color c, sk_blend blend, const sk_rect *clip);

/* src_r of src at (x, y) of dst, with opacity 0..255 applied to the
 * source first. */
void sk_blit(sk_image *dst, int x, int y, const sk_image *src, sk_rect src_r, sk_blend blend, uint8_t opacity,
             const sk_rect *clip);

typedef enum { SK_FILTER_NEAREST, SK_FILTER_BILINEAR } sk_filter;
/* src_r of src stretched over dst_r of dst: nearest for pixel art and
 * integer scales, bilinear otherwise. Pixel centres map onto pixel
 * centres; samples beyond src_r's edge repeat its edge.
 *
 * Exactly, along each axis: pixel j of a destination n wide maps its
 * centre to c = (j + 1/2) m / n of a source m wide. Nearest takes pixel
 * floor(c). Bilinear samples at c - 1/2, in 256ths rounded down: pixel
 * i and the next, the next weighted f / 256. It interpolates across,
 * then down, each step rounded: a channel is (a (256 - f) + b f + 128)
 * >> 8. Samples outside the part of src_r inside src repeat its edge. */
void sk_blit_scaled(sk_image *dst, sk_rect dst_r, const sk_image *src, sk_rect src_r, sk_filter filter,
                    sk_blend blend, uint8_t opacity, const sk_rect *clip);

/* An 8-bit coverage mask: a glyph, a rasterized path. */
typedef struct {
    const uint8_t *a;
    int w, h, stride; /* stride in bytes */
} sk_mask;

/* The mask at (x, y) of dst, painted in c: coverage times c's alpha,
 * source-over. Glyphs are drawn this way. */
void sk_fill_mask(sk_image *dst, int x, int y, const sk_mask *m, sk_color c, const sk_rect *clip);

/* ---- Paths ---- */

/* A 2D affine transform: x' = a x + c y + e, y' = b x + d y + f. */
typedef struct {
    float a, b, c, d, e, f;
} sk_xform;

#define SK_XFORM_IDENTITY ((sk_xform){1, 0, 0, 1, 0, 0})

typedef enum { SK_FILL_NONZERO, SK_FILL_EVENODD } sk_fill_rule;

/* Subpaths of lines and Bézier curves, in float pixels. A path owns its
 * points; free it with sk_path_free. As canvas has it, a line or curve
 * with no subpath open starts one at its first point, and after a close
 * it starts one where the closed subpath began. */
typedef struct sk_path sk_path;

sk_path *sk_path_new(void);
void sk_path_free(sk_path *p);
void sk_path_clear(sk_path *p);
void sk_path_move(sk_path *p, float x, float y);
void sk_path_line(sk_path *p, float x, float y);
void sk_path_quad(sk_path *p, float cx, float cy, float x, float y);
void sk_path_cubic(sk_path *p, float c1x, float c1y, float c2x, float c2y, float x, float y);
void sk_path_close(sk_path *p);
/* Closed shapes, as CSS boxes need them. radius is clamped to half the
 * shorter side; an ellipse fills its bounding box. */
void sk_path_rect(sk_path *p, float x, float y, float w, float h);
void sk_path_round_rect(sk_path *p, float x, float y, float w, float h, float radius);
void sk_path_ellipse(sk_path *p, float x, float y, float w, float h);

/* The path through m filled in c with anti-aliased edges (exact area
 * coverage per pixel, not supersampled), source-over.
 *
 * Every subpath is closed for filling. Curves become lines no more than
 * 0.2 pixel from them, after m. Points are then rounded to 1/256 of a
 * pixel, and from there integers decide: a pixel's coverage is its area
 * inside the path, a, as a running sum of signed areas, made
 * round(255 min(|a|, 1)) under nonzero, and under even-odd folded first
 * to 1 - |1 - (|a| mod 2)|. m NULL is the identity. A path with a NaN
 * or infinite point, after m, draws nothing. Pixels further than 2^22
 * from the origin are not drawn. */
void sk_fill_path(sk_image *dst, const sk_path *p, const sk_xform *m, sk_fill_rule rule, sk_color c,
                  const sk_rect *clip);
/* The same coverage into a mask of dst_w by dst_h (one byte a pixel,
 * stride dst_w), which the caller frees: for shapes drawn many times.
 * NULL when dst_w or dst_h is 0 or less. */
uint8_t *sk_path_mask(const sk_path *p, const sk_xform *m, sk_fill_rule rule, int dst_w, int dst_h);

typedef enum { SK_CAP_BUTT, SK_CAP_ROUND, SK_CAP_SQUARE } sk_cap;
typedef enum { SK_JOIN_MITER, SK_JOIN_ROUND, SK_JOIN_BEVEL } sk_join;
/* The outline of p stroked width wide, as a path to fill with nonzero:
 * lines, borders, the visualizer's traces. miter_limit as SVG's.
 *
 * The outline is added to out, which may be p itself. Curves are
 * flattened as sk_fill_path flattens them, in p's own units, and turn
 * round between their pieces. A subpath of no length, a line to where
 * it starts or a close, is a dot under a round or square cap, square to
 * the axes, and nothing under a butt cap; a lone move is nothing. A
 * miter_limit below 1 is 1. A width that is not positive and finite, or
 * a NaN or infinite point, strokes to nothing. */
void sk_path_stroke(sk_path *out, const sk_path *p, float width, sk_cap cap, sk_join join, float miter_limit);

#endif
