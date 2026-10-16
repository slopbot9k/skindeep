/* The folder picker of a Linux desktop (platform/picker.h): zenity,
 * else kdialog, run as a child on the core thread's loop, its answer
 * read from its stdout. Plain POSIX, so it builds, and its tests run
 * with a fake zenity, on every system; platform/linux/picker.c runs it
 * for the app. */
#ifndef SK_X11_PICKER_H
#define SK_X11_PICKER_H

#include "../../base/strv.h"
#include "../picker.h"

#define SK_PICKER_MISSING "no folder picker: install zenity or kdialog, or type the folder"

typedef enum { SK_PICKER_NONE, SK_PICKER_ZENITY, SK_PICKER_KDIALOG } sk_picker_prog;

/* The first of zenity and kdialog on path, a PATH value, with its file
 * malloc'd in *file. */
sk_picker_prog sk_picker_find(const char *path, char **file);
/* prog's arguments for o, its name first:
 *   zenity --file-selection --directory --title=T --filename=DIR/
 *   kdialog --getexistingdirectory DIR --title T */
void sk_picker_argv(sk_picker_prog prog, const sk_picker_opts *o, sk_strv *argv);
/* What n bytes of prog's stdout and its wait status say: a folder, in
 * *dir, malloc'd; a cancel, both NULL; or *err, malloc'd. */
void sk_picker_answer(sk_picker_prog prog, const char *out, size_t n, int wstatus, char **dir, char **err);

/* The answer on the loop's thread: dir and err as sk_picker_answer gives
 * them, the callee's to free. */
typedef void (*sk_picker_result)(void *ud, char *dir, char *err);

/* On loop's thread: the picker found on path run with o's arguments,
 * its stdout read and the child reaped by loop, then done; done at once
 * with SK_PICKER_MISSING when there is none, or why it could not run. */
void sk_picker_run(sk_loop *loop, const sk_picker_opts *o, const char *path, sk_picker_result done, void *ud);

#endif
