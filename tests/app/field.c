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

static void test_keypad_and_max_length(void)
{
    sk_field f;
    sk_field_init(&f);
    /* The keypad's keys type their text; its Enter is the caller's. */
    sk_key_event k = named(SK_KEY_KP_0 + 7, 0);
    snprintf(k.text, sizeof k.text, "7");
    CHECK(sk_field_key(&f, &k).took);
    k = named(SK_KEY_KP_ENTER, 0);
    snprintf(k.text, sizeof k.text, "\r");
    CHECK(!sk_field_key(&f, &k).took);
    CHECK_STR(sk_buf_cstr(&f.text), "7");
    /* maxLength 5: a full field takes nothing more, where the caret is,
     * and a paste is cut to what fits; a selection makes room. */
    f.max_chars = 5;
    sk_field_set(&f, "abcde");
    f.caret = f.anchor = 2;
    sk_key_event t = typed("x");
    sk_field_key(&f, &t);
    CHECK_STR(sk_buf_cstr(&f.text), "abcde");
    CHECK_INT(f.caret, 2);
    sk_field_set(&f, "ab");
    sk_field_insert(&f, "\xc3\xa9t\xc3\xa9s and more");
    CHECK_STR(sk_buf_cstr(&f.text), "ab\xc3\xa9t\xc3\xa9");
    f.anchor = 0;
    f.caret = 2;
    sk_field_insert(&f, "XYZ");
    CHECK_STR(sk_buf_cstr(&f.text), "XY\xc3\xa9t\xc3\xa9");
    sk_field_free(&f);
}

const sk_test sk_suite_field[] = {
    T(test_keypad_and_max_length),
    T(test_typing_and_select_all),
    T(test_the_clipboard_is_the_callers),
    T(test_shift_moves_select),
    {0},
};
