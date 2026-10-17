/* What the runtime's files share and view.h does not declare: the pack
 * as a view holds it, the view's nodes and widgets, and the passes each
 * file holds. The tests read a view through this. */
#ifndef SK_SKIN_VIEWINT_H
#define SK_SKIN_VIEWINT_H

#include "../base/map.h"
#include "paint.h"
#include "view.h"

#include <stdatomic.h>

/* Rows of a list laid out at most: past them a list's box has long
 * cut its rows off. */
#define SK_VIEW_MAX_ROWS 64
/* The splits a view moves; a pack holds no more than the four edges. */
#define SK_VIEW_MAX_SPLITS 4
/* app.css: a split's handle across the terminal's edge, in points. */
#define SK_VIEW_GRIP 8
/* scale.js's STRIP: the identity strip above the skin area. */
#define SK_VIEW_STRIP 22

struct sk_skinpack {
    sk_pack pack;
    uint8_t *bytes;
    bool own;
    atomic_int refs;
    /* The pack's faces, opened once: what a style's font index names
     * past the embedded DejaVu. NULL where one did not open. */
    sk_font **faces;
    /* Each list's row: the elements of its subtree in tree order. */
};

/* ---- Widgets ---- */

/* widgets.js's Spring: a damped spring toward a target, null while it
 * has none. */
typedef struct {
    double v, vel, target, zeta, w;
    bool has_v, has_target;
} sk_spring;

void sk_spring_init(sk_spring *s, double damping, double hz);
void sk_spring_set(sk_spring *s, bool has, double t);
/* The value after dt seconds; false while it has none. */
bool sk_spring_step(sk_spring *s, double dt, double *out);
/* At its target and still, to the display's precision. */
bool sk_spring_rests(const sk_spring *s);

/* widgets.js's format(): v as a widget shows it, into out. A string's
 * text as it is. now: Unix seconds, for until. */
void sk_widget_format(sk_buf *out, const sk_val *v, uint8_t format, uint8_t digits, double now);
/* format() of a number. */
void sk_widget_format_num(sk_buf *out, double v, uint8_t format, uint8_t digits, double now);

/* The elements of one widget, or of one meter of a limits widget, by
 * the part each plays; SK_PACK_NONE where it has none. */
typedef struct {
    uint32_t root, label, value, resets, resets_left;
    uint32_t track, fill, lamp, sheet, arc_fill, dial_text, turn;
    uint32_t items, row, toggle, opt_list, opt_other;
    uint32_t *segs, *marks; /* per segment, per mark */
    uint16_t nsegs, nmarks;
} sk_wparts;

/* A widget's state, widgets.js's closure. A limits widget's meters are
 * widgets of their own, bars or needles, in meters[group - 1][i]. */
typedef struct sk_wstate sk_wstate;
struct sk_wstate {
    const sk_pack_widget *w;
    uint8_t type;   /* sk_widget_type: a meter's is its bar or needle */
    bool meter;
    sk_wparts parts;
    sk_spring sp;
    sk_val raw;     /* the bound metric now, owned */
    bool fallback;  /* the fallback in force */
    /* led */
    bool lit, has_last;
    double last, pulse_at;
    /* sprite */
    int run_frame;
    double run_at;
    /* list: the items shown, text and state 0 todo, 1 doing, 2 done */
    char **item_text;
    uint8_t *item_state;
    size_t nitems;
    char *drawn;    /* the items' key as last drawn */
    /* limits */
    sk_wstate *meters[8];
    uint8_t count;
    char *limits_key;
    /* list and limits: the list widget's index among the view's lists */
    int list;
};

/* ---- Motion: transitions and animations ---- */

typedef struct sk_motion sk_motion;

/* ---- The view ---- */

/* A node of the tree laid out: an element, or one of a list's rows'
 * copies of one. Nodes 0 to the pack's element count are the elements
 * themselves, a list's first row among them; each list's other rows
 * follow, SK_VIEW_MAX_ROWS - 1 copies of its row's subtree. */
typedef struct {
    uint32_t el;
    uint16_t row;   /* the row it belongs to, from 0; 0 outside rows */
    bool live;      /* in the tree laid out */
    uint8_t var[SK_VAR_COUNT_OF_VARS];
    char *text;     /* its own text, owned, when has_text */
    bool has_text;
    float value, turn;
    bool hide;      /* not drawn: a gauge's mark without a value */
    bool own_svg;
    sk_pack_svg svg;
    const sk_css_style *base; /* its style in its state */
    sk_motion *mo;  /* its transitions and animations, or NULL */
    /* What the last frame painted of it: whether, where (the skin
     * area's px), and a hash of everything its paint depends on. */
    bool drawn;
    sk_frect ink;
    uint64_t sig;
} sk_vnode;

/* A list widget's rows. */
typedef struct {
    uint32_t widget;
    uint32_t items, row; /* its items element and its row element */
    uint32_t *sub;       /* the row's subtree, the row first, in tree order */
    uint32_t nsub;
    uint32_t base;       /* the node of its second row's first element */
    uint16_t rows;       /* rows shown now */
} sk_vlist;

/* An image decoded from the pack, on first use. */
typedef struct {
    sk_image im;
    uint8_t tried; /* 1 tried, 2 decoded */
} sk_vimage;

