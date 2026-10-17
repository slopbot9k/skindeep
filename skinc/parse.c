/* The parser of CSS Syntax Level 3, section 5: a stylesheet's rules,
 * a style block's declarations and @keyframes. skinc refuses what a
 * browser would drop: a declaration it cannot read, a rule nested in
 * a rule, an at-rule outside the subset. */
#include "css.h"

#include <string.h>

/* Blocks and functions nested deeper than this are refused, so a
 * hostile sheet cannot exhaust the stack. */
#define MAX_DEPTH 64

typedef struct {
    sk_lexer lx;
    sk_tok cur;
    bool have;
    sk_css_sheet *sheet;
    sk_diags *d;
    unsigned depth;
    bool deep;           /* the depth error was given */
    uint32_t url_fn;     /* the line of a url( function waiting for its string */
} parser;

typedef SK_VEC(sk_cv) cv_vec;

static sk_tok peek_tok(parser *p)
{
    if (!p->have) {
        p->cur = sk_lex(&p->lx);
        p->have = true;
    }
    return p->cur;
}

/* Every url, as validate.js scans the text for them: a url token, or
 * url( and the string in it. */
static void note_url(parser *p, sk_tok t)
{
    if (t.type == SK_TOK_WHITESPACE)
        return;
    if (p->url_fn && t.type == SK_TOK_STRING) {
        sk_css_url u = {t.text, p->url_fn};
        sk_vec_push(&p->sheet->urls, u);
    }
    p->url_fn = 0;
    if (t.type == SK_TOK_URL) {
        sk_css_url u = {t.text, t.line};
        sk_vec_push(&p->sheet->urls, u);
    } else if (t.type == SK_TOK_FUNCTION && sk_ident_is(t.text, "url")) {
        p->url_fn = t.line;
    }
}

static sk_tok take(parser *p)
{
    sk_tok t = peek_tok(p);
    p->have = false;
    note_url(p, t);
    if (t.type == SK_TOK_BAD_STRING)
        sk_diag_line(p->d, p->sheet->file, t.line, "a string runs past the end of its line");
    else if (t.type == SK_TOK_BAD_URL)
        sk_diag_line(p->d, p->sheet->file, t.line, "a url() is malformed: quote it, or escape its spaces, quotes and brackets");
    return t;
}

static sk_cv *keep_cvs(parser *p, cv_vec *v, size_t *n)
{
    *n = v->len;
    sk_cv *out = NULL;
    if (v->len) {
        out = sk_arena_array(&p->sheet->arena, v->len, sizeof *out);
        memcpy(out, v->ptr, v->len * sizeof *out);
    }
    sk_vec_free(v);
    return out;
}

static uint8_t closer(uint8_t open)
{
    return open == SK_TOK_LBRACE ? SK_TOK_RBRACE : open == SK_TOK_LBRACKET ? SK_TOK_RBRACKET : SK_TOK_RPAREN;
}

static void component(parser *p, sk_cv *out);

/* The contents of a block or a function, up to the token end. */
static sk_cv *contents(parser *p, uint8_t end, uint32_t line, size_t *n)
{
    cv_vec v = {0};
    p->depth++;
    for (;;) {
        sk_tok t = peek_tok(p);
        if (t.type == end) {
            take(p);
            break;
        }
        if (t.type == SK_TOK_EOF) {
            sk_diag_line(p->d, p->sheet->file, line, "a %s is not closed before the end of the file",
                         end == SK_TOK_RBRACE ? "{ block" : end == SK_TOK_RBRACKET ? "[ block" : "( or function");
            break;
        }
        if (p->depth > MAX_DEPTH) {
            if (!p->deep)
                sk_diag_line(p->d, p->sheet->file, t.line, "blocks nest more than %d deep", MAX_DEPTH);
            p->deep = true;
            take(p);
            continue;
        }
        sk_cv cv;
        component(p, &cv);
        sk_vec_push(&v, cv);
    }
    p->depth--;
    return keep_cvs(p, &v, n);
}

static void component(parser *p, sk_cv *out)
{
    memset(out, 0, sizeof *out);
    sk_tok t = take(p);
    out->tok = t;
    if (t.type == SK_TOK_LBRACE || t.type == SK_TOK_LBRACKET || t.type == SK_TOK_LPAREN) {
        out->kind = SK_CV_BLOCK;
        out->kids = contents(p, closer(t.type), t.line, &out->n);
    } else if (t.type == SK_TOK_FUNCTION) {
        out->kind = SK_CV_FUNCTION;
        out->kids = contents(p, SK_TOK_RPAREN, t.line, &out->n);
    } else {
        out->kind = SK_CV_TOKEN;
    }
}

