#include "keys.h"

#include "../../base/utf8.h"

#include <string.h>

enum {
    SHIFT = 1 << 0,
    LOCK = 1 << 1,
    CONTROL = 1 << 2,
};

void sk_x11_keymap_free(sk_x11_keymap *m)
{
    free(m->syms);
    memset(m, 0, sizeof *m);
}

void sk_x11_keymap_set(sk_x11_keymap *m, uint8_t min, int count, int per, const uint32_t *syms,
                       int per_mod, const uint8_t *mods)
{
    sk_x11_keymap_free(m);
    if (count <= 0 || per <= 0 || min + count - 1 > 255)
        return;
    m->min = min;
    m->max = (uint8_t)(min + count - 1);
    m->per = per;
    m->syms = sk_xreallocarray(NULL, (size_t)count * (size_t)per, sizeof *m->syms);
    memcpy(m->syms, syms, (size_t)count * (size_t)per * sizeof *m->syms);
    uint16_t meta = 0;
    for (int i = 0; i < 8; i++) {
        uint16_t bit = (uint16_t)(1u << i);
        for (int j = 0; j < per_mod; j++) {
            uint8_t kc = mods[i * per_mod + j];
            if (kc < m->min || kc > m->max)
                continue;
            const uint32_t *row = m->syms + (size_t)(kc - m->min) * (size_t)per;
            for (int k = 0; k < per; k++) {
                uint32_t s = row[k];
                if (i == 1) {
                    m->caps_lock |= s == SK_XK_CAPS_LOCK;
                    m->shift_lock |= s == SK_XK_SHIFT_LOCK;
                }
                if (i < 3)
                    continue; /* the group, NumLock, Alt and Super sit on Mod1 to Mod5 */
                if (s == SK_XK_MODE_SWITCH)
                    m->group |= bit;
                else if (s == SK_XK_ISO_LEVEL3_SHIFT)
                    m->level3 |= bit;
                else if (s == SK_XK_NUM_LOCK)
                    m->num_lock |= bit;
                else if (s == 0xffe9 || s == 0xffea) /* Alt_L, Alt_R */
                    m->alt |= bit;
                else if (s == 0xffe7 || s == 0xffe8) /* Meta_L, Meta_R */
                    meta |= bit;
                else if (s == 0xffeb || s == 0xffec) /* Super_L, Super_R */
                    m->super |= bit;
            }
        }
    }
    /* Meta stands in for Alt only where no key is Alt. */
    if (!m->alt)
        m->alt = meta;
}

bool sk_x11_keymap_load(sk_x11 *c, sk_x11_keymap *m)
{
    int count = c->max_keycode - c->min_keycode + 1;
    if (count <= 0)
        return false;
    uint64_t ks = sk_x11_get_keyboard_mapping(c, c->min_keycode, (uint8_t)count);
    uint64_t ms = sk_x11_get_modifier_mapping(c);
    sk_x11_reply a, b;
    uint8_t e;
    bool ok = sk_x11_reply_wait(c, ks, &a, &e);
    ok = sk_x11_reply_wait(c, ms, &b, &e) && ok;
    int per = ok ? a.data[1] : 0, per_mod = ok ? b.data[1] : 0;
    ok = ok && per > 0 && a.len >= 32 + (size_t)count * (size_t)per * 4 &&
         b.len >= 32 + (size_t)per_mod * 8;
    if (ok) {
        size_t n = (size_t)count * (size_t)per;
        uint32_t *syms = sk_xreallocarray(NULL, n, sizeof *syms);
        for (size_t i = 0; i < n; i++)
            syms[i] = sk_x11_get32(a.data + 32 + 4 * i);
        sk_x11_keymap_set(m, c->min_keycode, count, per, syms, per_mod, b.data + 32);
        free(syms);
    }
    sk_x11_reply_free(&a);
    sk_x11_reply_free(&b);
    return ok;
}

uint8_t sk_x11_keymap_find(const sk_x11_keymap *m, uint32_t sym)
{
    for (int kc = m->min; m->syms && kc <= m->max; kc++)
        if (m->syms[(size_t)(kc - m->min) * (size_t)m->per] == sym)
            return (uint8_t)kc;
    return 0;
}

static bool keypad(uint32_t sym)
{
    return (sym >= 0xff80 && sym <= 0xffbd) || (sym >= 0x11000000 && sym <= 0x1100ffff);
}

static uint32_t upper(uint32_t sym)
{
    uint32_t lo, up;
    sk_x11_keysym_case(sym, &lo, &up);
    return up;
}