struct sk_skinview {
    sk_skinpack *sp;
    const sk_pack *p;
    double now, last; /* the clock and the last step's, ms */
    double wall;      /* Unix seconds of this step */
    double (*wall_fn)(void *ud);
    void *wall_ud;
    /* The stage: points, its place in the window, and the scale. */
    double sx, sy, sw, sh, scale;
    double k;         /* the skin's own scale, decision 0006 */
    float lw, lh;     /* the layout's size: the stage over k */
    /* The splits: their kept sizes, a drag's, and the sizes laid out. */
    double saved[SK_VIEW_MAX_SPLITS], live[SK_VIEW_MAX_SPLITS], at[SK_VIEW_MAX_SPLITS];
    bool has_saved[SK_VIEW_MAX_SPLITS], has_live[SK_VIEW_MAX_SPLITS];
    sk_pack_rect *rects; /* each pack rect where the splits put it */
    /* The metrics, by name. */
    sk_map metrics;   /* name to an owned sk_val */
    SK_VEC(char *) metric_names;
    bool metrics_new;
    sk_map dials;     /* names open */
    bool list_off, menu_open, unseen;
    /* The tree. */
    sk_vnode *nodes;
    uint32_t nnodes;
    sk_lnode *lnodes;
    sk_pack_rect *lrects; /* per node, its rect where it has one */
    uint32_t stage;
    sk_wstate *widgets;
    uint32_t nwidgets;
    sk_vlist *lists;
    uint32_t nlists;
    int32_t *row_of; /* per element: the list whose row subtree holds it, or -1 */
    uint32_t *sub_at; /* per element in a row subtree: its place there */
    /* Layout and paint. */
    sk_layout layout;
    bool laid;
    sk_lfonts lfonts;
    sk_pimages images;
    sk_vimage *decoded;
    sk_glyph_cache *glyphs;
    sk_paint_cache *pcache;
    uint32_t *order;  /* the boxes in the order they paint, for hit tests */
    uint32_t norder;
    /* What changed. */
    bool dirty;       /* a step must run the passes */
    bool all;         /* the next draw paints the whole stage */
    SK_VEC(sk_rect) damage; /* the stage's device pixels to paint */
    double next_due;  /* the clock when a timed change is due, or < 0 */
    bool moving;      /* something moves every frame */
    /* The pointer. */
    bool pointer_in;
    double px, py;    /* window points */
    uint32_t hover;   /* the node under it, or SK_PACK_NONE */
    SK_VEC(uint32_t) hovered; /* it and its ancestors, whose hover is 1 */
    uint32_t pressed; /* the node a press went down on, or SK_PACK_NONE */
    int press_kind;   /* what the press holds: 0 none, 1 a node, 2 the menu, 3 a split */
    int drag_split;
    double drag_from, drag_start;
    int handle_hover; /* the handle under the pointer, or -1 */
    bool menu_hover, menu_down;
    /* The overlays as last drawn, frame pixels relative to the stage. */
    sk_rect menu_px, handle_px[SK_VIEW_MAX_SPLITS];
    bool handle_shown[SK_VIEW_MAX_SPLITS];
    sk_buf event_text; /* an event's metric or split */
};

/* ---- view.c ---- */

/* The node of element e in the context of node n: inside the row n
 * belongs to when e is in that row's subtree, else e's own. */
uint32_t sk_view_node_of(const sk_skinview *v, uint32_t n, uint32_t e);
/* Sets a node's variable, the view dirty when it changes. */
void sk_view_set_var(sk_skinview *v, uint32_t n, uint8_t var, uint8_t value);
/* A node's own text, NULL for none; the view dirty when it changes. */
void sk_view_set_text(sk_skinview *v, uint32_t n, const char *text);
/* A metric by name, NULL when absent. */
const sk_val *sk_view_metric(const sk_skinview *v, const char *name);
/* The stage's rectangle and the skin area's mapping, for the tests. */
void sk_view_run(sk_skinview *v);

/* ---- widgets.c ---- */

void sk_widgets_init(sk_skinview *v);
void sk_widgets_free(sk_skinview *v);
/* widgets.js's update(snapshot): the metrics given anew. */
void sk_widgets_update(sk_skinview *v);
/* frame(now, dt), every step: the springs and the clocks. Sets
 * v->moving and v->next_due for what keeps moving. */
void sk_widgets_frame(sk_skinview *v, double dt);
/* The dials and the list's choice applied. */
void sk_widgets_marks(sk_skinview *v);

/* ---- motion.c ---- */

/* Node n's style is now base (it was old, as shown): transitions start
 * for what the new style's transition names, animations start and stop
 * by name. */
void sk_motion_restyle(sk_skinview *v, uint32_t n, const sk_css_style *old_shown, const sk_css_style *base);
/* The style node n shows at the view's clock: its base with its
 * transitions and animations applied. moving: something of it moves
 * every frame; *due: the clock when a held value next changes. */
const sk_css_style *sk_motion_style(sk_skinview *v, uint32_t n, bool *moving, double *due);
/* A hash of what its motion changed of the style. */
uint64_t sk_motion_sig(const sk_motion *m);
void sk_motion_free(sk_motion *m);

/* CSS easing: a timing function at progress x in [0, 1]. */
double sk_timing_eval(const sk_css_timing *t, double x);

#endif
