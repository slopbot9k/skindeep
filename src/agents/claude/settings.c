/* The generated --settings file, as app/metrics/claude/settings.js
 * writes it: command hooks and the statusline relay, injected on the
 * child only (decisions 0009, 0010, 0020, 0030). Settings files are
 * read, never written. .claude.json is never read: it can hold a key.
 *
 * Decision 0036 changes the commands alone: each hook runs
 * `'<relay>' hook '<root>'` and the statusline `'<relay>' statusline
 * '<root>' <chain>`, skinned-relay in place of python3 and node. */
#include "internal.h"

#include "../../base/base64.h"
#include "../../base/paths.h"
#include "../../base/sha256.h"
#include "../../core/agentdirs.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

const char *const sk_claude_hook_events[SK_CLAUDE_HOOK_EVENTS] = {
    "PreToolUse", "PostToolUse", "PostToolUseFailure", "SubagentStart", "SubagentStop",
    "UserPromptSubmit", "Stop", "PreCompact", "PostCompact", "SessionStart", "Notification",
};

static const char *const MANAGED_NAMES[] = {"statusLine", "disableAllHooks", "allowManagedHooksOnly"};

/* Claude Code's managed settings, the highest source (decision 0010). */
const char *sk_claude_managed_dir(const sk_agent_ctx *ctx)
{
    if (ctx->managed_dir)
        return ctx->managed_dir;
    return sk_os_current() == SK_OS_MACOS ? "/Library/Application Support/ClaudeCode" : "/etc/claude-code";
}

void sk_claude_managed_names(unsigned managed, sk_buf *out)
{
    for (size_t i = 0; i < SK_LEN(MANAGED_NAMES); i++) {
        if (!(managed & (1u << i)))
            continue;
        if (out->len)
            sk_buf_append_c(out, ", ");
        sk_buf_append_c(out, MANAGED_NAMES[i]);
    }
}

const char *sk_claude_arg_value(char *const *argv, const char *name)
{
    size_t n = strlen(name);
    for (size_t i = 0; argv && argv[i]; i++) {
        if (!strcmp(argv[i], name))
            return argv[i + 1];
        if (!strncmp(argv[i], name, n) && argv[i][n] == '=')
            return argv[i] + n + 1;
    }
    return NULL;
}

void sk_claude_quote(sk_buf *out, const char *s)
{
    sk_buf_putc(out, '\'');
    for (; *s; s++) {
        if (*s == '\'')
            sk_buf_append_c(out, "'\\''");
        else
            sk_buf_putc(out, *s);
    }
    sk_buf_putc(out, '\'');
}

/* ---- Reading ---- */

static bool read_file(const char *path, sk_buf *out)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    char chunk[16384];
    bool ok = true;
    for (;;) {
        ssize_t r = read(fd, chunk, sizeof chunk);
        if (r < 0 && errno == EINTR)
            continue;
        if (r < 0)
            ok = false;
        if (r <= 0)
            break;
        sk_buf_append(out, chunk, (size_t)r);
    }
    close(fd);
    return ok;
}

static void copy_into(sk_arena *a, sk_json *dst, const sk_json *src)
{
    *dst = *src;
    switch (src->type) {
    case SK_JSON_STRING:
        dst->string.ptr = sk_arena_strndup(a, src->string.ptr, src->string.len);
        break;
    case SK_JSON_ARRAY:
        dst->array.items = sk_arena_array(a, src->array.len, sizeof *dst->array.items);
        for (size_t i = 0; i < src->array.len; i++)
            copy_into(a, &dst->array.items[i], &src->array.items[i]);
        break;
    case SK_JSON_OBJECT:
        dst->object.members = sk_arena_array(a, src->object.len, sizeof *dst->object.members);
        for (size_t i = 0; i < src->object.len; i++) {
            const sk_json_member *m = &src->object.members[i];
            dst->object.members[i].key.ptr = sk_arena_strndup(a, m->key.ptr, m->key.len);
            dst->object.members[i].key.len = m->key.len;
            copy_into(a, &dst->object.members[i].value, &m->value);
        }
        break;
    default:
        break;
    }
}

