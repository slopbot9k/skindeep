/* skinc's front end (decision 0037): a skin folder in, its element
 * tree out, as app/renderer/skin.js and widgets.js build the DOM, with
 * a computed style for every state each element can reach.
 *
 *   sk_front f;
 *   if (!sk_front_run(&f, "skins/deck", &diags)) print the diags;
 *   ... f.els, each el->table[...] an index into f.styles ...
 *   sk_front_free(&f);
 *
 * The cascade runs over three sheets: the user agent's few rules, the
 * app's own (app/renderer/widgets.css, loaded before the skin's in the
 * shadow root) and the skin's. */
#ifndef SKINC_FRONT_H
#define SKINC_FRONT_H

#include "css.h"
#include "props.h"
#include "skin.h"

#include <stdio.h>

typedef struct sk_el sk_el;

/* A state input: a variable of an element. */
typedef struct {
    sk_el *owner;
    uint8_t var, radix;
} sk_input;

/* A declaration ready for the cascade: which property it sets, and
 * where it stands. */
typedef enum {
    SK_LAYER_UA,
    SK_LAYER_HINT,     /* SVG presentation attributes */
    SK_LAYER_AUTHOR,   /* widgets.css, then skin.css */
    SK_LAYER_INLINE,   /* the style attributes skin.js and widgets.js set */
    SK_LAYER_AUTHOR_IMPORTANT,
    SK_LAYER_INLINE_IMPORTANT,
    SK_LAYER_UA_IMPORTANT,
} sk_cascade_layer;

typedef struct {
    const sk_css_decl *d;
    const sk_css_sheet *sheet;
    uint16_t prop;               /* a longhand, or 0 */
    const sk_shorthand *sh;      /* a shorthand, or NULL */
    bool custom;                 /* --name */
    int wide;                    /* SK_WIDE_*, or 0 */
    bool var;                    /* holds var() */
    bool internal;               /* the app's own: -sk-rect() and -sk-value() allowed */
    bool reported;               /* an error about it was given */
} sk_xdecl;

/* One selector of a rule, with what the cascade needs to rank it. */
typedef struct {
    const sk_selector *sel;
    sk_xdecl *decls;
    size_t ndecls;
    uint32_t spec, order;
    uint8_t layer;      /* SK_LAYER_UA or SK_LAYER_AUTHOR */
    const sk_css_rule *rule;
    const sk_css_sheet *sheet;
    uint32_t matched;   /* elements it matched in some state */
} sk_entry;

/* A custom property's computed value: its tokens, and how deep their
 * blocks and functions nest once resolved. */
typedef struct {
    const char *name;
    const sk_cv *cv;
    size_t n;
    int deep;
} sk_custom;

typedef struct {
    sk_custom *items;
    size_t n;
} sk_customs;

struct sk_el {
    uint32_t index;
    uint8_t kind, role; /* sk_el_kind, sk_role */
    const char *tag;    /* type selectors' name; NULL for the host and pseudo-elements */
    sk_el *parent, *first, *last, *next, *prev; /* children are elements only */
    sk_el *before, *after;                      /* ::before and ::after, when a rule may give one */
    /* The element's siblings for structural selectors: its position
     * and their count, from 1. A limits widget's meters count within
     * their group, which are next to each other among its children.
     * nkids counts the children as they are added. */
    uint32_t nth, siblings, nkids;
    const char **classes;
    uint16_t nclasses;
    uint16_t vars; /* the sk_var it owns, bit per var */
    bool repeated; /* a list's row: one element stands for every row */
    uint8_t group, meter; /* a limits meter's count group and place, from 1 */
    sk_el *limits;        /* for a meter and what it holds: its limits widget */
    int widget;           /* index in the skin's widgets, or -1 */
    uint8_t text_source;
    const char *text;
    sk_pack_svg svg;
    int rect;             /* 0 none, 1 + panel, or -(1 + widget), a toggle's 1000000 + widget */
    sk_xdecl *inline_decls, *hint_decls;
    size_t ninline, nhint;
    /* The cascade's results. */
    sk_entry **cands;     /* rule selectors that may match */
    size_t ncands;
    sk_input *inputs;
    uint8_t ninputs;
    uint32_t *table;      /* styles per state, the first input least significant */
    size_t ntable;
    uint16_t inherited;   /* inputs whose value changes what children inherit */
    sk_customs *customs;  /* per table entry */
    bool content_varies;  /* a pseudo-element whose content holds a counter() */
};

typedef struct {
    const char *path; /* relative to the skin */
    char *real;
} sk_front_image;

