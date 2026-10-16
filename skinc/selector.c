/* Selectors as decision 0037 takes them from Selectors Level 4: type,
 * universal, class and id selectors; :hover, :first-child,
 * :last-child, :only-child, :nth-child() and :nth-last-child() with
 * An+B; :not(), :is(), :where() and :has(); :host and :host(); the
 * pseudo-elements ::before and ::after; the four combinators. Anything
 * else is refused with the line it is on. */
#include "css.h"

#include <limits.h>
#include <string.h>

/* :not(:not(...)) nests no deeper than this. */
#define MAX_NEST 16

typedef struct {
    sk_arena *a;
    sk_diags *d;
    const char *file;
    uint32_t line;
    bool failed;
    unsigned nest;
} sp;

static void fail(sp *s, const char *fmt, ...) SK_PRINTF(2, 3);

#include <stdarg.h>

static void fail(sp *s, const char *fmt, ...)
{
    if (s->failed)
        return;
    s->failed = true;
    sk_buf b = {0};
    va_list ap;
    va_start(ap, fmt);
    sk_buf_vprintf(&b, fmt, ap);
    va_end(ap);
    sk_diag_line(s->d, s->file, s->line, "%s", sk_buf_cstr(&b));
    sk_buf_free(&b);
}

static bool ws(const sk_cv *c)
{
    return c->kind == SK_CV_TOKEN && c->tok.type == SK_TOK_WHITESPACE;
}

static bool delim(const sk_cv *c, uint32_t ch)
{
    return c->kind == SK_CV_TOKEN && c->tok.type == SK_TOK_DELIM && c->tok.delim == ch;
}

static bool tok(const sk_cv *c, uint8_t type)
{
    return c->kind == SK_CV_TOKEN && c->tok.type == type;
}

static uint32_t spec_add(uint32_t x, uint32_t y)
{
    uint32_t a = SK_MIN(255u, (x >> 16) + (y >> 16));
    uint32_t b = SK_MIN(255u, ((x >> 8) & 255) + ((y >> 8) & 255));
    uint32_t c = SK_MIN(255u, (x & 255) + (y & 255));
    return a << 16 | b << 8 | c;
}

static sk_selector_list *list(sp *s, const sk_cv *cv, size_t n, bool relative);

/* ---- An+B (CSS Syntax 3, section 6) ---- */

static bool int_tok(const sk_cv *c, bool *signed_)
{
    if (!tok(c, SK_TOK_NUMBER) || !c->tok.integer)
        return false;
    if (signed_)
        *signed_ = c->tok.text.len && (c->tok.text.ptr[0] == '+' || c->tok.text.ptr[0] == '-');
    return true;
}

static bool to_int(double v, int32_t *out)
{
    if (!(v >= -1e9 && v <= 1e9))
        return false;
    *out = (int32_t)v;
    return true;
}

/* "n", "n-", "n-12", in an ident or a unit, after its sign: A is
 * known; the rest gives B, a dash wanting a signless integer next, or
 * nothing. 0 bad, 1 done, 2 wants "- <signless>" style continuation
 * (just "n"), 3 wants a signless integer (it ended in "-"). */
static int n_rest(sk_str s, size_t i, int32_t *b)
{
    if (i >= s.len || (s.ptr[i] != 'n' && s.ptr[i] != 'N'))
        return 0;
    i++;
    if (i == s.len)
        return 2;
    if (s.ptr[i] != '-')
        return 0;
    i++;
    if (i == s.len)
        return 3;
    int64_t v = 0;
    for (; i < s.len; i++) {
        if (s.ptr[i] < '0' || s.ptr[i] > '9' || v > 1000000000)
            return 0;
        v = v * 10 + (s.ptr[i] - '0');
    }
    *b = (int32_t)-v;
    return 1;
}