uint32_t sk_x11_keysym(const sk_x11_keymap *m, uint8_t keycode, uint16_t state)
{
    if (!m->syms || keycode < m->min || keycode > m->max)
        return 0;
    const uint32_t *row = m->syms + (size_t)(keycode - m->min) * (size_t)m->per;
    uint32_t k[8] = {0};
    int n = m->per;
    while (n > 0 && row[n - 1] == 0)
        n--;
    memcpy(k, row, (size_t)SK_MIN(n, 8) * sizeof *k);
    /* A list of one keysym K reads as K NoSymbol K NoSymbol, of two K1 K2
     * as K1 K2 K1 K2. */
    if (n == 1) {
        k[2] = k[0];
    } else if (n == 2) {
        k[2] = k[0];
        k[3] = k[1];
    }
    /* Group 2 when Mode_switch is held and the key has one of its own: an
     * XKB server's single-group key repeats group 1 there, and may report
     * AltGr on Mode_switch's modifier too. AltGr's level of the group
     * follows the two groups, where XKB's core map puts it. */
    bool own_group2 = (k[2] || k[3]) && (k[2] != k[0] || k[3] != k[1]);
    int at = (state & m->group) && own_group2 ? 2 : 0;
    if ((state & m->level3) && k[4 + at])
        at += 4;
    uint32_t a = k[at], b = k[at + 1];
    /* A group whose second keysym is NoSymbol: the first, or its two
     * cases when it has both. */
    if (!b) {
        uint32_t lo, up;
        sk_x11_keysym_case(a, &lo, &up);
        if (lo != up) {
            a = lo;
            b = up;
        } else {
            b = a;
        }
    }
    bool shift = state & SHIFT, lock = state & LOCK;
    bool caps = lock && m->caps_lock, shift_lock = lock && !m->caps_lock && m->shift_lock;
    if ((state & m->num_lock) && keypad(b))
        return shift || shift_lock ? a : b;
    if (!shift && !lock)
        return a;
    if (!shift && caps)
        return upper(a);
    if (shift && caps)
        return upper(b);
    return shift || shift_lock ? b : a;
}

uint32_t sk_x11_mods(const sk_x11_keymap *m, uint16_t state)
{
    uint32_t mods = 0;
    if (state & SHIFT)
        mods |= SK_MOD_SHIFT;
    if (state & CONTROL)
        mods |= SK_MOD_CTRL;
    if (state & LOCK)
        mods |= SK_MOD_CAPS;
    if (state & m->alt)
        mods |= SK_MOD_ALT;
    if (state & m->super)
        mods |= SK_MOD_SUPER;
    return mods;
}

sk_key sk_x11_named_key(uint32_t sym)
{
    if (sym >= 0xffbe && sym <= 0xffd5)
        return (sk_key)(SK_KEY_F1 + (int)(sym - 0xffbe));
    if (sym >= 0xffb0 && sym <= 0xffb9)
        return (sk_key)(SK_KEY_KP_0 + (int)(sym - 0xffb0));
    switch (sym) {
    case 0xff0d:
        return SK_KEY_ENTER;
    case 0xff09:
    case 0xff89: /* KP_Tab */
    case SK_XK_ISO_LEFT_TAB:
        return SK_KEY_TAB;
    case 0xff08:
        return SK_KEY_BACKSPACE;
    case 0xff1b:
        return SK_KEY_ESCAPE;
    case 0xff52:
    case 0xff97:
        return SK_KEY_UP;
    case 0xff54:
    case 0xff99:
        return SK_KEY_DOWN;
    case 0xff51:
    case 0xff96:
        return SK_KEY_LEFT;
    case 0xff53:
    case 0xff98:
        return SK_KEY_RIGHT;
    case 0xff50:
    case 0xff95:
        return SK_KEY_HOME;
    case 0xff57:
    case 0xff9c:
        return SK_KEY_END;
    case 0xff55:
    case 0xff9a:
        return SK_KEY_PAGE_UP;
    case 0xff56:
    case 0xff9b:
        return SK_KEY_PAGE_DOWN;
    case 0xff63:
    case 0xff9e:
        return SK_KEY_INSERT;
    case 0xffff:
    case 0xff9f:
        return SK_KEY_DELETE;
    case 0xffae: /* KP_Decimal */
    case 0xffac: /* KP_Separator, the decimal comma */
        return SK_KEY_KP_DECIMAL;
    case 0xffaf:
        return SK_KEY_KP_DIVIDE;
    case 0xffaa:
        return SK_KEY_KP_MULTIPLY;
    case 0xffad:
        return SK_KEY_KP_SUBTRACT;
    case 0xffab:
        return SK_KEY_KP_ADD;
    case 0xff8d:
        return SK_KEY_KP_ENTER;
    case 0xffbd:
        return SK_KEY_KP_EQUAL;
    case 0xff67:
        return SK_KEY_MENU;
    default:
        return SK_KEY_NONE;
    }
}

static bool printable(uint32_t u)
{
    return u >= 0x20 && u != 0x7f && !(u >= 0x80 && u < 0xa0);
}

void sk_x11_key_event(const sk_x11_keymap *m, uint8_t keycode, uint16_t state, sk_key_event *out)
{
    memset(out, 0, sizeof *out);
    out->code = keycode;
    out->mods = sk_x11_mods(m, state);
    uint32_t sym = sk_x11_keysym(m, keycode, state);
    out->key = sk_x11_named_key(sym);
    uint32_t u = sk_x11_keysym_ucs(sym);
    if (printable(u))
        out->text[sk_utf8_encode(u, out->text)] = 0;
    if (out->key == SK_KEY_NONE) {
        uint32_t lo, up;
        sk_x11_keysym_case(sk_x11_keysym(m, keycode, 0), &lo, &up);
        u = sk_x11_keysym_ucs(lo);
        out->base = printable(u) ? u : 0;
    }
}
