/* The folder picker of New tab... (decision 0025): the system's own
 * dialog for choosing a folder, asked on the UI thread and answered on
 * it. On macOS, an NSOpenPanel, a sheet on the window. On Linux, zenity,
 * else kdialog, as a child the core thread spawns, reads and reaps
 * (platform/x11/picker.h). */
#ifndef SK_PLATFORM_PICKER_H
#define SK_PLATFORM_PICKER_H

#include "../core/loop.h"
#include "ui.h"

typedef struct {
    const char *title; /* "Folder", "Other profile" */
    const char *start; /* the folder it opens in; NULL: the system's choice */
    /* Hidden folders shown. GTK's and KDE's dialogs take no such option:
     * there the user shows them with the dialog's own toggle. */
    bool hidden;
} sk_picker_opts;

/* The answer: dir, the folder chosen; or dir and err both NULL when the
 * user cancelled; or err, why there is no picker. Valid during the
 * call. */
typedef void (*sk_picker_done)(void *ud, const char *dir, const char *err);

/* On the UI thread. parent: the window the picker belongs to, or NULL.
 * loop: the core thread's, which runs Linux's child. done comes once, on
 * the UI thread, after the call has returned. One picker is open at a
 * time: another asked meanwhile is answered with an error. */
void sk_picker_folder(sk_window *parent, sk_loop *loop, const sk_picker_opts *o, sk_picker_done done, void *ud);

#endif