static bool anb(sp *s, const sk_cv *cv, size_t n, int32_t *a, int32_t *b)
{
    size_t i = 0;
    while (i < n && ws(&cv[i]))
        i++;
    while (n > i && ws(&cv[n - 1]))
        n--;
    if (i >= n)
        return false;
    const sk_cv *c = &cv[i];
    *a = 0;
    *b = 0;
    int state = 0;
    if (tok(c, SK_TOK_IDENT) && sk_ident_is(c->tok.text, "odd") && i + 1 == n) {
        *a = 2;
        *b = 1;
        return true;
    }
    if (tok(c, SK_TOK_IDENT) && sk_ident_is(c->tok.text, "even") && i + 1 == n) {
        *a = 2;
        return true;
    }
    if (int_tok(c, NULL)) {
        return i + 1 == n && to_int(c->tok.num, b);
    }
    if (tok(c, SK_TOK_DIMENSION) && c->tok.integer) {
        if (!to_int(c->tok.num, a))
            return false;
        state = n_rest(c->tok.unit, 0, b);
        i++;
    } else {
        bool plus = false;
        if (delim(c, '+') && i + 1 < n && tok(&cv[i + 1], SK_TOK_IDENT)) {
            plus = true;
            i++;
            c = &cv[i];
        }
        if (!tok(c, SK_TOK_IDENT))
            return false;
        sk_str t = c->tok.text;
        if (!plus && t.len && t.ptr[0] == '-') {
            *a = -1;
            state = n_rest(t, 1, b);
        } else {
            *a = 1;
            state = n_rest(t, 0, b);
        }
        i++;
    }
    while (i < n && ws(&cv[i]))
        i++;
    if (state == 1)
        return i == n;
    if (state == 3) {
        bool sg;
        if (i + 1 != n || !int_tok(&cv[i], &sg) || sg || !to_int(cv[i].tok.num, b))
            return false;
        *b = -*b;
        return true;
    }
    if (state != 2)
        return false;
    if (i == n)
        return true;
    bool sg;
    if (int_tok(&cv[i], &sg) && sg)
        return i + 1 == n && to_int(cv[i].tok.num, b);
    int sign = delim(&cv[i], '+') ? 1 : delim(&cv[i], '-') ? -1 : 0;
    if (!sign)
        return false;
    i++;
    while (i < n && ws(&cv[i]))
        i++;
    if (i + 1 != n || !int_tok(&cv[i], &sg) || sg || !to_int(cv[i].tok.num, b))
        return false;
    *b *= sign;
    (void)s;
    return true;
}

/* ---- Compounds ---- */

typedef SK_VEC(sk_simple) simple_vec;

static uint32_t list_spec(const sk_selector_list *l)
{
    uint32_t best = 0;
    for (size_t i = 0; l && i < l->n; i++)
        best = SK_MAX(best, l->items[i].spec);
    return best;
}

static uint32_t simple_spec(const sk_simple *x)
{
    switch (x->kind) {
    case SK_SEL_TYPE:
        return 1;
    case SK_SEL_UNIVERSAL:
    case SK_SEL_WHERE:
        return 0;
    case SK_SEL_ID:
        return 1u << 16;
    case SK_SEL_NOT:
    case SK_SEL_IS:
    case SK_SEL_HAS:
        return list_spec(x->list);
    case SK_SEL_HOST:
        return spec_add(1u << 8, list_spec(x->list));
    default:
        return 1u << 8;
    }
}

/* A functional pseudo-class. */
static bool functional(sp *s, const sk_cv *f, sk_simple *out)
{
    sk_str name = f->tok.text;
    if (sk_ident_is(name, "nth-child") || sk_ident_is(name, "nth-last-child")) {
        out->kind = sk_ident_is(name, "nth-child") ? SK_SEL_NTH_CHILD : SK_SEL_NTH_LAST_CHILD;
        if (!anb(s, f->kids, f->n, &out->a, &out->b)) {
            fail(s, ":%.*s() needs An+B, such as 2n+1, odd or n+9", (int)name.len, name.ptr);
            return false;
        }
        return true;
    }
    bool rel = false;
    if (sk_ident_is(name, "not"))
        out->kind = SK_SEL_NOT;
    else if (sk_ident_is(name, "is"))
        out->kind = SK_SEL_IS;
    else if (sk_ident_is(name, "where"))
        out->kind = SK_SEL_WHERE;
    else if (sk_ident_is(name, "has")) {
        out->kind = SK_SEL_HAS;
        rel = true;
    } else if (sk_ident_is(name, "host"))
        out->kind = SK_SEL_HOST;
    else {
        fail(s, "the selector :%.*s() is not supported", (int)name.len, name.ptr);
        return false;
    }
    if (s->nest >= MAX_NEST) {
        fail(s, "selectors nest more than %d deep", MAX_NEST);
        return false;
    }
    s->nest++;
    out->list = list(s, f->kids, f->n, rel);
    s->nest--;
    if (!out->list)
        return false;
    for (size_t i = 0; i < out->list->n; i++) {
        if (out->list->items[i].pseudo) {
            fail(s, "::before and ::after cannot be inside :%.*s()", (int)name.len, name.ptr);
            return false;
        }
        if (out->kind == SK_SEL_HOST && out->list->items[i].n != 1) {
            fail(s, ":host() takes one compound selector");
            return false;
        }
        if (out->kind == SK_SEL_HAS) {
            for (size_t k = 0; k < out->list->items[i].n; k++) {
                sk_compound *c = &out->list->items[i].compounds[k];
                for (size_t j = 0; j < c->n; j++)
                    if (c->simples[j].kind == SK_SEL_HAS) {
                        fail(s, ":has() cannot be inside :has()");
                        return false;
                    }
            }
        }
    }
    return true;
}

