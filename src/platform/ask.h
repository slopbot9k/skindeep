/* Questions the app asks with the system's own dialogs, as main.js asks
 * them with dialog.showMessageBox: on macOS an NSAlert; on Linux zenity,
 * else kdialog (platform/x11/ask.h). */
#ifndef SK_PLATFORM_ASK_H
#define SK_PLATFORM_ASK_H

#include "../core/loop.h"
#include "ui.h"

#include <stddef.h>

/* Which skin a launch opens when windows recorded different ones at once
 * (decision 0022): one button a choice, the first the default and what a
 * cancel takes. On the UI thread, after sk_ui_open, before any window:
 * it blocks. The index of the choice made, 0 on a cancel; -1 when this
 * system has no way to ask, and the caller takes the first. */
int sk_ask_choice(const char *title, const char *message, const char *detail, const char *const *choices,
                  size_t n);

typedef struct {
    const char *title, *message, *detail;
    const char *ok, *cancel; /* the two buttons' labels, ok the default */
} sk_ask_question;

/* The answer, on the UI thread: true for ok. */
typedef void (*sk_ask_answered)(void *ud, bool ok);

/* q asked beside parent without waiting, as main.js asks Close N tabs?
 * (decision 0023): on macOS a sheet on parent, or an alert of its own
 * when parent is not on screen; on Linux a child run on loop's thread,
 * so no window stops drawing meanwhile. done comes later on the UI
 * thread; with no way to ask it comes with true, so what the user asked
 * for goes ahead, and a question asked while one is open is answered
 * false. The strings are copied. */
void sk_ask_confirm(sk_window *parent, sk_loop *loop, const sk_ask_question *q, sk_ask_answered done, void *ud);

#endif
