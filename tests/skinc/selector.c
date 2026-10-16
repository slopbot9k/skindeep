/* Selectors: what parses, how it prints, its specificity (a, b, c
 * packed as a << 16 | b << 8 | c), An+B, and what decision 0037
 * refuses. */
#include "../../skinc/css.h"
#include "../test.h"
#include "util.h"

#include <string.h>

/* The selector list of "sel {}" printed, or the first error. */
static const char *sel(const char *s, uint32_t *spec)
{
    static sk_buf out;
    sk_buf css = {0};
    sk_buf_printf(&css, "%s {}", s);
    sk_css_sheet sheet;
    sk_diags d = {0};
    sk_css_parse(&sheet, "t.css", sk_buf_cstr(&css), css.len, &d);
    sk_buf_free(&css);
    sk_buf_clear(&out);
    if (d.items.len) {
        sk_buf_append_c(&out, d.items.ptr[0]);
    } else if (sheet.rules.len) {
        const sk_selector_list *l = sheet.rules.ptr[0].sel;
        for (size_t i = 0; i < l->n; i++) {
            if (i)
                sk_buf_append_c(&out, ", ");
            sk_selector_print(&out, &l->items[i]);
        }
        if (spec)
            *spec = l->items[0].spec;
    }
    sk_css_sheet_free(&sheet);
    sk_diags_free(&d);
    return sk_buf_cstr(&out);
}

static uint32_t spec(const char *s)
{
    uint32_t x = 0xFFFFFFFF;
    sel(s, &x);
    return x;
}

#define SPEC(a, b, c) ((uint32_t)(a) << 16 | (uint32_t)(b) << 8 | (uint32_t)(c))

static void test_parse_and_print(void)
{
    CHECK_STR(sel(".a.b>.c +d~ *:hover::after", NULL), ".a.b > .c + d ~ *:hover::after");
    CHECK_STR(sel(".lava:has(.lamp.on)::before", NULL), ".lava:has(.lamp.on)::before");
    CHECK_STR(sel(".a .b:before, x:AFTER", NULL), ".a .b::before, x::after");
    CHECK_STR(sel(":host(.x) > .skin", NULL), ":host(.x) > .skin");
    CHECK_STR(sel(".w-limits > .w .label", NULL), ".w-limits > .w .label");
    CHECK_STR(sel(":not(.a, #b):is(.c):where(d)", NULL), ":not(.a, #b):is(.c):where(d)");
    CHECK_STR(sel(".s:has(> .a, + .b)", NULL), ".s:has(> .a, + .b)");
    CHECK_STR(sel("li:first-child:last-child:only-child", NULL), "li:first-child:last-child:only-child");
    /* Names print escaped, so they read back the same (found by fuzz-css). */
    CHECK_STR(sel("a\\.b.\\31 x#-\\32", NULL), "a\\.b.\\31 x#-\\32 ");
    CHECK_STR(sel(".\\-", NULL), ".\\-");
}

static void test_specificity(void)
{
    CHECK_INT(spec("*"), SPEC(0, 0, 0));
    CHECK_INT(spec("div"), SPEC(0, 0, 1));
    CHECK_INT(spec(".a"), SPEC(0, 1, 0));
    CHECK_INT(spec("#x"), SPEC(1, 0, 0));
    CHECK_INT(spec(".a:hover"), SPEC(0, 2, 0));
    CHECK_INT(spec("div.a::before"), SPEC(0, 1, 2));
    CHECK_INT(spec("text.value"), SPEC(0, 1, 1));
    CHECK_INT(spec(".lims.n-3 > :last-child"), SPEC(0, 3, 0));
    CHECK_INT(spec(".analyzer .seg:nth-child(n+9).on"), SPEC(0, 4, 0));
    /* :not, :is and :has take their most specific argument; :where none. */
    CHECK_INT(spec(":not(.a, #b)"), SPEC(1, 0, 0));
    CHECK_INT(spec(":is(.a, .b .c)"), SPEC(0, 2, 0));
    CHECK_INT(spec(":where(#a)"), SPEC(0, 0, 0));
    CHECK_INT(spec(".lava:has(.lamp.on)::before"), SPEC(0, 3, 1));
    /* :host is a pseudo-class, plus its argument. */
    CHECK_INT(spec(":host"), SPEC(0, 1, 0));
    CHECK_INT(spec(":host(.x)"), SPEC(0, 2, 0));
    /* Each part saturates at 255. */
    sk_buf many = {0};
    for (int i = 0; i < 300; i++)
        sk_buf_append_c(&many, ".a");
    CHECK_INT(spec(sk_buf_cstr(&many)), SPEC(0, 255, 0));
    sk_buf_free(&many);
}

