/* The tokenizer, against CSS Syntax Level 3's section 4: each case's
 * tokens as their type and text, as the spec's algorithms give them. */
#include "../../skinc/css.h"
#include "../test.h"

#include <string.h>

/* The tokens of s, one word each: type, then (text) and [unit]. */
static const char *tokens(const char *s, size_t n)
{
    static sk_buf out;
    static const char *const NAME[] = {"ident",  "fn",   "at",  "hash",  "string", "bad-string", "url",
                                       "bad-url", "delim", "num", "pct",   "dim",    "ws",         "cdo",
                                       "cdc",    ":",    ";",   ",",     "[",      "]",          "(",
                                       ")",      "{",    "}",   "eof"};
    sk_arena a;
    sk_arena_init(&a, 0);
    sk_lexer lx;
    sk_lexer_init(&lx, s, n, &a);
    sk_buf_clear(&out);
    for (int guard = 0; guard < 1000; guard++) {
        sk_tok t = sk_lex(&lx);
        if (t.type == SK_TOK_EOF)
            break;
        if (out.len)
            sk_buf_putc(&out, ' ');
        sk_buf_append_c(&out, NAME[t.type]);
        if (t.type == SK_TOK_DELIM)
            sk_buf_printf(&out, "(%c)", (char)t.delim);
        else if (t.type == SK_TOK_HASH)
            sk_buf_printf(&out, "(%.*s%s)", (int)t.text.len, t.text.ptr, t.id ? ",id" : "");
        else if (t.type == SK_TOK_NUMBER || t.type == SK_TOK_PERCENTAGE || t.type == SK_TOK_DIMENSION)
            sk_buf_printf(&out, "(%g%s)", t.num, t.integer ? ",int" : "");
        else if (t.text.len || t.type == SK_TOK_STRING || t.type == SK_TOK_URL)
            sk_buf_printf(&out, "(%.*s)", (int)t.text.len, t.text.ptr ? t.text.ptr : "");
        if (t.type == SK_TOK_DIMENSION)
            sk_buf_printf(&out, "[%.*s]", (int)t.unit.len, t.unit.ptr);
    }
    sk_arena_free(&a);
    return sk_buf_cstr(&out);
}

static const char *tok(const char *s)
{
    return tokens(s, strlen(s));
}

static void test_idents_functions_at(void)
{
    CHECK_STR(tok("a b-c _d --x -e"), "ident(a) ws ident(b-c) ws ident(_d) ws ident(--x) ws ident(-e)");
    CHECK_STR(tok("rgba(1)"), "fn(rgba) num(1,int) )");
    CHECK_STR(tok("@keyframes x"), "at(keyframes) ws ident(x)");
    CHECK_STR(tok("@1"), "delim(@) num(1,int)");
    CHECK_STR(tok("caf\xc3\xa9"), "ident(caf\xc3\xa9)");
}

static void test_escapes(void)
{
    /* A hex escape takes one space after it; the code point decodes. */
    CHECK_STR(tok("\\41 b"), "ident(Ab)");
    CHECK_STR(tok("a\\.b"), "ident(a.b)");
    /* Zero and surrogates are U+FFFD. */
    CHECK_STR(tok("\\0 x"), "ident(\xef\xbf\xbdx)");
    CHECK_STR(tok("\\d800"), "ident(\xef\xbf\xbd)");
    /* A backslash before a newline escapes nothing. */
    CHECK_STR(tok("\\\nx"), "delim(\\) ws ident(x)");
}

static void test_hashes(void)
{
    CHECK_STR(tok("#fff #a-b #1a #"), "hash(fff,id) ws hash(a-b,id) ws hash(1a) ws delim(#)");
}

static void test_strings(void)
{
    CHECK_STR(tok("\"a b\" 'c\"d'"), "string(a b) ws string(c\"d)");
    CHECK_STR(tok("\"a\\\"b\""), "string(a\"b)");
    /* An escaped newline continues the string. */
    CHECK_STR(tok("\"a\\\nb\""), "string(ab)");
    /* A newline ends it, bad, and is left for the next token. */
    CHECK_STR(tok("\"ab\ncd"), "bad-string(ab) ws ident(cd)");
    /* The end of the input closes it. */
    CHECK_STR(tok("'open"), "string(open)");
}

