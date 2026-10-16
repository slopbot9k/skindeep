/* A Claude session the app did not start (decision 0027), as
 * app/metrics/claude/watch.js follows it: its transcript alone, read by
 * the session core as when no statusline and no hooks were injected
 * (decision 0009). find() names the transcript; it is asked again every
 * 5 s, so a /clear, which starts a new transcript, is followed. The part
 * is read-only and writes no title (decision 0032). */
#include "internal.h"

#include "../../core/tail.h"

#include <string.h>

#define FIND_MS 5000

typedef struct {
    sk_bus *bus;
    char *const *argv;
    sk_log_fn log;
    void *log_ud;
    sk_claude_find_fn find;
    void *ud;
    void (*free_ud)(void *);
    sk_source *src;
    sk_claude_core *core;
    sk_tail *tail;
    char *file;
    bool closed, found_once;
    uint64_t last_find;
} watch;

static int prepare(void *self, sk_strv *args, char **notice)
{
    (void)args, (void)notice;
    watch *w = self;
    sk_claude_facts facts = {.hooks = false, .statusline = false, .log = w->log, .log_ud = w->log_ud};
    sk_claude_compaction_of(&facts.compaction, NULL, 0, w->argv, NULL);
    w->src = sk_bus_source(w->bus, sk_claude_source_spec(&facts));
    w->core = sk_claude_core_new(w->src, &facts);
    return 0;
}

static void attach(void *self, pid_t pid)
{
    (void)self, (void)pid;
}

static void on_line(void *ud, const sk_json *obj, bool history)
{
    watch *w = ud;
    sk_claude_core_line(w->core, w->file, obj, history);
}

/* A new transcript is a new session. One named before it is written is
 * followed once it is. */
static void follow(watch *w, uint64_t now)
{
    w->found_once = true;
    w->last_find = now;
    char *file = NULL, *session = NULL;
    if (!w->find(w->ud, &file, &session) || !file || !*file || (w->file && !strcmp(file, w->file))) {
        free(file);
        free(session);
        return;
    }
    if (w->tail) {
        sk_tail_stop(w->tail);
        sk_tail_free(w->tail);
        w->tail = NULL;
    }
    free(w->file);
    w->file = file;
    sk_claude_core_enter_ids(w->core, session, file);
    free(session);
    w->tail = sk_tail_new(w->file, on_line, w, -1);
}

static void poll_part(void *self, uint64_t now)
{
    watch *w = self;
    if (w->closed || !w->core)
        return;
    if (!w->found_once || now - w->last_find >= FIND_MS)
        follow(w, now);
    if (w->tail)
        sk_tail_poll(w->tail, now);
}

static void report(void *self, sk_report *out)
{
    watch *w = self;
    memset(out, 0, sizeof *out);
    out->session = w->core ? sk_claude_core_session(w->core) : NULL;
    out->title = w->core ? sk_claude_core_title(w->core) : NULL;
    out->channels[0] = (sk_channel){"transcript", w->tail ? SK_CH_LIVE : SK_CH_WAITING, w->file,
                                    w->file ? "folder" : NULL};
    out->n_channels = 1;
    out->notice = w->core && !w->file ? "meters waiting: no transcript found" : NULL;
    out->replaying = w->tail && sk_tail_replaying(w->tail);
}

static void close_part(void *self)
{
    watch *w = self;
    w->closed = true;
    if (w->tail)
        sk_tail_stop(w->tail);
    if (w->src)
        sk_source_close(w->src);
}

static void free_part(void *self)
{
    watch *w = self;
    close_part(w);
    sk_tail_free(w->tail);
    sk_claude_core_free(w->core);
    if (w->free_ud)
        w->free_ud(w->ud);
    free(w->file);
    free(w);
}

static const sk_part_ops OPS = {prepare, attach, poll_part, report, NULL, close_part, free_part};

sk_part sk_claude_watch_owned(const sk_agent_ctx *ctx, sk_claude_find_fn find, void *ud,
                              void (*free_ud)(void *))
{
    watch *w = sk_xcalloc(1, sizeof *w);
    w->bus = ctx->bus;
    w->argv = ctx->cmd && ctx->cmd[0] ? ctx->cmd + 1 : NULL;
    w->log = ctx->log;
    w->log_ud = ctx->log_ud;
    w->find = find;
    w->ud = ud;
    w->free_ud = free_ud;
    return (sk_part){&OPS, w};
}

sk_part sk_claude_watch(const sk_agent_ctx *ctx, sk_claude_find_fn find, void *ud)
{
    return sk_claude_watch_owned(ctx, find, ud, NULL);
}
