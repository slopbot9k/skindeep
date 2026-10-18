/* What the tests of the monitor, the dashboard and the sessions the app
 * did not start share: a watch part the test drives in place of Claude
 * Code's or Codex's, and a session as a scan finds it. */
#ifndef SK_TESTS_APP_FAKES_H
#define SK_TESTS_APP_FAKES_H

#include "../../src/app/foreign.h"

#include <string.h>

/* What the part was asked, and what it reports. The test owns it. */
typedef struct {
    int prepared, attached, polled, closed, freed;
    pid_t pid;
    const char *session, *title, *notice;
    bool replaying;
    sk_ch_state state; /* its transcript's */
} fake_part;

static inline int fake_prepare(void *self, sk_strv *args, char **notice)
{
    (void)args, (void)notice;
    ((fake_part *)self)->prepared++;
    return 0;
}

static inline void fake_attach(void *self, pid_t pid)
{
    fake_part *f = self;
    f->attached++;
    f->pid = pid;
}

static inline void fake_poll(void *self, uint64_t now)
{
    (void)now;
    ((fake_part *)self)->polled++;
}

static inline void fake_report(void *self, sk_report *out)
{
    fake_part *f = self;
    memset(out, 0, sizeof *out);
    out->session = f->session;
    out->title = f->title;
    out->notice = f->notice;
    out->replaying = f->replaying;
    out->channels[0] = (sk_channel){"transcript", f->state, NULL, NULL};
    out->n_channels = 1;
}

static inline void fake_close(void *self)
{
    ((fake_part *)self)->closed++;
}

static inline void fake_free(void *self)
{
    ((fake_part *)self)->freed++;
}

/* An agent on f beside Generic, on bus, as a watcher makes one. */
static inline sk_agent *fake_agent(fake_part *f, sk_bus *bus)
{
    static const sk_part_ops ops = {fake_prepare, fake_attach, fake_poll, fake_report,
                                    NULL,         fake_close,  fake_free, NULL};
    sk_agent_ctx ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.bus = bus;
    ctx.launched = 1;
    return sk_agent_watch(&ctx, "claude", (sk_part){&ops, f});
}

/* A session as a scan finds it, its strings copied. */
static inline void fake_session(sk_foreign_session *w, pid_t pid, double started, sk_profile_agent kind,
                                const char *cwd, char *const *argv)
{
    memset(w, 0, sizeof *w);
    w->s.pid = pid;
    w->s.kind = kind;
    w->s.started = started;
    w->s.cwd = sk_xstrdup(cwd);
    sk_strv_extend(&w->s.argv, argv);
    sk_strv_argv(&w->s.argv);
}

static inline void fake_session_free(sk_foreign_session *w)
{
    free(w->s.cwd);
    sk_strv_free(&w->s.argv);
}

/* The compact JSON of the value at a path of json: member names, or
 * "#n" for the nth element. NULL when there is none. */
static inline char *json_at(const char *json, const char *const *path)
{
    sk_arena a;
    sk_arena_init(&a, 0);
    const sk_json *v = json ? sk_json_parse(&a, json, strlen(json), NULL) : NULL;
    for (; v && *path; path++)
        v = **path == '#' ? sk_json_at(v, (size_t)atoi(*path + 1)) : sk_json_get(v, *path);
    char *out = NULL;
    if (v) {
        sk_buf b = {0};
        sk_jw w;
        sk_jw_init(&w, &b, 0);
        sk_jw_value(&w, v);
        out = b.ptr;
    }
    sk_arena_free(&a);
    return out;
}

#endif
