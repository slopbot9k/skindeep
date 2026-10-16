/* The cascade of CSS Cascade 4 over the element tree, once for each
 * state an element can reach.
 *
 * For each element, in tree order: discovery finds the rule selectors
 * that may match it and the state variables their results read; its
 * inputs are those and the inputs of its parent that change what it
 * inherits. Then, for every combination of the inputs' values, the
 * matching declarations are ranked (origin and importance, then
 * specificity, then order), custom properties and var() resolved, and
 * each property computed in the element's context: font-size first,
 * then color, then the rest, so em and currentcolor have their values.
 * Equal styles are kept once. Last, counters are counted through the
 * tree and counter() in generated content becomes text, or the
 * ordinal of a list's row. */
#include "front.h"

#include "../src/base/map.h"
#include "../src/base/paths.h"
#include "../src/base/utf8.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void sk_xdecl_init(sk_xdecl *x, const sk_css_decl *d, const sk_css_sheet *sheet, bool internal)
{
    memset(x, 0, sizeof *x);
    x->d = d;
    x->sheet = sheet;
    x->internal = internal;
    if (d->name.len >= 2 && d->name.ptr[0] == '-' && d->name.ptr[1] == '-') {
        x->custom = true;
        return;
    }
    const sk_propdef *p = sk_prop_find(d->name);
    if (p)
        x->prop = p->id;
    else
        x->sh = sk_shorthand_find(d->name);
    x->wide = sk_css_wide(d->value, d->n);
    x->var = sk_has_var(d->value, d->n);
}

/* ---- Component values as text ---- */

void sk_cv_print(sk_buf *b, const sk_cv *cv, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        const sk_cv *c = &cv[i];
        const sk_tok *t = &c->tok;
        if (c->kind == SK_CV_FUNCTION) {
            sk_buf_append_str(b, t->text);
            sk_buf_putc(b, '(');
            sk_cv_print(b, c->kids, c->n);
            sk_buf_putc(b, ')');
            continue;
        }
        if (c->kind == SK_CV_BLOCK) {
            char open = t->type == SK_TOK_LBRACE ? '{' : t->type == SK_TOK_LBRACKET ? '[' : '(';
            sk_buf_putc(b, open);
            sk_cv_print(b, c->kids, c->n);
            sk_buf_putc(b, open == '{' ? '}' : open == '[' ? ']' : ')');
            continue;
        }
        switch (t->type) {
        case SK_TOK_IDENT:
        case SK_TOK_NUMBER:
            sk_buf_append_str(b, t->text);
            break;
        case SK_TOK_AT_KEYWORD:
            sk_buf_putc(b, '@');
            sk_buf_append_str(b, t->text);
            break;
        case SK_TOK_HASH:
            sk_buf_putc(b, '#');
            sk_buf_append_str(b, t->text);
            break;
        case SK_TOK_STRING:
            sk_buf_putc(b, '"');
            sk_buf_append_str(b, t->text);
            sk_buf_putc(b, '"');
            break;
        case SK_TOK_URL:
            sk_buf_append_c(b, "url(");
            sk_buf_append_str(b, t->text);
            sk_buf_putc(b, ')');
            break;
        case SK_TOK_PERCENTAGE:
            sk_buf_append_str(b, t->text);
            sk_buf_putc(b, '%');
            break;
        case SK_TOK_DIMENSION:
            sk_buf_append_str(b, t->text);
            sk_buf_append_str(b, t->unit);
            break;
        case SK_TOK_WHITESPACE:
            sk_buf_putc(b, ' ');
            break;
        case SK_TOK_COLON:
            sk_buf_putc(b, ':');
            break;
        case SK_TOK_SEMICOLON:
            sk_buf_putc(b, ';');
            break;
        case SK_TOK_COMMA:
            sk_buf_append_c(b, ", ");
            break;
        case SK_TOK_DELIM: {
            char u[4];
            sk_buf_append(b, u, sk_utf8_encode(t->delim, u));
            break;
        }
        default:
            break;
        }
    }
}

/* ---- Messages ---- */

static void report(sk_front *f, sk_xdecl *x, const char *fmt, ...) SK_PRINTF(3, 4);

static void report(sk_front *f, sk_xdecl *x, const char *fmt, ...)
{
    if (x->reported)
        return;
    x->reported = true;
    sk_buf b = {0};
    va_list ap;
    va_start(ap, fmt);
    sk_buf_vprintf(&b, fmt, ap);
    va_end(ap);
    sk_diag_line(f->d, x->sheet->file, x->d->line, "%s", sk_buf_cstr(&b));
    sk_buf_free(&b);
}

static void value_text(sk_buf *b, const sk_cv *cv, size_t n)
{
    sk_buf t = {0};
    sk_cv_print(&t, cv, n);
    if (t.len > 80) {
        sk_buf_append(b, t.ptr, 77);
        sk_buf_append_c(b, "...");
    } else {
        sk_buf_append(b, t.ptr ? t.ptr : "", t.len);
    }
    sk_buf_free(&t);
}

/* ---- var() ---- */

typedef SK_VEC(sk_cv) cv_vec;

typedef struct {
    sk_front *f;
    sk_custom *items; /* the element's custom properties as they stand */
    size_t n;
    uint8_t *state;   /* per item: 0 raw, 1 resolving, 2 done, 3 invalid */
} customs_ctx;

static bool resolve_custom(customs_ctx *c, size_t i, int depth);

static sk_custom *find_custom(customs_ctx *c, sk_str name, size_t *at)
{
    for (size_t i = c->n; i-- > 0;)
        if (sk_str_eq_c(name, c->items[i].name)) {
            *at = i;
            return &c->items[i];
        }
    return NULL;
}

/* cv with every var() replaced by its custom property's tokens, or its
 * fallback; false when one names nothing and has none. */
