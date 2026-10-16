/* The core protocol's keyboard rules on the fake server's US keymap and a
 * few of their own: shift levels, Caps Lock, AltGr and Mode_switch, the
 * keypad with NumLock, named keys, and keysyms to Unicode. */
#include "../../src/platform/x11/keys.h"
#include "../test.h"
#include "x11_fake.h"

enum {
    SHIFT = 1,
    LOCK = 2,
    CONTROL = 4,
    MOD1 = 8,
    MOD2 = 16,
    MOD3 = 32,
    MOD4 = 64,
    MOD5 = 128,
};

static sk_x11_keymap us(void)
{
    sk_x11_keymap m = {0};
    int count;
    const uint32_t *syms = sk_xfake_keysyms(&count);
    sk_x11_keymap_set(&m, SK_XFAKE_MIN_KEYCODE, count, SK_XFAKE_PER, syms, SK_XFAKE_PER_MOD,
                      sk_xfake_modifiers());
    return m;
}

static sk_key_event press(const sk_x11_keymap *m, uint8_t kc, uint16_t state)
{
    sk_key_event e;
    sk_x11_key_event(m, kc, state, &e);
    return e;
}

/* The text a key types, until the next call. */
static const char *typed(const sk_x11_keymap *m, uint8_t kc, uint16_t state)
{
    static char text[16];
    memcpy(text, press(m, kc, state).text, sizeof text);
    return text;
}

static void test_modifier_roles(void)
{
    sk_x11_keymap m = us();
    CHECK_INT(m.group, MOD5);
    CHECK_INT(m.level3, MOD5);
    CHECK_INT(m.num_lock, MOD2);
    CHECK_INT(m.alt, MOD1);
    CHECK_INT(m.super, MOD4);
    CHECK(m.caps_lock);
    CHECK(!m.shift_lock);
    CHECK_INT(sk_x11_mods(&m, SHIFT | CONTROL | MOD1 | MOD4 | LOCK | MOD2),
              SK_MOD_SHIFT | SK_MOD_CTRL | SK_MOD_ALT | SK_MOD_SUPER | SK_MOD_CAPS);
    CHECK_INT(sk_x11_keymap_find(&m, 'a'), 38);
    CHECK_INT(sk_x11_keymap_find(&m, 0x12345), 0);
    sk_x11_keymap_free(&m);
}

static void test_shift_levels_and_caps_lock(void)
{
    sk_x11_keymap m = us();
    sk_key_event e = press(&m, 38, 0);
    CHECK_STR(e.text, "a");
    CHECK_INT(e.key, SK_KEY_NONE);
    CHECK_INT(e.base, 'a');
    CHECK_INT(e.code, 38);
    e = press(&m, 38, SHIFT);
    CHECK_STR(e.text, "A");
    CHECK_INT(e.base, 'a');
    CHECK_INT(e.mods, SK_MOD_SHIFT);
    e = press(&m, 38, LOCK);
    CHECK_STR(e.text, "A");
    CHECK_INT(e.mods, SK_MOD_CAPS);
    /* The core protocol keeps Shift with Caps Lock upper case. */
    CHECK_STR(typed(&m, 38, LOCK | SHIFT), "A");
    CHECK_STR(typed(&m, 10, LOCK), "1");
    CHECK_STR(typed(&m, 10, SHIFT), "!");
    CHECK_STR(typed(&m, 10, SHIFT | LOCK), "!");
    CHECK_STR(typed(&m, 48, SHIFT), "\"");
    CHECK_STR(typed(&m, 65, 0), " ");
    sk_x11_keymap_free(&m);
}

static void test_ctrl_alt_and_super_chords(void)
{
    sk_x11_keymap m = us();
    sk_key_event e = press(&m, 54, CONTROL);
    CHECK_STR(e.text, "c");
    CHECK_INT(e.base, 'c');
    CHECK_INT(e.mods, SK_MOD_CTRL);
    e = press(&m, 54, CONTROL | SHIFT);
    CHECK_STR(e.text, "C");
    CHECK_INT(e.base, 'c');
    CHECK_INT(press(&m, 53, MOD1).mods, SK_MOD_ALT);
    CHECK_INT(press(&m, 53, MOD4).mods, SK_MOD_SUPER);
    /* A modifier key alone names nothing and types nothing. */
    e = press(&m, 50, 0);
    CHECK_INT(e.key, SK_KEY_NONE);
    CHECK_STR(e.text, "");
    CHECK_INT(e.base, 0);
    sk_x11_keymap_free(&m);
}

