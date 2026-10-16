/* The layout of a skin (decision 0037): CSS's boxes computed from the
 * computed styles of a pack or of skinc's front end, so skinc and the
 * runtime place every box alike. skinc lays out what it pre-renders,
 * the runtime the rest, with this code.
 *
 *   sk_lnode nodes[n] = ...;            the tree, a style per element
 *   sk_layout l;
 *   sk_layout_run(&l, nodes, n, root, W, H, &fonts);
 *   ... l.boxes[i] is node i's box, in CSS px from the skin area ...
 *   sk_layout_free(&l);
 *
 * What it lays out is the subset of decision 0037: absolute and
 * relative positioning against the nearest positioned ancestor's
 * padding box (fixed against the skin area); blocks stacked in flow;
 * inline text on lines, broken at spaces under white-space normal,
 * with text-align, letter-spacing, text-transform, line-height and
 * ellipsis; floats at their container's edge; flex in one line, with
 * grow, shrink, basis, gaps, the alignments and first baselines; grid
 * with explicit and automatic tracks, spans and line numbers; an svg as
 * a replaced box of its viewBox's ratio; transforms as matrices about
 * their origin; and what each box paints beyond itself (shadows,
 * filters, text), so a painter knows how large a layer must be.
 *
 * Nothing here draws. src/skin/paint.h paints a laid-out tree. */
#ifndef SK_SKIN_LAYOUT_H
#define SK_SKIN_LAYOUT_H

#include "../base/arena.h"
#include "../text/text.h"
#include "pack.h"

/* ---- The tree laid out ---- */

/* One element, in document order: its children are first, first->next
 * and so on, a ::before first among them and an ::after last, as
 * sk_pack_el keeps them. The nodes must make a tree, every index below
 * the count or SK_PACK_NONE, as a pack's loader checks; nodes deeper
 * than 200 below the root get no box. */
typedef struct {
    uint32_t parent, first, next; /* SK_PACK_NONE when absent */
    uint8_t kind, role;           /* sk_el_kind, sk_role */
    /* Its style in the state laid out; NULL generates no box, as
     * display: none does. */
    const sk_css_style *style;
    /* Its own text, before its children (after a ::before); NULL for
     * none. A ::before's or ::after's text comes from its content when
     * this is NULL. */
    const char *text;
    const sk_pack_svg *svg;   /* its SVG geometry, or NULL */
    const sk_pack_rect *rect; /* its rect as placed, splits applied, for SK_LEN_RECT; NULL: the style's px */
    float value;              /* SK_LEN_VALUE's percentage: a bar's fill, a sprite's frame, a gauge's dash */
    float turn;               /* SK_SVG_TURN: the group's angle, degrees clockwise */
    int32_t row;              /* a list's row, for SK_CONTENT_ORDINAL */
} sk_lnode;

/* The faces a style's font index names: the embedded DejaVu first, in
 * sk_embedded_font's order, then the skin's fonts. NULL for an index
 * with no face: DejaVu Sans stands in. */
typedef struct {
    const sk_font *(*face)(void *data, uint16_t index);
    void *data;
} sk_lfonts;

/* ---- What layout gives ---- */

typedef struct {
    float x, y, w, h;
} sk_frect;

/* A glyph on a line: its face and glyph, the style the face lacks
 * (sk_glyph_ref's synth), and its pen x from its run's start. */
typedef struct {
    const sk_font *font;
    uint32_t glyph;
    uint8_t synth;
    float x;
} sk_lglyph;

/* Glyphs in one element's text style, on one line: the element whose
 * style draws them (its colour, shadows, decorations), their font size,
 * the pen's start and the baseline. In CSS px from the skin area, or
 * for an SVG text element in its user units. */
