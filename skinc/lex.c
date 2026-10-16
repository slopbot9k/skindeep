/* The tokenizer of CSS Syntax Level 3, section 4, over UTF-8 bytes. The
 * input's preprocessing happens as it reads: CR LF, CR and FF are one
 * newline, NUL and invalid UTF-8 are U+FFFD. */
#include "css.h"

#include "../src/base/utf8.h"

#include <stdlib.h>
#include <string.h>

#define END 0xFFFFFFFFu

void sk_lexer_init(sk_lexer *lx, const char *s, size_t n, sk_arena *arena)
{
    memset(lx, 0, sizeof *lx);
    lx->s = s;
    lx->n = n;
    lx->line = 1;
    lx->arena = arena;
}

/* The code point at byte i and its length, preprocessed. */
static uint32_t cp_at(const sk_lexer *lx, size_t i, size_t *len)
{
    if (i >= lx->n) {
        *len = 0;
        return END;
    }
    unsigned char c = (unsigned char)lx->s[i];
    if (c < 0x80) {
        *len = 1;
        if (c == '\r') {
            if (i + 1 < lx->n && lx->s[i + 1] == '\n')
                *len = 2;
            return '\n';
        }
        if (c == '\f')
            return '\n';
        return c ? c : 0xFFFD;
    }
    uint32_t cp;
    *len = sk_utf8_decode(lx->s + i, lx->n - i, &cp, NULL);
    return cp;
}

static uint32_t peek(const sk_lexer *lx, int k)
{
    size_t at = lx->i, len;
    for (int j = 0; j < k; j++) {
        cp_at(lx, at, &len);
        if (!len)
            return END;
        at += len;
    }
    return cp_at(lx, at, &len);
}

static uint32_t next(sk_lexer *lx)
{
    size_t len;
    uint32_t c = cp_at(lx, lx->i, &len);
    lx->i += len;
    if (c == '\n')
        lx->line++;
    return c;
}

static bool is_digit(uint32_t c)
{
    return c >= '0' && c <= '9';
}