/* The only settings fields the app keeps, copied into a. A settings env
 * block can hold credentials, so only the compaction keys of it are. */
static void keep(sk_arena *a, const sk_json *d, sk_claude_kept *k)
{
    memset(k, 0, sizeof *k);
    if (!d || d->type != SK_JSON_OBJECT)
        return;
    const sk_json *line = sk_json_get(d, "statusLine");
    if (line) {
        sk_json *copy = sk_arena_alloc(a, sizeof *copy);
        copy_into(a, copy, line);
        k->status_line = copy;
    }
    k->has_window = sk_json_as_number(sk_json_get(d, "autoCompactWindow"), &k->window);
    const sk_json *env = sk_json_get(d, "env");
    for (size_t i = 0; i < SK_CLAUDE_COMPACT_KEYS; i++) {
        sk_str v;
        if (sk_json_as_string(sk_json_get(env, sk_claude_compact_keys[i]), &v))
            k->env[i] = sk_arena_strndup(a, v.ptr, v.len);
    }
}

/* Parses file, keeps what keep() keeps in a new layer, and drops the
 * rest at once. A managed file is a layer only when it holds an object
 * or an array, and keeps the two hook keys beside. */
static void read_layer(sk_claude_layers *ls, const char *name, const char *file, bool managed)
{
    sk_buf text = {0};
    sk_arena scratch;
    sk_arena_init(&scratch, 0);
    const sk_json *d = NULL;
    if (file && read_file(file, &text))
        d = sk_json_parse(&scratch, text.ptr ? text.ptr : "", text.len, NULL);
    if (!managed || (d && (d->type == SK_JSON_OBJECT || d->type == SK_JSON_ARRAY))) {
        sk_claude_layer layer;
        memset(&layer, 0, sizeof layer);
        layer.name = name;
        layer.file = file ? sk_arena_strndup(&ls->arena, file, strlen(file)) : NULL;
        keep(&ls->arena, d, &layer.data);
        if (managed) {
            bool b = false;
            layer.disable_all_hooks = sk_json_as_bool(sk_json_get(d, "disableAllHooks"), &b) && b;
            layer.allow_managed_hooks_only = sk_json_as_bool(sk_json_get(d, "allowManagedHooksOnly"), &b) && b;
        }
        sk_vec_push(&ls->v, layer);
    }
    sk_arena_free(&scratch);
    sk_buf_free(&text);
}

void sk_claude_layers_init(sk_claude_layers *ls)
{
    memset(ls, 0, sizeof *ls);
    sk_arena_init(&ls->arena, 4096);
}

void sk_claude_layers_free(sk_claude_layers *ls)
{
    sk_arena_free(&ls->arena);
    sk_vec_free(&ls->v);
}

/* path.resolve of an absolute or relative path: absolute, with no ".",
 * ".." or empty part. */
static char *resolve(const char *p)
{
    sk_buf in = {0};
    if (p[0] != '/') {
        char cwd[4096];
        if (getcwd(cwd, sizeof cwd))
            sk_buf_append_c(&in, cwd);
        sk_buf_putc(&in, '/');
    }
    sk_buf_append_c(&in, p);
    sk_buf out = {0};
    const char *s = sk_buf_cstr(&in);
    while (*s) {
        while (*s == '/')
            s++;
        const char *e = s;
        while (*e && *e != '/')
            e++;
        size_t n = (size_t)(e - s);
        if (n == 2 && s[0] == '.' && s[1] == '.') {
            while (out.len && out.ptr[out.len - 1] != '/')
                out.len--;
            if (out.len)
                out.len--;
            if (out.ptr)
                out.ptr[out.len] = 0;
        } else if (n && !(n == 1 && s[0] == '.')) {
            sk_buf_putc(&out, '/');
            sk_buf_append(&out, s, n);
        }
        s = e;
    }
    if (!out.len)
        sk_buf_putc(&out, '/');
    sk_buf_free(&in);
    return out.ptr;
}

