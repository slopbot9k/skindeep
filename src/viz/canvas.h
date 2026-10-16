/* What the visualizer's modes draw with: the part of Canvas2D that
 * the JavaScript modes use, over an sk_image, so each mode reads as its
 * JavaScript does. A transform, globalAlpha, the composite operations
 * source-over and lighter, paths of lines, arcs, ellipses and curves,
 * fills and strokes with caps, joins and dashes, linear and radial
 * gradients, clipping, images drawn scaled, squares by the thousand,
 * and text in DejaVu Sans Mono.
 *
 * Coordinates are a mode's points, the CSS pixels of the JavaScript
 * canvas, and the canvas multiplies them by its scale into the image's
 * pixels: a mode draws the same picture at 1x and at 2x, sharper.
 *
 * As canvas has it, a path's points are taken through the transform of
 * when they are added; a gradient's through the transform of the fill.
 * Curves and arcs become lines a quarter of a pixel from them as they
 * are added. A stroke's width is the transform's scale times the line
 * width: exact for the rotations and uniform scales the modes use. */
#ifndef SK_VIZ_CANVAS_H
#define SK_VIZ_CANVAS_H

#include "../gfx/draw.h"
#include "../text/text.h"
#include "vmath.h"

/* x' = a x + c y + e, y' = b x + d y + f. */
typedef struct {
    double a, b, c, d, e, f;
} sk_vc_xform;

typedef enum { SK_VC_SOLID, SK_VC_LINEAR, SK_VC_RADIAL } sk_vc_paint_kind;

#define SK_VC_STOPS 4

/* A fill or stroke style: a colour, or a gradient of up to four stops
 * between colours, padded past its ends, interpolated unpremultiplied as
 * Chromium's canvas does. A radial gradient is canvas's two circles. */
typedef struct {
    sk_vc_paint_kind kind;
    sk_color color;
    double x0, y0, r0, x1, y1, r1;
    int n_stops;
    double off[SK_VC_STOPS];
    sk_color stop[SK_VC_STOPS];
} sk_vc_paint;

static inline sk_vc_paint sk_vc_solid(sk_color c)
{
    sk_vc_paint p = {0};
    p.kind = SK_VC_SOLID;
    p.color = c;
    return p;
}
sk_vc_paint sk_vc_linear(double x0, double y0, double x1, double y1);
sk_vc_paint sk_vc_radial(double x0, double y0, double r0, double x1, double y1, double r1);
void sk_vc_add_stop(sk_vc_paint *p, double off, sk_color c);

/* CSS's colours: r, g, b 0 to 255 and a 0 to 1, rounded to 8 bits as a
 * canvas parses them; hsl's s and l in percent, its h in degrees. */
sk_color sk_vc_rgba(double r, double g, double b, double a);
sk_color sk_vc_hsla(double h, double s, double l, double a);
static inline sk_color sk_vc_hsl(double h, double s, double l)
{
    return sk_vc_hsla(h, s, l, 1);
}
/* hsl's red, green and blue, 0 to 1, for s and l 0 to 1 (fern.js's). */
void sk_vc_hsl_rgb(double h, double s, double l, double out[3]);

typedef enum { SK_VC_LEFT, SK_VC_CENTER, SK_VC_RIGHT } sk_vc_align;
typedef enum { SK_VC_ALPHABETIC, SK_VC_MIDDLE, SK_VC_TOP } sk_vc_baseline;

/* The state save() keeps and restore() brings back. */
typedef struct {
    sk_vc_xform m; /* points to pixels, the scale included */
    double alpha;  /* globalAlpha */
    sk_blend op;   /* SK_BLEND_SRC_OVER, or SK_BLEND_ADD for lighter */
    double line_width;
    sk_cap cap;
    sk_join join;
    double miter;
    int n_dash;
    double dash[4];
    sk_rect clip;  /* pixels */
    uint8_t *mask; /* a clip's coverage, the image's size, or NULL */
    bool own_mask;
    double font_px;
    sk_vc_align align;
    sk_vc_baseline baseline;
} sk_vc_state;

typedef struct {
    float x, y;
} sk_vc_pt;

typedef struct {
    size_t start, n;
    bool closed;
} sk_vc_sub;

/* A path in pixels: subpaths of points, and the box they lie in. */
typedef struct {
    SK_VEC(sk_vc_pt) pts;
    SK_VEC(sk_vc_sub) subs;
    bool open; /* the last subpath takes more points */
    float minx, miny, maxx, maxy;
} sk_vc_path;

#define SK_VC_DEPTH 8

typedef struct {
    sk_image *im;
    double scale;
    sk_vc_state st;
    sk_vc_state saved[SK_VC_DEPTH];
    int depth;
    /* The current path, and one a rectangle is drawn with while the
     * current one waits. */
    sk_vc_path p, aside;
    /* Scratch: paths for gfx, a coverage mask. */
    sk_path *path, *outline;
    SK_VEC(uint8_t) cover;
    /* Text, made on first use. */
    sk_fontset *fonts;
    sk_glyph_cache *glyphs;
} sk_vc;

