/* platform/picker.h on macOS: an NSOpenPanel that chooses one folder, as
 * Electron's dialog.showOpenDialog makes one, run as a sheet on the
 * window, or as a panel of its own when the window is not on screen.
 * Either way it is begun with a completion handler and the UI thread's
 * run loop goes on: no modal loop runs inside the app's handlers.
 *
 * The handler is a block. C has none, so it is laid out here as clang
 * lays one out (the Blocks ABI): a global block, which AppKit's copy
 * and release leave as it is, answering the one picker open. */
#include "../picker.h"

#include "uimac.h"

#include <string.h>

/* NSModalResponseOK. */
#define RESPONSE_OK 1

typedef struct {
    unsigned long reserved, size;
    const char *signature;
} block_descriptor;

typedef struct {
    void *isa;
    int flags, reserved;
    void (*invoke)(void *self, long response);
    const block_descriptor *descriptor;
} block;

/* libSystem's class of global blocks. */
extern void *_NSConcreteGlobalBlock[32];

enum { BLOCK_IS_GLOBAL = 1 << 28, BLOCK_HAS_SIGNATURE = 1 << 30 };

/* The picker open: its panel, retained, and whom it answers. */
static struct {
    bool open;
    id panel;
    sk_picker_done done;
    void *ud;
} pending;

typedef struct {
    sk_picker_done done;
    void *ud;
    char *dir;
    const char *err;
    bool refused; /* asked while another was open */
} answer;

/* After the handler has returned, so AppKit is done with the panel. */
static void deliver(void *p)
{
    answer a = *(answer *)p;
    free(p);
    if (!a.refused) {
        sk_release(pending.panel);
        pending.panel = nil;
        pending.open = false;
    }
    a.done(a.ud, a.dir, a.err);
    free(a.dir);
}

static void post(sk_picker_done done, void *ud, char *dir, const char *err, bool refused)
{
    answer *a = sk_xmalloc(sizeof *a);
    *a = (answer){done, ud, dir, err, refused};
    sk_ui_post(deliver, a);
}

static void completed(void *self, long response)
{
    (void)self;
    char *dir = NULL;
    if (response == RESPONSE_OK && pending.panel) {
        id url = sk_msg(pending.panel, "URL");
        const char *p = url ? sk_utf8(sk_msg(url, "path")) : NULL;
        if (p && *p)
            dir = sk_xstrdup(p);
    }
    post(pending.done, pending.ud, dir, NULL, false);
}

static const block_descriptor descriptor = {0, sizeof(block), "v16@?0q8"};
static block handler = {_NSConcreteGlobalBlock, BLOCK_IS_GLOBAL | BLOCK_HAS_SIGNATURE, 0, completed, &descriptor};

id sk_macos_folder_panel(const sk_picker_opts *o)
{
    id panel = sk_msg(sk_cls("NSOpenPanel"), "openPanel");
    sk_msg_void_bool(panel, "setCanChooseDirectories:", YES);
    sk_msg_void_bool(panel, "setCanChooseFiles:", NO);
    sk_msg_void_bool(panel, "setAllowsMultipleSelection:", NO);
    sk_msg_void_bool(panel, "setCanCreateDirectories:", NO);
    sk_msg_void_bool(panel, "setShowsHiddenFiles:", o->hidden ? YES : NO);
    if (o->title) {
        /* A sheet shows no title, so the message says it too. */
        sk_msg_void_id(panel, "setTitle:", sk_nsstring(o->title));
        sk_msg_void_id(panel, "setMessage:", sk_nsstring(o->title));
    }
    if (o->start && *o->start) {
        id url = SK_MSG(id (*)(id, SEL, id, BOOL))(sk_cls("NSURL"), sk_sel("fileURLWithPath:isDirectory:"),
                                                   sk_nsstring(o->start), YES);
        sk_msg_void_id(panel, "setDirectoryURL:", url);
    }
    return panel;
}

void sk_picker_folder(sk_window *parent, sk_loop *loop, const sk_picker_opts *o, sk_picker_done done, void *ud)
{
    (void)loop;
    if (pending.open) {
        post(done, ud, NULL, "a folder picker is open already", true);
        return;
    }
    void *pool = objc_autoreleasePoolPush();
    pending.open = true;
    pending.panel = sk_retain(sk_macos_folder_panel(o));
    pending.done = done;
    pending.ud = ud;
    id win = parent ? sk_macos_nswindow(parent) : nil;
    if (win && sk_msg_bool(win, "isVisible"))
        SK_MSG(void (*)(id, SEL, id, void *))(pending.panel, sk_sel("beginSheetModalForWindow:completionHandler:"),
                                              win, &handler);
    else
        SK_MSG(void (*)(id, SEL, void *))(pending.panel, sk_sel("beginWithCompletionHandler:"), &handler);
    objc_autoreleasePoolPop(pool);
}
