/* CSS as skinc reads it (decision 0037): tokens and the parser of CSS
 * Syntax Level 3, the selectors of Selectors Level 4 that skins use,
 * and their specificity. Everything a sheet holds lives in the sheet's
 * arena. Errors carry the line, as validate.js gives them. */
#ifndef SKINC_CSS_H
#define SKINC_CSS_H

#include "../src/base/arena.h"
#include "../src/base/str.h"
#include "diag.h"

/* ---- Tokens ---- */

typedef enum {
    SK_TOK_IDENT,
    SK_TOK_FUNCTION,
    SK_TOK_AT_KEYWORD,
    SK_TOK_HASH,
    SK_TOK_STRING,
    SK_TOK_BAD_STRING,
    SK_TOK_URL,
    SK_TOK_BAD_URL,
    SK_TOK_DELIM,
    SK_TOK_NUMBER,
    SK_TOK_PERCENTAGE,
    SK_TOK_DIMENSION,
    SK_TOK_WHITESPACE,
    SK_TOK_CDO,
    SK_TOK_CDC,
    SK_TOK_COLON,
    SK_TOK_SEMICOLON,
    SK_TOK_COMMA,
    SK_TOK_LBRACKET,
    SK_TOK_RBRACKET,
    SK_TOK_LPAREN,
    SK_TOK_RPAREN,
    SK_TOK_LBRACE,
    SK_TOK_RBRACE,
    SK_TOK_EOF,
} sk_tok_type;

/* text: an ident's, function's, at-keyword's or hash's name, a string's
 * or url's value, with escapes decoded; a number's source. unit: a
 * dimension's unit. delim: the code point. */
typedef struct {
    uint8_t type;
    bool integer; /* number type integer */
    bool id;      /* hash type id */
    uint32_t line;
    uint32_t delim;
    double num;
    sk_str text, unit;
} sk_tok;

typedef struct {
    const char *s;
    size_t n, i;
    uint32_t line;
    sk_arena *arena;
    /* The line of the first comment, string or url the end of the
     * text left open, else 0. */
    uint32_t open_at;
} sk_lexer;

void sk_lexer_init(sk_lexer *lx, const char *s, size_t n, sk_arena *arena);
/* The next token; SK_TOK_EOF at the end, again and again. */
sk_tok sk_lex(sk_lexer *lx);

/* ---- Component values ---- */

typedef enum { SK_CV_TOKEN, SK_CV_FUNCTION, SK_CV_BLOCK } sk_cv_kind;

/* A token, a function (tok its function token, kids its arguments) or
 * a simple block (tok its opening bracket). */
typedef struct sk_cv sk_cv;
struct sk_cv {
    uint8_t kind;
    sk_tok tok;
    sk_cv *kids;
    size_t n;
};

typedef struct {
    sk_str name; /* as written; custom properties keep their case */
    sk_cv *value;
    size_t n;
    bool important;
    uint32_t line;
} sk_css_decl;

/* ---- Selectors ---- */

typedef enum {
    SK_SEL_TYPE,       /* name */
    SK_SEL_UNIVERSAL,
    SK_SEL_CLASS,      /* name */
    SK_SEL_ID,         /* name: matches nothing, a skin's elements have no id */
    SK_SEL_HOVER,
    SK_SEL_NTH_CHILD,  /* a, b */
    SK_SEL_NTH_LAST_CHILD,
    SK_SEL_FIRST_CHILD,
    SK_SEL_LAST_CHILD,
    SK_SEL_ONLY_CHILD,
    SK_SEL_NOT,        /* list */
    SK_SEL_IS,         /* list */
    SK_SEL_WHERE,      /* list */
    SK_SEL_HAS,        /* list of relative selectors */
    SK_SEL_HOST,       /* list: :host(<compound>), or NULL for :host */
} sk_sel_kind;

typedef enum {
    SK_COMB_NONE,       /* the first compound */
    SK_COMB_DESCENDANT,
    SK_COMB_CHILD,
    SK_COMB_NEXT,       /* + */
    SK_COMB_LATER,      /* ~ */
} sk_comb;

typedef enum { SK_PSEUDO_NONE, SK_PSEUDO_BEFORE, SK_PSEUDO_AFTER } sk_pseudo;

typedef struct sk_selector sk_selector;
typedef struct sk_selector_list sk_selector_list;

typedef struct {
    uint8_t kind;
    sk_str name;
    int32_t a, b;
    sk_selector_list *list;
} sk_simple;

/* A compound and the combinator that joins it to the compound before
 * it (SK_COMB_NONE for the first, or, in a relative selector, the
 * combinator to the anchor). */
typedef struct {
    uint8_t comb;
    sk_simple *simples;
    size_t n;
} sk_compound;

/* A complex selector, compounds left to right; pseudo the element it
 * ends on. spec is a << 16 | b << 8 | c, each saturating at 255. */
struct sk_selector {
    sk_compound *compounds;
    size_t n;
    uint8_t pseudo;
    uint32_t spec;
};

struct sk_selector_list {
    sk_selector *items;
    size_t n;
};

/* Parses the prelude of a style rule. NULL, with an error at line,
 * for anything outside decision 0037's subset. */
sk_selector_list *sk_selectors_parse(sk_arena *a, const sk_cv *cv, size_t n, uint32_t line, sk_diags *d,
                                     const char *file);
/* Appends the selector as CSS text, normalized. */
void sk_selector_print(sk_buf *out, const sk_selector *s);

/* ---- Sheets ---- */

typedef struct {
    sk_selector *sel;
    uint32_t spec;
} sk_css_sel_ref;

/* A style rule: its selectors and its declarations, in source order. */
typedef struct {
    sk_selector_list *sel;
    sk_css_decl *decls;
    size_t ndecls;
    uint32_t line;
    sk_str text; /* the prelude as written, trimmed, for messages */
} sk_css_rule;

typedef struct {
    sk_str selector; /* "0%", "from" ... as written */
    float *offsets;  /* 0 to 1, one for each of the frame's selectors */
    size_t noffsets;
    sk_css_decl *decls;
    size_t ndecls;
    uint32_t line;
} sk_css_frame;

typedef struct {
    sk_str name;
    sk_css_frame *frames;
    size_t nframes;
    uint32_t line;
} sk_css_keyframes;

/* A url() in the sheet: its value and line, for the file checks
 * validate.js makes of every one. */
typedef struct {
    sk_str value;
    uint32_t line;
} sk_css_url;

typedef struct {
    const char *file; /* for messages */
    sk_arena arena;
    SK_VEC(sk_css_rule) rules;
    SK_VEC(sk_css_keyframes) keyframes;
    SK_VEC(sk_css_url) urls;
    SK_VEC(uint32_t) imports; /* the line of each @import */
} sk_css_sheet;

/* Parses text into sheet, which the caller frees with
 * sk_css_sheet_free. Syntax errors, unknown at-rules and selectors
 * outside the subset go to d; the @imports and urls are kept for
 * the caller's file checks. Declarations are not checked here. */
void sk_css_parse(sk_css_sheet *sheet, const char *file, const char *text, size_t n, sk_diags *d);
void sk_css_sheet_free(sk_css_sheet *sheet);

/* ASCII case-insensitive comparison of an identifier with a lowercase
 * literal. */
bool sk_ident_is(sk_str s, const char *lower);

#endif
