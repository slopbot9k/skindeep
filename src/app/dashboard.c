/* Port of app/dashboard.js, with makeDashboard and openDashboard of
 * app/main.js. A watch agent's prepare returns at once here, so it is
 * attached at once. An agent closed is freed with its bus; what the live
 * checks read of it stays. The end reaches sk_tabs_ended from the loop,
 * as a monitor's does. Left to the window: the probe's lastDashboard,
 * which outlives the tab, and the app's set of dials with toggleDial,
 * which calls sk_dashboard_set_dials on each dashboard. */
#include "dashboard.h"

#include <string.h>
#include <time.h>

typedef struct {
    char *key;
    sk_agent *agent; /* NULL once closed */
    sk_bus *bus;
    bool closed;
} watcher;

typedef struct {
    const sk_dashboard_config *c;
    sk_tab *tab;
    int id;
    sk_history *history;
    sk_bus *bus; /* the tab's own, which nothing writes */
    /* Every watcher made, in order; those not closed are the watchers,
     * one per key, in the order they were made. */
    SK_VEC(watcher *) made;
    sk_tab_session_state state;
    int status;
    int timer, end_timer;
} dashboard;

static const sk_tab_strip STRIP = {
    "sessions", "every live agent session, sampled every 5 s", "", "read-only dashboard",
};

/* Date.now(). */
static double wall_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec * 1000 + (double)(ts.tv_nsec / 1000000);
}

static void cancel(dashboard *d, int *id)
{
    if (*id)
        sk_loop_cancel(d->c->loop, *id);
    *id = 0;
}

static void send_strip(dashboard *d)
{
    if (d->c->strip)
        d->c->strip(d->c->ud, d->tab, &STRIP);
}

static void send_view(dashboard *d)
{
    if (!d->c->send)
        return;
    sk_buf out = {0};
    sk_jw w;
    sk_jw_init(&w, &out, 0);
    sk_history_view(d->history, &w);
    d->c->send(d->c->ud, d->tab, sk_buf_cstr(&out));
    sk_buf_free(&out);
}

static watcher *watching(dashboard *d, const char *key)
{
    for (size_t i = 0; i < d->made.len; i++)
        if (!d->made.ptr[i]->closed && !strcmp(d->made.ptr[i]->key, key))
            return d->made.ptr[i];
    return NULL;
}

/* A session elsewhere: its watcher, made once, prepared and attached. */
static watcher *watch(dashboard *d, const sk_foreign_entry *e)
{
    watcher *w = watching(d, e->key);
    if (w)
        return w;
    w = sk_xcalloc(1, sizeof *w);
    w->key = sk_xstrdup(e->key);
    w->bus = sk_bus_new(d->c->log, d->c->ud);
    w->agent = d->c->watcher(d->c->ud, &e->s, w->bus);
    sk_vec_push(&d->made, w);
    if (w->agent) {
        sk_strv args = {0};
        char *notice = NULL;
        sk_agent_prepare(w->agent, &args, &notice);
        sk_strv_free(&args);
        free(notice);
        sk_agent_attach(w->agent, e->s.s.pid);
    }
    return w;
}

static void unwatch(dashboard *d, const char *key)
{
    watcher *w = watching(d, key);
    if (!w)
        return;
    w->closed = true;
    sk_agent_free(w->agent);
    sk_bus_free(w->bus);
    w->agent = NULL;
    w->bus = NULL;
}

static void unwatch_all(dashboard *d)
{
    for (size_t i = 0; i < d->made.len; i++)
        if (!d->made.ptr[i]->closed)
            unwatch(d, d->made.ptr[i]->key);
}

