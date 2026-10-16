/* What the layout's files share and layout.h does not declare: the
 * state of a run, a box's state while it is laid out, and the passes
 * each file holds. */
#ifndef SK_SKIN_INTERNAL_H
#define SK_SKIN_INTERNAL_H

#include "layout.h"

#include <math.h>

/* The keys of a rect (sk_pack_rect's order) the box properties name. */
enum { KEY_LEFT, KEY_RIGHT, KEY_WIDTH, KEY_TOP, KEY_BOTTOM, KEY_HEIGHT, KEY_NONE = -1 };

/* A box while it is laid out. Positions are of its border box, from
 * its parent's border box (in flow) or, for an absolute box, from its
 * containing block's, set when it is placed. */
typedef struct {
    float x, y, w, h;
    float baseline;      /* the first baseline, from the border box's top; NAN */
    bool out;            /* absolute or fixed: out of flow */
    bool floated;
    /* An absolute box's static position, from its parent's border box,
     * and how a flex or grid parent would align it alone. */
    float sx, sy;
    bool flex_static;
    bool row;            /* the flex parent's main axis is horizontal */
    bool reverse;        /* its main axis runs from the end */
    uint8_t justify, align;
    float cx, cy, cw, ch; /* the parent's content box, from its border box */
    /* Intrinsic widths of the border box, once measured. */
    bool measured;
    float min_w, max_w;
    /* The line boxes it lays out, from its border box, until it is
     * placed. */
    SK_VEC(sk_lrun) runs;
    SK_VEC(sk_lglyph) glyphs;
} lstate;

typedef struct {
    sk_layout *l;
    const sk_lnode *nodes;
    const sk_lfonts *fonts;
    lstate *st;
    uint32_t depth; /* nesting of layout calls, to stop a runaway tree */
    /* Each node reached once from the root, no deeper than
     * LAY_MAX_TREE: what may have a box. */
    uint8_t *reached;
} lay;

/* No tree is laid out deeper than this: the skins' trees are a dozen
 * deep, and every pass over a tree recurses. */
#define LAY_MAX_TREE 200

/* ---- layout.c ---- */

const sk_css_style *lay_style(const lay *L, uint32_t i);
/* A length of node i: px + pct of base; NAN for auto, none, normal,
 * or a percentage of an indefinite base. key: the rect key the
 * property is, for SK_LEN_RECT. */
float lay_len(const lay *L, uint32_t i, const sk_css_len *v, int key, float base);
/* Margins, borders and padding of node i, percentages of cbw. */
void lay_edges(lay *L, uint32_t i, float cbw);
/* Node i's left and right margins with no containing block. */
float lay_margins_w(const lay *L, uint32_t i);
float lay_bp_w(const lay *L, uint32_t i); /* borders and padding across */
float lay_bp_h(const lay *L, uint32_t i);
/* A width or height property as a border-box size; NAN for auto. */
float lay_size_w(const lay *L, uint32_t i, const sk_css_len *v, int key, float cbw);
float lay_size_h(const lay *L, uint32_t i, const sk_css_len *v, int key, float cbh);
/* w or h held between min-* and max-*. */
float lay_clamp_w(const lay *L, uint32_t i, float w, float cbw);
float lay_clamp_h(const lay *L, uint32_t i, float h, float cbh);
/* Lays out node i's contents at a border-box width w and height h
 * (NAN: from its content). The border-box height it takes. */
float lay_contents(lay *L, uint32_t i, float w, float h);
/* The border box's min-content and max-content contributions: its
 * width when definite, held by min-width and max-width. */
void lay_intrinsic(lay *L, uint32_t i, float *min_w, float *max_w);
/* The min-content and max-content widths of its content, with its
 * borders and padding, whatever its width says. */
void lay_content_widths(lay *L, uint32_t i, float *min_w, float *max_w);
/* Shrink-to-fit: the max-content width, no wider than avail, no
 * narrower than the min-content width. */
float lay_fit_w(lay *L, uint32_t i, float avail);
bool lay_is_svg(const lay *L, uint32_t i);
/* The box's display once generated, NONE when it has none. */
uint8_t lay_display(const lay *L, uint32_t i);
bool lay_out_of_flow(const lay *L, uint32_t i);
bool lay_block_level(const lay *L, uint32_t i);
/* The ratio of an svg's viewBox, width over height. */
float lay_svg_ratio(const lay *L, uint32_t i);

/* ---- inline.c ---- */

/* An item of a block container's inline content: a child, or the
 * container's own text. */
#define LAY_OWN_TEXT 0xFFFFFFFEu

/* Lays out the inline content of block container b, items[0..n) (its
 * inline children, the floats among them, its own text), in lines cw
 * wide from y below its content box's top. The height the lines take. */
float lay_inline(lay *L, uint32_t b, const uint32_t *items, size_t n, float cw, float y);
/* The widths of that content: the longest piece no line breaks, and
 * the whole on one line. */
void lay_inline_widths(lay *L, uint32_t b, const uint32_t *items, size_t n, float *min_w, float *max_w);
/* An SVG text element's run, in its user units. */
void lay_svg_text(lay *L, uint32_t i);
/* A node's line metrics: ascent and descent rounded as browsers round
 * them, and its line-height. */
void lay_line_metrics(const lay *L, uint32_t i, float *ascent, float *descent, float *line_height);

/* ---- flex.c ---- */

/* Flex and grid containers: their items laid out in the content box
 * cw by ch (NAN: from the items); the content height they take. */
float lay_flex(lay *L, uint32_t i, float cw, float ch);
float lay_grid(lay *L, uint32_t i, float cw, float ch);
void lay_flex_widths(lay *L, uint32_t i, float *min_w, float *max_w);
void lay_grid_widths(lay *L, uint32_t i, float *min_w, float *max_w);
/* The first baseline of an item laid out, from its border box's top;
 * synthesized from its bottom when it has none. */
float lay_item_baseline(const lay *L, uint32_t i);

#endif
