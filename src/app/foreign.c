/* Port of what app/main.js keeps of the sessions it did not start
 * (decisions 0027, 0028): homeScan, knownProfiles, channelOf,
 * foreignLabel, watchAgent, keyOf, watching, listSessions with RESCAN_MS,
 * liveSessions and openSession. */
#include "foreign.h"

#include "../agents/claude/claude.h"
#include "clitab.h"
#include "identity.h"
#include "monitor.h"

#include <math.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct sk_foreign {
    sk_foreign_config cfg;
    sk_profile_founds found; /* the last home scan */
    bool scanned;
    double scanned_at;
};

static void logf_(sk_foreign *f, const char *fmt, ...) SK_PRINTF(2, 3);
static void logf_(sk_foreign *f, const char *fmt, ...)
{
    if (!f->cfg.log)
        return;
    sk_buf b = {0};
    va_list ap;
    va_start(ap, fmt);
    sk_buf_vprintf(&b, fmt, ap);
    va_end(ap);
    f->cfg.log(f->cfg.ud, b.ptr);
    sk_buf_free(&b);
}

/* Date.now(). */
static double wall_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec * 1000 + (double)(ts.tv_nsec / 1000000);
}

static double now_of(const sk_foreign *f)
{
    return f->cfg.now ? f->cfg.now() : wall_ms();
}

/* ---- The machine's sessions, unless the config gives others ---- */

static void scan(sk_foreign *f, sk_live_sessions *out)
{
    if (f->cfg.scan)
        f->cfg.scan(f->cfg.ud, out);
    else
        sk_sessions_scan(getuid(), out);
}

static size_t ancestors(sk_foreign *f, pid_t pid, pid_t *out, size_t max)
{
    if (f->cfg.ancestors)
        return f->cfg.ancestors(f->cfg.ud, pid, out, max);
    return sk_sessions_ancestors(pid, out, max);
}

static bool same(sk_foreign *f, pid_t pid, double started)
{
    return f->cfg.same ? f->cfg.same(f->cfg.ud, pid, started) : sk_sessions_same(pid, started);
}

static bool channel(sk_foreign *f, const sk_live_session *s, char *const *profiles, sk_session_channel *out)
{
    memset(out, 0, sizeof *out);
    if (f->cfg.channel)
        return f->cfg.channel(f->cfg.ud, s, profiles, out);
    if (s->kind == SK_PROFILE_CLAUDE)
        return sk_sessions_claude_transcript(s->pid, s->cwd, s->started, profiles, out);
    return sk_sessions_codex_rollout(s, profiles, sk_codex_rollout_find, NULL, out);
}

/* ---- Copies ---- */

static void live_copy(sk_live_session *to, const sk_live_session *s)
{
    *to = (sk_live_session){s->pid, s->ppid, s->kind, s->started, sk_xstrdup(s->cwd ? s->cwd : ""), {0}};
    sk_strv_extend(&to->argv, s->argv.ptr);
    sk_strv_argv(&to->argv);
}

static void live_free(sk_live_session *s)
{
    free(s->cwd);
    sk_strv_free(&s->argv);
}

void sk_foreign_entries_free(sk_foreign_entries *l)
{
    for (size_t i = 0; i < l->len; i++) {
        sk_foreign_entry *e = &l->ptr[i];
        live_free(&e->s.s);
        if (e->s.found) {
            sk_session_channel_free(e->s.found);
            free(e->s.found);
        }
        free(e->key);
        free(e->folder);
        free(e->label);
    }
    sk_vec_free(l);
}

sk_foreign *sk_foreign_new(const sk_foreign_config *cfg)
{
    sk_foreign *f = sk_xcalloc(1, sizeof *f);
    f->cfg = *cfg;
    return f;
}

void sk_foreign_free(sk_foreign *f)
{
    if (!f)
        return;
    sk_profile_founds_free(&f->found);
    free(f);
}

void sk_foreign_key(sk_buf *out, pid_t pid, double started)
{
    sk_buf_printf(out, "%ld:%lld", (long)pid, (long long)floor(started + 0.5));
}

void sk_foreign_label(sk_buf *out, sk_profile_agent kind, const sk_session_channel *found,
                      const char *home)
{
    const sk_profile_agent_info *a = &sk_profile_agents[kind];
    if (!found || !found->profile || !*found->profile) {
        sk_buf_printf(out, "%s [?]", a->kind);
        return;
    }
    sk_envp env = {0};
    sk_envp_set(&env, a->variable, found->profile);
    char *cmd[] = {(char *)a->kind, NULL};
    sk_identity_label(out, cmd, &env, a->kind, home);
    sk_envp_free(&env);
}

/* A scan reads every directory under the home, 360 ms on a home of ten
 * profiles, so it is taken again only when older than max_age. */
