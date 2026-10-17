/* The question of platform/ask.h on a Linux desktop: zenity's radio
 * list, else kdialog's, run as a child the call waits for, the index of
 * the choice made read from its stdout. Plain POSIX, so it builds and
 * its tests run with a fake zenity on every system; platform/linux/
 * ask.c asks it for the app. */
#ifndef SK_X11_ASK_H
#define SK_X11_ASK_H

#include "../../base/strv.h"
#include "../ask.h"
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
/* zenity's --text is Pango markup: s with &, < and > escaped, into
 * out. */
void sk_ask_markup(sk_buf *out, const char *s);
/* The question asked with the first of zenity and kdialog on path, a
 * PATH value, waited for: the choice's index, or -1 when there is
 * neither or it could not run. */
int sk_ask_run(const char *path, const char *title, const char *message, const char *detail,
               const char *const *choices, size_t n);


/* The two-button question of sk_ask_confirm, prog's arguments:
 *   zenity --question --title=T --text=MESSAGE\n\nDETAIL --ok-label=OK
 *          --cancel-label=CANCEL
 *   kdialog --title T --yes-label OK --no-label CANCEL --yesno TEXT */
void sk_ask_confirm_argv(sk_picker_prog prog, const sk_ask_question *q, sk_strv *argv);
/* What its wait status says: true for ok, which exits 0. */
bool sk_ask_confirm_answer(int wstatus);
/* 1 for ok, 0 for cancel, -1 when there was no way to ask. */
typedef void (*sk_ask_ran)(void *ud, int answer);
/* On loop's thread: q asked with the first of zenity and kdialog on
 * path, the child reaped by loop, then done; done at once with -1 when
 * there is neither or it cannot run. */
void sk_ask_confirm_run(sk_loop *loop, const char *path, const sk_ask_question *q, sk_ask_ran done, void *ud);

#endif
