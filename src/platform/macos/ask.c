/* platform/ask.h on macOS: an NSAlert, as Electron's dialog.showMessageBox
 * makes one with no window, a button a choice, the first the default.
 * At launch, before any window, so it runs modal: the app is brought
 * forward first, as it is not yet. */
#include "../ask.h"

#include "uimac.h"

/* NSAlertFirstButtonReturn; NSAlertStyleInformational. */
#define FIRST_BUTTON 1000
#define STYLE_INFORMATIONAL 1

int sk_ask_choice(const char *title, const char *message, const char *detail, const char *const *choices,
                  size_t n)
{
    void *pool = objc_autoreleasePoolPush();
    id alert = sk_new("NSAlert");
    if (!alert) {
        objc_autoreleasePoolPop(pool);
        return -1;
    }
    sk_msg_void_long(alert, "setAlertStyle:", STYLE_INFORMATIONAL);
    sk_msg_void_id(alert, "setMessageText:", sk_nsstring(message));
    sk_msg_void_id(alert, "setInformativeText:", sk_nsstring(detail));
    sk_msg_void_id(sk_msg(alert, "window"), "setTitle:", sk_nsstring(title));
    for (size_t i = 0; i < n; i++)
        sk_msg_id(alert, "addButtonWithTitle:", sk_nsstring(choices[i]));
    id app = sk_msg(sk_cls("NSApplication"), "sharedApplication");
    sk_msg_void_bool(app, "activateIgnoringOtherApps:", YES);
    long r = sk_msg_long(alert, "runModal") - FIRST_BUTTON;
    sk_release(alert);
    objc_autoreleasePoolPop(pool);
    return r >= 0 && (size_t)r < n ? (int)r : 0;
}