static void test_urls(void)
{
    CHECK_STR(tok("url(a.png)"), "url(a.png)");
    CHECK_STR(tok("url(  a.png  )"), "url(a.png)");
    CHECK_STR(tok("URL(x)"), "url(x)");
    /* Quoted, it is a function holding a string. */
    CHECK_STR(tok("url(\"a b.png\")"), "fn(url) string(a b.png) )");
    CHECK_STR(tok("url( 'x')"), "fn(url) ws string(x) )");
    /* A space inside, a quote or a bracket makes it bad. */
    CHECK_STR(tok("url(a b) c"), "bad-url ws ident(c)");
    CHECK_STR(tok("url(a\"b) c"), "bad-url ws ident(c)");
    CHECK_STR(tok("url(a\\)b)"), "url(a)b)");
}

static void test_numbers(void)
{
    CHECK_STR(tok("1 -2 +3 .5 1.5 1e3 1E-2 -0.5e+1"),
              "num(1,int) ws num(-2,int) ws num(3,int) ws num(0.5) ws num(1.5) ws num(1000) ws num(0.01) ws num(-5)");
    CHECK_STR(tok("50% 10px -4s 1.8s 2n+1 1e"), "pct(50,int) ws dim(10,int)[px] ws dim(-4,int)[s] ws dim(1.8)[s] ws "
                                               "dim(2,int)[n] num(1,int) ws dim(1,int)[e]");
    CHECK_STR(tok("+.5em"), "dim(0.5)[em]");
    CHECK_STR(tok("1\\70 x"), "dim(1,int)[px]");
    /* A sign that starts no number is a delimiter. */
    CHECK_STR(tok("+ - ."), "delim(+) ws delim(-) ws delim(.)");
}

static void test_punctuation(void)
{
    CHECK_STR(tok("a{b:c;d,e}[f](g)"), "ident(a) { ident(b) : ident(c) ; ident(d) , ident(e) } [ ident(f) ] ( ident(g) )");
    CHECK_STR(tok("<!-- x -->"), "cdo ws ident(x) ws cdc");
    CHECK_STR(tok("> + ~ * ! /"), "delim(>) ws delim(+) ws delim(~) ws delim(*) ws delim(!) ws delim(/)");
}

static void test_comments(void)
{
    CHECK_STR(tok("a/* x */b"), "ident(a) ident(b)");
    CHECK_STR(tok("/**/a/* * / */"), "ident(a)");
    sk_arena a;
    sk_arena_init(&a, 0);
    sk_lexer lx;
    const char *s = "a\n/* open";
    sk_lexer_init(&lx, s, strlen(s), &a);
    while (sk_lex(&lx).type != SK_TOK_EOF)
        ;
    CHECK_INT(lx.open_at, 2);
    sk_arena_free(&a);
}

static void test_lines(void)
{
    /* CR LF, CR and FF are each one newline. */
    const char *s = "a\r\nb\rc\fd\ne";
    sk_arena a;
    sk_arena_init(&a, 0);
    sk_lexer lx;
    sk_lexer_init(&lx, s, strlen(s), &a);
    uint32_t lines[5];
    int n = 0;
    for (;;) {
        sk_tok t = sk_lex(&lx);
        if (t.type == SK_TOK_EOF)
            break;
        if (t.type == SK_TOK_IDENT && n < 5)
            lines[n++] = t.line;
    }
    CHECK_INT(n, 5);
    for (int i = 0; i < n; i++)
        CHECK_INT(lines[i], i + 1);
    sk_arena_free(&a);
}

static void test_bytes(void)
{
    /* NUL and invalid UTF-8 read as U+FFFD, which starts an ident. */
    CHECK_STR(tokens("a\0b", 3), "ident(a\xef\xbf\xbd" "b)");
    CHECK_STR(tok("\xff"), "ident(\xef\xbf\xbd)");
    CHECK_STR(tok(""), "");
}

const sk_test sk_suite_skinc_lex[] = {
    T(test_idents_functions_at), T(test_escapes), T(test_hashes),  T(test_strings), T(test_urls),
    T(test_numbers),             T(test_punctuation), T(test_comments), T(test_lines), T(test_bytes),
    {0},
};
