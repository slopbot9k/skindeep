/* The question of platform/ask.h on a Linux desktop: zenity's radio
 * list, else kdialog's, run as a child the call waits for, the index of
 * the choice made read from its stdout. Plain POSIX, so it builds and
 * its tests run with a fake zenity on every system; platform/linux/
 * ask.c asks it for the app. */
#ifndef SK_X11_ASK_H
#define SK_X11_ASK_H

#include "../../base/strv.h"
#include "picker.h"

/* prog's arguments, its name first, each choice's index the tag it
 * prints, the first chosen to begin with:
 *   zenity --list --radiolist --title=T --text=MESSAGE\n\nDETAIL
 *          --column= --column= --column=Skin --hide-column=2
 *          --print-column=2 --hide-header --width=460 --height=280
 *          TRUE 0 C0 FALSE 1 C1 ...
 *   kdialog --title T --radiolist MESSAGE\n\nDETAIL 0 C0 on 1 C1 off ... */
void sk_ask_argv(sk_picker_prog prog, const char *title, const char *message, const char *detail,
                 const char *const *choices, size_t n, sk_strv *argv);
/* What n_out bytes of the child's stdout and its wait status say of n
 * choices: the index it printed, 0 for a cancel, a failure, or anything
 * else. */
int sk_ask_answer(const char *out, size_t n_out, int wstatus, size_t n);
/* The question asked with the first of zenity and kdialog on path, a
 * PATH value, waited for: the choice's index, or -1 when there is
 * neither or it could not run. */
int sk_ask_run(const char *path, const char *title, const char *message, const char *detail,
               const char *const *choices, size_t n);

#endif