void sk_dashboard_board(sk_jw *w, const sk_board *b)
{
    if (b->state == SK_BOARD_NONE) {
        sk_jw_null(w);
        return;
    }
    sk_jw_object(w);
    sk_jw_key(w, SK_STR("state"));
    if (b->state == SK_BOARD_LOADING) {
        sk_jw_string(w, SK_STR("loading"));
    } else if (b->state == SK_BOARD_NO_ACCESS) {
        sk_jw_string(w, SK_STR("no access"));
    } else {
        sk_jw_string(w, SK_STR("ok"));
        sk_jw_key(w, SK_STR("open"));
        sk_jw_array(w);
        for (size_t i = 0; i < SK_BOARD_DAYS; i++)
            sk_jw_int(w, b->series.open[i]);
        sk_jw_array_end(w);
        sk_jw_key(w, SK_STR("closed"));
        sk_jw_array(w);
        for (size_t i = 0; i < SK_BOARD_DAYS; i++)
            sk_jw_int(w, b->series.closed[i]);
        sk_jw_array_end(w);
        sk_jw_key(w, SK_STR("openNow"));
        sk_jw_int(w, b->series.open_now);
        sk_jw_key(w, SK_STR("closed14"));
        sk_jw_int(w, b->series.closed14);
    }
    sk_jw_object_end(w);
}

/* What a row of the view carries beside its series. */
static void write_meta(sk_buf *out, const sk_foreign_entry *e, const sk_board *board)
{
    sk_jw w;
    sk_jw_init(&w, out, 0);
    sk_jw_object(&w);
    sk_jw_key(&w, SK_STR("label"));
    sk_jw_string(&w, sk_str_c(e->label ? e->label : ""));
    sk_jw_key(&w, SK_STR("folder"));
    sk_jw_string(&w, sk_str_c(e->folder ? e->folder : ""));
    sk_jw_key(&w, SK_STR("state"));
    sk_jw_string(&w, sk_str_c(e->state ? e->state : ""));
    sk_jw_key(&w, SK_STR("where"));
    sk_jw_string(&w, sk_str_c(e->where ? e->where : ""));
    sk_jw_key(&w, SK_STR("tab"));
    if (e->tab_id)
        sk_jw_int(&w, e->tab_id);
    else
        sk_jw_null(&w);
    sk_jw_key(&w, SK_STR("board"));
    sk_dashboard_board(&w, board);
    sk_jw_object_end(&w);
}

typedef struct {
    sk_history_values values;
    bool some;
    sk_buf meta;
    char *session;
} sample_parts;

static void parts_free(sample_parts *p)
{
    sk_vec_free(&p->values);
    sk_buf_free(&p->meta);
    free(p->session);
    memset(p, 0, sizeof *p);
}

static bool sampled(const sk_history_sample *samples, size_t n, const char *key)
{
    for (size_t i = 0; i < n; i++)
        if (!strcmp(samples[i].key, key))
            return true;
    return false;
}

static void sample(dashboard *d)
{
    if (d->state != SK_TAB_SESSION_RUNNING)
        return;
    sk_foreign_entries entries = {0};
    d->c->list(d->c->ud, &entries);
    char *const *dials = sk_history_dials(d->history);
    sk_history_sample *samples = sk_xcalloc(entries.len + 1, sizeof *samples);
    sample_parts *parts = sk_xcalloc(entries.len + 1, sizeof *parts);
    size_t n = 0;
    for (size_t i = 0; i < entries.len; i++) {
        const sk_foreign_entry *e = &entries.ptr[i];
        sk_agent *agent;
        sk_bus *bus;
        if (e->tab) {
            unwatch(d, e->key);
            agent = e->agent;
            bus = e->bus;
        } else {
            watcher *w = watch(d, e);
            agent = w->agent;
            bus = w->bus;
        }
        sk_board board = {SK_BOARD_NONE, {{0}, {0}, 0, 0}};
        if (d->c->board)
            d->c->board(d->c->ud, e->s.s.cwd, &board);
        sk_report r;
        memset(&r, 0, sizeof r);
        if (agent)
            sk_agent_report(agent, &r);
        /* A key sampled twice keeps its first place and its last values,
         * as a Map does. */
        size_t at = n;
        for (size_t k = 0; k < n; k++)
            if (!strcmp(samples[k].key, e->key))
                at = k;
        sample_parts *p = &parts[at];
        if (at < n)
            parts_free(p);
        else
            n++;
        /* A session whose history is still being read has no values
         * yet: its part-read totals would make the rest of the history a
         * rise. */
        if (!r.replaying && bus) {
            sk_history_pick(bus, dials, &p->values);
            p->some = true;
        }
        p->session = r.session && *r.session ? sk_xstrdup(r.session) : NULL;
        write_meta(&p->meta, e, &board);
        samples[at] = (sk_history_sample){e->key, p->some ? &p->values : NULL, p->meta.ptr, p->session};
    }
    for (size_t i = 0; i < d->made.len; i++)
        if (!d->made.ptr[i]->closed && !sampled(samples, n, d->made.ptr[i]->key))
            unwatch(d, d->made.ptr[i]->key);
    sk_history_tick(d->history, d->c->now ? d->c->now() : wall_ms(), samples, n);
    send_view(d);
    for (size_t i = 0; i < n; i++)
        parts_free(&parts[i]);
    free(parts);
    free(samples);
    sk_foreign_entries_free(&entries);
}