/* A canvas over im at scale, its transform the scale alone. im is the
 * caller's and must outlive the canvas; sk_vc_target points the canvas
 * at another image, of any size, and resets its state. */
void sk_vc_init(sk_vc *vc, sk_image *im, double scale);
void sk_vc_target(sk_vc *vc, sk_image *im, double scale);
void sk_vc_free(sk_vc *vc);

void sk_vc_save(sk_vc *vc);
void sk_vc_restore(sk_vc *vc);
/* setTransform: the points' transform, the scale applied after it. */
void sk_vc_set_transform(sk_vc *vc, double a, double b, double c, double d, double e, double f);
void sk_vc_reset_transform(sk_vc *vc);
void sk_vc_translate(sk_vc *vc, double x, double y);
void sk_vc_scale(sk_vc *vc, double sx, double sy);
void sk_vc_rotate(sk_vc *vc, double a);
/* A point of the mode's into pixels. */
void sk_vc_apply(const sk_vc *vc, double x, double y, double *px, double *py);

static inline void sk_vc_alpha(sk_vc *vc, double a)
{
    vc->st.alpha = a >= 0 && a <= 1 ? a : vc->st.alpha;
}
static inline void sk_vc_op(sk_vc *vc, sk_blend op)
{
    vc->st.op = op;
}
static inline void sk_vc_line_width(sk_vc *vc, double w)
{
    if (w > 0 && w < 1e9)
        vc->st.line_width = w;
}
static inline void sk_vc_cap(sk_vc *vc, sk_cap c)
{
    vc->st.cap = c;
}
static inline void sk_vc_join(sk_vc *vc, sk_join j)
{
    vc->st.join = j;
}
/* setLineDash with n of 0, 2 or 4 lengths; [] for none. */
void sk_vc_dash(sk_vc *vc, const double *d, int n);

/* ---- Paths ---- */

void sk_vc_begin(sk_vc *vc);
void sk_vc_move(sk_vc *vc, double x, double y);
void sk_vc_line(sk_vc *vc, double x, double y);
void sk_vc_quad(sk_vc *vc, double cx, double cy, double x, double y);
void sk_vc_cubic(sk_vc *vc, double c1x, double c1y, double c2x, double c2y, double x, double y);
void sk_vc_close(sk_vc *vc);
/* arc and ellipse as canvas draws them: a line from the current point
 * to the start, then the arc, the whole circle when the sweep reaches
 * 2 pi in its direction. */
void sk_vc_arc(sk_vc *vc, double x, double y, double r, double a0, double a1, bool ccw);
void sk_vc_ellipse(sk_vc *vc, double x, double y, double rx, double ry, double rot, double a0, double a1,
                   bool ccw);
void sk_vc_rect(sk_vc *vc, double x, double y, double w, double h);

void sk_vc_fill(sk_vc *vc, const sk_vc_paint *p, sk_fill_rule rule);
void sk_vc_stroke(sk_vc *vc, const sk_vc_paint *p);
/* The stroke over its shadow, as shadowBlur blur and shadowColor
 * shadow give one: the stroke's coverage blurred by a Gaussian of
 * standard deviation blur / 2 points, in shadow, under it. The blur is
 * three box blurs at a fraction of the pixels, which a shadow that soft
 * does not show. */
void sk_vc_stroke_glow(sk_vc *vc, const sk_vc_paint *p, sk_color shadow, double blur);
/* The path intersected into the clip. */
void sk_vc_clip(sk_vc *vc, sk_fill_rule rule);

/* The current path stays as it is. */
void sk_vc_fill_rect(sk_vc *vc, double x, double y, double w, double h, const sk_vc_paint *p);
void sk_vc_stroke_rect(sk_vc *vc, double x, double y, double w, double h, const sk_vc_paint *p);
/* fillRect of a colour at globalAlpha times alpha, for the many small
 * squares of a frame: the transform is taken to be a scale and a
 * translation. */
void sk_vc_square(sk_vc *vc, double x, double y, double w, double h, sk_color c, double alpha);
/* The whole image filled with c, every pixel replaced, ignoring the
 * transform and the clip: a canvas cleared to a colour. */
void sk_vc_clear(sk_vc *vc, sk_color c);

/* drawImage of src at (x, y) w by h, in points under the transform,
 * taken to be a scale and a translation; bilinear when smooth. */
void sk_vc_image(sk_vc *vc, const sk_image *src, double x, double y, double w, double h, bool smooth);

/* ---- Text: DejaVu Sans Mono ---- */

static inline void sk_vc_font(sk_vc *vc, double px)
{
    vc->st.font_px = px;
}
static inline void sk_vc_align_text(sk_vc *vc, sk_vc_align a, sk_vc_baseline b)
{
    vc->st.align = a;
    vc->st.baseline = b;
}
/* measureText: the advance, and the ink's box about the pen as
 * actualBoundingBox*, all in points. */
typedef struct {
    double width, left, right, ascent, descent;
} sk_vc_metrics;
void sk_vc_measure(sk_vc *vc, const char *utf8, sk_vc_metrics *m);
void sk_vc_text(sk_vc *vc, const char *utf8, double x, double y, sk_color c);

#endif
