/* Port of app/monitor.js and makeMonitor of app/main.js. The JavaScript
 * awaited each adapter's prepare; here it returns at once. Its end
 * resolved a promise the tabs read on a later tick; here it reaches
 * sk_tabs_ended from the loop, so a start, a poll or a hang-up never
 * frees the tab under its caller. */
#include "monitor.h"

#include "identity.h"

#include <stdarg.h>
#include <string.h>

#define READ_ONLY "read-only monitor"

typedef struct {
    const sk_monitor_config *c;
    sk_tab *tab;
    int id;
    pid_t pid;
    double started;
    char *label, *cmdline, *cwd; /* the strip's: cwd with ~ */
    char *notice;                /* the agent's, or NULL */
    sk_buf shown;                /* the strip's notice */
    sk_bus *bus;
    sk_agent *agent;
    sk_tab_session_state state;
    int status;
    int poll_timer, end_timer;
} monitor;

static void logf_(monitor *m, const char *fmt, ...) SK_PRINTF(2, 3);
static void logf_(monitor *m, const char *fmt, ...)
{
    if (!m->c->log)
        return;
    sk_buf b = {0};
    va_list ap;
    va_start(ap, fmt);
    sk_buf_vprintf(&b, fmt, ap);
    va_end(ap);
    m->c->log(m->c->ud, b.ptr);
    sk_buf_free(&b);
}

static void cancel(monitor *m, int *id)
{
    if (*id)
        sk_loop_cancel(m->c->loop, *id);
    *id = 0;
}

static bool alive(monitor *m)
{
    return m->c->same ? m->c->same(m->c->ud, m->pid, m->started) : sk_sessions_same(m->pid, m->started);
}

static void strip_of(monitor *m, sk_tab_strip *out)
{
    sk_buf_clear(&m->shown);
    if (m->notice)
        sk_buf_printf(&m->shown, "%s  ", m->notice);
    sk_buf_append_c(&m->shown, READ_ONLY);
    *out = (sk_tab_strip){m->label, m->cmdline, m->cwd, m->shown.ptr};
}

static void send_strip(monitor *m)
{
    if (!m->c->strip)
        return;
    sk_tab_strip s;
    strip_of(m, &s);
    m->c->strip(m->c->ud, m->tab, &s);
}

/* The tabs may free this tab, and so m, inside the call. */
static void fire_end(sk_loop *loop, void *ud)
{
    (void)loop;
    monitor *m = ud;
    m->end_timer = 0;
    sk_tabs_ended(m->c->tabs, m->id, m->status);
}

/* The first end stands. */
static void end(monitor *m, int code)
{
    if (m->state == SK_TAB_SESSION_ENDED)
        return;
    m->status = code;
    cancel(m, &m->poll_timer);
    sk_agent_close(m->agent);
    m->state = SK_TAB_SESSION_ENDED;
    m->end_timer = sk_loop_timer(m->c->loop, 0, 0, fire_end, m);
}

/* The session's end ends the tab. The notice follows the report. */
static void poll_once(monitor *m)
{
    if (m->state != SK_TAB_SESSION_RUNNING)
        return;
    if (!alive(m)) {
        end(m, 0);
        return;
    }
    sk_report r;
    sk_agent_report(m->agent, &r);
    const char *notice = r.notice && *r.notice ? r.notice : NULL;
    if (notice == m->notice || (notice && m->notice && !strcmp(notice, m->notice)))
        return;
    free(m->notice);
    m->notice = notice ? sk_xstrdup(notice) : NULL;
    send_strip(m);
}

static void on_poll(sk_loop *loop, void *ud)
{
    (void)loop;
    poll_once(ud);
}

static void start(monitor *m)
{
    if (m->state != SK_TAB_SESSION_IDLE)
        return;
    m->state = SK_TAB_SESSION_PREPARING;
    sk_strv args = {0};
    char *notice = NULL;
    sk_agent_prepare(m->agent, &args, &notice);
    sk_strv_free(&args);
    free(notice);
    if (!alive(m)) {
        end(m, 0);
        return;
    }
    m->state = SK_TAB_SESSION_RUNNING;
    sk_agent_attach(m->agent, m->pid);
    if (m->c->pane) {
        sk_buf text = {0};
        sk_buf_printf(&text,
                      "\x1b[1mRead-only monitor\x1b[0m of %s, pid %ld, in %s.\r\n"
                      "The session runs in another terminal. This tab shows its meters and\r\n"
                      "never types into it. Closing this tab ends the monitor, not the session.\r\n",
                      m->label, (long)m->pid, m->cwd);
        m->c->pane(m->c->ud, m->tab, text.ptr);
        sk_buf_free(&text);
    }
    send_strip(m);
    uint64_t every = m->c->poll_ms ? m->c->poll_ms : SK_MONITOR_POLL_MS;
    m->poll_timer = sk_loop_timer(m->c->loop, every, every, on_poll, m);
    logf_(m, "monitor: watching %ld: %s", (long)m->pid, m->cmdline);
}