const sk_profile_founds *sk_foreign_home_scan(sk_foreign *f, double max_age)
{
    double now = now_of(f);
    if (!f->scanned || now - f->scanned_at > max_age) {
        sk_profile_founds_free(&f->found);
        if (f->cfg.home && *f->cfg.home)
            sk_profiles_scan(f->cfg.home, SK_PROFILES_SCAN_DEPTH, SK_PROFILES_SCAN_CAP, &f->found);
        f->scanned = true;
        f->scanned_at = now_of(f);
    }
    return &f->found;
}

static void push_once(sk_strv *out, size_t from, char *dir)
{
    for (size_t i = from; i < out->len; i++) {
        if (!strcmp(out->ptr[i], dir)) {
            free(dir);
            return;
        }
    }
    sk_strv_push(out, dir);
    free(dir);
}

static pid_t pid_of(const sk_tab *t)
{
    const sk_tab_session *s = &t->session;
    return s->ops && s->ops->pid ? s->ops->pid(s->self) : 0;
}

void sk_foreign_profiles(sk_foreign *f, sk_profile_agent kind, sk_strv *out)
{
    size_t from = out->len;
    push_once(out, from, sk_profiles_default(kind));
    push_once(out, from, sk_profiles_of(f->cfg.env, kind));
    for (size_t i = 0; f->cfg.tabs && i < sk_tabs_count(f->cfg.tabs); i++) {
        sk_tab *t = sk_tabs_at(f->cfg.tabs, i);
        const char *agent = sk_agent_provider(t->cmd.ptr)->kind;
        if (t->kind == SK_TAB_CLI && !strcmp(agent, sk_profile_agents[kind].kind))
            push_once(out, from, sk_profiles_of(&t->env, kind));
    }
    if (f->cfg.profiles_dir) {
        sk_profile_records records = {0};
        sk_profiles_read(f->cfg.profiles_dir, &records);
        for (size_t i = 0; i < records.len; i++)
            if (records.ptr[i].agent == kind)
                push_once(out, from, sk_xstrdup(records.ptr[i].dir));
        sk_profile_records_free(&records);
    }
    for (size_t i = 0; i < f->found.len; i++)
        if (f->found.ptr[i].agent == kind)
            push_once(out, from, sk_xstrdup(f->found.ptr[i].dir));
}

bool sk_foreign_channel(sk_foreign *f, const sk_live_session *s, sk_session_channel *out)
{
    sk_strv profiles = {0};
    sk_foreign_profiles(f, s->kind, &profiles);
    bool found = channel(f, s, sk_strv_argv(&profiles), out);
    sk_strv_free(&profiles);
    return found;
}

sk_tab *sk_foreign_watching(sk_foreign *f, pid_t pid)
{
    for (size_t i = 0; f->cfg.tabs && i < sk_tabs_count(f->cfg.tabs); i++) {
        sk_tab *t = sk_tabs_at(f->cfg.tabs, i);
        pid_t p;
        double started;
        if (sk_monitor_watched(t, &p, &started) && p == pid && same(f, pid, started))
            return t;
    }
    return NULL;
}

/* byPid: the last CLI in the list's order to run pid, as a Map built
 * from the list keeps it. */
static sk_tab *tab_of_pid(sk_foreign *f, pid_t pid)
{
    sk_tab *hit = NULL;
    for (size_t i = 0; pid && f->cfg.tabs && i < sk_tabs_count(f->cfg.tabs); i++) {
        sk_tab *t = sk_tabs_at(f->cfg.tabs, i);
        if (pid_of(t) == pid)
            hit = t;
    }
    return hit;
}

static bool listed(const sk_live_sessions *all, pid_t pid)
{
    for (size_t i = 0; i < all->len; i++)
        if (all->ptr[i].pid == pid)
            return true;
    return false;
}

static void tab_parts(const sk_tab *t, sk_agent **agent, sk_bus **bus)
{
    *agent = sk_clitab_agent(t);
    *bus = sk_clitab_bus(t);
    if (!*agent) {
        *agent = sk_monitor_agent(t);
        *bus = sk_monitor_bus(t);
    }
}

static const char *busy_state(const sk_bus *bus)
{
    const sk_val *v = bus ? sk_bus_get(bus, "state.busy") : NULL;
    if (!v || v->type != SK_VAL_NUM)
        return "unknown";
    return v->num == 1 ? "working" : v->num == 0 ? "waiting" : "unknown";
}