static void test_altgr_level(void)
{
    sk_x11_keymap m = us();
    CHECK_STR(typed(&m, 24, MOD5), "@");
    CHECK_STR(typed(&m, 24, MOD5 | SHIFT), "\xce\xa9"); /* Ω */
    CHECK_STR(typed(&m, 26, MOD5), "\xe2\x82\xac");     /* € */
    CHECK_STR(typed(&m, 26, MOD5 | SHIFT), "\xe2\x82\xac");
    /* A key with no AltGr level types its own. */
    CHECK_STR(typed(&m, 38, MOD5), "a");
    CHECK_STR(typed(&m, 38, MOD5 | SHIFT), "A");
    CHECK_INT(press(&m, 24, MOD5).base, 'q');
    sk_x11_keymap_free(&m);
}

static void test_mode_switch_group(void)
{
    /* An old keymap: Mode_switch alone on Mod3, group 2 its own. */
    uint32_t syms[4 * 4] = {
        'a', 'A', 0xe4, 0xc4,   /* 8: a A adiaeresis Adiaeresis */
        SK_XK_MODE_SWITCH, 0, 0, 0, /* 9 */
        'b', 0, 0, 0,           /* 10: b alone */
        ';', 0, 0, 0,           /* 11: semicolon alone */
    };
    uint8_t mods[8] = {0, 0, 0, 0, 0, 9, 0, 0};
    sk_x11_keymap m = {0};
    sk_x11_keymap_set(&m, 8, 4, 4, syms, 1, mods);
    CHECK_INT(m.group, MOD3);
    CHECK_INT(m.level3, 0);
    CHECK_STR(typed(&m, 8, 0), "a");
    CHECK_STR(typed(&m, 8, MOD3), "\xc3\xa4");
    CHECK_STR(typed(&m, 8, MOD3 | SHIFT), "\xc3\x84");
    /* One keysym: its two cases, or itself twice. */
    CHECK_STR(typed(&m, 10, SHIFT), "B");
    CHECK_STR(typed(&m, 10, MOD3 | SHIFT), "B");
    CHECK_STR(typed(&m, 11, SHIFT), ";");
    /* No Lock key: Lock means nothing. */
    CHECK_STR(typed(&m, 8, LOCK), "a");
    sk_x11_keymap_free(&m);
}

static void test_keypad_and_num_lock(void)
{
    sk_x11_keymap m = us();
    sk_key_event e = press(&m, 79, 0);
    CHECK_INT(e.key, SK_KEY_HOME);
    CHECK_STR(e.text, "");
    CHECK_INT(e.base, 0);
    e = press(&m, 79, MOD2);
    CHECK_INT(e.key, SK_KEY_KP_0 + 7);
    CHECK_STR(e.text, "7");
    CHECK_INT(e.mods, 0);
    /* Shift undoes NumLock. */
    CHECK_INT(press(&m, 79, MOD2 | SHIFT).key, SK_KEY_HOME);
    e = press(&m, 91, MOD2);
    CHECK_INT(e.key, SK_KEY_KP_DECIMAL);
    CHECK_STR(e.text, ".");
    CHECK_INT(press(&m, 91, 0).key, SK_KEY_DELETE);
    e = press(&m, 104, 0);
    CHECK_INT(e.key, SK_KEY_KP_ENTER);
    CHECK_STR(e.text, "");
    e = press(&m, 63, 0);
    CHECK_INT(e.key, SK_KEY_KP_MULTIPLY);
    CHECK_STR(e.text, "*");
    CHECK_INT(press(&m, 106, 0).key, SK_KEY_KP_DIVIDE);
    CHECK_INT(press(&m, 82, 0).key, SK_KEY_KP_SUBTRACT);
    CHECK_INT(press(&m, 86, 0).key, SK_KEY_KP_ADD);
    CHECK_INT(press(&m, 84, 0).key, SK_KEY_NONE); /* KP_Begin */
    sk_x11_keymap_free(&m);
}

