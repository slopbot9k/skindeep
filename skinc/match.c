/* Selector matching against the element tree in a state. A state is
 * the values of the variables of decision 0037 (.on, .nodata, :hover
 * and the rest): a class or :hover a variable gives is read from it.
 *
 * Discovery runs with every variable unknown and three-valued logic:
 * a selector that is surely false is no candidate, and the variables
 * it read on the way are the ones its result can depend on. AND stops
 * at a sure NO and OR at a sure YES, the same in both modes, so a
 * state's matching never reads a variable discovery did not record. */
#include "front.h"

#include <string.h>

static sk_tri t_and(sk_tri a, sk_tri b)
{
    if (a == SK_NO || b == SK_NO)
        return SK_NO;
    return a == SK_MAYBE || b == SK_MAYBE ? SK_MAYBE : SK_YES;
}

static sk_tri t_or(sk_tri a, sk_tri b)
{
    if (a == SK_YES || b == SK_YES)
        return SK_YES;
    return a == SK_MAYBE || b == SK_MAYBE ? SK_MAYBE : SK_NO;
}

static sk_tri t_not(sk_tri a)
{
    return a == SK_MAYBE ? SK_MAYBE : a == SK_YES ? SK_NO : SK_YES;
}

const char *sk_var_name(uint8_t var)
{
    static const char *const N[] = {"hover", "nodata", "open", "fallback", "on", "list", "item", "count", "model", "span"};
    return var < SK_LEN(N) ? N[var] : "?";
}

uint8_t sk_var_radix(const sk_front *f, const sk_el *owner, uint8_t var)
{
    (void)owner;
    switch (var) {
    case SK_VAR_LIST:
    case SK_VAR_ITEM:
        return 3;
    case SK_VAR_COUNT:
        return SK_PACK_MAX_LIMITS + 1;
    case SK_VAR_SPAN:
        return (uint8_t)(f->spans.len + 2);
    default:
        return 2;
    }
}

/* The values of var that give the class name, a bit per value. */
static uint32_t class_values(const sk_front *f, uint8_t var, sk_str name)
{
    switch (var) {
    case SK_VAR_NODATA:
        return sk_str_eq_c(name, "nodata") ? 2u : 0;
    case SK_VAR_OPEN:
        return sk_str_eq_c(name, "open") ? 2u : 0;
    case SK_VAR_FALLBACK:
        return sk_str_eq_c(name, "fallback") ? 2u : 0;
    case SK_VAR_ON:
        return sk_str_eq_c(name, "on") ? 2u : 0;
    case SK_VAR_LIST:
        return sk_str_eq_c(name, "checklist") ? 2u : sk_str_eq_c(name, "checklist-off") ? 4u : 0;
    case SK_VAR_ITEM:
        return sk_str_eq_c(name, "todo") ? 1u : sk_str_eq_c(name, "doing") ? 2u : sk_str_eq_c(name, "done") ? 4u : 0;
    case SK_VAR_COUNT:
        if (sk_str_eq_c(name, "nodata"))
            return 1u;
        if (name.len >= 3 && name.ptr[0] == 'n' && name.ptr[1] == '-') {
            uint64_t k;
            if (sk_str_to_u64(sk_str_sub(name, 2, SIZE_MAX), &k) && k <= SK_PACK_MAX_LIMITS &&
                !(name.len > 3 && name.ptr[2] == '0'))
                return 1u << k;
        }
        return 0;
    case SK_VAR_MODEL:
        return sk_str_eq_c(name, "account") ? 1u : sk_str_eq_c(name, "model") ? 2u : 0;
    case SK_VAR_SPAN:
        if (sk_str_eq_c(name, "spend"))
            return 1u;
        for (size_t i = 0; i < f->spans.len && i < 29; i++)
            if (sk_str_eq_c(name, f->spans.ptr[i]))
                return 1u << (i + 1);
        return 0;
    default:
        return 0;
    }
}

/* A variable's value in the state, or -1 when it is not known. */
static int lookup(sk_match *m, const sk_el *owner, uint8_t var)
{
    if (m->pin && owner == m->pin && var == SK_VAR_COUNT)
        return m->pin_count;
    for (size_t i = 0; i < m->n; i++)
        if (m->inputs[i].owner == owner && m->inputs[i].var == var) {
            if (m->values[i] >= 0)
                return m->values[i];
            break;
        }
    if (!m->discover)
        return 0;
    for (size_t i = 0; i < m->found.len; i++)
        if (m->found.ptr[i].owner == owner && m->found.ptr[i].var == var)
            return -1;
    sk_input in = {(sk_el *)owner, var, sk_var_radix(m->f, owner, var)};
    sk_vec_push(&m->found, in);
    return -1;
}