static bool substitute(customs_ctx *c, const sk_cv *cv, size_t n, cv_vec *out, int depth)
{
    if (depth > 32)
        return false;
    for (size_t i = 0; i < n; i++) {
        const sk_cv *x = &cv[i];
        if (x->kind == SK_CV_FUNCTION && sk_ident_is(x->tok.text, "var")) {
            size_t k = 0;
            while (k < x->n && x->kids[k].kind == SK_CV_TOKEN && x->kids[k].tok.type == SK_TOK_WHITESPACE)
                k++;
            if (k >= x->n || x->kids[k].kind != SK_CV_TOKEN || x->kids[k].tok.type != SK_TOK_IDENT ||
                x->kids[k].tok.text.len < 3 || memcmp(x->kids[k].tok.text.ptr, "--", 2) != 0)
                return false;
            sk_str name = x->kids[k].tok.text;
            size_t j = k + 1;
            while (j < x->n && x->kids[j].kind == SK_CV_TOKEN && x->kids[j].tok.type == SK_TOK_WHITESPACE)
                j++;
            bool has_fallback = false;
            if (j < x->n) {
                if (x->kids[j].kind != SK_CV_TOKEN || x->kids[j].tok.type != SK_TOK_COMMA)
                    return false;
                has_fallback = true;
                j++;
            }
            size_t at;
            sk_custom *cu = find_custom(c, name, &at);
            if (cu && resolve_custom(c, at, depth + 1)) {
                for (size_t q = 0; q < cu->n; q++)
                    sk_vec_push(out, cu->cv[q]);
            } else if (has_fallback) {
                if (!substitute(c, x->kids + j, x->n - j, out, depth + 1))
                    return false;
            } else {
                return false;
            }
            continue;
        }
        if (x->kind != SK_CV_TOKEN && sk_has_var(x->kids, x->n)) {
            cv_vec kids = {0};
            if (!substitute(c, x->kids, x->n, &kids, depth + 1)) {
                sk_vec_free(&kids);
                return false;
            }
            sk_cv y = *x;
            y.n = kids.len;
            y.kids = kids.len ? sk_arena_array(&c->f->arena, kids.len, sizeof *y.kids) : NULL;
            if (kids.len)
                memcpy(y.kids, kids.ptr, kids.len * sizeof *y.kids);
            sk_vec_free(&kids);
            sk_vec_push(out, y);
            continue;
        }
        sk_vec_push(out, *x);
    }
    return true;
}

static bool resolve_custom(customs_ctx *c, size_t i, int depth)
{
    if (c->state[i] == 2)
        return true;
    if (c->state[i] == 3 || c->state[i] == 1 || depth > 32) {
        c->state[i] = 3;
        return false;
    }
    c->state[i] = 1;
    cv_vec out = {0};
    bool ok = substitute(c, c->items[i].cv, c->items[i].n, &out, depth + 1);
    if (ok) {
        sk_cv *cv = out.len ? sk_arena_array(&c->f->arena, out.len, sizeof *cv) : NULL;
        if (out.len)
            memcpy(cv, out.ptr, out.len * sizeof *cv);
        c->items[i].cv = cv;
        c->items[i].n = out.len;
    }
    sk_vec_free(&out);
    c->state[i] = ok ? 2 : 3;
    return ok;
}

/* ---- Images and keyframes, for values ---- */

static bool image_of(void *data, sk_str url, uint32_t *index)
{
    sk_front *f = data;
    char *u = sk_xstrndup(url.ptr ? url.ptr : "", url.len);
    sk_str t = sk_str_trim(sk_str_c(u));
    char *key = NULL;
    bool ok = false;
    if (t.len >= 11 && strncasecmp(t.ptr, "data:image/", 11) == 0) {
        key = sk_xstrndup(t.ptr, t.len);
        ok = true;
    } else if (t.len && t.ptr[0] != '/' && !strchr(u, ':')) {
        char *rel = sk_xstrndup(t.ptr, t.len);
        char *full = sk_path_join(f->url_base, rel);
        key = realpath(full, NULL);
        size_t rl = strlen(f->skin.real);
        ok = key && strncmp(key, f->skin.real, rl) == 0 && key[rl] == '/';
        free(rel);
        free(full);
    }
    if (ok) {
        size_t i = 0;
        for (; i < f->images.len; i++)
            if (!strcmp(f->images.ptr[i].real, key))
                break;
        if (i == f->images.len) {
            const char *shown = key;
            size_t rl = strlen(f->skin.real);
            if (!strncmp(key, f->skin.real, rl) && key[rl] == '/')
                shown = key + rl + 1;
            sk_front_image im = {sk_arena_strndup(&f->arena, shown, strlen(shown)), key};
            sk_vec_push(&f->images, im);
            key = NULL;
        }
        *index = (uint32_t)i;
    }
    free(key);
    free(u);
    return ok;
}

static bool keyframes_of(void *data, sk_str name, uint32_t *index)
{
    sk_front *f = data;
    for (size_t i = f->keyframes.len; i-- > 0;)
        if (sk_str_eq_c(name, f->keyframes.ptr[i].name)) {
            *index = (uint32_t)i;
            return true;
        }
    return false;
}

void sk_vctx_init(sk_front *f, sk_vctx *c, const sk_css_style *parent)
{
    memset(c, 0, sizeof *c);
    c->arena = &f->arena;
    c->font_size = c->parent_font_size = parent->font_size;
    c->color = c->parent_color = parent->color;
    c->parent = parent;
    c->image = image_of;
    c->keyframes = keyframes_of;
    c->data = f;
}

/* ---- Fonts ---- */

typedef struct {
    const char *family;
    uint16_t weight;
    uint8_t style;
} face_t;