typedef struct {
    sk_skin skin;
    bool skin_ok;
    sk_arena arena;
    sk_css_sheet ua, app, css;
    sk_css_sheet inline_sheet, hint_sheet; /* the app's own declarations */
    const char *url_base; /* the directory a url being parsed starts from */
    bool has_css;
    char *css_path, *css_shown;
    SK_VEC(sk_entry) entries;
    SK_VEC(sk_el *) els;
    SK_VEC(sk_css_style *) styles;
    SK_VEC(sk_front_image) images;
    SK_VEC(sk_pack_keyframes) keyframes;
    char *css_dir;               /* the stylesheet's directory, its urls' base */
    sk_diags css_diags;          /* the stylesheet's syntax errors, given after validate.js's */
    SK_VEC(const char *) spans;  /* the span-<x> classes the sheets name */
    size_t states;               /* table entries in all */
    size_t max_states;           /* the most in all; 0 is SK_FRONT_MAX_STATES */
    bool states_said;            /* the cap was reported */
    const char *css_text;        /* in place of the stylesheet's file, when set */
    size_t css_len;
    size_t cascades;
    sk_diags *d;
    void *dedupe;                /* style text to index */
} sk_front;

/* A skin reaches at most this many states in all. */
#define SK_FRONT_MAX_STATES SK_PACK_MAX_TOTAL_STATES

/* Checks the skin at dir and computes its styles. False with messages
 * in d for a skin that fails. f is ready for sk_front_free either way. */
bool sk_front_run(sk_front *f, const char *dir, sk_diags *d);
/* The same with the stylesheet's text given, as a fuzz target or an
 * editor gives it, and a cap on the states (0 for the default). */
bool sk_front_run_text(sk_front *f, const char *dir, const char *css, size_t n, size_t max_states, sk_diags *d);
void sk_front_free(sk_front *f);

/* The summary skinc prints; verbose adds every element's states and
 * the values that differ from the initial ones. */
void sk_front_summary(const sk_front *f, FILE *out, bool verbose);

/* The element of a widget (by id) with the role given, the nth (from
 * 0) in tree order; role SK_ROLE_WIDGET for the root. NULL when none. */
const sk_el *sk_front_find(const sk_front *f, const char *widget, uint8_t role, int nth);
/* The first panel element with this class. */
const sk_el *sk_front_panel(const sk_front *f, const char *klass);
/* An element's style with the variables of its inputs as given: each
 * (owner, var) pair named takes its value, every other input 0. */
typedef struct {
    const sk_el *owner;
    uint8_t var, value;
} sk_assign;
const sk_css_style *sk_front_style(const sk_front *f, const sk_el *el, const sk_assign *a, size_t n);

/* The app's own stylesheet, app/renderer/widgets.css as the C port
 * keeps it, and the user agent's rules skins rely on. */
const char *sk_widgets_css(void);
extern const char sk_ua_css[];

/* ---- Inside the front end ---- */

/* The selector matcher, three-valued while it looks for the variables
 * a selector reads: TRUE, FALSE or UNKNOWN. */
typedef enum { SK_NO, SK_YES, SK_MAYBE } sk_tri;

/* A result of the matcher at a compound of a selector, an element and an
 * anchor, in the sk_match_selector call of generation gen. */
typedef struct {
    const void *at;
    const sk_el *el, *anchor;
    uint32_t gen;
    uint8_t r;
} sk_memo;

typedef struct {
    const sk_input *inputs; /* the variables known: values[i] for inputs[i] */
    const int *values;      /* -1 for unknown */
    size_t n;
    const sk_el *pin;       /* a limits widget whose count is fixed */
    uint8_t pin_count;
    bool discover;          /* record what is read and not known */
    struct {
        sk_input *ptr;
        size_t len, cap;
    } found; /* the variables read */
    bool repeated;          /* a selector counted a list's rows */
    const sk_front *f;
    struct {
        sk_memo *ptr;
        size_t cap, live;
        uint32_t gen;
    } memo; /* what the matcher found in this call, by generation */
} sk_match;

sk_tri sk_match_selector(sk_match *m, const sk_selector *s, const sk_el *el);
void sk_match_free(sk_match *m);
/* The radix of a variable on its owner. */
uint8_t sk_var_radix(const sk_front *f, const sk_el *owner, uint8_t var);
const char *sk_var_name(uint8_t var);

/* The context a value of el is parsed in, its parent's style given. */
void sk_vctx_init(sk_front *f, sk_vctx *c, const sk_css_style *parent);
/* Component values as CSS text. */
void sk_cv_print(sk_buf *b, const sk_cv *cv, size_t n);

/* A declaration classified for the cascade. */
void sk_xdecl_init(sk_xdecl *x, const sk_css_decl *d, const sk_css_sheet *sheet, bool internal);

/* The tree of the skin's manifest. */
void sk_tree_build(sk_front *f);

/* Every element's styles. */
void sk_cascade_run(sk_front *f);

#endif