static bool is_hex(uint32_t c)
{
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int hex_value(uint32_t c)
{
    return is_digit(c) ? (int)(c - '0') : (int)((c | 0x20) - 'a' + 10);
}

static bool is_space(uint32_t c)
{
    return c == '\n' || c == '\t' || c == ' ';
}

static bool ident_start(uint32_t c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (c >= 0x80 && c != END);
}

static bool ident_char(uint32_t c)
{
    return ident_start(c) || is_digit(c) || c == '-';
}

static bool non_printable(uint32_t c)
{
    return c <= 8 || c == 0x0B || (c >= 0x0E && c <= 0x1F) || c == 0x7F;
}

static bool valid_escape(uint32_t a, uint32_t b)
{
    return a == '\\' && b != '\n';
}

static bool starts_ident(uint32_t a, uint32_t b, uint32_t c)
{
    if (a == '-')
        return ident_start(b) || b == '-' || valid_escape(b, c);
    if (ident_start(a))
        return true;
    return a == '\\' && valid_escape(a, b);
}

static bool starts_number(uint32_t a, uint32_t b, uint32_t c)
{
    if (a == '+' || a == '-')
        return is_digit(b) || (b == '.' && is_digit(c));
    if (a == '.')
        return is_digit(b);
    return is_digit(a);
}

static void opened(sk_lexer *lx, uint32_t line)
{
    if (!lx->open_at)
        lx->open_at = line;
}

static sk_str keep(sk_lexer *lx, const sk_buf *b)
{
    return sk_str_n(sk_arena_strndup(lx->arena, b->ptr ? b->ptr : "", b->len), b->len);
}

/* The code point an escape stands for; the backslash is consumed. */
static uint32_t escaped(sk_lexer *lx)
{
    uint32_t c = next(lx);
    if (c == END)
        return 0xFFFD;
    if (!is_hex(c))
        return c;
    uint32_t v = (uint32_t)hex_value(c);
    for (int k = 1; k < 6 && is_hex(peek(lx, 0)); k++)
        v = v * 16 + (uint32_t)hex_value(next(lx));
    if (is_space(peek(lx, 0)))
        next(lx);
    if (v == 0 || (v >= 0xD800 && v <= 0xDFFF) || v > 0x10FFFF)
        return 0xFFFD;
    return v;
}

static void ident_sequence(sk_lexer *lx, sk_buf *b)
{
    for (;;) {
        uint32_t c = peek(lx, 0);
        if (ident_char(c)) {
            sk_utf8_append(b, next(lx));
        } else if (valid_escape(c, peek(lx, 1))) {
            next(lx);
            sk_utf8_append(b, escaped(lx));
        } else {
            return;
        }
    }
}

static void comments(sk_lexer *lx)
{
    while (peek(lx, 0) == '/' && peek(lx, 1) == '*') {
        uint32_t from = lx->line;
        next(lx);
        next(lx);
        for (;;) {
            uint32_t c = next(lx);
            if (c == END) {
                opened(lx, from);
                return;
            }
            if (c == '*' && peek(lx, 0) == '/') {
                next(lx);
                break;
            }
        }
    }
}

static sk_tok string_token(sk_lexer *lx, uint32_t quote, sk_tok t)
{
    sk_buf b = {0};
    t.type = SK_TOK_STRING;
    for (;;) {
        uint32_t c = peek(lx, 0);
        if (c == END) {
            opened(lx, t.line);
            break;
        }
        if (c == quote) {
            next(lx);
            break;
        }
        if (c == '\n') {
            /* Left for the next token, as the spec says. */
            t.type = SK_TOK_BAD_STRING;
            break;
        }
        next(lx);
        if (c == '\\') {
            uint32_t d = peek(lx, 0);
            if (d == END)
                continue;
            if (d == '\n') {
                next(lx);
                continue;
            }
            sk_utf8_append(&b, escaped(lx));
            continue;
        }
        sk_utf8_append(&b, c);
    }
    t.text = keep(lx, &b);
    sk_buf_free(&b);
    return t;
}

/* After a bad url: everything up to its ) or the end. */
static void bad_url_rest(sk_lexer *lx)
{
    for (;;) {
        uint32_t c = next(lx);
        if (c == ')' || c == END)
            return;
        if (valid_escape(c, peek(lx, 0)))
            escaped(lx);
    }
}

static sk_tok url_token(sk_lexer *lx, sk_tok t)
{
    sk_buf b = {0};
    t.type = SK_TOK_URL;
    while (is_space(peek(lx, 0)))
        next(lx);
    for (;;) {
        uint32_t c = next(lx);
        if (c == ')')
            break;
        if (c == END) {
            opened(lx, t.line);
            break;
        }
        if (is_space(c)) {
            while (is_space(peek(lx, 0)))
                next(lx);
            uint32_t d = peek(lx, 0);
            if (d == ')' || d == END) {
                if (next(lx) == END)
                    opened(lx, t.line);
                break;
            }
            bad_url_rest(lx);
            t.type = SK_TOK_BAD_URL;
            break;
        }
        if (c == '"' || c == '\'' || c == '(' || non_printable(c)) {
            bad_url_rest(lx);
            t.type = SK_TOK_BAD_URL;
            break;
        }
        if (c == '\\') {
            if (valid_escape(c, peek(lx, 0))) {
                sk_utf8_append(&b, escaped(lx));
                continue;
            }
            bad_url_rest(lx);
            t.type = SK_TOK_BAD_URL;
            break;
        }
        sk_utf8_append(&b, c);
    }
    t.text = keep(lx, &b);
    sk_buf_free(&b);
    return t;
}

static sk_tok ident_like(sk_lexer *lx, sk_tok t)
{
    sk_buf b = {0};
    ident_sequence(lx, &b);
    t.text = keep(lx, &b);
    sk_buf_free(&b);
    if (sk_ident_is(t.text, "url") && peek(lx, 0) == '(') {
        next(lx);
        while (is_space(peek(lx, 0)) && is_space(peek(lx, 1)))
            next(lx);
        uint32_t c = peek(lx, 0), d = peek(lx, 1);
        if (c == '"' || c == '\'' || (is_space(c) && (d == '"' || d == '\''))) {
            t.type = SK_TOK_FUNCTION;
            return t;
        }
        return url_token(lx, t);
    }
    if (peek(lx, 0) == '(') {
        next(lx);
        t.type = SK_TOK_FUNCTION;
        return t;
    }
    t.type = SK_TOK_IDENT;
    return t;
}

static sk_tok numeric(sk_lexer *lx, sk_tok t)
{
    sk_buf repr = {0};
    t.integer = true;
    uint32_t c = peek(lx, 0);
    if (c == '+' || c == '-')
        sk_buf_putc(&repr, (char)next(lx));
    while (is_digit(peek(lx, 0)))
        sk_buf_putc(&repr, (char)next(lx));
    if (peek(lx, 0) == '.' && is_digit(peek(lx, 1))) {
        sk_buf_putc(&repr, (char)next(lx));
        while (is_digit(peek(lx, 0)))
            sk_buf_putc(&repr, (char)next(lx));
        t.integer = false;
    }
    c = peek(lx, 0);
    uint32_t d = peek(lx, 1);
    if ((c == 'e' || c == 'E') && (is_digit(d) || ((d == '+' || d == '-') && is_digit(peek(lx, 2))))) {
        sk_buf_putc(&repr, (char)next(lx));
        if (!is_digit(peek(lx, 0)))
            sk_buf_putc(&repr, (char)next(lx));
        while (is_digit(peek(lx, 0)))
            sk_buf_putc(&repr, (char)next(lx));
        t.integer = false;
    }
    t.num = strtod(sk_buf_cstr(&repr), NULL);
    t.text = keep(lx, &repr);
    sk_buf_free(&repr);
    if (starts_ident(peek(lx, 0), peek(lx, 1), peek(lx, 2))) {
        sk_buf unit = {0};
        ident_sequence(lx, &unit);
        t.unit = keep(lx, &unit);
        sk_buf_free(&unit);
        t.type = SK_TOK_DIMENSION;
    } else if (peek(lx, 0) == '%') {
        next(lx);
        t.type = SK_TOK_PERCENTAGE;
    } else {
        t.type = SK_TOK_NUMBER;
    }
    return t;
}

static sk_tok simple(sk_tok t, uint8_t type)
{
    t.type = type;
    return t;
}

sk_tok sk_lex(sk_lexer *lx)
{
    comments(lx);
    sk_tok t;
    memset(&t, 0, sizeof t);
    t.line = lx->line;
    uint32_t c = peek(lx, 0);
    if (c == END)
        return simple(t, SK_TOK_EOF);
    if (is_space(c)) {
        while (is_space(peek(lx, 0)))
            next(lx);
        return simple(t, SK_TOK_WHITESPACE);
    }
    if (c == '"' || c == '\'') {
        next(lx);
        return string_token(lx, c, t);
    }
    if (c == '#') {
        uint32_t a = peek(lx, 1), b = peek(lx, 2);
        if (ident_char(a) || valid_escape(a, b)) {
            next(lx);
            t.type = SK_TOK_HASH;
            t.id = starts_ident(peek(lx, 0), peek(lx, 1), peek(lx, 2));
            sk_buf name = {0};
            ident_sequence(lx, &name);
            t.text = keep(lx, &name);
            sk_buf_free(&name);
            return t;
        }
    }
    if (c == '+' || c == '.') {
        if (starts_number(c, peek(lx, 1), peek(lx, 2)))
            return numeric(lx, t);
    }
    if (c == '-') {
        if (starts_number(c, peek(lx, 1), peek(lx, 2)))
            return numeric(lx, t);
        if (peek(lx, 1) == '-' && peek(lx, 2) == '>') {
            next(lx);
            next(lx);
            next(lx);
            return simple(t, SK_TOK_CDC);
        }
        if (starts_ident(c, peek(lx, 1), peek(lx, 2)))
            return ident_like(lx, t);
    }
    if (c == '<' && peek(lx, 1) == '!' && peek(lx, 2) == '-' && peek(lx, 3) == '-') {
        for (int k = 0; k < 4; k++)
            next(lx);
        return simple(t, SK_TOK_CDO);
    }
    if (c == '@' && starts_ident(peek(lx, 1), peek(lx, 2), peek(lx, 3))) {
        next(lx);
        sk_buf name = {0};
        ident_sequence(lx, &name);
        t.text = keep(lx, &name);
        sk_buf_free(&name);
        return simple(t, SK_TOK_AT_KEYWORD);
    }
    if (c == '\\' && valid_escape(c, peek(lx, 1)))
        return ident_like(lx, t);
    if (is_digit(c))
        return numeric(lx, t);
    if (ident_start(c))
        return ident_like(lx, t);
    next(lx);
    switch (c) {
    case '(':
        return simple(t, SK_TOK_LPAREN);
    case ')':
        return simple(t, SK_TOK_RPAREN);
    case '[':
        return simple(t, SK_TOK_LBRACKET);
    case ']':
        return simple(t, SK_TOK_RBRACKET);
    case '{':
        return simple(t, SK_TOK_LBRACE);
    case '}':
        return simple(t, SK_TOK_RBRACE);
    case ',':
        return simple(t, SK_TOK_COMMA);
    case ':':
        return simple(t, SK_TOK_COLON);
    case ';':
        return simple(t, SK_TOK_SEMICOLON);
    default:
        t.type = SK_TOK_DELIM;
        t.delim = c;
        return t;
    }
}

bool sk_ident_is(sk_str s, const char *lower)
{
    size_t n = strlen(lower);
    if (s.len != n)
        return false;
    for (size_t i = 0; i < n; i++) {
        char c = s.ptr[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c + 32);
        if (c != lower[i])
            return false;
    }
    return true;
}