static const face_t EMBEDDED[] = {
    {"DejaVu Sans Mono", 400, SK_FSTYLE_NORMAL}, {"DejaVu Sans Mono", 700, SK_FSTYLE_NORMAL},
    {"DejaVu Sans Mono", 400, SK_FSTYLE_OBLIQUE}, {"DejaVu Sans Mono", 700, SK_FSTYLE_OBLIQUE},
    {"DejaVu Sans", 400, SK_FSTYLE_NORMAL},       {"DejaVu Sans", 700, SK_FSTYLE_NORMAL},
};
#define NEMBEDDED 6

static face_t face_at(const sk_front *f, size_t i)
{
    if (i < NEMBEDDED)
        return EMBEDDED[i];
    const sk_skin_font *s = &f->skin.fonts[i - NEMBEDDED];
    face_t t = {s->family, s->weight, s->style};
    return t;
}

static const char *generic(const char *name)
{
    static const char *const SANS[] = {"serif",       "sans-serif",    "cursive", "fantasy", "system-ui", "ui-serif",
                                       "ui-sans-serif", "ui-rounded", "math",    "emoji",   "fangsong"};
    if (!strcasecmp(name, "monospace") || !strcasecmp(name, "ui-monospace"))
        return "DejaVu Sans Mono";
    for (size_t i = 0; i < SK_LEN(SANS); i++)
        if (!strcasecmp(name, SANS[i]))
            return "DejaVu Sans";
    return name;
}

/* CSS Fonts 4's matching of style, then weight, among one family's
 * faces. */
static bool pick(const sk_front *f, const char *family, uint16_t weight, uint8_t style, uint16_t *out)
{
    size_t total = NEMBEDDED + f->skin.nfonts;
    static const uint8_t ORDER[3][3] = {{SK_FSTYLE_NORMAL, SK_FSTYLE_OBLIQUE, SK_FSTYLE_ITALIC},
                                        {SK_FSTYLE_ITALIC, SK_FSTYLE_OBLIQUE, SK_FSTYLE_NORMAL},
                                        {SK_FSTYLE_OBLIQUE, SK_FSTYLE_ITALIC, SK_FSTYLE_NORMAL}};
    int best_style = -1;
    for (int k = 0; k < 3 && best_style < 0; k++)
        for (size_t i = 0; i < total; i++) {
            face_t t = face_at(f, i);
            if (!strcasecmp(t.family, family) && t.style == ORDER[style][k])
                best_style = ORDER[style][k];
        }
    if (best_style < 0)
        return false;
    long best = -1, best_key = 0;
    for (size_t i = 0; i < total; i++) {
        face_t t = face_at(f, i);
        if (strcasecmp(t.family, family) || t.style != best_style)
            continue;
        long w = t.weight, key;
        /* A rank: lower is nearer, as the spec orders the search. */
        if (w == weight)
            key = 0;
        else if (weight >= 400 && weight <= 500)
            key = w > weight && w <= 500 ? w - weight : w < weight ? 1000 + (weight - w) : 2000 + (w - 500);
        else if (weight < 400)
            key = w < weight ? weight - w : 1000 + (w - weight);
        else
            key = w > weight ? w - weight : 1000 + (weight - w);
        if (best < 0 || key < best_key) {
            best = (long)i;
            best_key = key;
        }
    }
    *out = (uint16_t)best;
    return true;
}

static void resolve_font(const sk_front *f, sk_css_style *s)
{
    uint16_t face = 4;
    bool found = false;
    for (uint16_t i = 0; i < s->nfont_family && !found; i++)
        found = pick(f, generic(s->font_family[i]), s->font_weight, s->font_style, &face);
    if (!found)
        pick(f, "DejaVu Sans", s->font_weight, s->font_style, &face);
    face_t t = face_at(f, face);
    s->font = face;
    s->font_synth = 0;
    if (s->font_weight >= 600 && t.weight < 600)
        s->font_synth |= 1;
    if (s->font_style != SK_FSTYLE_NORMAL && t.style == SK_FSTYLE_NORMAL)
        s->font_synth |= 2;
}

/* ---- One element in one state ---- */

typedef struct {
    sk_xdecl *x;
    uint8_t layer;
    uint32_t spec, order;
} ranked;

static int by_rank(const void *a, const void *b)
{
    const ranked *x = a, *y = b;
    if (x->layer != y->layer)
        return x->layer < y->layer ? -1 : 1;
    if (x->spec != y->spec)
        return x->spec < y->spec ? -1 : 1;
    return x->order < y->order ? -1 : x->order > y->order ? 1 : 0;
}

static uint8_t important_layer(uint8_t layer)
{
    return layer == SK_LAYER_UA       ? SK_LAYER_UA_IMPORTANT
           : layer == SK_LAYER_INLINE ? SK_LAYER_INLINE_IMPORTANT
                                      : SK_LAYER_AUTHOR_IMPORTANT;
}

typedef SK_VEC(ranked) ranked_vec;
typedef SK_VEC(sk_input) input_vec;
typedef SK_VEC(sk_entry *) entry_vec;

static void add(ranked_vec *v, sk_xdecl *x, uint8_t layer, uint32_t spec, uint32_t order)
{
    ranked r = {x, x->d->important ? important_layer(layer) : layer, spec, order};
    sk_vec_push(v, r);
}

static sk_css_style ROOT; /* what the host inherits: the initial values */
static bool root_ready;

static const sk_css_style *root_style(void)
{
    if (!root_ready) {
        sk_style_initial(&ROOT, (sk_css_color){0, 0, 0, 1});
        root_ready = true;
    }
    return &ROOT;
}

typedef struct {
    sk_front *f;
    sk_vctx vc;
    sk_css_style *out;
    const sk_css_style *parent;
    sk_css_style init;
    customs_ctx cc;
    /* Shorthands parsed for this style: the declaration and what it
     * gave. */
    SK_VEC(sk_xdecl *) sh_decl;
    SK_VEC(sk_css_style *) sh_style;
    bool cache;
} computer;

/* The tokens a declaration gives, var() substituted; NULL with *bad
 * when substitution fails. */
