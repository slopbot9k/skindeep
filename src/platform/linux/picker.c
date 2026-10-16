/* platform/picker.h on Linux: zenity, else kdialog (platform/x11/
 * picker.c), spawned and reaped on the core thread, its answer posted to
 * the UI thread. */
#include "../picker.h"

#include "../x11/picker.h"

typedef struct {
    sk_loop *loop;
    char *title, *start, *path;
    sk_picker_opts opts;
    sk_picker_done done;
    void *ud;
    char *dir, *err;
    bool refused; /* asked while another was open */
} request;

/* The UI thread's: whether a picker is open. */
static bool busy;

static char *dup_or_null(const char *s)
{
    return s ? sk_xstrdup(s) : NULL;
}

static void deliver(void *p)
{
    request *q = p;
    if (!q->refused)
        busy = false;
    q->done(q->ud, q->dir, q->err);
    free(q->title);
    free(q->start);
    free(q->path);
    free(q->dir);
    free(q->err);
    free(q);
}

static void answered(void *ud, char *dir, char *err)
{
    request *q = ud;
    q->dir = dir;
    q->err = err;
    sk_ui_post(deliver, q);
}

static void run(void *p)
{
    request *q = p;
    sk_picker_run(q->loop, &q->opts, q->path, answered, q);
}

void sk_picker_folder(sk_window *parent, sk_loop *loop, const sk_picker_opts *o, sk_picker_done done, void *ud)
{
    (void)parent;
    request *q = sk_xcalloc(1, sizeof *q);
    q->loop = loop;
    q->title = dup_or_null(o->title);
    q->start = dup_or_null(o->start);
    q->path = dup_or_null(getenv("PATH"));
    q->opts = (sk_picker_opts){q->title, q->start, o->hidden};
    q->done = done;
    q->ud = ud;
    if (busy) {
        q->refused = true;
        q->err = sk_xstrdup("a folder picker is open already");
        sk_ui_post(deliver, q);
        return;
    }
    busy = true;
    sk_loop_post(loop, run, q);
}
