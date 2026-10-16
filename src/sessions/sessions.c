/* Ports app/sessions.js: decision 0027, with the profiles of decision
 * 0025 and the rollout routes of decision 0013 through the caller. */
#include "sessions.h"

#include "../base/json.h"
#include "../core/agentdirs.h"
#include "../platform/proc.h"
#include "../platform/procinfo.h"
#include "files.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *const DAEMONS[] = {"app-server", "mcp-server"};

/* path.basename: the last part, trailing slashes dropped. */
static sk_str basename_of(const char *p)
{
    size_t n = strlen(p);
    while (n > 1 && p[n - 1] == '/')
        n--;
    size_t i = n;
    while (i > 0 && p[i - 1] != '/')
        i--;
    return sk_str_n(p + i, n - i);
}

/* The agent a process runs: its name, or the script a node runs. */
static bool kind_of(const char *name, const sk_strv *argv, sk_profile_agent *out)
{
    if (sk_profiles_agent_of(sk_str_c(name), out))
        return true;
    if (strcmp(name, "node") || argv->len < 2 || !argv->ptr[1][0])
        return false;
    return sk_profiles_agent_of(basename_of(argv->ptr[1]), out);
}

static bool is_daemon(const sk_strv *argv)
{
    for (size_t i = 0; i < argv->len; i++)
        for (size_t k = 0; k < SK_LEN(DAEMONS); k++)
            if (!strcmp(argv->ptr[i], DAEMONS[k]))
                return true;
    return false;
}

static void session_free(sk_session *s)
{
    free(s->cwd);
    sk_strv_free(&s->argv);
}

/* Newest first; a tie in the order /proc lists them, by pid as text. */
static bool before(const sk_session *a, const sk_session *b)
{
    if (a->started != b->started)
        return a->started > b->started;
    char x[24], y[24];
    snprintf(x, sizeof x, "%ld", (long)a->pid);
    snprintf(y, sizeof y, "%ld", (long)b->pid);
    return strcmp(x, y) < 0;
}

void sk_sessions_scan(uid_t uid, sk_sessions *out)
{
    sk_procinfo_owners all = {0};
    sk_procinfo_list(&all);
    sk_sessions found = {0};
    for (size_t i = 0; i < all.len; i++) {
        pid_t pid = all.ptr[i].pid;
        sk_procinfo info;
        if (all.ptr[i].uid != uid || !sk_procinfo_read(pid, &info))
            continue;
        sk_buf name = {0};
        sk_profile_agent kind;
        bool candidate = sk_procinfo_name(pid, &name) &&
                         (!strcmp(name.ptr, "node") || sk_profiles_agent_of(sk_buf_str(&name), &kind));
        sk_session s = {pid, info.ppid, SK_PROFILE_CLAUDE, (double)info.started_ms, NULL, {0}};
        if (candidate) {
            sk_procinfo_argv(pid, &s.argv);
            candidate = kind_of(name.ptr, &s.argv, &s.kind) &&
                        !(s.kind == SK_PROFILE_CODEX && is_daemon(&s.argv)) &&
                        (s.cwd = sk_procinfo_cwd(pid)) != NULL;
        }
        sk_buf_free(&name);
        if (candidate)
            sk_vec_push(&found, s);
        else
            session_free(&s);
    }
    sk_vec_free(&all);
    size_t from = out->len;
    for (size_t i = 0; i < found.len; i++) {
        sk_session *s = &found.ptr[i];
        bool inner = false;
        for (size_t k = 0; k < found.len && !inner; k++)
            inner = found.ptr[k].pid == s->ppid && found.ptr[k].kind == s->kind;
        if (inner) {
            session_free(s);
            continue;
        }
        sk_vec_push(out, *s);
        for (size_t j = out->len - 1; j > from && before(&out->ptr[j], &out->ptr[j - 1]); j--) {
            sk_session t = out->ptr[j];
            out->ptr[j] = out->ptr[j - 1];
            out->ptr[j - 1] = t;
        }
    }
    sk_vec_free(&found);
}

void sk_sessions_free(sk_sessions *s)
{
    for (size_t i = 0; i < s->len; i++)
        session_free(&s->ptr[i]);
    sk_vec_free(s);
}