/* One compound from cv[*i], stopping at whitespace, a combinator or the
 * end. Sets *pseudo when it ends on a pseudo-element. */
static bool compound(sp *s, const sk_cv *cv, size_t n, size_t *i, sk_compound *out, uint8_t *pseudo)
{
    simple_vec v = {0};
    *pseudo = SK_PSEUDO_NONE;
    bool ok = true;
    size_t start = *i;
    while (*i < n && ok) {
        const sk_cv *c = &cv[*i];
        if (ws(c) || delim(c, '>') || delim(c, '+') || delim(c, '~') || tok(c, SK_TOK_COMMA))
            break;
        if (*pseudo) {
            fail(s, "nothing may follow ::before or ::after");
            ok = false;
            break;
        }
        sk_simple x;
        memset(&x, 0, sizeof x);
        if (tok(c, SK_TOK_IDENT) || delim(c, '*')) {
            if (*i != start) {
                fail(s, "a type selector must come first in its compound");
                ok = false;
                break;
            }
            if (*i + 1 < n && delim(&cv[*i + 1], '|')) {
                fail(s, "namespaces are not supported");
                ok = false;
                break;
            }
            x.kind = tok(c, SK_TOK_IDENT) ? SK_SEL_TYPE : SK_SEL_UNIVERSAL;
            x.name = c->tok.text;
            (*i)++;
        } else if (delim(c, '.')) {
            if (*i + 1 >= n || !tok(&cv[*i + 1], SK_TOK_IDENT)) {
                fail(s, "a . must be followed by a class name");
                ok = false;
                break;
            }
            x.kind = SK_SEL_CLASS;
            x.name = cv[*i + 1].tok.text;
            *i += 2;
        } else if (tok(c, SK_TOK_HASH)) {
            if (!c->tok.id) {
                fail(s, "#%.*s is not an id", (int)c->tok.text.len, c->tok.text.ptr);
                ok = false;
                break;
            }
            x.kind = SK_SEL_ID;
            x.name = c->tok.text;
            (*i)++;
        } else if (tok(c, SK_TOK_COLON)) {
            const sk_cv *d = *i + 1 < n ? &cv[*i + 1] : NULL;
            bool element = false;
            if (d && tok(d, SK_TOK_COLON)) {
                element = true;
                d = *i + 2 < n ? &cv[*i + 2] : NULL;
            }
            if (!d || (!tok(d, SK_TOK_IDENT) && d->kind != SK_CV_FUNCTION)) {
                fail(s, "a : must be followed by a pseudo-class");
                ok = false;
                break;
            }
            sk_str name = d->tok.text;
            *i += element ? 3 : 2;
            if (d->kind == SK_CV_FUNCTION) {
                if (element || !functional(s, d, &x)) {
                    if (element)
                        fail(s, "the pseudo-element ::%.*s() is not supported", (int)name.len, name.ptr);
                    ok = false;
                    break;
                }
            } else if (sk_ident_is(name, "before") || sk_ident_is(name, "after")) {
                *pseudo = sk_ident_is(name, "before") ? SK_PSEUDO_BEFORE : SK_PSEUDO_AFTER;
                continue;
            } else if (element) {
                fail(s, "the pseudo-element ::%.*s is not supported", (int)name.len, name.ptr);
                ok = false;
                break;
            } else if (sk_ident_is(name, "hover")) {
                x.kind = SK_SEL_HOVER;
            } else if (sk_ident_is(name, "first-child")) {
                x.kind = SK_SEL_FIRST_CHILD;
            } else if (sk_ident_is(name, "last-child")) {
                x.kind = SK_SEL_LAST_CHILD;
            } else if (sk_ident_is(name, "only-child")) {
                x.kind = SK_SEL_ONLY_CHILD;
            } else if (sk_ident_is(name, "host")) {
                x.kind = SK_SEL_HOST;
            } else if (sk_ident_is(name, "root")) {
                fail(s, ":root matches nothing in a skin: the stage is .skin");
                ok = false;
                break;
            } else {
                fail(s, "the selector :%.*s is not supported", (int)name.len, name.ptr);
                ok = false;
                break;
            }
        } else if (c->kind == SK_CV_BLOCK && c->tok.type == SK_TOK_LBRACKET) {
            fail(s, "attribute selectors are not supported");
            ok = false;
            break;
        } else {
            fail(s, "a selector cannot hold this");
            ok = false;
            break;
        }
        sk_vec_push(&v, x);
    }
    if (ok && !v.len && !*pseudo) {
        fail(s, "a selector is empty");
        ok = false;
    }
    if (!ok) {
        sk_vec_free(&v);
        return false;
    }
    out->n = v.len;
    out->simples = v.len ? sk_arena_array(s->a, v.len, sizeof *out->simples) : NULL;
    if (v.len)
        memcpy(out->simples, v.ptr, v.len * sizeof *out->simples);
    sk_vec_free(&v);
    return true;
}

