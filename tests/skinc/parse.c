/* The parser: rules, declarations, !important, @keyframes, the
 * at-rules a skin may not use, the urls validate.js checks, and the
 * errors where a browser would drop something silently. */
#include "../../skinc/css.h"
#include "../test.h"
#include "util.h"

#include <string.h>

static void parse(sk_css_sheet *s, sk_diags *d, const char *css)
{
    memset(d, 0, sizeof *d);
    sk_css_parse(s, "t.css", css, strlen(css), d);
}

static void test_rules_and_declarations(void)
{
    sk_css_sheet s;
    sk_diags d;
    parse(&s, &d,
          "/* a skin */\n"
          ".a, .b > c { color: red; --Glow: 1px  2px ;\n"
          "  background : url(a.png) ! IMPORTANT }\n"
          "d{}\n");
    CHECK_INT(d.items.len, 0);
    CHECK_INT(s.rules.len, 2);
    sk_css_rule *r = &s.rules.ptr[0];
    CHECK_INT(r->line, 2);
    CHECK_INT(r->sel->n, 2);
    CHECK_STR(r->text.ptr, ".a, .b > c");
    CHECK_INT(r->ndecls, 3);
    CHECK_MEM(r->decls[0].name, "color");
    CHECK(!r->decls[0].important);
    CHECK_MEM(r->decls[1].name, "--Glow");
    CHECK_INT(r->decls[1].line, 2);
    /* Trimmed at both ends, the inside kept. */
    CHECK_INT(r->decls[1].n, 3);
    CHECK_MEM(r->decls[2].name, "background");
    CHECK(r->decls[2].important);
    CHECK_INT(r->decls[2].n, 1);
    CHECK_INT(r->decls[2].line, 3);
    CHECK_INT(s.rules.ptr[1].ndecls, 0);
    sk_css_sheet_free(&s);
    sk_diags_free(&d);
}

static void test_keyframes(void)
{
    sk_css_sheet s;
    sk_diags d;
    parse(&s, &d,
          "@keyframes spin { from { transform: rotate(0) }\n"
          "  50%, to { opacity: 1; } }\n"
          "@keyframes \"q\" { 0% { opacity: 0 } }\n");
    CHECK_INT(d.items.len, 0);
    CHECK_INT(s.keyframes.len, 2);
    sk_css_keyframes *k = &s.keyframes.ptr[0];
    CHECK_MEM(k->name, "spin");
    CHECK_INT(k->nframes, 2);
    CHECK_INT(k->frames[0].noffsets, 1);
    CHECK(k->frames[0].offsets[0] == 0);
    CHECK_INT(k->frames[1].noffsets, 2);
    CHECK(k->frames[1].offsets[0] == 0.5f && k->frames[1].offsets[1] == 1);
    CHECK_INT(k->frames[1].line, 2);
    CHECK_INT(k->frames[1].ndecls, 1);
    CHECK_MEM(s.keyframes.ptr[1].name, "q");
    sk_css_sheet_free(&s);
    sk_diags_free(&d);

    parse(&s, &d, "@keyframes none { from {} }\n@keyframes x { 120% {} }\n@keyframes y { a {} }\n");
    CHECK(skt_said(&d, "t.css: line 1: @keyframes needs a name"));
    CHECK(skt_said(&d, "t.css: line 2: a keyframe's selector must be from, to or percentages"));
    CHECK(skt_said(&d, "t.css: line 3: a keyframe's selector"));
    sk_css_sheet_free(&s);
    sk_diags_free(&d);
}

static void test_at_rules(void)
{
    sk_css_sheet s;
    sk_diags d;
    parse(&s, &d,
          "@import \"x.css\";\n"
          "@font-face { font-family: x; }\n"
          "@media (min-width: 1px) { a { color: red } }\n"
          "@IMPORT url(y.css);\n"
          "a { color: red }\n");
    /* @import is validate.js's to report: the parser keeps its lines. */
    CHECK_INT(s.imports.len, 2);
    CHECK_INT(s.imports.ptr[0], 1);
    CHECK_INT(s.imports.ptr[1], 4);
    CHECK_INT(d.items.len, 2);
    CHECK(skt_said(&d, "t.css: line 2: @font-face is not allowed: declare fonts in skin.json's fonts"));
    CHECK(skt_said(&d, "t.css: line 3: @media is not supported"));
    CHECK_INT(s.rules.len, 1);
    sk_css_sheet_free(&s);
    sk_diags_free(&d);
}

