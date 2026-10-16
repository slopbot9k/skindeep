/* The properties of decision 0037's subset, and the typed values each
 * takes: a declaration's component values in, sk_css_style's fields
 * out, computed. A value is parsed in its element's context, which
 * gives em its font size and currentcolor its colour, so the cascade
 * parses each winning declaration for each element and state. */
#ifndef SKINC_PROPS_H
#define SKINC_PROPS_H

#include "../src/skin/pack.h"
#include "css.h"

/* What a value needs of its element. */
typedef struct {
    sk_arena *arena;          /* lists */
    float font_size;          /* em */
    float parent_font_size;   /* em and % in font-size */
    sk_css_color color;       /* currentcolor */
    sk_css_color parent_color;
    const sk_css_style *parent; /* NULL at the root */
    bool internal; /* the app's own declarations: -sk-rect(N) and -sk-value() are lengths */
    /* A url's image source, or false when it names none. */
    bool (*image)(void *data, sk_str url, uint32_t *index);
    /* An @keyframes rule's index, or false when none has the name. */
    bool (*keyframes)(void *data, sk_str name, uint32_t *index);
    void *data;
    const char *why; /* why the last value failed, when more is known than that it did */
} sk_vctx;

/* A cursor over a value's component values that steps over
 * whitespace. */
typedef struct {
    const sk_cv *cv;
    size_t n, i;
} sk_vcur;

void sk_vcur_init(sk_vcur *v, const sk_cv *cv, size_t n);
const sk_cv *sk_vcur_peek(sk_vcur *v);
const sk_cv *sk_vcur_next(sk_vcur *v);
bool sk_vcur_end(sk_vcur *v);
/* Consumes a comma when one is next. */
bool sk_vcur_comma(sk_vcur *v);
/* The values up to the next top-level comma, as a cursor of their own;
 * the comma is consumed. */
sk_vcur sk_vcur_item(sk_vcur *v);

/* ---- Values ---- */

enum {
    SK_L_PCT = 1,     /* percentages */
    SK_L_NEG = 2,     /* negative values */
    SK_L_AUTO = 4,    /* auto */
    SK_L_NONE = 8,    /* none */
    SK_L_NORMAL = 16, /* normal */
    SK_L_NUMBER = 32, /* a unitless number is px, as SVG's lengths are */
};
bool sk_v_len(sk_vctx *c, const sk_cv *v, unsigned flags, sk_css_len *out);
bool sk_v_number(const sk_cv *v, float *out);
bool sk_v_integer(const sk_cv *v, int32_t *out);
/* A number or a percentage, as a factor: 130% is 1.3. */
bool sk_v_factor(const sk_cv *v, float *out);
/* Degrees; a unitless 0 when zero_ok. */
bool sk_v_angle(const sk_cv *v, bool zero_ok, float *out);
/* Seconds. */
bool sk_v_time(const sk_cv *v, float *out);
bool sk_v_color(sk_vctx *c, const sk_cv *v, sk_css_color *out);
bool sk_v_ident(const sk_cv *v, const char *lower);
/* url(), a gradient; none when none_ok. */
bool sk_v_image(sk_vctx *c, const sk_cv *v, bool none_ok, sk_css_image *out);
/* A position of 1 to 4 values from the cursor, as background-position
 * and gradients' "at" take them. */
bool sk_v_position(sk_vctx *c, sk_vcur *v, sk_css_position *out);
/* A shadow: box-shadow's when box, else text-shadow's and
 * drop-shadow()'s. */
bool sk_v_shadow(sk_vctx *c, sk_vcur *v, bool box, sk_css_shadow *out);
bool sk_v_timing(const sk_cv *v, sk_css_timing *out);
/* A transform list: none (count 0) or functions. */
bool sk_v_transform(sk_vctx *c, sk_vcur *v, sk_css_xf **out, uint16_t *n);

/* The named colours of CSS Color 4; false for any other name. */
bool sk_named_color(sk_str name, sk_css_color *out);

/* ---- Properties ---- */

typedef struct {
    const char *name;
    uint16_t id; /* sk_prop */
    bool inherited;
} sk_propdef;

typedef struct {
    const char *name;
    const uint16_t *longhands;
    size_t n;
} sk_shorthand;

/* A longhand by name, or by id; NULL when not in the subset. */
const sk_propdef *sk_prop_find(sk_str name);
const sk_propdef *sk_prop_def(uint16_t id);
const sk_shorthand *sk_shorthand_find(sk_str name);
/* Every property and shorthand name the subset knows, for messages. */
size_t sk_prop_count(void);

/* Parses a longhand's value into its fields of out. False when it is
 * not a valid value; c->why may say why. */
bool sk_prop_parse(uint16_t id, sk_vctx *c, const sk_cv *cv, size_t n, sk_css_style *out);
/* A shorthand's value into all its longhands' fields, those it leaves
 * out at their initial values. */
bool sk_shorthand_parse(const sk_shorthand *sh, sk_vctx *c, const sk_cv *cv, size_t n, sk_css_style *out);
/* Copies one longhand's fields. */
void sk_prop_copy(uint16_t id, sk_css_style *dst, const sk_css_style *src);
/* True when the two styles hold the same value of the longhand. */
bool sk_prop_equal(uint16_t id, const sk_css_style *a, const sk_css_style *b);
/* Every field at its initial value, currentcolor as current. */
void sk_style_initial(sk_css_style *s, sk_css_color current);
/* The computed values that need the whole style: a border's or
 * outline's width is 0 while its style is none or hidden. */
void sk_style_finish(sk_css_style *s);

/* Appends the longhand's value, as CSS text, for the summary and for
 * telling styles apart. */
void sk_prop_print(sk_buf *out, uint16_t id, const sk_css_style *s);

/* A declaration's value is one of inherit, initial, unset: its kind,
 * or 0. */
enum { SK_WIDE_INHERIT = 1, SK_WIDE_INITIAL, SK_WIDE_UNSET };
int sk_css_wide(const sk_cv *cv, size_t n);
/* True when the value holds var() anywhere. */
bool sk_has_var(const sk_cv *cv, size_t n);

#endif