size_t sk_sessions_ancestors(pid_t pid, pid_t *out, size_t max)
{
    size_t n = 0;
    max = SK_MIN(max, (size_t)SK_SESSIONS_MAX_ANCESTORS);
    for (pid_t p = pid; n < max;) {
        sk_proc_stat st;
        if (!sk_proc_read(p, &st) || st.ppid <= 1)
            break;
        out[n++] = p = st.ppid;
    }
    return n;
}

bool sk_sessions_same(pid_t pid, double started)
{
    sk_procinfo info;
    if (!sk_procinfo_read(pid, &info))
        return false;
    double d = (double)info.started_ms - started;
    return d < 1000 && d > -1000;
}

static const char *const DAYS[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *const MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

void sk_claude_proc_start(sk_buf *out, sk_os os, uint64_t start)
{
    if (os == SK_OS_LINUX) {
        sk_buf_printf(out, "%llu", (unsigned long long)start);
        return;
    }
    /* ps's lstart, "%a %b %e %T %Y", in the C locale, in UTC. */
    time_t t = (time_t)(start / 1000000u);
    struct tm tm;
    if (!gmtime_r(&t, &tm))
        return;
    sk_buf_printf(out, "%s %s %2d %02d:%02d:%02d %d", DAYS[tm.tm_wday % 7], MONTHS[tm.tm_mon % 12],
                  tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, tm.tm_year + 1900);
}

/* s without leading and trailing spaces, each run of them one space. */
static void squeeze(sk_buf *out, sk_str s)
{
    s = sk_str_trim(s);
    for (size_t i = 0; i < s.len; i++) {
        bool space = s.ptr[i] == ' ' || s.ptr[i] == '\t';
        if (!space)
            sk_buf_putc(out, s.ptr[i]);
        else if (out->len && out->ptr[out->len - 1] != ' ')
            sk_buf_putc(out, ' ');
    }
}

bool sk_claude_proc_start_is(sk_str proc_start, sk_os os, uint64_t start)
{
    sk_buf want = {0};
    sk_claude_proc_start(&want, os, start);
    bool same;
    if (os == SK_OS_LINUX) {
        same = sk_str_eq(proc_start, sk_buf_str(&want));
    } else {
        sk_buf a = {0}, b = {0};
        squeeze(&a, proc_start);
        squeeze(&b, sk_buf_str(&want));
        same = a.len && sk_str_eq(sk_buf_str(&a), sk_buf_str(&b));
        sk_buf_free(&a);
        sk_buf_free(&b);
    }
    sk_buf_free(&want);
    return same;
}

/* Profiles grouped by the real folder they keep sub in, in the order
 * first seen: profiles that link one folder cannot be told apart. */
typedef struct {
    char *real;
    sk_strv profiles;
} group;
typedef SK_VEC(group) groups;

static void by_real_dir(char *const *profiles, const char *sub, groups *out)
{
    for (; profiles && *profiles; profiles++) {
        char *p = sk_path_join(*profiles, sub);
        char *real = realpath(p, NULL);
        free(p);
        if (!real)
            continue;
        group *g = NULL;
        for (size_t i = 0; i < out->len && !g; i++)
            if (!strcmp(out->ptr[i].real, real))
                g = &out->ptr[i];
        if (g) {
            free(real);
        } else {
            sk_vec_push(out, ((group){real, {0}}));
            g = &out->ptr[out->len - 1];
        }
        sk_strv_push(&g->profiles, *profiles);
    }
}

static void groups_free(groups *g)
{
    for (size_t i = 0; i < g->len; i++) {
        free(g->ptr[i].real);
        sk_strv_free(&g->ptr[i].profiles);
    }
    sk_vec_free(g);
}

/* The one profile of ps that holds session-env/<session>, a folder
 * Claude Code makes in the profile itself even when its projects folder
 * is linked from another, tested by name alone. NULL when none or
 * several do. */
static char *holder(char *const *ps, const char *session)
{
    const char *one = NULL;
    size_t holds = 0;
    for (; ps && *ps; ps++) {
        char *env = sk_path_join(*ps, "session-env");
        char *p = sk_path_join(env, session);
        if (sk_files_is_dir(p)) {
            holds++;
            one = *ps;
        }
        free(p);
        free(env);
    }
    return holds == 1 ? sk_xstrdup(one) : NULL;
}

static bool plain_name(sk_str s)
{
    if (!s.len)
        return false;
    for (size_t i = 0; i < s.len; i++) {
        char c = s.ptr[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
            return false;
    }
    return true;
}

/* The three fields of one record, read with the pull reader so that no
 * other value is decoded. String(procStart) for a string or a number;
 * any other kind of value names no start. */
typedef struct {
    bool pid_ok, start_ok, session_ok;
    double pid;
    sk_buf start, session;
} record_fields;

static bool read_fields(const char *text, size_t n, record_fields *f)
{
    sk_jr r;
    sk_jr_init(&r, text, n);
    bool ok = sk_jr_peek(&r) == SK_JSON_OBJECT && sk_jr_object(&r);
    sk_str key;
    while (ok && sk_jr_key(&r, &key)) {
        sk_json_type t = sk_jr_peek(&r);
        if (sk_str_eq_c(key, "pid") && t == SK_JSON_NUMBER) {
            f->pid_ok = sk_jr_number(&r, &f->pid);
        } else if (sk_str_eq_c(key, "procStart") && (t == SK_JSON_NUMBER || t == SK_JSON_STRING)) {
            sk_buf_clear(&f->start);
            double d;
            sk_str s;
            if (t == SK_JSON_NUMBER && sk_jr_number(&r, &d))
                sk_json_format_number(&f->start, d);
            else if (t == SK_JSON_STRING && sk_jr_string(&r, &s))
                sk_buf_append_str(&f->start, s);
            f->start_ok = true;
        } else if (sk_str_eq_c(key, "sessionId") && t == SK_JSON_STRING) {
            sk_str s;
            sk_buf_clear(&f->session);
            f->session_ok = sk_jr_string(&r, &s);
            if (f->session_ok)
                sk_buf_append_str(&f->session, s);
        } else {
            /* A repeated key holds its last value, as in JSON.parse. */
            if (sk_str_eq_c(key, "pid"))
                f->pid_ok = false;
            else if (sk_str_eq_c(key, "procStart"))
                f->start_ok = false;
            else if (sk_str_eq_c(key, "sessionId"))
                f->session_ok = false;
            sk_jr_skip(&r);
        }
    }
    ok = ok && !sk_jr_failed(&r) && sk_jr_done(&r);
    sk_jr_free(&r);
    return ok;
}

bool sk_sessions_claude_record(pid_t pid, char *const *profiles, sk_claude_record *out)
{
    sk_procinfo info;
    if (!sk_procinfo_read(pid, &info))
        return false;
    sk_os os = sk_os_current();
    groups g = {0};
    by_real_dir(profiles, "sessions", &g);
    bool found = false;
    for (size_t i = 0; i < g.len && !found; i++) {
        sk_buf path = {0}, text = {0};
        sk_buf_printf(&path, "%s/%ld.json", g.ptr[i].real, (long)pid);
        record_fields f = {0};
        if (sk_files_read(path.ptr, 1 << 20, &text) && read_fields(text.ptr, text.len, &f) && f.pid_ok &&
            f.pid == (double)pid && f.start_ok &&
            sk_claude_proc_start_is(sk_buf_str(&f.start), os, info.start) && f.session_ok &&
            plain_name(sk_buf_str(&f.session))) {
            out->session = sk_xstrdup(f.session.ptr);
            memset(&out->profiles, 0, sizeof out->profiles);
            sk_strv_extend(&out->profiles, sk_strv_argv(&g.ptr[i].profiles));
            found = true;
        }
        sk_buf_free(&f.start);
        sk_buf_free(&f.session);
        sk_buf_free(&text);
        sk_buf_free(&path);
    }
    groups_free(&g);
    return found;
}

void sk_claude_record_free(sk_claude_record *r)
{
    free(r->session);
    sk_strv_free(&r->profiles);
    r->session = NULL;
}

void sk_session_channel_free(sk_session_channel *c)
{
    free(c->file);
    free(c->session);
    free(c->profile);
    free(c->home);
    memset(c, 0, sizeof *c);
}

/* <real>/<slug of cwd>, Claude Code's folder for cwd under projects. */
static char *project_dir(const char *real, const char *cwd)
{
    sk_buf slug = {0};
    sk_claude_slug(&slug, sk_str_c(cwd));
    char *d = sk_path_join(real, sk_buf_cstr(&slug));
    sk_buf_free(&slug);
    return d;
}

static bool by_record(const char *cwd, const sk_claude_record *rec, sk_session_channel *out)
{
    char *const *ps = rec->profiles.ptr;
    out->profile = rec->profiles.len == 1 ? sk_xstrdup(ps[0]) : holder(ps, rec->session);
    out->session = sk_xstrdup(rec->session);
    groups g = {0};
    by_real_dir(ps, "projects", &g);
    for (size_t i = 0; i < g.len && !out->file; i++) {
        char *d = project_dir(g.ptr[i].real, cwd);
        sk_buf name = {0};
        sk_buf_printf(&name, "%s.jsonl", rec->session);
        char *file = sk_path_join(d, name.ptr);
        if (sk_files_mtime_ms(file, &out->mtime))
            out->file = file;
        else
            free(file);
        sk_buf_free(&name);
        free(d);
    }
    groups_free(&g);
    if (!out->file)
        out->mtime = 0;
    return true;
}

bool sk_sessions_claude_transcript(pid_t pid, const char *cwd, double started, char *const *profiles,
                                   sk_session_channel *out)
{
    memset(out, 0, sizeof *out);
    sk_claude_record rec;
    if (pid && sk_sessions_claude_record(pid, profiles, &rec)) {
        by_record(cwd, &rec, out);
        sk_claude_record_free(&rec);
        return true;
    }
    groups g = {0};
    by_real_dir(profiles, "projects", &g);
    for (size_t i = 0; i < g.len; i++) {
        char *d = project_dir(g.ptr[i].real, cwd);
        sk_strv names = {0};
        if (sk_files_list(d, &names)) {
            for (size_t k = 0; k < names.len; k++) {
                sk_str n = sk_str_c(names.ptr[k]);
                double m;
                if (!sk_str_suffix(n, SK_STR(".jsonl")))
                    continue;
                char *file = sk_path_join(d, names.ptr[k]);
                if (!sk_files_mtime_ms(file, &m) || m < started - SK_SESSIONS_GRACE_MS ||
                    (out->file && m <= out->mtime)) {
                    free(file);
                    continue;
                }
                free(out->file);
                free(out->session);
                out->file = file;
                out->session = sk_xstrndup(n.ptr, n.len - 6);
                out->mtime = m;
            }
        }
        sk_strv_free(&names);
        free(d);
    }
    groups_free(&g);
    if (!out->file)
        return false;
    out->profile = holder(profiles, out->session);
    return true;
}

bool sk_sessions_codex_rollout(const sk_session *s, char *const *homes, sk_codex_rollout_fn find,
                               void *ud, sk_session_channel *out)
{
    memset(out, 0, sizeof *out);
    groups g = {0};
    by_real_dir(homes, "sessions", &g);
    char *const *args = s->argv.len ? s->argv.ptr + 1 : (char *const[]){NULL};
    bool found = false;
    for (size_t i = 0; i < g.len && !found; i++) {
        const sk_strv *ps = &g.ptr[i].profiles;
        sk_codex_rollout r = {0};
        if (!find(ud, s->pid, ps->ptr[0], args, s->cwd, s->started, &r) || !r.file) {
            free(r.file);
            free(r.thread);
            continue;
        }
        double m = 0;
        if (!sk_files_mtime_ms(r.file, &m))
            m = 0;
        out->file = r.file;
        out->session = r.thread;
        out->mtime = m;
        out->profile = ps->len == 1 ? sk_xstrdup(ps->ptr[0]) : NULL;
        out->home = sk_xstrdup(ps->ptr[0]);
        out->how = r.how;
        found = true;
    }
    groups_free(&g);
    return found;
}

const char *sk_sessions_state_of(double mtime, double now)
{
    if (!mtime)
        return "unknown";
    double ago = now - mtime;
    return ago < 10000 ? "working" : ago < 600000 ? "waiting" : "idle";
}
