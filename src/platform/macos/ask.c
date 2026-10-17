/* platform/ask.h on macOS: an NSAlert, as Electron's dialog.showMessageBox
 * makes one. The launch's question, before any window, runs modal, the
 * app brought forward first, as it is not yet; a confirm is a sheet on
 * its window, its answer a completion handler (uimac.h's block). */
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

/* The question open: its alert, retained, and whom it answers. */
static struct {
    bool open;
    id alert;
    sk_ask_answered done;
    void *ud;
} asking;

typedef struct {
    sk_ask_answered done;
    void *ud;
    bool yes, refused; /* asked while another was open */
} reply;

/* After the handler has returned, so AppKit is done with the alert. */
static void deliver(void *p)
{
    reply r = *(reply *)p;
    free(p);
    if (!r.refused) {
        sk_release(asking.alert);
        asking.alert = nil;
        asking.open = false;
    }
    r.done(r.ud, r.yes);
}

static void post_reply(sk_ask_answered done, void *ud, bool yes, bool refused)
{
    reply *r = sk_xmalloc(sizeof *r);
    *r = (reply){done, ud, yes, refused};
    sk_ui_post_owned(deliver, r, free);
}

static void answered(void *self, long response)
{
    (void)self;
    post_reply(asking.done, asking.ud, response == FIRST_BUTTON, false);
}

SK_MACOS_BLOCK(handler, answered);

void sk_ask_confirm(sk_window *parent, sk_loop *loop, const sk_ask_question *q, sk_ask_answered done, void *ud)
{
    (void)loop;
    if (asking.open) {
        post_reply(done, ud, false, true);
        return;
    }
    void *pool = objc_autoreleasePoolPush();
    id alert = sk_new("NSAlert");
    if (!alert) {
        objc_autoreleasePoolPop(pool);
        post_reply(done, ud, true, true);
        return;
    }
    sk_msg_void_long(alert, "setAlertStyle:", STYLE_INFORMATIONAL);
    sk_msg_void_id(alert, "setMessageText:", sk_nsstring(q->message ? q->message : ""));
    sk_msg_void_id(alert, "setInformativeText:", sk_nsstring(q->detail ? q->detail : ""));
    sk_msg_id(alert, "addButtonWithTitle:", sk_nsstring(q->ok ? q->ok : "OK"));
    sk_msg_id(alert, "addButtonWithTitle:", sk_nsstring(q->cancel ? q->cancel : "Cancel"));
    asking.open = true;
    asking.alert = alert;
    asking.done = done;
    asking.ud = ud;
    id win = parent ? sk_macos_nswindow(parent) : nil;
    if (win && sk_msg_bool(win, "isVisible")) {
        SK_MSG(void (*)(id, SEL, id, void *))(alert, sk_sel("beginSheetModalForWindow:completionHandler:"), win,
                                              &handler);
    } else {
        sk_msg_void_id(sk_msg(alert, "window"), "setTitle:", sk_nsstring(q->title ? q->title : ""));
        answered(NULL, sk_msg_long(alert, "runModal"));
    }
    objc_autoreleasePoolPop(pool);
}