static const sk_cv *tokens(computer *c, sk_xdecl *x, size_t *n, bool *bad)
{
    *bad = false;
    if (!x->var) {
        *n = x->d->n;
        return x->d->value;
    }
    cv_vec out = {0};
    if (!substitute(&c->cc, x->d->value, x->d->n, &out, 0)) {
        sk_vec_free(&out);
        *bad = true;
        return NULL;
    }
    sk_cv *cv = out.len ? sk_arena_array(&c->f->arena, out.len, sizeof *cv) : NULL;
    if (out.len)
        memcpy(cv, out.ptr, out.len * sizeof *cv);
    *n = out.len;
    sk_vec_free(&out);
    return cv;
}

static void fail(computer *c, sk_xdecl *x, const sk_cv *cv, size_t n)
{
    sk_buf v = {0};
    if (cv)
        value_text(&v, cv, n);
    if (!cv)
        report(c->f, x, "\"%.*s\": a var() names no custom property and has no fallback", (int)x->d->name.len,
               x->d->name.ptr);
    else
        report(c->f, x, "\"%.*s\": after var(), \"%s\" is not a valid value%s%s", (int)x->d->name.len,
               x->d->name.ptr, sk_buf_cstr(&v), c->vc.why ? ": " : "", c->vc.why ? c->vc.why : "");
    sk_buf_free(&v);
}

/* The value of longhand id, from x (the winning declaration or NULL). */
static void apply(computer *c, uint16_t id, sk_xdecl *x, bool inherited)
{
    int wide = x ? x->wide : 0;
    if (!x)
        wide = inherited ? SK_WIDE_INHERIT : SK_WIDE_INITIAL;
    if (wide == SK_WIDE_UNSET)
        wide = inherited ? SK_WIDE_INHERIT : SK_WIDE_INITIAL;
    if (wide == SK_WIDE_INHERIT) {
        sk_prop_copy(id, c->out, c->parent);
        return;
    }
    if (wide == SK_WIDE_INITIAL) {
        sk_prop_copy(id, c->out, &c->init);
        return;
    }
    size_t n;
    bool bad;
    const sk_cv *cv = tokens(c, x, &n, &bad);
    c->vc.internal = x->internal;
    c->vc.why = NULL;
    c->f->url_base = x->internal ? c->f->skin.real : c->f->css_dir;
    bool ok = false;
    if (!bad) {
        if (x->prop) {
            ok = sk_prop_parse(id, &c->vc, cv, n, c->out);
        } else {
            sk_css_style *s = NULL;
            for (size_t i = 0; c->cache && i < c->sh_decl.len; i++)
                if (c->sh_decl.ptr[i] == x)
                    s = c->sh_style.ptr[i];
            if (!s) {
                s = sk_arena_zalloc(&c->f->arena, sizeof *s);
                *s = *c->out;
                if (!sk_shorthand_parse(x->sh, &c->vc, cv, n, s)) {
                    s = NULL;
                } else if (c->cache) {
                    sk_vec_push(&c->sh_decl, x);
                    sk_vec_push(&c->sh_style, s);
                }
            }
            if (s) {
                sk_prop_copy(id, c->out, s);
                ok = true;
            }
        }
    }
    if (!ok) {
        fail(c, x, bad ? NULL : cv, n);
        sk_prop_copy(id, c->out, inherited ? c->parent : &c->init);
    }
}

/* The custom properties: the parent's, then the winners', var()
 * resolved. */
static void customs(computer *c, const sk_customs *parent, ranked *r, size_t nr)
{
    SK_VEC(sk_custom) items = {0};
    for (size_t i = 0; parent && i < parent->n; i++)
        sk_vec_push(&items, parent->items[i]);
    for (size_t i = 0; i < nr; i++) {
        sk_xdecl *x = r[i].x;
        if (!x->custom)
            continue;
        const char *name = sk_arena_strndup(&c->f->arena, x->d->name.ptr, x->d->name.len);
        size_t at = SIZE_MAX;
        for (size_t k = 0; k < items.len; k++)
            if (!strcmp(items.ptr[k].name, name))
                at = k;
        int wide = sk_css_wide(x->d->value, x->d->n);
        if (wide == SK_WIDE_INITIAL) {
            if (at != SIZE_MAX) {
                memmove(&items.ptr[at], &items.ptr[at + 1], (items.len - at - 1) * sizeof *items.ptr);
                items.len--;
            }
            continue;
        }
        if (wide == SK_WIDE_INHERIT || wide == SK_WIDE_UNSET) {
            const sk_custom *p = NULL;
            for (size_t k = 0; parent && k < parent->n; k++)
                if (!strcmp(parent->items[k].name, name))
                    p = &parent->items[k];
            if (at != SIZE_MAX && p)
                items.ptr[at] = *p;
            continue;
        }
        sk_custom cu = {name, x->d->value, x->d->n};
        if (at != SIZE_MAX)
            items.ptr[at] = cu;
        else
            sk_vec_push(&items, cu);
    }
    c->cc.f = c->f;
    c->cc.n = items.len;
    c->cc.items = items.len ? sk_arena_array(&c->f->arena, items.len, sizeof *c->cc.items) : NULL;
    if (items.len)
        memcpy(c->cc.items, items.ptr, items.len * sizeof *items.ptr);
    sk_vec_free(&items);
    c->cc.state = c->cc.n ? sk_arena_zalloc(&c->f->arena, c->cc.n) : NULL;
    /* The parent's are resolved already. */
    for (size_t i = 0; i < c->cc.n; i++)
        for (size_t k = 0; parent && k < parent->n; k++)
            if (c->cc.items[i].cv == parent->items[k].cv)
                c->cc.state[i] = 2;
    for (size_t i = 0; i < c->cc.n; i++)
        resolve_custom(&c->cc, i, 0);
    /* An invalid one is as if absent. */
    size_t w = 0;
    for (size_t i = 0; i < c->cc.n; i++)
        if (c->cc.state[i] != 3)
            c->cc.items[w++] = c->cc.items[i];
    c->cc.n = w;
}