void sk_foreign_sessions(sk_foreign *f, sk_foreign_entries *out)
{
    double now = now_of(f);
    sk_live_sessions all = {0};
    scan(f, &all);
    sk_foreign_entries shown = {0}, rest = {0};
    for (size_t i = 0; i < all.len; i++) {
        const sk_live_session *s = &all.ptr[i];
        sk_tab *tab = tab_of_pid(f, s->pid);
        if (!tab) {
            pid_t up[SK_SESSIONS_MAX_ANCESTORS];
            size_t n = ancestors(f, s->pid, up, SK_LEN(up));
            pid_t hit = 0;
            for (size_t k = 0; k < n && !tab; k++)
                if ((tab = tab_of_pid(f, up[k])))
                    hit = up[k];
            if (tab && listed(&all, hit))
                continue;
        }
        sk_foreign_entry e;
        memset(&e, 0, sizeof e);
        live_copy(&e.s.s, s);
        sk_buf b = {0};
        sk_foreign_key(&b, s->pid, s->started);
        e.key = sk_xstrdup(sk_buf_cstr(&b));
        sk_buf_clear(&b);
        sk_identity_tildify(&b, e.s.s.cwd, f->cfg.home);
        e.folder = sk_xstrdup(sk_buf_cstr(&b));
        sk_buf_clear(&b);
        e.age = now - s->started;
        if (tab) {
            e.tab = tab;
            e.tab_id = tab->id;
            e.where = "tab";
            e.label = sk_xstrdup(tab->label);
            tab_parts(tab, &e.agent, &e.bus);
            e.state = busy_state(e.bus);
            sk_vec_push(&shown, e);
            sk_buf_free(&b);
            continue;
        }
        sk_session_channel ch;
        bool found = sk_foreign_channel(f, s, &ch);
        if (!found || !ch.profile) {
            sk_session_channel_free(&ch);
            sk_foreign_home_scan(f, SK_FOREIGN_RESCAN_MS);
            found = sk_foreign_channel(f, s, &ch);
        }
        if (found) {
            e.s.found = sk_xmalloc(sizeof *e.s.found);
            *e.s.found = ch;
        } else {
            sk_session_channel_free(&ch);
        }
        sk_tab *w = sk_foreign_watching(f, s->pid);
        e.tab = w;
        e.tab_id = w ? w->id : 0;
        e.where = w ? "monitor" : "";
        if (w)
            tab_parts(w, &e.agent, &e.bus);
        sk_foreign_label(&b, s->kind, e.s.found, f->cfg.home);
        e.label = sk_xstrdup(sk_buf_cstr(&b));
        e.state = sk_sessions_state_of(e.s.found ? e.s.found->mtime : 0, now);
        sk_buf_free(&b);
        if (w)
            sk_vec_push(&shown, e);
        else
            sk_vec_push(&rest, e);
    }
    for (size_t i = 0; i < shown.len; i++)
        sk_vec_push(out, shown.ptr[i]);
    for (size_t i = 0; i < rest.len; i++)
        sk_vec_push(out, rest.ptr[i]);
    sk_vec_free(&shown);
    sk_vec_free(&rest);
    sk_sessions_free(&all);
}

void sk_foreign_rows(const sk_foreign_entries *l, sk_jw *w)
{
    sk_jw_array(w);
    for (size_t i = 0; i < l->len; i++) {
        const sk_foreign_entry *e = &l->ptr[i];
        sk_jw_object(w);
        sk_jw_key(w, SK_STR("key"));
        sk_jw_string(w, sk_str_c(e->key));
        sk_jw_key(w, SK_STR("folder"));
        sk_jw_string(w, sk_str_c(e->folder));
        sk_jw_key(w, SK_STR("age"));
        sk_jw_number(w, e->age);
        sk_jw_key(w, SK_STR("tab"));
        if (e->tab_id)
            sk_jw_int(w, e->tab_id);
        else
            sk_jw_null(w);
        sk_jw_key(w, SK_STR("where"));
        sk_jw_string(w, sk_str_c(e->where));
        sk_jw_key(w, SK_STR("label"));
        sk_jw_string(w, sk_str_c(e->label));
        sk_jw_key(w, SK_STR("state"));
        sk_jw_string(w, sk_str_c(e->state));
        sk_jw_object_end(w);
    }
    sk_jw_array_end(w);
}

/* ---- watchAgent ---- */

/* What a Claude watch part owns: the session it looks for, whose argv is
 * the part's command. */
typedef struct {
    sk_foreign *f;
    sk_live_session s;
} claude_find;

static bool find_transcript(void *ud, char **file, char **session)
{
    claude_find *c = ud;
    sk_session_channel ch;
    if (!sk_foreign_channel(c->f, &c->s, &ch)) {
        sk_session_channel_free(&ch);
        return false;
    }
    *file = ch.file;
    *session = ch.session;
    ch.file = ch.session = NULL;
    sk_session_channel_free(&ch);
    return true;
}