static bool is_ws(const sk_cv *cv)
{
    return cv->kind == SK_CV_TOKEN && cv->tok.type == SK_TOK_WHITESPACE;
}

static bool is_tok(const sk_cv *cv, uint8_t type)
{
    return cv->kind == SK_CV_TOKEN && cv->tok.type == type;
}

/* Trims whitespace from both ends of cv[0..n). */
static const sk_cv *trim(const sk_cv *cv, size_t *n)
{
    while (*n && is_ws(cv)) {
        cv++;
        (*n)--;
    }
    while (*n && is_ws(&cv[*n - 1]))
        (*n)--;
    return cv;
}

static const char *describe(const sk_cv *cv)
{
    if (cv->kind == SK_CV_FUNCTION)
        return "a function";
    if (cv->kind == SK_CV_BLOCK)
        return "a block";
    switch (cv->tok.type) {
    case SK_TOK_IDENT:
        return "a name";
    case SK_TOK_AT_KEYWORD:
        return "an at-rule";
    case SK_TOK_HASH:
        return "a #";
    case SK_TOK_STRING:
        return "a string";
    case SK_TOK_NUMBER:
    case SK_TOK_PERCENTAGE:
    case SK_TOK_DIMENSION:
        return "a number";
    case SK_TOK_COLON:
        return "a :";
    default:
        return "a stray character";
    }
}

/* One declaration from cv[0..n), which starts with its name. */
static bool declaration(parser *p, const sk_cv *cv, size_t n, sk_css_decl *out)
{
    memset(out, 0, sizeof *out);
    out->name = cv[0].tok.text;
    out->line = cv[0].tok.line;
    size_t i = 1;
    while (i < n && is_ws(&cv[i]))
        i++;
    if (i >= n || !is_tok(&cv[i], SK_TOK_COLON)) {
        sk_diag_line(p->d, p->sheet->file, out->line, "\"%.*s\" needs a : and a value", (int)out->name.len,
                     out->name.ptr);
        return false;
    }
    size_t len = n - i - 1;
    const sk_cv *v = trim(cv + i + 1, &len);
    /* !important: the last two non-whitespace values. */
    if (len >= 2 && is_tok(&v[len - 1], SK_TOK_IDENT) && sk_ident_is(v[len - 1].tok.text, "important")) {
        size_t j = len - 1;
        while (j > 0 && is_ws(&v[j - 1]))
            j--;
        if (j > 0 && is_tok(&v[j - 1], SK_TOK_DELIM) && v[j - 1].tok.delim == '!') {
            out->important = true;
            len = j - 1;
            v = trim(v, &len);
        }
    }
    out->value = (sk_cv *)v;
    out->n = len;
    return true;
}

/* The declarations of a style or keyframe block. */
static sk_css_decl *declarations(parser *p, const sk_cv *cv, size_t n, size_t *count, const char *what)
{
    SK_VEC(sk_css_decl) v = {0};
    size_t i = 0;
    while (i < n) {
        const sk_cv *c = &cv[i];
        if (is_ws(c) || is_tok(c, SK_TOK_SEMICOLON)) {
            i++;
            continue;
        }
        size_t end = i;
        while (end < n && !is_tok(&cv[end], SK_TOK_SEMICOLON))
            end++;
        /* A nested rule (CSS Nesting) reaches its { before any ;. */
        bool nested = false;
        for (size_t k = i; k < end; k++)
            if (cv[k].kind == SK_CV_BLOCK && cv[k].tok.type == SK_TOK_LBRACE)
                nested = true;
        if (nested && !is_tok(c, SK_TOK_AT_KEYWORD)) {
            sk_diag_line(p->d, p->sheet->file, c->tok.line, "a rule inside %s is not supported", what);
        } else if (is_tok(c, SK_TOK_IDENT)) {
            sk_css_decl decl;
            if (declaration(p, c, end - i, &decl))
                sk_vec_push(&v, decl);
        } else if (is_tok(c, SK_TOK_AT_KEYWORD)) {
            sk_diag_line(p->d, p->sheet->file, c->tok.line, "@%.*s inside %s is not supported", (int)c->tok.text.len,
                         c->tok.text.ptr, what);
        } else {
            sk_diag_line(p->d, p->sheet->file, c->tok.line, "%s where a declaration should start, in %s",
                         describe(c), what);
        }
        i = end;
    }
    *count = v.len;
    sk_css_decl *out = NULL;
    if (v.len) {
        out = sk_arena_array(&p->sheet->arena, v.len, sizeof *out);
        memcpy(out, v.ptr, v.len * sizeof *out);
    }
    sk_vec_free(&v);
    return out;
}

