#include "field.h"

#include "../base/utf8.h"
#include "js.h"

#include <stdlib.h>
#include <string.h>

size_t sk_field_prev(const sk_buf *t, size_t i)
{
    if (!i)
        return 0;
    i--;
    while (i && ((unsigned char)t->ptr[i] & 0xC0) == 0x80)
        i--;
    return i;
}

size_t sk_field_next(const sk_buf *t, size_t i)
{
    if (i >= t->len)
        return t->len;
    i++;
    while (i < t->len && ((unsigned char)t->ptr[i] & 0xC0) == 0x80)
        i++;
    return i;
}

/* A word's character: a letter, a digit, _, anything past ASCII but a
 * space. Paths break at / . - and ~. */
static bool word_at(const sk_buf *t, size_t i)
{
    uint32_t cp;
    sk_utf8_decode(t->ptr + i, t->len - i, &cp, NULL);
    return (cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || cp == '_' ||
           (cp >= 0x80 && !sk_js_space(cp));
}

size_t sk_field_word_left(const sk_buf *t, size_t i)
{
    while (i && !word_at(t, sk_field_prev(t, i)))
        i = sk_field_prev(t, i);
    while (i && word_at(t, sk_field_prev(t, i)))
        i = sk_field_prev(t, i);
    return i;
}

size_t sk_field_word_right(const sk_buf *t, size_t i)
{
    while (i < t->len && !word_at(t, i))
        i = sk_field_next(t, i);
    while (i < t->len && word_at(t, i))
        i = sk_field_next(t, i);
    return i;
}

void sk_field_word_around(const sk_buf *t, size_t i, size_t *lo, size_t *hi)
{
    if (i >= t->len && i)
        i = sk_field_prev(t, i);
    bool w = i < t->len && word_at(t, i);
    size_t a = i, b = i;
    while (a && word_at(t, sk_field_prev(t, a)) == w)
        a = sk_field_prev(t, a);
    while (b < t->len && word_at(t, b) == w)
        b = sk_field_next(t, b);
    *lo = a;
    *hi = b;
}

size_t sk_field_sel_lo(const sk_field *f)
{
    return SK_MIN(f->caret, f->anchor);
}

size_t sk_field_sel_hi(const sk_field *f)
{
    return SK_MAX(f->caret, f->anchor);
}

bool sk_field_has_sel(const sk_field *f)
{
    return f->caret != f->anchor;
}

void sk_field_init(sk_field *f)
{
    memset(f, 0, sizeof *f);
    sk_buf_reserve(&f->text, 0);
}

void sk_field_free(sk_field *f)
{
    sk_buf_free(&f->text);
    memset(f, 0, sizeof *f);
}

void sk_field_set(sk_field *f, const char *s)
{
    sk_buf_clear(&f->text);
    sk_buf_append_c(&f->text, s ? s : "");
    f->caret = f->anchor = f->text.len;
    f->scroll = 0;
}

void sk_field_select_all(sk_field *f)
{
    f->anchor = 0;
    f->caret = f->text.len;
}

void sk_field_replace(sk_field *f, size_t lo, size_t hi, const char *s, size_t n)
{
    sk_buf b = {0};
    sk_buf_append(&b, f->text.ptr, lo);
    sk_buf_append(&b, s, n);
    sk_buf_append(&b, f->text.ptr + hi, f->text.len - hi);
    sk_buf_free(&f->text);
    f->text = b;
    f->caret = f->anchor = lo + n;
}

void sk_field_insert(sk_field *f, const char *s)
{
    sk_buf clean = {0}, text = {0};
    sk_utf8_sanitize(&clean, s, strlen(s));
    for (size_t i = 0; i < clean.len; i++)
        if (clean.ptr[i] != '\n' && clean.ptr[i] != '\r')
            sk_buf_putc(&text, clean.ptr[i]);
    sk_field_replace(f, sk_field_sel_lo(f), sk_field_sel_hi(f), text.ptr, text.len);
    sk_buf_free(&clean);
    sk_buf_free(&text);
}

char *sk_field_selection(const sk_field *f)
{
    return sk_xstrndup(f->text.ptr + sk_field_sel_lo(f), sk_field_sel_hi(f) - sk_field_sel_lo(f));
}

static void move_caret(sk_field *f, size_t to, bool extend, sk_field_key_result *r)
{
    f->caret = to;
    if (!extend)
        f->anchor = to;
    else
        r->selected = sk_field_has_sel(f);
}

static void delete_range(sk_field *f, size_t lo, size_t hi)
{
    sk_field_replace(f, lo, hi, NULL, 0);
}

/* The selection for the clipboard, when there is one. */
static void copy(const sk_field *f, sk_field_key_result *r)
{
    if (sk_field_has_sel(f))
        r->copy = sk_field_selection(f);
}

sk_field_key_result sk_field_key(sk_field *f, const sk_key_event *k)
{
    sk_field_key_result r = {true, NULL, false, false};
    uint32_t m = k->mods & (SK_MOD_SHIFT | SK_MOD_CTRL | SK_MOD_ALT | SK_MOD_SUPER);
    bool shift = m & SK_MOD_SHIFT;
    uint32_t rest = m & ~(uint32_t)SK_MOD_SHIFT;
    const uint32_t word = SK_FIELD_WORD_MOD, cmd = SK_FIELD_CMD_MOD;
    sk_buf *t = &f->text;
    switch (k->key) {
    case SK_KEY_LEFT:
    case SK_KEY_RIGHT: {
        bool left = k->key == SK_KEY_LEFT;
        size_t to;
        if (rest == word)
            to = left ? sk_field_word_left(t, f->caret) : sk_field_word_right(t, f->caret);
#ifdef __APPLE__
        else if (rest == SK_MOD_SUPER)
            to = left ? 0 : t->len;
#endif
        else if (rest)
            return r.took = false, r;
        else if (!shift && sk_field_has_sel(f))
            to = left ? sk_field_sel_lo(f) : sk_field_sel_hi(f);
        else
            to = left ? sk_field_prev(t, f->caret) : sk_field_next(t, f->caret);
        move_caret(f, to, shift, &r);
        return r;
    }
#ifdef __APPLE__
    case SK_KEY_UP:
    case SK_KEY_DOWN:
        if (rest && rest != SK_MOD_SUPER)
            return r.took = false, r;
        move_caret(f, k->key == SK_KEY_UP ? 0 : t->len, shift, &r);
        return r;
#endif
    case SK_KEY_HOME:
    case SK_KEY_END:
        if (rest)
            return r.took = false, r;
        move_caret(f, k->key == SK_KEY_HOME ? 0 : t->len, shift, &r);
        return r;
    case SK_KEY_BACKSPACE:
        if (sk_field_has_sel(f) && (!rest || rest == word))
            delete_range(f, sk_field_sel_lo(f), sk_field_sel_hi(f));
        else if (rest == word)
            delete_range(f, sk_field_word_left(t, f->caret), f->caret);
#ifdef __APPLE__
        else if (rest == SK_MOD_SUPER)
            delete_range(f, 0, sk_field_has_sel(f) ? sk_field_sel_hi(f) : f->caret);
#endif
        else if (!rest)
            delete_range(f, sk_field_prev(t, f->caret), f->caret);
        else
            r.took = false;
        return r;
    case SK_KEY_DELETE:
#ifndef __APPLE__
        if (m == SK_MOD_SHIFT) {
            copy(f, &r);
            delete_range(f, sk_field_sel_lo(f), sk_field_sel_hi(f));
            return r;
        }
#endif
        if (sk_field_has_sel(f) && (!rest || rest == word))
            delete_range(f, sk_field_sel_lo(f), sk_field_sel_hi(f));
        else if (rest == word)
            delete_range(f, f->caret, sk_field_word_right(t, f->caret));
        else if (!rest)
            delete_range(f, f->caret, sk_field_next(t, f->caret));
        else
            r.took = false;
        return r;
#ifndef __APPLE__
    case SK_KEY_INSERT:
        if (m == SK_MOD_SHIFT)
            r.paste = true;
        else if (m == SK_MOD_CTRL)
            copy(f, &r);
        else
            r.took = false;
        return r;
#endif
    default:
        break;
    }
    if (rest == cmd) {
        switch (k->base) {
        case 'a':
            sk_field_select_all(f);
            r.selected = sk_field_has_sel(f);
            return r;
        case 'c':
            copy(f, &r);
            return r;
        case 'x':
            copy(f, &r);
            delete_range(f, sk_field_sel_lo(f), sk_field_sel_hi(f));
            return r;
        case 'v':
            r.paste = true;
            return r;
        default:
            break;
        }
    }
#ifdef __APPLE__
    /* Cocoa's own bindings: Ctrl+A and Ctrl+E to either end. */
    if (rest == SK_MOD_CTRL && (k->base == 'a' || k->base == 'e')) {
        move_caret(f, k->base == 'a' ? 0 : t->len, shift, &r);
        return r;
    }
    const uint32_t not_typing = SK_MOD_CTRL | SK_MOD_SUPER;
#else
    const uint32_t not_typing = SK_MOD_CTRL | SK_MOD_SUPER | SK_MOD_ALT;
#endif
    if (k->text[0] && !(m & not_typing) && k->key == SK_KEY_NONE) {
        sk_field_insert(f, k->text);
        return r;
    }
    r.took = false;
    return r;
}
