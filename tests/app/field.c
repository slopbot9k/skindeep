/* A one-line text field (field.c): what each key reports to its caller
 * besides the edit, the clipboard's part; the editing itself is checked
 * through the chooser's fields (tests/app/chooser.c). */
#include "../../src/app/field.h"
#include "../test.h"

#include <stdlib.h>

static sk_key_event typed(const char *text)
{
    sk_key_event k;
    memset(&k, 0, sizeof k);
    snprintf(k.text, sizeof k.text, "%s", text);
    k.base = (uint32_t)(unsigned char)text[0];
    return k;
}

static sk_key_event chord(uint32_t base, uint32_t mods)
{
    sk_key_event k;
    memset(&k, 0, sizeof k);
    k.base = base;
    k.mods = mods;
    return k;
}

static sk_key_event named(sk_key key, uint32_t mods)
{
    sk_key_event k;
    memset(&k, 0, sizeof k);
    k.key = key;
    k.mods = mods;
    return k;
}

static void test_typing_and_select_all(void)
{
    sk_field f;
    sk_field_init(&f);
    sk_field_set(&f, "deck");
    sk_field_select_all(&f);
    sk_key_event k = typed("x");
    sk_field_key_result r = sk_field_key(&f, &k);
    CHECK(r.took && !r.copy && !r.paste && !r.selected);
    CHECK_STR(sk_buf_cstr(&f.text), "x");
    k = chord('a', SK_FIELD_CMD_MOD);
    r = sk_field_key(&f, &k);
    CHECK(r.took && r.selected);
    CHECK_INT(sk_field_sel_lo(&f), 0);
    CHECK_INT(sk_field_sel_hi(&f), 1);
    sk_field_free(&f);
}

static void test_the_clipboard_is_the_callers(void)
{
    sk_field f;
    sk_field_init(&f);
    sk_field_set(&f, "one two");
    sk_field_select_all(&f);
    sk_key_event k = chord('c', SK_FIELD_CMD_MOD);
    sk_field_key_result r = sk_field_key(&f, &k);
    CHECK_STR(r.copy, "one two");
    CHECK_STR(sk_buf_cstr(&f.text), "one two");
    free(r.copy);
    /* Cut: copied, then gone. */
    k = chord('x', SK_FIELD_CMD_MOD);
    r = sk_field_key(&f, &k);
    CHECK_STR(r.copy, "one two");
    CHECK_STR(sk_buf_cstr(&f.text), "");
    free(r.copy);
    /* Nothing selected copies nothing; paste is asked for. */
    k = chord('c', SK_FIELD_CMD_MOD);
    r = sk_field_key(&f, &k);
    CHECK(r.took && !r.copy);
    k = chord('v', SK_FIELD_CMD_MOD);
    r = sk_field_key(&f, &k);
    CHECK(r.took && r.paste);
    sk_field_free(&f);
}

static void test_shift_moves_select(void)
{
    sk_field f;
    sk_field_init(&f);
    sk_field_set(&f, "abc");
    sk_key_event k = named(SK_KEY_LEFT, SK_MOD_SHIFT);
    sk_field_key_result r = sk_field_key(&f, &k);
    CHECK(r.selected);
    CHECK_INT(f.caret, 2);
    CHECK_INT(f.anchor, 3);
    /* Enter, Escape and Tab are the caller's. */
    k = named(SK_KEY_ENTER, 0);
    CHECK(!sk_field_key(&f, &k).took);
    k = named(SK_KEY_ESCAPE, 0);
    CHECK(!sk_field_key(&f, &k).took);
    k = named(SK_KEY_TAB, 0);
    CHECK(!sk_field_key(&f, &k).took);
    sk_field_free(&f);
}

const sk_test sk_suite_field[] = {
    T(test_typing_and_select_all),
    T(test_the_clipboard_is_the_callers),
    T(test_shift_moves_select),
    {0},
};
