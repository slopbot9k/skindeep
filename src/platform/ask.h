/* A question the app asks before its first window opens (decision 0022):
 * which skin, when windows recorded different ones at once, as main.js
 * asks it with dialog.showMessageBox, one button a choice, the first the
 * default and what a cancel takes. On macOS an NSAlert; on Linux zenity,
 * else kdialog, as a child the call waits for (platform/x11/ask.h). On
 * the UI thread, after sk_ui_open, before any window: it blocks. */
#ifndef SK_PLATFORM_ASK_H
#define SK_PLATFORM_ASK_H

#include <stddef.h>

/* The index of the choice made, 0 on a cancel; -1 when this system has
 * no way to ask, and the caller takes the first. */
int sk_ask_choice(const char *title, const char *message, const char *detail, const char *const *choices,
                  size_t n);

#endif