typedef struct {
    sk_css_style *style;
    sk_customs customs;
} computed;

/* el's style in the state m holds; origin is what its selectors match
 * (el itself, or the element a pseudo-element belongs to). */
static computed compute(sk_front *f, sk_el *el, sk_el *origin, sk_match *m, const sk_css_style *parent,
                        const sk_customs *pcust)
{
    ranked_vec r = {0};
    for (size_t i = 0; i < el->nhint; i++)
        add(&r, &el->hint_decls[i], SK_LAYER_HINT, 0, (uint32_t)i);
    for (size_t i = 0; i < el->ncands; i++) {
        sk_entry *e = el->cands[i];
        if (sk_match_selector(m, e->sel, origin) != SK_YES)
            continue;
        e->matched++;
        for (size_t j = 0; j < e->ndecls; j++)
            add(&r, &e->decls[j], e->layer, e->spec, e->order << 12 | (uint32_t)SK_MIN(j, 4095u));
    }
    for (size_t i = 0; i < el->ninline; i++)
        add(&r, &el->inline_decls[i], SK_LAYER_INLINE, 0, (uint32_t)i);
    if (r.len > 1)
        qsort(r.ptr, r.len, sizeof *r.ptr, by_rank);
    sk_xdecl *win[SK_PROP_COUNT];
    memset(win, 0, sizeof win);
    for (size_t i = 0; i < r.len; i++) {
        sk_xdecl *x = r.ptr[i].x;
        if (x->prop)
            win[x->prop] = x;
        else if (x->sh)
            for (size_t k = 0; k < x->sh->n; k++)
                win[x->sh->longhands[k]] = x;
    }
    computer c;
    memset(&c, 0, sizeof c);
    c.f = f;
    c.parent = parent;
    c.out = sk_arena_zalloc(&f->arena, sizeof *c.out);
    sk_vctx_init(f, &c.vc, parent);
    customs(&c, pcust, r.ptr, r.len);
    sk_style_initial(&c.init, parent->color);
    *c.out = c.init;
    /* font-size, then color, then the rest. */
    apply(&c, SK_PROP_FONT_SIZE, win[SK_PROP_FONT_SIZE], true);
    c.vc.font_size = c.out->font_size;
    apply(&c, SK_PROP_COLOR, win[SK_PROP_COLOR], true);
    c.vc.color = c.out->color;
    sk_style_initial(&c.init, c.out->color);
    c.init.font_size = 16;
    c.cache = true;
    for (uint16_t id = 1; id < SK_PROP_COUNT; id++) {
        if (id == SK_PROP_FONT_SIZE || id == SK_PROP_COLOR)
            continue;
        const sk_propdef *d = sk_prop_def(id);
        if (!d)
            continue;
        apply(&c, id, win[id], d->inherited);
    }
    sk_style_finish(c.out);
    /* CSS 2.1 9.7 and Flexbox 4: an absolute box does not float, and
     * an absolute or floated box, a flex item and a grid item are
     * block-level. ::before and ::after of a flex container are its
     * items too. */
    sk_css_style *o = c.out;
    if (o->position == SK_POS_ABSOLUTE || o->position == SK_POS_FIXED)
        o->float_ = SK_FLOAT_NONE;
    bool item = el != f->els.ptr[0] && (parent->display == SK_DISPLAY_FLEX || parent->display == SK_DISPLAY_GRID);
    if (o->display != SK_DISPLAY_NONE &&
        (o->position == SK_POS_ABSOLUTE || o->position == SK_POS_FIXED || o->float_ != SK_FLOAT_NONE || item) &&
        (o->display == SK_DISPLAY_INLINE || o->display == SK_DISPLAY_INLINE_BLOCK))
        o->display = SK_DISPLAY_BLOCK;
    resolve_font(f, c.out);
    computed out = {c.out, {c.cc.items, c.cc.n}};
    sk_vec_free(&c.sh_decl);
    sk_vec_free(&c.sh_style);
    sk_vec_free(&r);
    return out;
}

/* ---- Styles kept once ---- */

static void style_text(sk_buf *b, const sk_css_style *s)
{
    for (uint16_t id = 1; id < SK_PROP_COUNT; id++) {
        sk_buf_printf(b, "%u:", (unsigned)id);
        sk_prop_print(b, id, s);
        sk_buf_putc(b, ';');
    }
}

static uint32_t keep_style(sk_front *f, sk_css_style *s)
{
    sk_map *map = f->dedupe;
    sk_buf b = {0};
    style_text(&b, s);
    void **slot = sk_map_put(map, sk_buf_str(&b));
    sk_buf_free(&b);
    if (*slot)
        return (uint32_t)((uintptr_t)*slot - 1);
    sk_vec_push(&f->styles, s);
    *slot = (void *)(uintptr_t)f->styles.len;
    return (uint32_t)(f->styles.len - 1);
}

/* What a child inherits: the inherited properties, any property some
 * declaration inherits explicitly, and the custom properties. */
static bool *inherit_used;

static void fingerprint(sk_buf *b, const sk_css_style *s, const sk_customs *cu)
{
    for (uint16_t id = 1; id < SK_PROP_COUNT; id++) {
        const sk_propdef *d = sk_prop_def(id);
        if (!d || !(d->inherited || inherit_used[id]))
            continue;
        sk_buf_printf(b, "%u:", (unsigned)id);
        sk_prop_print(b, id, s);
        sk_buf_putc(b, ';');
    }
    /* The custom properties, sorted by name. */
    size_t n = cu ? cu->n : 0;
    const sk_custom **sorted = n ? sk_xcalloc(n, sizeof *sorted) : NULL;
    for (size_t i = 0; i < n; i++) {
        size_t k = i;
        while (k > 0 && strcmp(sorted[k - 1]->name, cu->items[i].name) > 0) {
            sorted[k] = sorted[k - 1];
            k--;
        }
        sorted[k] = &cu->items[i];
    }
    for (size_t i = 0; i < n; i++) {
        sk_buf_printf(b, "%s=", sorted[i]->name);
        sk_cv_print(b, sorted[i]->cv, sorted[i]->n);
        sk_buf_putc(b, ';');
    }
    free(sorted);
}

