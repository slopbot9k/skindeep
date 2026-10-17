/* platform/ask.h on Linux: zenity, else kdialog (platform/x11/ask.c).
 * The launch's question is waited for on the UI thread; a confirm runs
 * its child on the core thread's loop and posts the answer back. */
#include "../ask.h"

#include "../x11/ask.h"

#include <stdlib.h>

int sk_ask_choice(const char *title, const char *message, const char *detail, const char *const *choices,
                  size_t n)
{
    return sk_ask_run(getenv("PATH"), title, message, detail, choices, n);
}

typedef struct {
    sk_loop *loop;
    char *title, *message, *detail, *ok, *cancel, *path;
    sk_ask_answered done;
    void *ud;
    bool yes, refused; /* asked while another was open */
} request;

/* The UI thread's: whether a question is open. */
static bool busy;

static char *dup_or_empty(const char *s)
{
    return sk_xstrdup(s ? s : "");
}

static void request_free(void *p)
{
    request *q = p;
    free(q->title);
    free(q->message);
    free(q->detail);
    free(q->ok);
    free(q->cancel);
    free(q->path);
    free(q);
}

static void deliver(void *p)
{
    request *q = p;
    if (!q->refused)
        busy = false;
    q->done(q->ud, q->yes);
    request_free(q);
}

static void ran(void *ud, int answer)
{
    request *q = ud;
    q->yes = answer != 0;
    sk_ui_post_owned(deliver, q, request_free);
}

static void run(void *p)
{
    request *q = p;
    sk_ask_question s = {q->title, q->message, q->detail, q->ok, q->cancel};
    sk_ask_confirm_run(q->loop, q->path, &s, ran, q);
}

void sk_ask_confirm(sk_window *parent, sk_loop *loop, const sk_ask_question *q, sk_ask_answered done, void *ud)
{
    (void)parent;
    request *r = sk_xcalloc(1, sizeof *r);
    r->loop = loop;
    r->title = dup_or_empty(q->title);
    r->message = dup_or_empty(q->message);
    r->detail = dup_or_empty(q->detail);
    r->ok = dup_or_empty(q->ok);
    r->cancel = dup_or_empty(q->cancel);
    r->path = getenv("PATH") ? sk_xstrdup(getenv("PATH")) : NULL;
    r->done = done;
    r->ud = ud;
    if (busy) {
        r->refused = true;
        sk_ui_post_owned(deliver, r, request_free);
        return;
    }
    busy = true;
    sk_loop_post_owned(loop, run, r, request_free);
}