static void free_find(void *ud)
{
    claude_find *c = ud;
    live_free(&c->s);
    free(c);
}

/* started, ms since the epoch, on the seam's clock: as long before its
 * now as before the wall clock's. Never 0, which is a session of the
 * app's. */
static uint64_t launched_of(double started)
{
    uint64_t now = sk_now_ms();
    double ago = wall_ms() - started;
    if (ago < 0)
        ago = 0;
    return ago < (double)now ? now - (uint64_t)ago : 1;
}

sk_agent *sk_foreign_watch(sk_foreign *f, const sk_foreign_session *w, sk_bus *bus)
{
    sk_agent_ctx ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.cwd = w->s.cwd;
    ctx.bus = bus;
    ctx.loop = f->cfg.loop;
    ctx.log = f->cfg.log;
    ctx.log_ud = f->cfg.ud;
    ctx.launched = launched_of(w->s.started);
    if (w->s.kind == SK_PROFILE_CLAUDE) {
        claude_find *c = sk_xcalloc(1, sizeof *c);
        c->f = f;
        live_copy(&c->s, &w->s);
        ctx.cmd = sk_strv_argv(&c->s.argv);
        ctx.cwd = c->s.cwd;
        return sk_agent_watch(&ctx, "claude", sk_claude_watch_owned(&ctx, find_transcript, c, free_find));
    }
    /* Codex copies what it keeps of the context. */
    char *dflt = w->found && w->found->home ? NULL : sk_profiles_default(SK_PROFILE_CODEX);
    sk_envp env = {0};
    sk_envp_set(&env, "CODEX_HOME", dflt ? dflt : w->found->home);
    ctx.env = &env;
    ctx.cmd = w->s.argv.ptr ? w->s.argv.ptr : (char *const[]){NULL};
    sk_agent *a = sk_agent_watch(&ctx, "codex", sk_provider_codex.create(&ctx));
    sk_envp_free(&env);
    free(dflt);
    return a;
}

/* ---- openSession ---- */

static bool among(const pid_t *pids, size_t n, pid_t pid)
{
    for (size_t i = 0; i < n; i++)
        if (pids[i] == pid)
            return true;
    return false;
}

int sk_foreign_open(sk_foreign *f, const char *key, char **err)
{
    *err = NULL;
    sk_live_sessions all = {0};
    scan(f, &all);
    const sk_live_session *s = NULL;
    sk_buf k = {0};
    for (size_t i = 0; i < all.len && !s; i++) {
        sk_buf_clear(&k);
        sk_foreign_key(&k, all.ptr[i].pid, all.ptr[i].started);
        if (!strcmp(sk_buf_cstr(&k), key))
            s = &all.ptr[i];
    }
    sk_buf_free(&k);
    if (!s) {
        sk_sessions_free(&all);
        *err = sk_xstrdup("That session has ended.");
        return -1;
    }
    pid_t up[SK_SESSIONS_MAX_ANCESTORS];
    size_t n = ancestors(f, s->pid, up, SK_LEN(up));
    sk_tab *own = NULL;
    for (size_t i = 0; f->cfg.tabs && i < sk_tabs_count(f->cfg.tabs) && !own; i++) {
        sk_tab *t = sk_tabs_at(f->cfg.tabs, i);
        pid_t p = pid_of(t);
        if (p == s->pid || (p && among(up, n, p)))
            own = t;
    }
    if (!own)
        own = sk_foreign_watching(f, s->pid);
    if (own) {
        sk_tabs_activate(f->cfg.tabs, own->id);
        sk_sessions_free(&all);
        return 0;
    }
    sk_session_channel ch;
    bool found = sk_foreign_channel(f, s, &ch);
    sk_foreign_session w = {*s, found ? &ch : NULL};
    sk_buf label = {0};
    sk_foreign_label(&label, s->kind, w.found, f->cfg.home);
    sk_strv cmd = {0};
    sk_strv_extend(&cmd, s->argv.ptr);
    const char *name = sk_buf_cstr(&label);
    sk_tab_spec spec = {SK_TAB_MONITOR, sk_strv_argv(&cmd), s->cwd, NULL, NULL, name, NULL, &w};
    char *why = NULL;
    sk_tab *tab = sk_tabs_add(f->cfg.tabs, &spec, -1, &why);
    int rc = 0;
    if (!tab && why) {
        logf_(f, "monitor: %s", why);
        sk_buf msg = {0};
        sk_buf_printf(&msg, "cannot watch it: %s", why);
        *err = msg.ptr;
        rc = -1;
    }
    free(why);
    sk_strv_free(&cmd);
    sk_buf_free(&label);
    sk_session_channel_free(&ch);
    sk_sessions_free(&all);
    return rc;
}