static sk_tri has_class(sk_match *m, const sk_el *el, sk_str name)
{
    for (uint16_t i = 0; i < el->nclasses; i++)
        if (sk_str_eq_c(name, el->classes[i]))
            return SK_YES;
    sk_tri r = SK_NO;
    for (uint8_t v = 0; v < SK_VAR_COUNT_OF_VARS; v++) {
        if (!(el->vars & (1u << v)))
            continue;
        uint32_t mask = class_values(m->f, v, name);
        if (!mask)
            continue;
        int val = lookup(m, el, v);
        if (val < 0)
            r = t_or(r, SK_MAYBE);
        else if (mask >> val & 1u)
            return SK_YES;
    }
    return r;
}

static bool anb(int32_t a, int32_t b, int64_t pos)
{
    if (a == 0)
        return pos == b;
    int64_t d = pos - b;
    return d % a == 0 && d / a >= 0;
}

/* The element before el among its siblings: a limits widget's meters
 * see only the meters of their count. */
static const sk_el *sib_prev(sk_match *m, const sk_el *el)
{
    (void)m;
    if (el->group)
        return el->meter > 1 ? el->prev : NULL;
    return el->prev;
}

static sk_tri complex_at(sk_match *m, const sk_selector *s, int i, const sk_el *el, const sk_el *anchor);
static sk_tri compound(sk_match *m, const sk_compound *c, const sk_el *el, bool featureful);

static sk_tri list_match(sk_match *m, const sk_selector_list *l, const sk_el *el)
{
    sk_tri r = SK_NO;
    for (size_t i = 0; l && i < l->n && r != SK_YES; i++)
        r = t_or(r, complex_at(m, &l->items[i], (int)l->items[i].n - 1, el, NULL));
    return r;
}

/* The element's meter, when it is a limits widget's meter or inside
 * one, below anchor: it is there only while the widget shows that
 * many meters. */
static sk_tri present(sk_match *m, const sk_el *y, const sk_el *anchor)
{
    for (const sk_el *p = y; p && p != anchor; p = p->parent) {
        if (p->repeated) {
            m->repeated = true;
            return SK_MAYBE;
        }
        if (p->group) {
            int v = lookup(m, p->limits, SK_VAR_COUNT);
            return v < 0 ? SK_MAYBE : v == p->group ? SK_YES : SK_NO;
        }
    }
    return SK_YES;
}

/* :has(): some element after the anchor, in its subtree or after it
 * among its siblings, matches a relative selector from the anchor. */
static sk_tri has_in(sk_match *m, const sk_selector *s, const sk_el *scope, const sk_el *anchor)
{
    sk_tri r = SK_NO;
    for (const sk_el *c = scope->first; c && r != SK_YES; c = c->next) {
        sk_tri p = present(m, c, anchor);
        if (p != SK_NO)
            r = t_or(r, t_and(p, complex_at(m, s, (int)s->n - 1, c, anchor)));
        if (r != SK_YES)
            r = t_or(r, has_in(m, s, c, anchor));
    }
    return r;
}

static sk_tri has_match(sk_match *m, const sk_selector_list *l, const sk_el *el)
{
    sk_tri r = SK_NO;
    for (size_t i = 0; i < l->n && r != SK_YES; i++) {
        const sk_selector *s = &l->items[i];
        uint8_t first = s->compounds[0].comb;
        if (first == SK_COMB_DESCENDANT || first == SK_COMB_CHILD) {
            r = t_or(r, has_in(m, s, el, el));
        } else {
            /* + and ~: the siblings after el, and what they hold. */
            for (const sk_el *sib = el->next; sib && r != SK_YES; sib = sib->next) {
                if (el->group && sib->group != el->group)
                    continue;
                if (el->repeated || sib->repeated) {
                    m->repeated = true;
                    r = t_or(r, SK_MAYBE);
                    break;
                }
                r = t_or(r, complex_at(m, s, (int)s->n - 1, sib, el));
                if (r != SK_YES && s->n > 1)
                    r = t_or(r, has_in(m, s, sib, el));
            }
        }
    }
    return r;
}