static void style_rule(parser *p, cv_vec *prelude, const sk_cv *block, uint32_t line)
{
    size_t n = prelude->len;
    const sk_cv *pre = trim(prelude->ptr, &n);
    sk_selector_list *sel = sk_selectors_parse(&p->sheet->arena, pre, n, line, p->d, p->sheet->file);
    size_t ndecls = 0;
    sk_css_decl *decls = declarations(p, block->kids, block->n, &ndecls, "a style rule");
    if (!sel)
        return;
    sk_css_rule r = {sel, decls, ndecls, line, {0}};
    sk_buf text = {0};
    for (size_t i = 0; i < sel->n; i++) {
        if (i)
            sk_buf_append_c(&text, ", ");
        sk_selector_print(&text, &sel->items[i]);
    }
    r.text = sk_str_n(sk_arena_strndup(&p->sheet->arena, sk_buf_cstr(&text), text.len), text.len);
    sk_buf_free(&text);
    sk_vec_push(&p->sheet->rules, r);
}

/* A keyframe's selector: from, to, or percentages, by commas. */
static bool frame_offsets(parser *p, const sk_cv *cv, size_t n, uint32_t line, sk_css_frame *f)
{
    SK_VEC(float) v = {0};
    bool want = true, ok = true;
    for (size_t i = 0; i < n && ok; i++) {
        const sk_cv *c = &cv[i];
        if (is_ws(c))
            continue;
        if (want && is_tok(c, SK_TOK_PERCENTAGE) && c->tok.num >= 0 && c->tok.num <= 100) {
            sk_vec_push(&v, (float)(c->tok.num / 100));
            want = false;
        } else if (want && is_tok(c, SK_TOK_IDENT) && sk_ident_is(c->tok.text, "from")) {
            sk_vec_push(&v, 0.0f);
            want = false;
        } else if (want && is_tok(c, SK_TOK_IDENT) && sk_ident_is(c->tok.text, "to")) {
            sk_vec_push(&v, 1.0f);
            want = false;
        } else if (!want && is_tok(c, SK_TOK_COMMA)) {
            want = true;
        } else {
            ok = false;
        }
    }
    if (want)
        ok = false;
    if (!ok) {
        sk_diag_line(p->d, p->sheet->file, line, "a keyframe's selector must be from, to or percentages from 0%% to 100%%");
        sk_vec_free(&v);
        return false;
    }
    f->noffsets = v.len;
    f->offsets = sk_arena_array(&p->sheet->arena, v.len, sizeof(float));
    memcpy(f->offsets, v.ptr, v.len * sizeof(float));
    sk_vec_free(&v);
    return true;
}

static void keyframes(parser *p, const sk_cv *pre, size_t npre, const sk_cv *block, uint32_t line)
{
    pre = trim(pre, &npre);
    if (npre != 1 || !(is_tok(&pre[0], SK_TOK_IDENT) || is_tok(&pre[0], SK_TOK_STRING)) ||
        (is_tok(&pre[0], SK_TOK_IDENT) &&
         (sk_ident_is(pre[0].tok.text, "none") || sk_ident_is(pre[0].tok.text, "initial") ||
          sk_ident_is(pre[0].tok.text, "inherit") || sk_ident_is(pre[0].tok.text, "unset") ||
          sk_ident_is(pre[0].tok.text, "default"))) ||
        !pre[0].tok.text.len) {
        sk_diag_line(p->d, p->sheet->file, line, "@keyframes needs a name");
        return;
    }
    if (!block) {
        sk_diag_line(p->d, p->sheet->file, line, "@keyframes needs a { block } of frames");
        return;
    }
    sk_css_keyframes k = {pre[0].tok.text, NULL, 0, line};
    SK_VEC(sk_css_frame) frames = {0};
    size_t start = 0;
    for (size_t i = 0; i < block->n; i++) {
        const sk_cv *c = &block->kids[i];
        if (c->kind == SK_CV_BLOCK && c->tok.type == SK_TOK_LBRACE) {
            size_t n = i - start;
            const sk_cv *sel = trim(block->kids + start, &n);
            uint32_t at = n ? sel[0].tok.line : c->tok.line;
            sk_css_frame f;
            memset(&f, 0, sizeof f);
            f.line = at;
            if (frame_offsets(p, sel, n, at, &f)) {
                f.decls = declarations(p, c->kids, c->n, &f.ndecls, "a keyframe");
                sk_vec_push(&frames, f);
            }
            start = i + 1;
        } else if (is_tok(c, SK_TOK_SEMICOLON)) {
            sk_diag_line(p->d, p->sheet->file, c->tok.line, "a ; between keyframes");
            start = i + 1;
        }
    }
    /* An empty block has no kids to point into. */
    size_t rest = block->n - start;
    if (rest)
        trim(block->kids + start, &rest);
    if (rest)
        sk_diag_line(p->d, p->sheet->file, block->kids[start].tok.line, "a keyframe has no { block }");
    k.nframes = frames.len;
    if (frames.len) {
        k.frames = sk_arena_array(&p->sheet->arena, frames.len, sizeof *k.frames);
        memcpy(k.frames, frames.ptr, frames.len * sizeof *k.frames);
    }
    sk_vec_free(&frames);
    sk_vec_push(&p->sheet->keyframes, k);
}