static void on_sample(sk_loop *loop, void *ud)
{
    (void)loop;
    sample(ud);
}

static void start(dashboard *d)
{
    if (d->state != SK_TAB_SESSION_IDLE)
        return;
    d->state = SK_TAB_SESSION_RUNNING;
    send_strip(d);
    sample(d);
    uint64_t every = d->c->interval_ms ? d->c->interval_ms : SK_HISTORY_INTERVAL_MS;
    d->timer = sk_loop_timer(d->c->loop, every, every, on_sample, d);
}

/* The tabs may free this tab, and so d, inside the call. */
static void fire_end(sk_loop *loop, void *ud)
{
    (void)loop;
    dashboard *d = ud;
    d->end_timer = 0;
    sk_tabs_ended(d->c->tabs, d->id, d->status);
}

static void end(dashboard *d, int code)
{
    if (d->state == SK_TAB_SESSION_ENDED)
        return;
    d->state = SK_TAB_SESSION_ENDED;
    cancel(d, &d->timer);
    unwatch_all(d);
    d->status = code;
    d->end_timer = sk_loop_timer(d->c->loop, 0, 0, fire_end, d);
}

static void op_hangup(void *self)
{
    end(self, 0);
}

static sk_tab_session_state op_state(void *self)
{
    return ((dashboard *)self)->state;
}

static pid_t op_pid(void *self)
{
    (void)self;
    return 0;
}

static void op_free(void *self)
{
    dashboard *d = self;
    cancel(d, &d->timer);
    cancel(d, &d->end_timer);
    unwatch_all(d);
    for (size_t i = 0; i < d->made.len; i++) {
        free(d->made.ptr[i]->key);
        free(d->made.ptr[i]);
    }
    sk_vec_free(&d->made);
    sk_history_free(d->history);
    sk_bus_free(d->bus);
    free(d);
}

/* It follows no session of its own, keeps no name and leaves no screen. */
static const sk_tab_session_ops ops = {op_hangup, op_state, op_pid, NULL, NULL, NULL, NULL, op_free};

int sk_dashboard_make(const sk_dashboard_config *c, sk_tab *tab, const sk_tab_spec *spec, char **err)
{
    (void)spec;
    if (!c->tabs || !c->loop || !c->list || !c->watcher) {
        *err = sk_xstrdup("the app has no sessions to sample");
        return -1;
    }
    dashboard *d = sk_xcalloc(1, sizeof *d);
    d->c = c;
    d->tab = tab;
    d->id = tab->id;
    double interval = (double)(c->interval_ms ? c->interval_ms : SK_HISTORY_INTERVAL_MS);
    d->history = sk_history_new(c->points, c->keep_ended_ms, interval);
    if (c->dials && c->dials->len)
        sk_history_set_dials(d->history, c->dials->ptr, c->dials->len);
    d->bus = sk_bus_new(c->log, c->ud);
    d->state = SK_TAB_SESSION_IDLE;
    tab->session.ops = &ops;
    tab->session.self = d;
    return 0;
}