/* Claude Code reads project and local settings from the git root when
 * the cwd is inside a repository (measured 2026-10-11), else from the
 * cwd. malloc'd. */
static char *project_dir(const char *cwd)
{
    char *d = resolve(cwd);
    for (;;) {
        char *git = sk_path_join(d, ".git");
        struct stat st;
        bool found = stat(git, &st) == 0;
        free(git);
        if (found)
            return d;
        char *slash = strrchr(d, '/');
        if (!slash || !strcmp(d, "/")) {
            free(d);
            return sk_xstrdup(cwd);
        }
        if (slash == d)
            slash[1] = 0;
        else
            *slash = 0;
    }
}

static bool allowed(const char *list, const char *name)
{
    const char *s = list;
    for (;;) {
        const char *e = strchr(s, ',');
        size_t n = e ? (size_t)(e - s) : strlen(s);
        if (sk_str_eq_c(sk_claude_trim(sk_str_n(s, n)), name))
            return true;
        if (!e)
            return false;
        s = e + 1;
    }
}

void sk_claude_layers_read(sk_claude_layers *ls, const sk_envp *env, const char *cwd, char *const *argv)
{
    const char *sources = sk_claude_arg_value(argv, "--setting-sources");
    if (!sources || !*sources)
        sources = "user,project,local";
    char *cfg = sk_claude_config_dir(env);
    char *proj = project_dir(cwd && *cwd ? cwd : ".");
    char *user = cfg ? sk_path_join(cfg, "settings.json") : NULL;
    char *project = sk_path_join(proj, ".claude/settings.json");
    char *local = sk_path_join(proj, ".claude/settings.local.json");
    const struct {
        const char *name, *file;
    } all[] = {{"user", user}, {"project", project}, {"local", local}};
    for (size_t i = 0; i < SK_LEN(all); i++)
        if (allowed(sources, all[i].name))
            read_layer(ls, all[i].name, all[i].file, false);
    free(cfg);
    free(proj);
    free(user);
    free(project);
    free(local);
}