/* A complex selector from cv[0..n), trimmed. A relative one may start
 * with a combinator. */
static bool complex_sel(sp *s, const sk_cv *cv, size_t n, bool relative, sk_selector *out)
{
    SK_VEC(sk_compound) v = {0};
    size_t i = 0;
    uint8_t comb = relative ? SK_COMB_DESCENDANT : SK_COMB_NONE;
    bool ok = true;
    memset(out, 0, sizeof *out);
    while (i < n && ok) {
        /* A combinator, with whitespace around it. */
        bool space = false;
        while (i < n && ws(&cv[i])) {
            space = true;
            i++;
        }
        if (i >= n)
            break;
        uint8_t explicit_ = delim(&cv[i], '>')   ? SK_COMB_CHILD
                            : delim(&cv[i], '+') ? SK_COMB_NEXT
                            : delim(&cv[i], '~') ? SK_COMB_LATER
                                                 : SK_COMB_NONE;
        if (explicit_) {
            if (v.len == 0 && !relative) {
                fail(s, "a selector cannot start with a combinator");
                ok = false;
                break;
            }
            if (v.len == 0 && relative && comb != SK_COMB_DESCENDANT) {
                fail(s, "two combinators in a row");
                ok = false;
                break;
            }
            comb = explicit_;
            i++;
            while (i < n && ws(&cv[i]))
                i++;
            if (i >= n || delim(&cv[i], '>') || delim(&cv[i], '+') || delim(&cv[i], '~')) {
                fail(s, "a combinator needs a selector after it");
                ok = false;
                break;
            }
        } else if (v.len) {
            if (!space) {
                fail(s, "a selector cannot hold this");
                ok = false;
                break;
            }
            comb = SK_COMB_DESCENDANT;
        }
        if (out->pseudo) {
            fail(s, "nothing may follow ::before or ::after");
            ok = false;
            break;
        }
        sk_compound c;
        uint8_t pseudo;
        if (!compound(s, cv, n, &i, &c, &pseudo)) {
            ok = false;
            break;
        }
        c.comb = comb;
        out->pseudo = pseudo;
        sk_vec_push(&v, c);
        comb = SK_COMB_NONE;
    }
    if (ok && !v.len) {
        fail(s, "a selector is empty");
        ok = false;
    }
    if (!ok) {
        sk_vec_free(&v);
        return false;
    }
    out->n = v.len;
    out->compounds = sk_arena_array(s->a, v.len, sizeof *out->compounds);
    memcpy(out->compounds, v.ptr, v.len * sizeof *out->compounds);
    sk_vec_free(&v);
    uint32_t spec = out->pseudo ? 1 : 0;
    for (size_t k = 0; k < out->n; k++)
        for (size_t j = 0; j < out->compounds[k].n; j++)
            spec = spec_add(spec, simple_spec(&out->compounds[k].simples[j]));
    out->spec = spec;
    return true;
}

