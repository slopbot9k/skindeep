/* Keys on macOS (platform/ui.h): virtual key codes to sk_key, an
 * NSEvent's characters to text, and the layout's unmodified character
 * to base. The virtual key codes are Carbon's kVK_ (HIToolbox's
 * Events.h), which name positions on the keyboard. */
#include "uimac.h"

#include "../../base/utf8.h"

#include <string.h>

uint32_t sk_macos_mods(unsigned long flags)
{
    uint32_t m = 0;
    if (flags & NSEventModifierFlagShift)
        m |= SK_MOD_SHIFT;
    if (flags & NSEventModifierFlagControl)
        m |= SK_MOD_CTRL;
    if (flags & NSEventModifierFlagOption)
        m |= SK_MOD_ALT;
    if (flags & NSEventModifierFlagCommand)
        m |= SK_MOD_SUPER;
    if (flags & NSEventModifierFlagCapsLock)
        m |= SK_MOD_CAPS;
    return m;
}

sk_key sk_macos_key(unsigned code)
{
    /* F1 to F20 by their codes, which follow no order. */
    static const unsigned char fkeys[20] = {
        0x7A, 0x78, 0x63, 0x76, 0x60, 0x61, 0x62, 0x64, 0x65, 0x6D,
        0x67, 0x6F, 0x69, 0x6B, 0x71, 0x6A, 0x40, 0x4F, 0x50, 0x5A,
    };
    /* The keypad's 0 to 9; 7 and 8 are not adjacent. */
    static const unsigned char kpdigits[10] = {0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5B, 0x5C};
    switch (code) {
    case 0x24: return SK_KEY_ENTER;
    case 0x30: return SK_KEY_TAB;
    case 0x33: return SK_KEY_BACKSPACE; /* kVK_Delete */
    case 0x35: return SK_KEY_ESCAPE;
    case 0x7E: return SK_KEY_UP;
    case 0x7D: return SK_KEY_DOWN;
    case 0x7B: return SK_KEY_LEFT;
    case 0x7C: return SK_KEY_RIGHT;
    case 0x73: return SK_KEY_HOME;
    case 0x77: return SK_KEY_END;
    case 0x74: return SK_KEY_PAGE_UP;
    case 0x79: return SK_KEY_PAGE_DOWN;
    case 0x72: return SK_KEY_INSERT; /* kVK_Help, where PC keyboards have Insert */
    case 0x75: return SK_KEY_DELETE; /* kVK_ForwardDelete */
    case 0x41: return SK_KEY_KP_DECIMAL;
    case 0x4B: return SK_KEY_KP_DIVIDE;
    case 0x43: return SK_KEY_KP_MULTIPLY;
    case 0x4E: return SK_KEY_KP_SUBTRACT;
    case 0x45: return SK_KEY_KP_ADD;
    case 0x4C: return SK_KEY_KP_ENTER;
    case 0x51: return SK_KEY_KP_EQUAL;
    case 0x6E: return SK_KEY_MENU; /* kVK_ContextualMenu */
    default: break;
    }
    for (int i = 0; i < 20; i++)
        if (fkeys[i] == code)
            return (sk_key)(SK_KEY_F1 + i);
    for (int i = 0; i < 10; i++)
        if (kpdigits[i] == code)
            return (sk_key)(SK_KEY_KP_0 + i);
    return SK_KEY_NONE;
}

/* Not text: the C0 and C1 controls, DEL, and the private-use range
 * where AppKit puts its function keys (NSUpArrowFunctionKey and on). */
static bool is_text(uint32_t cp)
{
    return cp >= 0x20 && !(cp >= 0x7F && cp < 0xA0) && !(cp >= 0xF700 && cp <= 0xF8FF);
}

/* The text characters of s, as UTF-8, as many as fit. */
static void text_of(id s, char out[16])
{
    size_t len = 0;
    const char *u = sk_utf8(s);
    size_t n = u ? strlen(u) : 0;
    for (size_t i = 0; i < n;) {
        uint32_t cp;
        i += sk_utf8_decode(u + i, n - i, &cp, NULL);
        if (!is_text(cp))
            continue;
        char enc[4];
        size_t k = sk_utf8_encode(cp, enc);
        if (len + k >= 16)
            break;
        memcpy(out + len, enc, k);
        len += k;
    }
    out[len] = 0;
}

/* The characters the event's key gives on the current layout with only
 * the modifiers in flags held (macOS 10.15 and later), or nil. */
static id characters_with(id ev, unsigned long flags)
{
    if (!sk_responds(ev, "charactersByApplyingModifiers:"))
        return nil;
    return SK_MSG(id (*)(id, SEL, unsigned long))(ev, sk_sel("charactersByApplyingModifiers:"), flags);
}

void sk_macos_key_event(id ev, sk_key_event *out)
{
    memset(out, 0, sizeof *out);
    unsigned long flags = sk_msg_ulong(ev, "modifierFlags");
    unsigned code = (unsigned)SK_MSG(unsigned short (*)(id, SEL))(ev, sk_sel("keyCode"));
    out->code = code;
    out->mods = sk_macos_mods(flags);
    out->repeat = sk_msg_bool(ev, "isARepeat");
    out->key = sk_macos_key(code);
    if (out->key != SK_KEY_NONE)
        return;

    /* Ctrl and Cmd turn a key's characters into controls or nothing;
     * the text is what the layout gives without them. */
    id chars = nil;
    if (flags & (NSEventModifierFlagControl | NSEventModifierFlagCommand))
        chars = characters_with(ev, flags & (NSEventModifierFlagShift | NSEventModifierFlagOption |
                                             NSEventModifierFlagCapsLock));
    if (!chars)
        chars = sk_msg(ev, "characters");
    text_of(chars, out->text);

    id plain = characters_with(ev, 0);
    if (!plain)
        plain = sk_msg(ev, "charactersIgnoringModifiers");
    char base[16];
    text_of(sk_msg(plain, "lowercaseString"), base);
    if (base[0])
        sk_utf8_decode(base, strlen(base), &out->base, NULL);
}