/* ---- States ---- */

static size_t table_index(const sk_el *p, const sk_input *in, const int *vals, size_t n, const sk_el *pin,
                          uint8_t pin_count)
{
    size_t idx = 0, stride = 1;
    for (uint8_t i = 0; i < p->ninputs; i++) {
        int v = 0;
        for (size_t k = 0; k < n; k++)
            if (in[k].owner == p->inputs[i].owner && in[k].var == p->inputs[i].var)
                v = vals[k];
        if (pin && p->inputs[i].owner == pin && p->inputs[i].var == SK_VAR_COUNT)
            v = pin_count;
        idx += (size_t)v * stride;
        stride *= p->inputs[i].radix;
    }
    return idx;
}

/* The limits widget whose count el's existence fixes, if any. */
static const sk_el *pin_of(const sk_el *el, uint8_t *count)
{
    for (const sk_el *p = el; p; p = p->parent)
        if (p->group) {
            *count = p->group;
            return p->limits;
        }
    return NULL;
}

static void push_input(input_vec *v, sk_input in)
{
    for (size_t i = 0; i < v->len; i++)
        if (v->ptr[i].owner == in.owner && v->ptr[i].var == in.var)
            return;
    sk_vec_push(v, in);
}

static void describe(sk_buf *b, const sk_front *f, const sk_el *e)
{
    const sk_el *w = e;
    while (w && w->role != SK_ROLE_WIDGET && w->role != SK_ROLE_PANEL && w->role != SK_ROLE_TOGGLE)
        w = w->parent;
    if (e->kind == SK_EL_BEFORE || e->kind == SK_EL_AFTER)
        sk_buf_append_c(b, e->kind == SK_EL_BEFORE ? "::before of " : "::after of ");
    if (w && w->widget >= 0)
        sk_buf_printf(b, "widget \"%s\"", f->skin.widgets[w->widget].w.id);
    else if (w && w->role == SK_ROLE_PANEL)
        sk_buf_printf(b, "panel \"%s\"", f->skin.panels[w->rect - 1].klass);
    else
        sk_buf_printf(b, "element %u", (unsigned)e->index);
}

/* Discovery: the candidates for el (or its pseudo-element) and the
 * variables they read. */
static void discover(sk_front *f, sk_el *target, uint8_t pseudo, entry_vec *cands, input_vec *found, bool *repeated)
{
    sk_match m;
    memset(&m, 0, sizeof m);
    m.f = f;
    m.discover = true;
    m.pin = pin_of(target, &m.pin_count);
    for (size_t i = 0; i < f->entries.len; i++) {
        sk_entry *e = &f->entries.ptr[i];
        if (e->sel->pseudo != pseudo)
            continue;
        m.repeated = false;
        if (sk_match_selector(&m, e->sel, target) != SK_NO) {
            sk_vec_push(cands, e);
            *repeated = *repeated || m.repeated;
        }
    }
    for (size_t i = 0; i < m.found.len; i++)
        push_input(found, m.found.ptr[i]);
    sk_vec_free(&m.found);
}

static sk_el *pseudo_new(sk_front *f, sk_el *origin, uint8_t kind)
{
    sk_el *e = sk_arena_zalloc(&f->arena, sizeof *e);
    e->index = (uint32_t)f->els.len;
    e->kind = kind;
    e->parent = origin;
    e->widget = origin->widget;
    e->limits = origin->limits;
    e->text_source = SK_TEXT_STATIC;
    e->svg.kind = SK_SVG_NONE;
    e->svg.image = SK_PACK_NONE;
    sk_vec_push(&f->els, e);
    if (kind == SK_EL_BEFORE)
        origin->before = e;
    else
        origin->after = e;
    return e;
}