static int by_name(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

void sk_claude_managed_read(sk_claude_layers *ls, const char *dir)
{
    char *main_file = sk_path_join(dir, "managed-settings.json");
    char *drop = sk_path_join(dir, "managed-settings.d");
    read_layer(ls, "managed", main_file, true);
    sk_strv names = {0};
    DIR *d = opendir(drop);
    struct dirent *e;
    while (d && (e = readdir(d))) {
        size_t n = strlen(e->d_name);
        if (n >= 5 && !strcmp(e->d_name + n - 5, ".json"))
            sk_strv_push(&names, e->d_name);
    }
    if (d)
        closedir(d);
    if (names.len)
        qsort(names.ptr, names.len, sizeof *names.ptr, by_name);
    for (size_t i = 0; i < names.len; i++) {
        char *f = sk_path_join(drop, names.ptr[i]);
        read_layer(ls, "managed", f, true);
        free(f);
    }
    sk_strv_free(&names);
    free(main_file);
    free(drop);
}

/* ---- The statusline ---- */

static bool object_like(const sk_json *v)
{
    return v && (v->type == SK_JSON_OBJECT || v->type == SK_JSON_ARRAY);
}

/* The effective statusLine of the layers, merged key by key, later
 * winning. Only the keys the injected entry reads are kept. */
typedef struct {
    bool any;
    const sk_json *command, *padding, *refresh, *hide_vim;
} status_line;

static void effective(const sk_claude_layers *ls, status_line *eff)
{
    memset(eff, 0, sizeof *eff);
    for (size_t i = 0; i < ls->v.len; i++) {
        const sk_json *s = ls->v.ptr[i].data.status_line;
        if (!object_like(s))
            continue;
        eff->any = true;
        const sk_json *v;
        if ((v = sk_json_get(s, "command")))
            eff->command = v;
        if ((v = sk_json_get(s, "padding")))
            eff->padding = v;
        if ((v = sk_json_get(s, "refreshInterval")))
            eff->refresh = v;
        if ((v = sk_json_get(s, "hideVimModeIndicator")))
            eff->hide_vim = v;
    }
    eff->any = eff->any && sk_claude_truthy(eff->command);
}

static unsigned managed_keys(const sk_claude_layers *ms)
{
    unsigned k = 0;
    for (size_t i = 0; i < ms->v.len; i++) {
        const sk_claude_layer *l = &ms->v.ptr[i];
        if (sk_claude_truthy(l->data.status_line) && object_like(l->data.status_line))
            k |= SK_CLAUDE_MANAGED_STATUSLINE;
        if (l->disable_all_hooks)
            k |= SK_CLAUDE_MANAGED_DISABLE_HOOKS;
        if (l->allow_managed_hooks_only)
            k |= SK_CLAUDE_MANAGED_HOOKS_ONLY;
    }
    return k;
}

/* ---- Writing ---- */

static void settings_text(sk_buf *out, const sk_agent_ctx *ctx, const status_line *chain)
{
    sk_buf hook = {0};
    sk_claude_quote(&hook, ctx->relay);
    sk_buf_append_c(&hook, " hook ");
    sk_claude_quote(&hook, ctx->root);
    sk_jw w;
    sk_jw_init(&w, out, 1);
    sk_jw_object(&w);
    sk_jw_key(&w, SK_STR("hooks"));
    sk_jw_object(&w);
    for (size_t i = 0; i < SK_CLAUDE_HOOK_EVENTS; i++) {
        sk_jw_key(&w, sk_str_c(sk_claude_hook_events[i]));
        sk_jw_array(&w);
        sk_jw_object(&w);
        sk_jw_key(&w, SK_STR("hooks"));
        sk_jw_array(&w);
        sk_jw_object(&w);
        sk_jw_key(&w, SK_STR("type"));
        sk_jw_string(&w, SK_STR("command"));
        sk_jw_key(&w, SK_STR("command"));
        sk_jw_string(&w, sk_buf_str(&hook));
        sk_jw_key(&w, SK_STR("timeout"));
        sk_jw_int(&w, 1);
        sk_jw_object_end(&w);
        sk_jw_array_end(&w);
        sk_jw_object_end(&w);
        sk_jw_array_end(&w);
    }
    sk_jw_object_end(&w);
    if (ctx->statusline == SK_STATUSLINE_ALWAYS || chain->any) {
        sk_buf line = {0};
        sk_claude_quote(&line, ctx->relay);
        sk_buf_append_c(&line, " statusline ");
        sk_claude_quote(&line, ctx->root);
        sk_buf_putc(&line, ' ');
        if (chain->any)
            sk_base64url_encode(&line, chain->command->string.ptr, chain->command->string.len);
        else
            sk_buf_putc(&line, '-');
        sk_jw_key(&w, SK_STR("statusLine"));
        sk_jw_object(&w);
        sk_jw_key(&w, SK_STR("type"));
        sk_jw_string(&w, SK_STR("command"));
        sk_jw_key(&w, SK_STR("command"));
        sk_jw_string(&w, sk_buf_str(&line));
        sk_jw_key(&w, SK_STR("padding"));
        if (chain->any && chain->padding)
            sk_jw_value(&w, chain->padding);
        else
            sk_jw_int(&w, 0);
        if (chain->any && chain->refresh) {
            sk_jw_key(&w, SK_STR("refreshInterval"));
            sk_jw_value(&w, chain->refresh);
        }
        if (chain->any && chain->hide_vim) {
            sk_jw_key(&w, SK_STR("hideVimModeIndicator"));
            sk_jw_value(&w, chain->hide_vim);
        }
        sk_jw_object_end(&w);
        sk_buf_free(&line);
    }
    sk_jw_object_end(&w);
    sk_buf_free(&hook);
}

/* The text names no window, tab or port: the relays find their window by
 * ancestry (decision 0030). So one text serves every window with the
 * same chain and mode, and the file outlives the window, named for the
 * hash of its text and never removed. Returns the file, malloc'd, or
 * NULL with errno. */
static char *write_settings(const char *store, const sk_buf *text)
{
    char hex[65];
    sk_sha256_hex(text->ptr, text->len, hex);
    hex[16] = 0;
    sk_buf file = {0};
    sk_buf_printf(&file, "%s%s%s.json", store, store[0] && store[strlen(store) - 1] == '/' ? "" : "/", hex);
    sk_buf old = {0};
    bool same = read_file(file.ptr, &old) && old.len == text->len && !memcmp(old.ptr, text->ptr, text->len);
    sk_buf_free(&old);
    if (same)
        return file.ptr;
    sk_buf tmp = {0};
    sk_buf_printf(&tmp, "%s.%ld.tmp", file.ptr, (long)getpid());
    int rc = -1;
    if (sk_mkdirs(store, 0700) == 0) {
        int fd = open(tmp.ptr, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        if (fd >= 0) {
            size_t done = 0;
            rc = 0;
            while (done < text->len) {
                ssize_t w = write(fd, text->ptr + done, text->len - done);
                if (w < 0 && errno == EINTR)
                    continue;
                if (w <= 0) {
                    rc = -1;
                    break;
                }
                done += (size_t)w;
            }
            int saved = errno;
            close(fd);
            errno = saved;
            if (rc == 0 && rename(tmp.ptr, file.ptr) != 0)
                rc = -1;
            if (rc != 0) {
                saved = errno;
                unlink(tmp.ptr);
                errno = saved;
            }
        }
    }
    int saved = errno;
    sk_buf_free(&tmp);
    if (rc != 0) {
        sk_buf_free(&file);
        errno = saved;
        return NULL;
    }
    return file.ptr;
}

/* The settings file of ls's chain into out->args. */
static int inject(const sk_agent_ctx *ctx, const sk_claude_layers *ls, sk_claude_prepared *out)
{
    status_line chain;
    effective(ls, &chain);
    /* A command that is not text cannot be carried, as Buffer.from
     * refused it. */
    if ((chain.any && chain.command->type != SK_JSON_STRING) || !ctx->relay || !ctx->root || !ctx->store) {
        errno = EINVAL;
        return -1;
    }
    sk_buf text = {0};
    settings_text(&text, ctx, &chain);
    char *file = write_settings(ctx->store, &text);
    sk_buf_free(&text);
    if (!file)
        return -1;
    sk_strv_push(&out->args, "--settings");
    sk_strv_push(&out->args, file);
    free(file);
    out->statusline = ctx->statusline == SK_STATUSLINE_ALWAYS || chain.any;
    out->chain = chain.any;
    return 0;
}

int sk_claude_settings_prepare(const sk_agent_ctx *ctx, sk_claude_prepared *out)
{
    memset(out, 0, sizeof *out);
    char *const *argv = ctx->cmd && ctx->cmd[0] ? ctx->cmd + 1 : NULL;
    sk_claude_layers ls, ms;
    sk_claude_layers_init(&ls);
    sk_claude_layers_init(&ms);
    sk_claude_layers_read(&ls, ctx->env, ctx->cwd, argv);
    sk_claude_managed_read(&ms, sk_claude_managed_dir(ctx));
    SK_VEC(sk_claude_kept) sources = {0};
    for (size_t i = 0; i < ls.v.len; i++)
        sk_vec_push(&sources, ls.v.ptr[i].data);
    for (size_t i = 0; i < ms.v.len; i++)
        sk_vec_push(&sources, ms.v.ptr[i].data);
    sk_claude_compaction_of(&out->compaction, sources.ptr, sources.len, argv, ctx->env);
    sk_vec_free(&sources);
    out->managed = managed_keys(&ms);
    int rc = 0;
    if (sk_claude_arg_value(argv, "--settings"))
        out->reason = "--settings given";
    else
        rc = inject(ctx, &ls, out);
    int saved = errno;
    sk_claude_layers_free(&ls);
    sk_claude_layers_free(&ms);
    errno = saved;
    return rc;
}

void sk_claude_prepared_free(sk_claude_prepared *p)
{
    sk_strv_free(&p->args);
}