static void test_urls_kept(void)
{
    sk_css_sheet s;
    sk_diags d;
    parse(&s, &d,
          "a { background: url(images/a.png) }\n"
          "b { background: url( \"images/b b.png\" ), url('c.svg') }\n"
          "/* url(nope.png) */ c { content: \"url(not.png)\" }\n");
    CHECK_INT(d.items.len, 0);
    CHECK_INT(s.urls.len, 3);
    CHECK_MEM(s.urls.ptr[0].value, "images/a.png");
    CHECK_INT(s.urls.ptr[0].line, 1);
    CHECK_MEM(s.urls.ptr[1].value, "images/b b.png");
    CHECK_INT(s.urls.ptr[1].line, 2);
    CHECK_MEM(s.urls.ptr[2].value, "c.svg");
    sk_css_sheet_free(&s);
    sk_diags_free(&d);
}

static void test_errors(void)
{
    sk_css_sheet s;
    sk_diags d;
    parse(&s, &d,
          "a { color }\n"
          "b { .c { color: red } }\n"
          "d { @apply x; }\n"
          "e { 12px; color: red }\n"
          "f { content: \"open\n"
          "  }\n"
          "g { background: url(a b) }\n"
          "{ color: red }\n");
    CHECK(skt_said(&d, "t.css: line 1: \"color\" needs a : and a value"));
    CHECK(skt_said(&d, "t.css: line 2: a rule inside a style rule is not supported"));
    CHECK(skt_said(&d, "t.css: line 3: @apply inside a style rule is not supported"));
    CHECK(skt_said(&d, "t.css: line 4: a number where a declaration should start"));
    CHECK(skt_said(&d, "t.css: line 5: a string runs past the end of its line"));
    CHECK(skt_said(&d, "t.css: line 7: a url() is malformed"));
    CHECK(skt_said(&d, "t.css: line 8: a rule has no selector"));
    /* What parses still counts. */
    CHECK_INT(s.rules.len, 6);
    CHECK_INT(s.rules.ptr[3].ndecls, 1);
    sk_css_sheet_free(&s);
    sk_diags_free(&d);
}

static void test_unclosed(void)
{
    sk_css_sheet s;
    sk_diags d;
    parse(&s, &d, "a { color: red\n");
    CHECK(skt_said(&d, "t.css: line 1: a { block is not closed before the end of the file"));
    CHECK_INT(s.rules.len, 1);
    sk_css_sheet_free(&s);
    sk_diags_free(&d);

    parse(&s, &d, "a { color: red }\n.b\n");
    CHECK(skt_said(&d, "t.css: line 2: a rule has no { block }"));
    sk_css_sheet_free(&s);
    sk_diags_free(&d);

    parse(&s, &d, "a { color: red }\n/* open\n");
    CHECK(skt_said(&d, "t.css: line 2: a comment, string or url() is not closed before the end of the file"));
    sk_css_sheet_free(&s);
    sk_diags_free(&d);
}

static void test_deep(void)
{
    sk_buf css = {0};
    sk_buf_append_c(&css, "a { b: ");
    for (int i = 0; i < 200; i++)
        sk_buf_append_c(&css, "f(");
    sk_css_sheet s;
    sk_diags d;
    parse(&s, &d, sk_buf_cstr(&css));
    CHECK(skt_said(&d, "blocks nest more than 64 deep"));
    sk_css_sheet_free(&s);
    sk_diags_free(&d);
    sk_buf_free(&css);
}

const sk_test sk_suite_skinc_parse[] = {
    T(test_rules_and_declarations), T(test_keyframes), T(test_at_rules), T(test_urls_kept),
    T(test_errors),                 T(test_unclosed),  T(test_deep),     {0},
};
