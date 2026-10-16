/* What the files of the macOS window layer share, and the hooks its
 * tests use. */
#ifndef SK_MACOS_UIMAC_H
#define SK_MACOS_UIMAC_H

#include "../ui.h"
#include "objc.h"

/* keys.c: the SK_MOD_ bits of NSEventModifierFlags. */
uint32_t sk_macos_mods(unsigned long flags);
/* The named key of a virtual key code (Carbon's kVK_), or SK_KEY_NONE. */
sk_key sk_macos_key(unsigned code);
/* An NSEvent of type KeyDown or KeyUp as ui.h has it. */
void sk_macos_key_event(id ev, sk_key_event *out);

/* For tests: the window's NSWindow, which events are sent through. */
id sk_macos_nswindow(sk_window *w);
/* For tests: the window as though its backing scale were scale, with the
 * SK_EV_RESIZE a real change brings; 0 goes back to the real one. */
void sk_macos_force_scale(sk_window *w, double scale);

#endif
