/* The core protocol's keyboard: GetKeyboardMapping's keysyms and
 * GetModifierMapping's modifiers, read by the protocol's rules for
 * groups, shift levels, Caps Lock and the keypad, into platform/ui.h's
 * keys and text. There is no XKB: AltGr's level is found where an XKB
 * server's core map puts it, after the two groups. */
#ifndef SK_X11_KEYS_H
#define SK_X11_KEYS_H

#include "../ui.h"
#include "x11.h"

/* Keysyms the rules name. */
enum {
    SK_XK_ISO_LEVEL3_SHIFT = 0xfe03,
    SK_XK_ISO_LEFT_TAB = 0xfe20,
    SK_XK_MODE_SWITCH = 0xff7e,
    SK_XK_NUM_LOCK = 0xff7f,
    SK_XK_CAPS_LOCK = 0xffe5,
    SK_XK_SHIFT_LOCK = 0xffe6,
};

typedef struct {
    uint8_t min, max; /* keycodes */
    int per;          /* keysyms per keycode */
    uint32_t *syms;   /* (max - min + 1) * per */
    /* Masks of the state field: Mode_switch's (the group), the AltGr
     * level's (ISO_Level3_Shift), NumLock's, Alt's and Super's. */
    uint16_t group, level3, num_lock, alt, super;
    bool caps_lock, shift_lock; /* what Lock means */
} sk_x11_keymap;

/* count * per keysyms from keycode min, and GetModifierMapping's 8 *
 * per_mod keycodes, Shift's first. Replaces what m held. */
void sk_x11_keymap_set(sk_x11_keymap *m, uint8_t min, int count, int per, const uint32_t *syms,
                       int per_mod, const uint8_t *mods);
void sk_x11_keymap_free(sk_x11_keymap *m);
/* Both mappings from the server, in one round trip. */
bool sk_x11_keymap_load(sk_x11 *c, sk_x11_keymap *m);
/* The first keycode whose first keysym is sym, 0 for none. */
uint8_t sk_x11_keymap_find(const sk_x11_keymap *m, uint32_t sym);

/* The keysym keycode gives with state's modifiers, 0 for none. */
uint32_t sk_x11_keysym(const sk_x11_keymap *m, uint8_t keycode, uint16_t state);
/* state's modifiers as SK_MOD_* bits. */
uint32_t sk_x11_mods(const sk_x11_keymap *m, uint16_t state);
/* A KeyPress or KeyRelease as platform/ui.h has it. The keypad's
 * character keys are named and carry their character as text too. */
void sk_x11_key_event(const sk_x11_keymap *m, uint8_t keycode, uint16_t state, sk_key_event *out);

sk_key sk_x11_named_key(uint32_t sym);
/* The character a keysym stands for, 0 for none: Latin-1, Unicode
 * keysyms, the keypad, and Markus Kuhn's table for the rest. */
uint32_t sk_x11_keysym_ucs(uint32_t sym);
/* sym's lower and upper case forms, both sym when it has none. */
void sk_x11_keysym_case(uint32_t sym, uint32_t *lower, uint32_t *upper);

#endif
