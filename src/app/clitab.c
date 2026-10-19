#include "clitab.h"

#include "../base/paths.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
    const sk_clitab_config *c;
    sk_tab *tab;
    int id;
    char *dir;
    char *note;
    /* The agent keeps a pointer to its context. The command, folder and
     * environment are the tab's, which frees its session first. */
    sk_agent_ctx ctx;
    sk_bus *bus;
    sk_agent *agent;
    sk_session *session;
    /* The terminal's last screen, kept when the CLI ended by itself. */
    char *last_screen;
} clitab;

static void on_spawn(void *ud, pid_t pid)
{
    clitab *t = ud;
    if (t->c->spawned)
        t->c->spawned(t->c->ud, t->tab, pid);
}

static void on_output(void *ud, const char *bytes, size_t n)
{
    clitab *t = ud;
    if (t->c->output)
        t->c->output(t->c->ud, t->tab, bytes, n);
}

/* The session's lines come with its own ud, the tab's: the app's go on
 * with the app's. */
static void on_log(void *ud, const char *msg)
{
    clitab *t = ud;
    if (t->c->log)
        t->c->log(t->c->ud, msg);
}

/* The agent's and the bus's lines, the adapter's "following ..." among
 * them, say which tab they are of. */
static void tab_log(void *ud, const char *msg)
{
    clitab *t = ud;
    if (!t->c->log)
        return;
    sk_buf b = {0};
    sk_buf_printf(&b, "tab %d: %s", t->id, msg);
    t->c->log(t->c->ud, sk_buf_cstr(&b));
    sk_buf_free(&b);
}

static void on_tell(void *ud, const char *msg)
{
    clitab *t = ud;
    if (t->c->tell)
        t->c->tell(t->c->ud, msg);
}

/* The tabs may free this tab, and so t, inside the call. A CLI that
 * exited by itself leaves its last screen, as in a terminal, read before
 * the end; a hung-up one does not (app/session.js's finalScreen). */
static void on_end(void *ud, int status)
{
    clitab *t = ud;
    if (t->c->screen && !t->last_screen && sk_session_pid(t->session) > 0 && !sk_session_hung_up(t->session))
        t->last_screen = t->c->screen(t->c->ud, t->tab);
    sk_tabs_ended(t->c->tabs, t->id, status);
}

static void op_hangup(void *self)
{
    sk_session_hangup(((clitab *)self)->session);
}

static sk_tab_session_state op_state(void *self)
{
    switch (sk_session_get_state(((clitab *)self)->session)) {
    case SK_SESSION_IDLE:
        return SK_TAB_SESSION_IDLE;
    case SK_SESSION_PREPARING:
        return SK_TAB_SESSION_PREPARING;
    case SK_SESSION_RUNNING:
        return SK_TAB_SESSION_RUNNING;
    case SK_SESSION_ENDING:
        return SK_TAB_SESSION_ENDING;
    case SK_SESSION_ENDED:
        break;
    }
    return SK_TAB_SESSION_ENDED;
}

static pid_t op_pid(void *self)
{
    return sk_session_pid(((clitab *)self)->session);
}

static const char *op_session_id(void *self)
{
    sk_report r;
    sk_agent_report(((clitab *)self)->agent, &r);
    return r.session;
}

static const char *op_title(void *self)
{
    sk_report r;
    sk_agent_report(((clitab *)self)->agent, &r);
    return r.title;
}

static bool op_rename(void *self, const char *name)
{
    return sk_agent_rename(((clitab *)self)->agent, name);
}

static const char *op_last_screen(void *self)
{
    return ((clitab *)self)->last_screen;
}

static void op_free(void *self)
{
    clitab *t = self;
    sk_session_free(t->session);
    sk_agent_free(t->agent);
    sk_bus_free(t->bus);
    free(t->dir);
    free(t->note);
    free(t->last_screen);
    free(t);
}

static const sk_tab_session_ops ops = {
    op_hangup, op_state, op_pid, op_session_id, op_title, op_rename, op_last_screen, op_free,
};

int sk_clitab_make(const sk_clitab_config *c, sk_tab *tab, const sk_tab_spec *spec, char **err)
{
    if (!c->tabs || !c->run_dir || !tab->file) {
        *err = sk_xstrdup("the app has no runtime dir or program for the tab");
        return -1;
    }
    sk_buf name = {0};
    sk_buf_printf(&name, "tab-%d", tab->id);
    char *dir = sk_path_join(c->run_dir, name.ptr);
    sk_buf_free(&name);
    if (mkdir(dir, 0700) != 0) {
        sk_buf why = {0};
        sk_buf_printf(&why, "cannot make %s: %s", dir, strerror(errno));
        *err = why.ptr;
        free(dir);
        return -1;
    }
    clitab *t = sk_xcalloc(1, sizeof *t);
    t->c = c;
    t->tab = tab;
    t->id = tab->id;
    t->dir = dir;
    t->note = spec->note ? sk_xstrdup(spec->note) : NULL;
    t->bus = sk_bus_new(tab_log, t);
    t->ctx.cmd = tab->cmd.ptr;
    t->ctx.cwd = tab->cwd;
    t->ctx.env = &tab->env;
    t->ctx.run_dir = dir;
    t->ctx.root = c->root;
    t->ctx.store = c->store;
    t->ctx.relay = c->relay;
    t->ctx.statusline = c->statusline;
    t->ctx.bus = t->bus;
    t->ctx.loop = c->loop;
    t->ctx.log = tab_log;
    t->ctx.log_ud = t;
    t->agent = sk_agent_open(&t->ctx);
    sk_session_opts o;
    memset(&o, 0, sizeof o);
    o.loop = c->loop;
    o.file = tab->file;
    o.argv = tab->cmd.ptr;
    o.cwd = *tab->cwd ? tab->cwd : NULL;
    o.env = &tab->env;
    o.label = tab->label;
    o.agent = t->agent;
    o.on_spawn = on_spawn;
    o.on_output = on_output;
    o.on_end = on_end;
    o.log = on_log;
    o.tell = on_tell;
    o.ud = t;
    t->session = sk_session_new(&o);
    tab->session.ops = &ops;
    tab->session.self = t;
    return 0;
}

static clitab *of(const sk_tab *tab)
{
    return tab && tab->session.ops == &ops ? tab->session.self : NULL;
}

sk_session *sk_clitab_session(const sk_tab *tab)
{
    clitab *t = of(tab);
    return t ? t->session : NULL;
}

sk_agent *sk_clitab_agent(const sk_tab *tab)
{
    clitab *t = of(tab);
    return t ? t->agent : NULL;
}

sk_bus *sk_clitab_bus(const sk_tab *tab)
{
    clitab *t = of(tab);
    return t ? t->bus : NULL;
}

const char *sk_clitab_note(const sk_tab *tab)
{
    clitab *t = of(tab);
    return t ? t->note : NULL;
}
