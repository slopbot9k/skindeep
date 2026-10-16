/* Ports app/profiles.js: decisions 0025, 0022 (records by write and
 * rename), 0027 (the home scan). */
#include "profiles.h"

#include "../base/json.h"
#include "../base/sha256.h"
#include "../core/agentdirs.h"
#include "files.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

const sk_profile_agent_info sk_profile_agents[SK_PROFILE_AGENTS] = {
    {"claude", "Claude Code", "claude", "CLAUDE_CONFIG_DIR", ".claude"},
    {"codex", "Codex", "codex", "CODEX_HOME", ".codex"},
};

bool sk_profiles_agent_of(sk_str kind, sk_profile_agent *out)
{
    for (int a = 0; a < SK_PROFILE_AGENTS; a++) {
        if (sk_str_eq_c(kind, sk_profile_agents[a].kind)) {
            *out = (sk_profile_agent)a;
            return true;
        }
    }
    return false;
}

char *sk_profiles_dir(const sk_env *e)
{
    char *state = sk_path_state(e);
    if (!state)
        return NULL;
    char *d = sk_path_join(state, "profiles");
    free(state);
    return d;
}

char *sk_profiles_default(sk_profile_agent a)
{
    char *d = a == SK_PROFILE_CLAUDE ? sk_claude_config_dir(NULL) : sk_codex_home(NULL);
    /* No home at all: where os.homedir() would point at worst. */
    return d ? d : sk_path_join("/", sk_profile_agents[a].dflt);
}

char *sk_profiles_of(const sk_envp *env, sk_profile_agent a)
{
    const char *v = env ? sk_envp_get(env, sk_profile_agents[a].variable) : NULL;
    return v && *v ? sk_files_resolve(v) : sk_profiles_default(a);
}

void sk_profiles_env_for(sk_envp *env, sk_profile_agent a, const char *p)
{
    char *dflt = sk_profiles_default(a);
    char *r = sk_files_resolve(p);
    sk_envp_set(env, sk_profile_agents[a].variable, strcmp(r, dflt) ? r : NULL);
    free(r);
    free(dflt);
}

/* <d>/<agent>-<the first 16 hex digits of the sha256 of p>. */
static char *file_of(const char *d, sk_profile_agent a, const char *p)
{
    char hex[65];
    sk_sha256_hex(p, strlen(p), hex);
    sk_buf name = {0};
    sk_buf_printf(&name, "%s-%.16s", sk_profile_agents[a].kind, hex);
    char *f = sk_path_join(d, name.ptr);
    sk_buf_free(&name);
    return f;
}