/* Inputs, every state, the table and what children inherit. */
static void states(sk_front *f, sk_el *el, sk_el *origin, input_vec *found)
{
    uint8_t pin_count = 0;
    const sk_el *pin = pin_of(origin, &pin_count);
    const sk_el *parent = el == origin ? el->parent : origin;
    input_vec in = {0};
    for (size_t i = 0; i < found->len; i++)
        if (!(pin && found->ptr[i].owner == pin && found->ptr[i].var == SK_VAR_COUNT))
            push_input(&in, found->ptr[i]);
    for (uint8_t i = 0; parent && i < parent->ninputs; i++)
        if (parent->inherited & (1u << i))
            if (!(pin && parent->inputs[i].owner == pin && parent->inputs[i].var == SK_VAR_COUNT))
                push_input(&in, parent->inputs[i]);
    size_t total = 1;
    for (size_t i = 0; i < in.len; i++)
        total *= in.ptr[i].radix;
    /* The cap on the states in all: past it, an element takes its
     * default state alone, and the skin is refused. */
    size_t cap = f->max_states ? f->max_states : SK_FRONT_MAX_STATES;
    if (f->states + SK_MIN(total, (size_t)SK_PACK_MAX_STATES) > cap) {
        if (!f->states_said)
            sk_diag(f->d, f->css_shown ? f->css_shown : "skin.css", "(states)",
                    "the skin reaches more than %zu states in all", cap);
        f->states_said = true;
        in.len = 0;
        total = 1;
    }
    if (in.len > SK_PACK_MAX_INPUTS || total > SK_PACK_MAX_STATES) {
        sk_buf b = {0};
        describe(&b, f, el);
        sk_buf_append_c(&b, " can reach too many states: it depends on");
        for (size_t i = 0; i < in.len; i++)
            sk_buf_printf(&b, " %s", sk_var_name(in.ptr[i].var));
        sk_diag(f->d, f->css_shown ? f->css_shown : "skin.css", "(states)", "%s", sk_buf_cstr(&b));
        sk_buf_free(&b);
        while (in.len > SK_PACK_MAX_INPUTS || total > SK_PACK_MAX_STATES) {
            total /= in.ptr[in.len - 1].radix;
            in.len--;
        }
    }
    el->ninputs = (uint8_t)in.len;
    el->inputs = in.len ? sk_arena_array(&f->arena, in.len, sizeof *el->inputs) : NULL;
    if (in.len)
        memcpy(el->inputs, in.ptr, in.len * sizeof *el->inputs);
    sk_vec_free(&in);
    el->ntable = total;
    el->table = sk_arena_array(&f->arena, total, sizeof *el->table);
    el->customs = sk_arena_array(&f->arena, total, sizeof *el->customs);
    char **prints = sk_xcalloc(total, sizeof *prints);
    int vals[SK_PACK_MAX_INPUTS + 1];
    sk_match m;
    memset(&m, 0, sizeof m);
    m.f = f;
    m.pin = pin;
    m.pin_count = pin_count;
    m.inputs = el->inputs;
    m.values = vals;
    m.n = el->ninputs;
    for (size_t idx = 0; idx < total; idx++) {
        size_t rest = idx;
        for (uint8_t i = 0; i < el->ninputs; i++) {
            vals[i] = (int)(rest % el->inputs[i].radix);
            rest /= el->inputs[i].radix;
        }
        const sk_css_style *ps = root_style();
        const sk_customs *pc = NULL;
        if (parent) {
            size_t pidx = table_index(parent, el->inputs, vals, el->ninputs, pin, pin_count);
            ps = f->styles.ptr[parent->table[pidx]];
            pc = &parent->customs[pidx];
        }
        computed c = compute(f, el, origin, &m, ps, pc);
        f->cascades++;
        el->table[idx] = keep_style(f, c.style);
        el->customs[idx] = c.customs;
        sk_buf fp = {0};
        fingerprint(&fp, c.style, &c.customs);
        prints[idx] = fp.ptr ? fp.ptr : sk_xstrdup("");
    }
    f->states += total;
    /* An input is inherited-relevant when changing it alone changes
     * what children inherit, in some state. */
    el->inherited = 0;
    size_t stride = 1;
    for (uint8_t i = 0; i < el->ninputs; i++) {
        uint8_t radix = el->inputs[i].radix;
        for (size_t idx = 0; idx < total && !(el->inherited & (1u << i)); idx++) {
            if ((idx / stride) % radix != 0)
                continue;
            for (uint8_t v = 1; v < radix; v++)
                if (strcmp(prints[idx], prints[idx + v * stride]) != 0) {
                    el->inherited |= (uint16_t)(1u << i);
                    break;
                }
        }
        stride *= radix;
    }
    for (size_t i = 0; i < total; i++)
        free(prints[i]);
    free(prints);
}

static void element(sk_front *f, sk_el *el)
{
    entry_vec cands = {0};
    input_vec found = {0};
    bool repeated = false;
    discover(f, el, SK_PSEUDO_NONE, &cands, &found, &repeated);
    if (repeated) {
        sk_buf b = {0};
        describe(&b, f, el);
        sk_diag(f->d, f->css_shown ? f->css_shown : "skin.css", "(selectors)",
                "a selector counts a list's rows or reaches into them by position (:nth-child, +, ~, :has()) "
                "for %s: a list's rows come and go",
                sk_buf_cstr(&b));
        sk_buf_free(&b);
    }
    el->ncands = cands.len;
    el->cands = cands.len ? sk_arena_array(&f->arena, cands.len, sizeof *el->cands) : NULL;
    if (cands.len)
        memcpy(el->cands, cands.ptr, cands.len * sizeof *el->cands);
    states(f, el, el, &found);
    sk_vec_free(&cands);
    sk_vec_free(&found);
    /* ::before and ::after where some rule may give one. */
    if (el->kind != SK_EL_DIV && el->kind != SK_EL_SPAN)
        return;
    static const uint8_t PSEUDO[2] = {SK_PSEUDO_BEFORE, SK_PSEUDO_AFTER};
    for (int k = 0; k < 2; k++) {
        entry_vec pc = {0};
        input_vec pf = {0};
        bool rep = false;
        discover(f, el, PSEUDO[k], &pc, &pf, &rep);
        if (pc.len) {
            sk_el *p = pseudo_new(f, el, k ? SK_EL_AFTER : SK_EL_BEFORE);
            p->ncands = pc.len;
            p->cands = sk_arena_array(&f->arena, pc.len, sizeof *p->cands);
            memcpy(p->cands, pc.ptr, pc.len * sizeof *p->cands);
            states(f, p, el, &pf);
        }
        sk_vec_free(&pc);
        sk_vec_free(&pf);
    }
}

/* ---- Counters ---- */

typedef struct {
    const char *name;
    int64_t base, step;
    uint32_t level;
    bool poisoned;
} counter;

typedef struct {
    sk_front *f;
    SK_VEC(counter) stack;
    bool *said;
} counting;

static counter *innermost(counting *c, const char *name)
{
    for (size_t i = c->stack.len; i-- > 0;)
        if (!strcmp(c->stack.ptr[i].name, name))
            return &c->stack.ptr[i];
    return NULL;
}

static bool always_none(const sk_front *f, const sk_el *e)
{
    for (size_t i = 0; i < e->ntable; i++)
        if (f->styles.ptr[e->table[i]]->display != SK_DISPLAY_NONE)
            return false;
    return e->ntable > 0;
}

/* The counters an element resets and increments, the same in every
 * state, or an error. */