static void test_named_keys(void)
{
    sk_x11_keymap m = us();
    static const struct {
        uint8_t kc;
        uint16_t state;
        sk_key key;
    } cases[] = {
        {36, 0, SK_KEY_ENTER},       {23, 0, SK_KEY_TAB},         {23, SHIFT, SK_KEY_TAB},
        {22, 0, SK_KEY_BACKSPACE},   {9, 0, SK_KEY_ESCAPE},       {9, SHIFT, SK_KEY_ESCAPE},
        {111, 0, SK_KEY_UP},         {116, 0, SK_KEY_DOWN},       {113, 0, SK_KEY_LEFT},
        {114, 0, SK_KEY_RIGHT},      {110, 0, SK_KEY_HOME},       {115, 0, SK_KEY_END},
        {112, 0, SK_KEY_PAGE_UP},    {117, 0, SK_KEY_PAGE_DOWN},  {118, 0, SK_KEY_INSERT},
        {119, 0, SK_KEY_DELETE},     {67, 0, SK_KEY_F1},          {71, 0, SK_KEY_F1 + 4},
        {96, 0, SK_KEY_F1 + 11},     {135, 0, SK_KEY_MENU},
    };
    for (size_t i = 0; i < SK_LEN(cases); i++) {
        sk_key_event e = press(&m, cases[i].kc, cases[i].state);
        CHECK_INT(e.key, cases[i].key);
        CHECK_STR(e.text, "");
        CHECK_INT(e.base, 0);
    }
    CHECK_INT(press(&m, 23, SHIFT).mods, SK_MOD_SHIFT);
    CHECK_INT(sk_x11_named_key(0xffd5), SK_KEY_F24);
    CHECK_INT(sk_x11_named_key(0xffac), SK_KEY_KP_DECIMAL);
    CHECK_INT(sk_x11_named_key(0xffbd), SK_KEY_KP_EQUAL);
    sk_x11_keymap_free(&m);
}

static void test_dead_keys_and_other_scripts(void)
{
    uint32_t syms[3 * 2] = {
        0xfe51, 0xfe51, /* 8: dead_acute */
        0x6c1, 0x6e1,   /* 9: Cyrillic_a Cyrillic_A */
        0x7e1, 0,       /* 10: Greek_alpha alone */
    };
    uint8_t mods[8] = {0, 66};
    sk_x11_keymap m = {0};
    sk_x11_keymap_set(&m, 8, 3, 2, syms, 1, mods);
    sk_key_event e = press(&m, 8, 0);
    CHECK_INT(e.key, SK_KEY_NONE);
    CHECK_STR(e.text, "");
    CHECK_STR(typed(&m, 9, 0), "\xd0\xb0");     /* а */
    CHECK_STR(typed(&m, 9, SHIFT), "\xd0\x90"); /* А */
    CHECK_INT(press(&m, 9, SHIFT).base, 0x430);
    CHECK_STR(typed(&m, 10, SHIFT), "\xce\x91"); /* Α, from α's own case */
    sk_x11_keymap_free(&m);
}

static void test_keysym_case(void)
{
    static const uint32_t pairs[][2] = {
        {'a', 'A'},       {0xe9, 0xc9},           {0xff, 0x13be},         {0x1b1, 0x1a1},
        {0x6c1, 0x6e1},   {0x7e1, 0x7c1},         {0x1000101, 0x1000100}, {0x10003c9, 0x10003a9},
        {0x1000456, 0x1000406},
    };
    for (size_t i = 0; i < SK_LEN(pairs); i++) {
        uint32_t lo, up;
        sk_x11_keysym_case(pairs[i][0], &lo, &up);
        CHECK_INT(lo, pairs[i][0]);
        CHECK_INT(up, pairs[i][1]);
        sk_x11_keysym_case(pairs[i][1], &lo, &up);
        CHECK_INT(lo, pairs[i][0]);
        CHECK_INT(up, pairs[i][1]);
    }
    uint32_t lo, up;
    sk_x11_keysym_case('1', &lo, &up);
    CHECK(lo == '1' && up == '1');
    sk_x11_keysym_case(0xffb5, &lo, &up);
    CHECK(lo == 0xffb5 && up == 0xffb5);
}

static void test_keysyms_to_unicode(void)
{
    static const uint32_t pairs[][2] = {
        {'A', 'A'},         {0xe9, 0xe9},       {0x01a1, 0x0104}, {0x06c1, 0x0430},
        {0x07d9, 0x03a9},   {0x20ac, 0x20ac},   {0x13a4, 0x20ac}, {0x0eff, 0x20a9},
        {0x0aa2, 0x2002},   {0x04a4, 0x3001},   {0x010020ac, 0x20ac}, {0x0101f600, 0x1f600},
        {0xffb5, '5'},      {0xff80, ' '},      {0xffbd, '='},    {0xff0d, 0},
        {0xfe51, 0},        {0x0100d800, 0},    {0x01110000, 0},  {0x08b1, 0},
        {0x1f, 0},
    };
    for (size_t i = 0; i < SK_LEN(pairs); i++)
        CHECK_INT(sk_x11_keysym_ucs(pairs[i][0]), pairs[i][1]);
}

const sk_test sk_suite_x11_keys[] = {
    T(test_modifier_roles),
    T(test_shift_levels_and_caps_lock),
    T(test_ctrl_alt_and_super_chords),
    T(test_altgr_level),
    T(test_mode_switch_group),
    T(test_keypad_and_num_lock),
    T(test_named_keys),
    T(test_dead_keys_and_other_scripts),
    T(test_keysym_case),
    T(test_keysyms_to_unicode),
    {0},
};