int sk_profiles_record(const char *d, sk_profile_agent a, const char *p, double now)
{
    if (sk_mkdirs(d, 0777) != 0)
        return -1;
    char *f = file_of(d, a, p);
    const char *base = strrchr(f, '/') + 1;
    sk_buf tmp = {0};
    sk_buf_printf(&tmp, "%s/.%s.%ld.tmp", d, base, (long)getpid());
    sk_buf text = {0};
    sk_jw w;
    sk_jw_init(&w, &text, 0);
    sk_jw_object(&w);
    sk_jw_key(&w, SK_STR("t"));
    sk_jw_number(&w, now);
    sk_jw_key(&w, SK_STR("agent"));
    sk_jw_string(&w, sk_str_c(sk_profile_agents[a].kind));
    sk_jw_key(&w, SK_STR("dir"));
    sk_jw_string(&w, sk_str_c(p));
    sk_jw_object_end(&w);
    sk_buf_putc(&text, '\n');

    int fd = open(tmp.ptr, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    bool ok = fd >= 0;
    for (size_t done = 0; ok && done < text.len;) {
        ssize_t n = write(fd, text.ptr + done, text.len - done);
        if (n > 0)
            done += (size_t)n;
        else if (n == 0 || errno != EINTR)
            ok = false;
    }
    if (fd >= 0 && close(fd) != 0)
        ok = false;
    if (ok && rename(tmp.ptr, f) != 0)
        ok = false;
    if (!ok && fd >= 0) {
        int saved = errno;
        unlink(tmp.ptr);
        errno = saved;
    }
    sk_buf_free(&text);
    sk_buf_free(&tmp);
    free(f);
    return ok ? 0 : -1;
}

/* /^(claude|codex)-[0-9a-f]{16}$/ */
static bool record_name(const char *n)
{
    const char *dash = strchr(n, '-');
    sk_profile_agent a;
    if (!dash || !sk_profiles_agent_of(sk_str_n(n, (size_t)(dash - n)), &a) || strlen(dash + 1) != 16)
        return false;
    for (const char *c = dash + 1; *c; c++)
        if (!((*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'f')))
            return false;
    return true;
}

/* One record, or false when the file is not one. */
static bool read_record(const char *path, sk_profile_record *out)
{
    sk_buf text = {0};
    bool ok = sk_files_read(path, 1 << 20, &text);
    sk_arena arena;
    sk_arena_init(&arena, 0);
    const sk_json *r = ok ? sk_json_parse(&arena, text.ptr, text.len, NULL) : NULL;
    const sk_json *t = sk_json_get(r, "t");
    sk_str agent, dir;
    ok = t && t->type == SK_JSON_NUMBER && sk_json_as_string(sk_json_get(r, "agent"), &agent) &&
         sk_profiles_agent_of(agent, &out->agent) && sk_json_as_string(sk_json_get(r, "dir"), &dir) &&
         dir.len && dir.ptr[0] == '/' && !memchr(dir.ptr, 0, dir.len);
    if (ok) {
        out->t = t->number;
        out->dir = sk_xstrndup(dir.ptr, dir.len);
    }
    sk_arena_free(&arena);
    sk_buf_free(&text);
    return ok;
}

void sk_profiles_read(const char *d, sk_profile_records *out)
{
    sk_strv names = {0};
    size_t from = out->len;
    if (sk_files_list(d, &names)) {
        for (size_t i = 0; i < names.len; i++) {
            if (!record_name(names.ptr[i]))
                continue;
            char *path = sk_path_join(d, names.ptr[i]);
            sk_profile_record r;
            if (read_record(path, &r))
                sk_vec_push(out, r);
            free(path);
        }
    }
    sk_strv_free(&names);
    /* Newest first, ties in the order read, as Array.prototype.sort is
     * stable. */
    for (size_t i = from + 1; i < out->len; i++) {
        sk_profile_record r = out->ptr[i];
        size_t j = i;
        for (; j > from && out->ptr[j - 1].t < r.t; j--)
            out->ptr[j] = out->ptr[j - 1];
        out->ptr[j] = r;
    }
}

void sk_profiles_forget(const char *d, sk_profile_agent a, const char *p)
{
    char *f = file_of(d, a, p);
    unlink(f);
    free(f);
}

static bool has(const sk_strv *names, const char *n)
{
    for (size_t i = 0; i < names->len; i++)
        if (!strcmp(names->ptr[i], n))
            return true;
    return false;
}

/* A config dir is told by the names it holds. Claude Code: its
 * .credentials.json, or a .claude.json beside its projects, sessions or
 * settings.json. Codex: its auth.json beside its config.toml, sessions
 * or history.jsonl. */
static bool kind_of_dir(const sk_strv *names, sk_profile_agent *out)
{
    if (has(names, ".credentials.json") ||
        (has(names, ".claude.json") &&
         (has(names, "projects") || has(names, "sessions") || has(names, "settings.json")))) {
        *out = SK_PROFILE_CLAUDE;
        return true;
    }
    if (has(names, "auth.json") &&
        (has(names, "config.toml") || has(names, "sessions") || has(names, "history.jsonl"))) {
        *out = SK_PROFILE_CODEX;
        return true;
    }
    return false;
}

static bool skipped(const char *n)
{
    return !strcmp(n, ".git") || !strcmp(n, "node_modules") || !strcmp(n, ".cache");
}

static int by_dir(const void *a, const void *b)
{
    return strcmp(((const sk_profile_found *)a)->dir, ((const sk_profile_found *)b)->dir);
}

void sk_profiles_scan(const char *root, int depth, size_t cap, sk_profile_founds *out)
{
    size_t from = out->len;
    sk_strv level = {0};
    sk_strv_push(&level, root);
    size_t listed = 0;
    for (int d = 0; d <= depth && level.len; d++) {
        sk_strv next = {0};
        bool full = false;
        for (size_t i = 0; i < level.len && !full; i++) {
            const char *p = level.ptr[i];
            if (listed++ >= cap) {
                full = true;
                break;
            }
            sk_strv names = {0};
            if (!sk_files_list(p, &names)) {
                sk_strv_free(&names);
                continue;
            }
            sk_profile_agent agent;
            if (d > 0 && kind_of_dir(&names, &agent)) {
                sk_vec_push(out, ((sk_profile_found){agent, sk_xstrdup(p)}));
            } else if (d < depth) {
                for (size_t k = 0; k < names.len; k++) {
                    if (skipped(names.ptr[k]))
                        continue;
                    char *child = sk_path_join(p, names.ptr[k]);
                    struct stat st;
                    if (lstat(child, &st) == 0 && S_ISDIR(st.st_mode))
                        sk_strv_push(&next, child);
                    free(child);
                }
            }
            sk_strv_free(&names);
        }
        sk_strv_free(&level);
        level = next;
        if (full)
            break;
    }
    sk_strv_free(&level);
    if (out->len > from)
        qsort(out->ptr + from, out->len - from, sizeof *out->ptr, by_dir);
}

static bool listed_already(const sk_profile_choices *out, size_t from, const char *dir)
{
    for (size_t i = from; i < out->len; i++)
        if (!strcmp(out->ptr[i].dir, dir))
            return true;
    return false;
}

void sk_profiles_list(sk_profile_agent a, const sk_profile_records *records, char *const *open,
                      size_t n_open, const sk_profile_founds *found, sk_profile_choices *out)
{
    char *dflt = sk_profiles_default(a);
    SK_VEC(const char *) all = {0};
    SK_VEC(const char *) mine = {0};
    sk_vec_push(&all, dflt);
    for (size_t i = 0; i < n_open; i++)
        sk_vec_push(&all, open[i]);
    for (size_t i = 0; records && i < records->len; i++) {
        if (records->ptr[i].agent == a) {
            sk_vec_push(&all, records->ptr[i].dir);
            sk_vec_push(&mine, records->ptr[i].dir);
        }
    }
    for (size_t i = 0; found && i < found->len; i++)
        if (found->ptr[i].agent == a)
            sk_vec_push(&all, found->ptr[i].dir);
    size_t from = out->len;
    for (size_t i = 0; i < all.len; i++) {
        const char *p = all.ptr[i];
        if (listed_already(out, from, p))
            continue;
        bool remembered = false;
        for (size_t k = 0; k < mine.len && !remembered; k++)
            remembered = !strcmp(mine.ptr[k], p);
        sk_vec_push(out, ((sk_profile_choice){sk_xstrdup(p), !strcmp(p, dflt), remembered}));
    }
    sk_vec_free(&all);
    sk_vec_free(&mine);
    free(dflt);
}

void sk_profile_records_free(sk_profile_records *r)
{
    for (size_t i = 0; i < r->len; i++)
        free(r->ptr[i].dir);
    sk_vec_free(r);
}

void sk_profile_founds_free(sk_profile_founds *f)
{
    for (size_t i = 0; i < f->len; i++)
        free(f->ptr[i].dir);
    sk_vec_free(f);
}

void sk_profile_choices_free(sk_profile_choices *c)
{
    for (size_t i = 0; i < c->len; i++)
        free(c->ptr[i].dir);
    sk_vec_free(c);
}