static const sk_css_style *counter_style(counting *c, const sk_el *e)
{
    const sk_css_style *s0 = c->f->styles.ptr[e->table[0]];
    for (size_t i = 1; i < e->ntable; i++) {
        const sk_css_style *s = c->f->styles.ptr[e->table[i]];
        if (!sk_prop_equal(SK_PROP_COUNTER_RESET, s0, s) || !sk_prop_equal(SK_PROP_COUNTER_INCREMENT, s0, s)) {
            sk_buf b = {0};
            describe(&b, c->f, e);
            sk_diag(c->f->d, c->f->css_shown ? c->f->css_shown : "skin.css", "(counters)",
                    "%s resets or increments counters in some states only: skinc counts them once",
                    sk_buf_cstr(&b));
            sk_buf_free(&b);
            break;
        }
    }
    return s0;
}

/* counter() in a pseudo-element's content, in every state. */
static void resolve_content(counting *c, sk_el *p)
{
    for (size_t i = 0; i < p->ntable; i++) {
        const sk_css_style *s = c->f->styles.ptr[p->table[i]];
        bool any = false;
        for (uint16_t k = 0; k < s->ncontent; k++)
            any = any || s->content[k].kind == SK_CONTENT_COUNTER;
        if (!any)
            continue;
        sk_css_style *t = sk_arena_alloc(&c->f->arena, sizeof *t);
        *t = *s;
        sk_css_content *parts = sk_arena_array(&c->f->arena, s->ncontent, sizeof *parts);
        memcpy(parts, s->content, s->ncontent * sizeof *parts);
        for (uint16_t k = 0; k < s->ncontent; k++) {
            if (parts[k].kind != SK_CONTENT_COUNTER)
                continue;
            counter *ct = innermost(c, parts[k].text);
            int64_t base = ct ? ct->base : 0, step = ct ? ct->step : 0;
            if (ct && ct->poisoned) {
                sk_buf b = {0};
                describe(&b, c->f, p);
                sk_diag(c->f->d, c->f->css_shown ? c->f->css_shown : "skin.css", "(counters)",
                        "%s shows counter(%s) after a list's rows, which it would count: skinc cannot",
                        sk_buf_cstr(&b), parts[k].text);
                sk_buf_free(&b);
            }
            if (step) {
                parts[k].kind = SK_CONTENT_ORDINAL;
                parts[k].base = (int32_t)base;
                parts[k].step = (int32_t)step;
            } else {
                parts[k].kind = SK_CONTENT_TEXT;
                char num[32];
                snprintf(num, sizeof num, "%lld", (long long)base);
                parts[k].text = sk_arena_strndup(&c->f->arena, num, strlen(num));
            }
        }
        t->content = parts;
        p->table[i] = keep_style(c->f, t);
    }
}

static void count(counting *c, sk_el *e, uint32_t depth)
{
    if (e->ntable && always_none(c->f, e))
        return;
    if (e->ntable) {
        const sk_css_style *s = counter_style(c, e);
        if (e->group && (s->ncounter_reset || s->ncounter_increment)) {
            sk_diag(c->f->d, c->f->css_shown ? c->f->css_shown : "skin.css", "(counters)",
                    "counters inside a limits widget's meters are not supported");
        }
        for (uint16_t i = 0; i < s->ncounter_reset; i++) {
            counter k = {s->counter_reset[i].name, s->counter_reset[i].value, 0, depth ? depth - 1 : 0, false};
            sk_vec_push(&c->stack, k);
        }
        for (uint16_t i = 0; i < s->ncounter_increment; i++) {
            counter *k = innermost(c, s->counter_increment[i].name);
            if (!k) {
                counter z = {s->counter_increment[i].name, 0, 0, depth ? depth - 1 : 0, false};
                sk_vec_push(&c->stack, z);
                k = &c->stack.ptr[c->stack.len - 1];
            }
            int64_t inc = s->counter_increment[i].value;
            k->base += inc;
            if (e->repeated)
                k->step += inc;
        }
    }
    if (e->before) {
        count(c, e->before, depth + 1);
        resolve_content(c, e->before);
    }
    for (sk_el *ch = e->first; ch; ch = ch->next)
        count(c, ch, depth + 1);
    if (e->after) {
        count(c, e->after, depth + 1);
        resolve_content(c, e->after);
    }
    /* What e's children reset ends with e. */
    while (c->stack.len && c->stack.ptr[c->stack.len - 1].level >= depth && depth > 0)
        c->stack.len--;
    if (e->repeated)
        for (size_t i = 0; i < c->stack.len; i++)
            if (c->stack.ptr[i].step)
                c->stack.ptr[i].poisoned = true;
}

/* ---- The run ---- */

void sk_cascade_run(sk_front *f)
{
    sk_map *map = sk_xcalloc(1, sizeof *map);
    sk_map_init(map);
    f->dedupe = map;
    bool used[SK_PROP_COUNT];
    memset(used, 0, sizeof used);
    inherit_used = used;
    /* A property some declaration inherits explicitly passes to
     * children like an inherited one. */
    for (size_t i = 0; i < f->entries.len; i++) {
        const sk_entry *e = &f->entries.ptr[i];
        for (size_t j = 0; j < e->ndecls; j++) {
            const sk_xdecl *x = &e->decls[j];
            if (x->wide != SK_WIDE_INHERIT && !(x->var))
                continue;
            if (x->prop)
                used[x->prop] = true;
            else if (x->sh)
                for (size_t k = 0; k < x->sh->n; k++)
                    used[x->sh->longhands[k]] = true;
        }
    }
    for (size_t i = 0; i < f->els.len; i++) {
        sk_el *el = f->els.ptr[i];
        if (el->kind == SK_EL_BEFORE || el->kind == SK_EL_AFTER)
            continue;
        element(f, el);
    }
    counting c;
    memset(&c, 0, sizeof c);
    c.f = f;
    count(&c, f->els.ptr[0], 0);
    sk_vec_free(&c.stack);
    inherit_used = NULL;
}