typedef struct {
    uint32_t node;
    uint32_t block;      /* the block container whose line holds it */
    float x, y;          /* the pen's start and the baseline */
    float px;            /* the em */
    float width;         /* its advance */
    float ascent, descent; /* its font's, at px */
    uint32_t first, n;   /* its glyphs, in sk_layout's glyphs */
    uint8_t deco;        /* SK_DECO_*: the decorations that cross it */
    sk_css_color deco_color;
} sk_lrun;

/* A box. Every geometry is in CSS px from the skin area's top left,
 * before any transform; xform maps that space to where the box and its
 * subtree are drawn, its ancestors' transforms included. */
typedef struct {
    bool generated;            /* false: display none, or no style */
    bool svg_content;          /* a shape, text, image or group inside an svg: no CSS box */
    uint32_t node;
    sk_frect border, padding, content;
    float bw[4], pad[4], margin[4]; /* SK_TOP, SK_RIGHT, SK_BOTTOM, SK_LEFT */
    float baseline;            /* the first baseline's y, or NAN */
    uint32_t cb;               /* the box whose padding box contains it; SK_PACK_NONE for the root */
    uint32_t clipper;          /* the nearest box whose overflow clip applies to it, or SK_PACK_NONE */
    bool has_xform;            /* its own transform is not the identity */
    sk_xform local;            /* its own transform, about its origin */
    sk_xform xform;            /* local after every ancestor's */
    uint32_t runs, nruns;      /* the line boxes it holds: its text and its inline descendants' */
    /* An svg's mapping of its viewBox into its content box. */
    sk_xform viewbox;
    /* What it and its subtree may paint, in its own space (before
     * local): its border box, shadows, outline, text and descendants,
     * clipped where it clips them. */
    sk_frect ink;
} sk_lbox;

typedef struct {
    const sk_lnode *nodes;
    uint32_t n, root;
    sk_lbox *boxes; /* n, one per node */
    sk_lrun *runs;
    size_t nruns;
    sk_lglyph *glyphs;
    size_t nglyphs;
    float width, height; /* the skin area */
    sk_arena arena;
} sk_layout;

/* Lays out the subtree of nodes[root], an element placed against the
 * skin area of w by h CSS px (the stage), with faces from fonts. */
void sk_layout_run(sk_layout *l, const sk_lnode *nodes, uint32_t n, uint32_t root, float w, float h,
                   const sk_lfonts *fonts);
void sk_layout_free(sk_layout *l);

/* ---- What the painter shares ---- */

/* The face of a style, never NULL: fonts' face, or DejaVu Sans. */
const sk_font *sk_layout_face(const sk_lfonts *fonts, const sk_css_style *s);

/* A length against a base: px plus pct of base (base NAN takes no
 * percentage). Rect and value kinds as node gives them. */
float sk_layout_len(const sk_css_len *l, float base);

/* r mapped through m: the bounds of its four corners. */
sk_frect sk_frect_map(const sk_xform *m, sk_frect r);
sk_frect sk_frect_union(sk_frect a, sk_frect b);
sk_frect sk_frect_intersect(sk_frect a, sk_frect b);
static inline bool sk_frect_empty(sk_frect r)
{
    return !(r.w > 0) || !(r.h > 0);
}
/* a then b: the transform that applies b first, then a. */
sk_xform sk_xform_mul(const sk_xform *a, const sk_xform *b);
/* The inverse; false when m is singular. */
bool sk_xform_invert(const sk_xform *m, sk_xform *out);

/* The transform a style's transform list makes for a border box,
 * about its transform-origin. */
sk_xform sk_layout_transform(const sk_css_style *s, const sk_css_xf *xf, uint16_t n, sk_frect border);

/* How far a box's own paint reaches past its border box: box shadows,
 * outline, border-image outsets; and what its filters add to whatever
 * the box paints. */
float sk_layout_shadow_reach(const sk_css_shadow *s, uint16_t n, bool box);
sk_frect sk_layout_filter_reach(const sk_css_style *s, sk_frect ink);

#endif