static void op_hangup(void *self)
{
    end(self, 0);
}

static sk_tab_session_state op_state(void *self)
{
    return ((monitor *)self)->state;
}

static pid_t op_pid(void *self)
{
    (void)self;
    return 0;
}

static const char *op_session_id(void *self)
{
    sk_report r;
    sk_agent_report(((monitor *)self)->agent, &r);
    return r.session;
}

static const char *op_title(void *self)
{
    sk_report r;
    sk_agent_report(((monitor *)self)->agent, &r);
    return r.title;
}

static bool op_rename(void *self, const char *name)
{
    return sk_agent_rename(((monitor *)self)->agent, name);
}

static const char *op_last_screen(void *self)
{
    (void)self;
    return NULL;
}

static void op_free(void *self)
{
    monitor *m = self;
    cancel(m, &m->poll_timer);
    cancel(m, &m->end_timer);
    sk_agent_free(m->agent);
    sk_bus_free(m->bus);
    free(m->label);
    free(m->cmdline);
    free(m->cwd);
    free(m->notice);
    sk_buf_free(&m->shown);
    free(m);
}

static const sk_tab_session_ops ops = {
    op_hangup, op_state, op_pid, op_session_id, op_title, op_rename, op_last_screen, op_free,
};

int sk_monitor_make(const sk_monitor_config *c, sk_tab *tab, const sk_tab_spec *spec, char **err)
{
    const sk_foreign_session *w = spec->data;
    if (!c->tabs || !c->loop || !c->watcher || !w) {
        *err = sk_xstrdup("the app has no session to watch");
        return -1;
    }
    monitor *m = sk_xcalloc(1, sizeof *m);
    m->c = c;
    m->tab = tab;
    m->id = tab->id;
    m->pid = w->s.pid;
    m->started = w->s.started;
    m->label = sk_xstrdup(tab->label);
    sk_buf b = {0};
    sk_identity_quote_argv(&b, w->s.argv.ptr ? w->s.argv.ptr : (char *const[]){NULL});
    m->cmdline = sk_xstrdup(sk_buf_cstr(&b));
    sk_buf_clear(&b);
    sk_identity_tildify(&b, w->s.cwd ? w->s.cwd : "", c->home);
    m->cwd = sk_xstrdup(sk_buf_cstr(&b));
    sk_buf_free(&b);
    m->bus = sk_bus_new(c->log, c->ud);
    m->agent = c->watcher(c->ud, w, m->bus);
    if (!m->agent) {
        sk_bus_free(m->bus);
        free(m->label);
        free(m->cmdline);
        free(m->cwd);
        free(m);
        *err = sk_xstrdup("the session cannot be watched");
        return -1;
    }
    m->state = SK_TAB_SESSION_IDLE;
    tab->session.ops = &ops;
    tab->session.self = m;
    return 0;
}

static monitor *of(const sk_tab *tab)
{
    return tab && tab->session.ops == &ops ? tab->session.self : NULL;
}

void sk_monitor_start(sk_tab *tab)
{
    monitor *m = of(tab);
    if (m)
        start(m);
}

void sk_monitor_poll(sk_tab *tab)
{
    monitor *m = of(tab);
    if (m)
        poll_once(m);
}

bool sk_monitor_strip(const sk_tab *tab, sk_tab_strip *out)
{
    monitor *m = of(tab);
    if (m)
        strip_of(m, out);
    return m != NULL;
}

sk_agent *sk_monitor_agent(const sk_tab *tab)
{
    monitor *m = of(tab);
    return m ? m->agent : NULL;
}

sk_bus *sk_monitor_bus(const sk_tab *tab)
{
    monitor *m = of(tab);
    return m ? m->bus : NULL;
}

bool sk_monitor_watched(const sk_tab *tab, pid_t *pid, double *started)
{
    monitor *m = of(tab);
    if (!m)
        return false;
    *pid = m->pid;
    *started = m->started;
    return true;
}