static void test_anb(void)
{
    CHECK_STR(sel(":nth-child(n+9)", NULL), ":nth-child(1n+9)");
    CHECK_STR(sel(":nth-child(2n-1)", NULL), ":nth-child(2n-1)");
    CHECK_STR(sel(":nth-child(-n+3)", NULL), ":nth-child(-1n+3)");
    CHECK_STR(sel(":nth-child(odd)", NULL), ":nth-child(2n+1)");
    CHECK_STR(sel(":nth-child(EVEN)", NULL), ":nth-child(2n+0)");
    CHECK_STR(sel(":nth-child( 5 )", NULL), ":nth-child(0n+5)");
    CHECK_STR(sel(":nth-child(+n)", NULL), ":nth-child(1n+0)");
    CHECK_STR(sel(":nth-child( 3n + 1 )", NULL), ":nth-child(3n+1)");
    CHECK_STR(sel(":nth-child(3n - 1)", NULL), ":nth-child(3n-1)");
    CHECK_STR(sel(":nth-child(2n- 1)", NULL), ":nth-child(2n-1)");
    CHECK_STR(sel(":nth-last-child(-n-2)", NULL), ":nth-last-child(-1n-2)");
    CHECK(strstr(sel(":nth-child(n-)", NULL), "needs An+B"));
    CHECK(strstr(sel(":nth-child(2n++1)", NULL), "needs An+B"));
    CHECK(strstr(sel(":nth-child(1.5n)", NULL), "needs An+B"));
    CHECK(strstr(sel(":nth-child()", NULL), "needs An+B"));
    CHECK(strstr(sel(":nth-child(+ n)", NULL), "needs An+B"));
}

static void test_refused(void)
{
    CHECK_STR(sel(":focus", NULL), "t.css: line 1: the selector :focus is not supported");
    CHECK_STR(sel("a::selection", NULL), "t.css: line 1: the pseudo-element ::selection is not supported");
    CHECK_STR(sel("[data-id=x]", NULL), "t.css: line 1: attribute selectors are not supported");
    CHECK_STR(sel(".a[x]", NULL), "t.css: line 1: attribute selectors are not supported");
    CHECK_STR(sel("ns|div", NULL), "t.css: line 1: namespaces are not supported");
    CHECK_STR(sel(":root .a", NULL), "t.css: line 1: :root matches nothing in a skin: the stage is .skin");
    CHECK_STR(sel(":has(:has(a))", NULL), "t.css: line 1: :has() cannot be inside :has()");
    CHECK_STR(sel(":not(::before)", NULL), "t.css: line 1: ::before and ::after cannot be inside :not()");
    CHECK_STR(sel("a::before b", NULL), "t.css: line 1: nothing may follow ::before or ::after");
    CHECK_STR(sel("a::before:hover", NULL), "t.css: line 1: nothing may follow ::before or ::after");
    CHECK_STR(sel("> a", NULL), "t.css: line 1: a selector cannot start with a combinator");
    CHECK_STR(sel("a > > b", NULL), "t.css: line 1: a combinator needs a selector after it");
    CHECK_STR(sel("a >", NULL), "t.css: line 1: a combinator needs a selector after it");
    CHECK_STR(sel(".", NULL), "t.css: line 1: a . must be followed by a class name");
    CHECK_STR(sel("#1a", NULL), "t.css: line 1: #1a is not an id");
    CHECK_STR(sel("a, , b", NULL), "t.css: line 1: a selector is empty");
    CHECK_STR(sel(".a.b*", NULL), "t.css: line 1: a type selector must come first in its compound");
    CHECK_STR(sel(":host(.a .b)", NULL), "t.css: line 1: :host() takes one compound selector");
    CHECK_STR(sel(":matches(.a)", NULL), "t.css: line 1: the selector :matches() is not supported");
    CHECK_STR(sel("a:", NULL), "t.css: line 1: a : must be followed by a pseudo-class");
}

static void test_nesting_limit(void)
{
    sk_buf s = {0};
    for (int i = 0; i < 40; i++)
        sk_buf_append_c(&s, ":not(");
    sk_buf_append_c(&s, "a");
    for (int i = 0; i < 40; i++)
        sk_buf_putc(&s, ')');
    CHECK(strstr(sel(sk_buf_cstr(&s), NULL), "selectors nest more than 16 deep"));
    sk_buf_free(&s);
}

const sk_test sk_suite_skinc_selector[] = {
    T(test_parse_and_print), T(test_specificity), T(test_anb), T(test_refused), T(test_nesting_limit), {0},
};
