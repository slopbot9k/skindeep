/* platform/ask.h on Linux: zenity, else kdialog (platform/x11/ask.c),
 * waited for on the UI thread at launch. */
#include "../ask.h"

#include "../x11/ask.h"

#include <stdlib.h>

int sk_ask_choice(const char *title, const char *message, const char *detail, const char *const *choices,
                  size_t n)
{
    return sk_ask_run(getenv("PATH"), title, message, detail, choices, n);
}