static sk_selector_list *list(sp *s, const sk_cv *cv, size_t n, bool relative)
{
    SK_VEC(sk_selector) v = {0};
    size_t start = 0;
    for (size_t i = 0; i <= n; i++) {
        if (i < n && !tok(&cv[i], SK_TOK_COMMA))
            continue;
        size_t a = start, b = i;
        while (a < b && ws(&cv[a]))
            a++;
        while (b > a && ws(&cv[b - 1]))
            b--;
        sk_selector sel;
        if (!complex_sel(s, cv + a, b - a, relative, &sel)) {
            sk_vec_free(&v);
            return NULL;
        }
        sk_vec_push(&v, sel);
        start = i + 1;
    }
    sk_selector_list *l = sk_arena_zalloc(s->a, sizeof *l);
    l->n = v.len;
    l->items = sk_arena_array(s->a, v.len, sizeof *l->items);
    memcpy(l->items, v.ptr, v.len * sizeof *l->items);
    sk_vec_free(&v);
    return l;
}

sk_selector_list *sk_selectors_parse(sk_arena *a, const sk_cv *cv, size_t n, uint32_t line, sk_diags *d,
                                     const char *file)
{
    sp s = {a, d, file, line, false, 0};
    if (!n) {
        fail(&s, "a rule has no selector");
        return NULL;
    }
    return list(&s, cv, n, false);
}

/* ---- Printing ---- */

static void print_list(sk_buf *out, const sk_selector_list *l)
{
    for (size_t i = 0; l && i < l->n; i++) {
        if (i)
            sk_buf_append_c(out, ", ");
        sk_selector_print(out, &l->items[i]);
    }
}

static const char *const COMB[] = {"", " ", " > ", " + ", " ~ "};

void sk_selector_print(sk_buf *out, const sk_selector *s)
{
    for (size_t k = 0; k < s->n; k++) {
        const sk_compound *c = &s->compounds[k];
        if (k || c->comb != SK_COMB_DESCENDANT)
            sk_buf_append_c(out, k ? COMB[c->comb] : c->comb ? sk_str_trim(sk_str_c(COMB[c->comb])).ptr : "");
        if (!k && c->comb > SK_COMB_DESCENDANT)
            sk_buf_putc(out, ' ');
        for (size_t j = 0; j < c->n; j++) {
            const sk_simple *x = &c->simples[j];
            switch (x->kind) {
            case SK_SEL_TYPE:
                sk_buf_append_str(out, x->name);
                break;
            case SK_SEL_UNIVERSAL:
                sk_buf_putc(out, '*');
                break;
            case SK_SEL_CLASS:
                sk_buf_putc(out, '.');
                sk_buf_append_str(out, x->name);
                break;
            case SK_SEL_ID:
                sk_buf_putc(out, '#');
                sk_buf_append_str(out, x->name);
                break;
            case SK_SEL_HOVER:
                sk_buf_append_c(out, ":hover");
                break;
            case SK_SEL_FIRST_CHILD:
                sk_buf_append_c(out, ":first-child");
                break;
            case SK_SEL_LAST_CHILD:
                sk_buf_append_c(out, ":last-child");
                break;
            case SK_SEL_ONLY_CHILD:
                sk_buf_append_c(out, ":only-child");
                break;
            case SK_SEL_NTH_CHILD:
            case SK_SEL_NTH_LAST_CHILD:
                sk_buf_printf(out, ":%s(%dn%+d)", x->kind == SK_SEL_NTH_CHILD ? "nth-child" : "nth-last-child",
                              (int)x->a, (int)x->b);
                break;
            case SK_SEL_NOT:
            case SK_SEL_IS:
            case SK_SEL_WHERE:
            case SK_SEL_HAS:
                sk_buf_append_c(out, x->kind == SK_SEL_NOT  ? ":not("
                                     : x->kind == SK_SEL_IS ? ":is("
                                     : x->kind == SK_SEL_WHERE ? ":where("
                                                               : ":has(");
                print_list(out, x->list);
                sk_buf_putc(out, ')');
                break;
            case SK_SEL_HOST:
                sk_buf_append_c(out, ":host");
                if (x->list) {
                    sk_buf_putc(out, '(');
                    print_list(out, x->list);
                    sk_buf_putc(out, ')');
                }
                break;
            }
        }
    }
    if (s->pseudo)
        sk_buf_append_c(out, s->pseudo == SK_PSEUDO_BEFORE ? "::before" : "::after");
}