static sk_tri structural(sk_match *m, const sk_simple *x, const sk_el *el)
{
    if (el->repeated) {
        m->repeated = true;
        return SK_MAYBE;
    }
    if (!el->parent)
        return SK_NO;
    int64_t pos = el->nth, n = el->siblings;
    switch (x->kind) {
    case SK_SEL_FIRST_CHILD:
        return pos == 1 ? SK_YES : SK_NO;
    case SK_SEL_LAST_CHILD:
        return pos == n ? SK_YES : SK_NO;
    case SK_SEL_ONLY_CHILD:
        return n == 1 ? SK_YES : SK_NO;
    case SK_SEL_NTH_CHILD:
        return anb(x->a, x->b, pos) ? SK_YES : SK_NO;
    default:
        return anb(x->a, x->b, n - pos + 1) ? SK_YES : SK_NO;
    }
}

static sk_tri compound(sk_match *m, const sk_compound *c, const sk_el *el, bool featureful)
{
    bool host = el->kind == SK_EL_HOST;
    sk_tri r = SK_YES;
    for (size_t j = 0; j < c->n && r != SK_NO; j++) {
        const sk_simple *x = &c->simples[j];
        sk_tri t = SK_NO;
        if (host && !featureful) {
            /* The shadow host is featureless: :host alone matches it. */
            if (x->kind != SK_SEL_HOST)
                return SK_NO;
            t = x->list ? compound(m, &x->list->items[0].compounds[0], el, true) : SK_YES;
        } else {
            switch (x->kind) {
            case SK_SEL_TYPE:
                t = el->tag && sk_ident_is(x->name, el->tag) ? SK_YES : SK_NO;
                break;
            case SK_SEL_UNIVERSAL:
                t = SK_YES;
                break;
            case SK_SEL_CLASS:
                t = has_class(m, el, x->name);
                break;
            case SK_SEL_ID:
                t = SK_NO;
                break;
            case SK_SEL_HOVER: {
                int v = lookup(m, el, SK_VAR_HOVER);
                t = v < 0 ? SK_MAYBE : v ? SK_YES : SK_NO;
                break;
            }
            case SK_SEL_FIRST_CHILD:
            case SK_SEL_LAST_CHILD:
            case SK_SEL_ONLY_CHILD:
            case SK_SEL_NTH_CHILD:
            case SK_SEL_NTH_LAST_CHILD:
                t = host ? SK_NO : structural(m, x, el);
                break;
            case SK_SEL_NOT:
                t = t_not(list_match(m, x->list, el));
                break;
            case SK_SEL_IS:
            case SK_SEL_WHERE:
                t = list_match(m, x->list, el);
                break;
            case SK_SEL_HAS:
                t = has_match(m, x->list, el);
                break;
            case SK_SEL_HOST:
                t = SK_NO;
                break;
            }
        }
        r = t_and(r, t);
    }
    return r;
}

/* Compound i of s at el, and the compounds left of it through their
 * combinators; anchor stands left of compound 0 in a relative
 * selector. */
static sk_tri complex_at(sk_match *m, const sk_selector *s, int i, const sk_el *el, const sk_el *anchor)
{
    if (!el)
        return SK_NO;
    sk_tri r = compound(m, &s->compounds[i], el, false);
    if (r == SK_NO)
        return SK_NO;
    uint8_t comb = s->compounds[i].comb;
    if (i == 0 && !anchor)
        return r;
    /* Left of compound 0 is the anchor itself; left of the others, an
     * element that is not the anchor. */
    sk_tri left = SK_NO;
    switch (comb) {
    case SK_COMB_CHILD:
    case SK_COMB_NEXT: {
        if (comb == SK_COMB_NEXT && el->repeated) {
            m->repeated = true;
            left = SK_MAYBE;
            break;
        }
        const sk_el *p = comb == SK_COMB_CHILD ? el->parent : sib_prev(m, el);
        if (!p)
            break;
        if (i == 0)
            left = p == anchor ? SK_YES : SK_NO;
        else if (p != anchor)
            left = complex_at(m, s, i - 1, p, anchor);
        break;
    }
    case SK_COMB_DESCENDANT:
    case SK_COMB_LATER:
        if (comb == SK_COMB_LATER && el->repeated) {
            m->repeated = true;
            left = SK_MAYBE;
            break;
        }
        for (const sk_el *p = comb == SK_COMB_DESCENDANT ? el->parent : sib_prev(m, el); p && left != SK_YES;
             p = comb == SK_COMB_DESCENDANT ? p->parent : sib_prev(m, p)) {
            if (i == 0) {
                if (p == anchor)
                    left = SK_YES;
                continue;
            }
            if (p == anchor)
                break;
            left = t_or(left, complex_at(m, s, i - 1, p, anchor));
        }
        break;
    default:
        break;
    }
    return t_and(r, left);
}

sk_tri sk_match_selector(sk_match *m, const sk_selector *s, const sk_el *el)
{
    return complex_at(m, s, (int)s->n - 1, el, NULL);
}