static void at_rule(parser *p)
{
    sk_tok at = take(p);
    cv_vec prelude = {0};
    sk_cv block;
    bool has_block = false;
    for (;;) {
        sk_tok t = peek_tok(p);
        if (t.type == SK_TOK_SEMICOLON) {
            take(p);
            break;
        }
        if (t.type == SK_TOK_EOF)
            break;
        if (t.type == SK_TOK_LBRACE) {
            component(p, &block);
            has_block = true;
            break;
        }
        sk_cv cv;
        component(p, &cv);
        sk_vec_push(&prelude, cv);
    }
    sk_str name = at.text;
    if (sk_ident_is(name, "import")) {
        /* validate.js's message, given by the manifest's checks. */
        sk_vec_push(&p->sheet->imports, at.line);
    } else if (sk_ident_is(name, "keyframes")) {
        keyframes(p, prelude.ptr, prelude.len, has_block ? &block : NULL, at.line);
    } else if (sk_ident_is(name, "font-face")) {
        sk_diag_line(p->d, p->sheet->file, at.line, "@font-face is not allowed: declare fonts in skin.json's fonts");
    } else {
        sk_diag_line(p->d, p->sheet->file, at.line, "@%.*s is not supported", (int)name.len, name.ptr);
    }
    sk_vec_free(&prelude);
}

static void qualified_rule(parser *p)
{
    cv_vec prelude = {0};
    uint32_t line = peek_tok(p).line;
    for (;;) {
        sk_tok t = peek_tok(p);
        if (t.type == SK_TOK_EOF) {
            sk_diag_line(p->d, p->sheet->file, line, "a rule has no { block }");
            break;
        }
        if (t.type == SK_TOK_LBRACE) {
            sk_cv block;
            component(p, &block);
            style_rule(p, &prelude, &block, line);
            break;
        }
        sk_cv cv;
        component(p, &cv);
        sk_vec_push(&prelude, cv);
    }
    sk_vec_free(&prelude);
}

void sk_css_parse(sk_css_sheet *sheet, const char *file, const char *text, size_t n, sk_diags *d)
{
    memset(sheet, 0, sizeof *sheet);
    sheet->file = file;
    sk_arena_init(&sheet->arena, 0);
    parser p;
    memset(&p, 0, sizeof p);
    p.sheet = sheet;
    p.d = d;
    sk_lexer_init(&p.lx, text, n, &sheet->arena);
    for (;;) {
        sk_tok t = peek_tok(&p);
        if (t.type == SK_TOK_EOF)
            break;
        if (t.type == SK_TOK_WHITESPACE || t.type == SK_TOK_CDO || t.type == SK_TOK_CDC) {
            take(&p);
            continue;
        }
        if (t.type == SK_TOK_AT_KEYWORD)
            at_rule(&p);
        else
            qualified_rule(&p);
    }
    if (p.lx.open_at)
        sk_diag_line(d, file, p.lx.open_at, "a comment, string or url() is not closed before the end of the file");
}

void sk_css_sheet_free(sk_css_sheet *sheet)
{
    sk_vec_free(&sheet->rules);
    sk_vec_free(&sheet->keyframes);
    sk_vec_free(&sheet->urls);
    sk_vec_free(&sheet->imports);
    sk_arena_free(&sheet->arena);
}
