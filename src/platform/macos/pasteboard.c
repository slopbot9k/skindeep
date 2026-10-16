/* The clipboard of platform/ui.h on macOS: the general pasteboard's
 * plain text. macOS has no PRIMARY selection. The text is read when
 * asked and handed over on a later turn of the UI thread, as X11's
 * answer comes. */
#include "uimac.h"

#include <dispatch/dispatch.h>

static id pasteboard(void)
{
    return sk_msg(sk_cls("NSPasteboard"), "generalPasteboard");
}

void sk_clipboard_set(const char *utf8)
{
    void *pool = objc_autoreleasePoolPush();
    id pb = pasteboard();
    sk_msg_long(pb, "clearContents");
    SK_MSG(BOOL (*)(id, SEL, id, id))(pb, sk_sel("setString:forType:"), sk_nsstring(utf8), NSPasteboardTypeString);
    objc_autoreleasePoolPop(pool);
}

void sk_primary_set(const char *utf8)
{
    (void)utf8;
}

typedef struct {
    void (*done)(void *ud, const char *utf8);
    void *ud;
    char *text;
} answer;

static void deliver(void *p)
{
    answer *a = p;
    void *pool = objc_autoreleasePoolPush();
    a->done(a->ud, a->text);
    objc_autoreleasePoolPop(pool);
    free(a->text);
    free(a);
}

void sk_clipboard_get(bool primary, void (*done)(void *ud, const char *utf8), void *ud)
{
    answer *a = sk_xcalloc(1, sizeof *a);
    a->done = done;
    a->ud = ud;
    if (!primary) {
        void *pool = objc_autoreleasePoolPush();
        const char *s = sk_utf8(sk_msg_id(pasteboard(), "stringForType:", NSPasteboardTypeString));
        a->text = s ? sk_xstrdup(s) : NULL;
        objc_autoreleasePoolPop(pool);
    }
    dispatch_async_f(dispatch_get_main_queue(), a, deliver);
}