static dashboard *of(const sk_tab *tab)
{
    return tab && tab->session.ops == &ops ? tab->session.self : NULL;
}

sk_tab *sk_dashboard_open(sk_tabs *tabs, const char *cwd, char **err)
{
    if (err)
        *err = NULL;
    for (size_t i = 0; i < sk_tabs_count(tabs); i++) {
        sk_tab *t = sk_tabs_at(tabs, i);
        if (t->kind == SK_TAB_DASHBOARD) {
            sk_tabs_activate(tabs, t->id);
            return t;
        }
    }
    char *cmd[] = {"sessions", NULL};
    sk_tab_spec spec = {SK_TAB_DASHBOARD, cmd, cwd, NULL, NULL, "sessions", NULL, NULL};
    return sk_tabs_add(tabs, &spec, -1, err);
}

void sk_dashboard_start(sk_tab *tab)
{
    dashboard *d = of(tab);
    if (d)
        start(d);
}

void sk_dashboard_sample(sk_tab *tab)
{
    dashboard *d = of(tab);
    if (d)
        sample(d);
}

void sk_dashboard_set_dials(sk_tab *tab, char *const *names, size_t n)
{
    dashboard *d = of(tab);
    if (!d)
        return;
    sk_history_set_dials(d->history, names, n);
    if (d->state == SK_TAB_SESSION_RUNNING)
        send_view(d);
}

bool sk_dashboard_strip(const sk_tab *tab, sk_tab_strip *out)
{
    if (!of(tab))
        return false;
    *out = STRIP;
    return true;
}

sk_bus *sk_dashboard_bus(const sk_tab *tab)
{
    dashboard *d = of(tab);
    return d ? d->bus : NULL;
}

/* Whether a watcher follows its transcript or rollout: its adapter's
 * channel, reported first, is live. */
static bool tailing(watcher *w)
{
    if (w->closed || !w->agent)
        return false;
    sk_report r;
    sk_agent_report(w->agent, &r);
    if (!r.n_channels || r.channels[0].state != SK_CH_LIVE)
        return false;
    return !strcmp(r.channels[0].name, "transcript") || !strcmp(r.channels[0].name, "rollout");
}

bool sk_dashboard_report(const sk_tab *tab, sk_jw *w)
{
    dashboard *d = of(tab);
    if (!d)
        return false;
    sk_jw_object(w);
    sk_jw_key(w, SK_STR("ticks"));
    sk_jw_int(w, (int64_t)sk_history_tick_count(d->history));
    sk_jw_key(w, SK_STR("rows"));
    sk_jw_array(w);
    for (size_t i = 0; i < sk_history_row_count(d->history); i++)
        sk_jw_string(w, sk_str_c(sk_history_row_key(d->history, i)));
    sk_jw_array_end(w);
    sk_jw_key(w, SK_STR("dials"));
    sk_jw_array(w);
    for (char *const *p = sk_history_dials(d->history); *p; p++)
        sk_jw_string(w, sk_str_c(*p));
    sk_jw_array_end(w);
    sk_jw_key(w, SK_STR("watching"));
    sk_jw_array(w);
    for (size_t i = 0; i < d->made.len; i++)
        if (!d->made.ptr[i]->closed)
            sk_jw_string(w, sk_str_c(d->made.ptr[i]->key));
    sk_jw_array_end(w);
    sk_jw_key(w, SK_STR("made"));
    sk_jw_array(w);
    for (size_t i = 0; i < d->made.len; i++) {
        watcher *m = d->made.ptr[i];
        sk_jw_object(w);
        sk_jw_key(w, SK_STR("key"));
        sk_jw_string(w, sk_str_c(m->key));
        sk_jw_key(w, SK_STR("closed"));
        sk_jw_bool(w, m->closed);
        sk_jw_key(w, SK_STR("tailing"));
        sk_jw_bool(w, tailing(m));
        sk_jw_object_end(w);
    }
    sk_jw_array_end(w);
    sk_jw_object_end(w);
    return true;
}
