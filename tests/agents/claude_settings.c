/* The settings writer at its interface, as test/claude-settings.test.js
 * has it: a temp profile, a temp project that is its own git root, a
 * temp runtime root and store. The relay commands are decision 0036's:
 * skinned-relay in place of python3 and node. */
#include "../../src/agents/claude/internal.h"
#include "../../src/base/base64.h"
#include "../../src/base/paths.h"
#include "../../src/base/sha256.h"
#include "../test.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#define RELAY "/opt/sk/bin/skinned-relay"
#define SECRET "sk-ant-test-credential-0123456789"

typedef struct {
    char *top, *cfg, *repo, *run, *root, *store, *no_managed;
    sk_envp env;
} dirs;

static char *under(const char *top, const char *name)
{
    return sk_path_join(top, name);
}

static void dirs_make(dirs *d)
{
    d->top = sk_xstrdup(sk_test_tmpdir());
    d->cfg = under(d->top, "cfg");
    d->repo = under(d->top, "repo");
    d->run = under(d->top, "run");
    d->root = under(d->top, "root");
    d->store = under(d->top, "store");
    d->no_managed = under(d->top, "no-managed");
    const char *made[] = {d->cfg, d->repo, d->run, d->root};
    for (size_t i = 0; i < SK_LEN(made); i++)
        CHECK(sk_mkdirs(made[i], 0700) == 0);
    char *git = under(d->repo, ".git"), *claude = under(d->repo, ".claude");
    CHECK(sk_mkdirs(git, 0700) == 0 && sk_mkdirs(claude, 0700) == 0);
    free(git);
    free(claude);
    sk_envp_from(&d->env, NULL);
    sk_envp_set(&d->env, "CLAUDE_CONFIG_DIR", d->cfg);
}

static void dirs_free(dirs *d)
{
    char **all[] = {&d->top, &d->cfg, &d->repo, &d->run, &d->root, &d->store, &d->no_managed};
    for (size_t i = 0; i < SK_LEN(all); i++)
        free(*all[i]);
    sk_envp_free(&d->env);
}

static void write_file(const char *path, const char *text)
{
    char *dir = sk_xstrdup(path);
    *strrchr(dir, '/') = 0;
    CHECK(sk_mkdirs(dir, 0700) == 0);
    free(dir);
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

static void write_in(const char *dir, const char *rel, const char *text)
{
    char *p = sk_path_join(dir, rel);
    write_file(p, text);
    free(p);
}

static sk_agent_ctx ctx_of(dirs *d, char *const *cmd, const char *cwd, sk_statusline_mode mode, const char *managed)
{
    sk_agent_ctx c;
    memset(&c, 0, sizeof c);
    c.cmd = cmd;
    c.cwd = cwd ? cwd : d->repo;
    c.env = &d->env;
    c.run_dir = d->run;
    c.root = d->root;
    c.store = d->store;
    c.relay = RELAY;
    c.statusline = mode;
    c.managed_dir = managed ? managed : d->no_managed;
    return c;
}

static char *const CLAUDE[] = {"claude", NULL};

static sk_claude_prepared prepare(dirs *d, char *const *cmd, const char *cwd, sk_statusline_mode mode,
                                  const char *managed)
{
    sk_agent_ctx c = ctx_of(d, cmd ? cmd : CLAUDE, cwd, mode, managed);
    sk_claude_prepared r;
    CHECK(sk_claude_settings_prepare(&c, &r) == 0);
    return r;
}

static sk_arena A;

/* The settings file r names, parsed. */
static const sk_json *read_settings(const sk_claude_prepared *r)
{
    CHECK(r->args.len == 2);
    if (r->args.len != 2)
        return NULL;
    char *text = sk_test_read(r->args.ptr[1], NULL);
    CHECK(text != NULL);
    const sk_json *v = text ? sk_json_parse(&A, text, strlen(text), NULL) : NULL;
    free(text);
    return v;
}

static const char *command_of(const sk_json *s)
{
    sk_str c = {"", 0};
    sk_json_as_string(sk_json_dig(s, "statusLine", "command", NULL), &c);
    return c.ptr;
}

/* The chained command the statusline relay carries, malloc'd, or NULL. */
static char *chain_of(const sk_claude_prepared *r)
{
    const sk_json *s = read_settings(r);
    const char *cmd = command_of(s);
    const char *last = strrchr(cmd, ' ');
    last = last ? last + 1 : cmd;
    if (!strcmp(last, "-"))
        return NULL;
    sk_buf out = {0};
    CHECK(sk_base64url_decode(&out, sk_str_c(last)));
    sk_buf_reserve(&out, 0);
    return out.ptr;
}

#define CHECK_CHAIN(r, want)                                                 \
    do {                                                                     \
        char *c_ = chain_of(r);                                              \
        const char *w_ = (want);                                             \
        if (w_)                                                              \
            CHECK_STR(c_, w_);                                               \
        else                                                                 \
            CHECK(c_ == NULL);                                               \
        free(c_);                                                            \
    } while (0)

static void test_w2_setting_sources_project_ignores_a_user_statusline(void)
{
    sk_arena_init(&A, 0);
    dirs d;
    dirs_make(&d);
    write_in(d.cfg, "settings.json", "{\"statusLine\":{\"type\":\"command\",\"command\":\"user-line\"}}");
    sk_claude_prepared r = prepare(&d, NULL, NULL, SK_STATUSLINE_ALWAYS, NULL);
    CHECK_CHAIN(&r, "user-line");
    sk_claude_prepared_free(&r);
    char *project[] = {"claude", "--setting-sources", "project", NULL};
    r = prepare(&d, project, NULL, SK_STATUSLINE_ALWAYS, NULL);
    CHECK_CHAIN(&r, NULL);
    sk_claude_prepared_free(&r);
    dirs_free(&d);
    sk_arena_free(&A);
}

static void test_w3_a_statusline_in_the_git_roots_local_settings_is_chained_from_below(void)
{
    sk_arena_init(&A, 0);
    dirs d;
    dirs_make(&d);
    write_in(d.repo, ".claude/settings.local.json", "{\"statusLine\":{\"type\":\"command\",\"command\":\"local-line\"}}");
    char *sub = sk_path_join(d.repo, "a/b");
    CHECK(sk_mkdirs(sub, 0700) == 0);
    sk_claude_prepared r = prepare(&d, NULL, sub, SK_STATUSLINE_ALWAYS, NULL);
    CHECK_CHAIN(&r, "local-line");
    sk_claude_prepared_free(&r);
    free(sub);
    dirs_free(&d);
    sk_arena_free(&A);
}

static void test_w4_a_settings_env_block_keeps_only_the_compaction_keys(void)
{
    sk_arena_init(&A, 0);
    dirs d;
    dirs_make(&d);
    write_in(d.cfg, "settings.json",
             "{\"env\":{\"ANTHROPIC_AUTH_TOKEN\":\"" SECRET "\",\"CLAUDE_CODE_AUTO_COMPACT_WINDOW\":\"400000\"}}");
    sk_claude_prepared r = prepare(&d, NULL, NULL, SK_STATUSLINE_ALWAYS, NULL);
    CHECK(r.compaction.window == 400000);
    for (size_t i = 0; i < r.args.len; i++)
        CHECK(strstr(r.args.ptr[i], SECRET) == NULL);
    sk_claude_layers ls;
    sk_claude_layers_init(&ls);
    sk_claude_layers_read(&ls, &d.env, d.repo, NULL);
    CHECK_INT(ls.v.len, 3);
    for (size_t i = 0; i < ls.v.len; i++)
        for (size_t k = 0; k < SK_CLAUDE_COMPACT_KEYS; k++) {
            const char *v = ls.v.ptr[i].data.env[k];
            if (i == 0 && k == 0)
                CHECK_STR(v, "400000");
            else
                CHECK(v == NULL);
        }
    sk_claude_layers_free(&ls);
    char *file = sk_test_read(r.args.ptr[1], NULL);
    CHECK(file && !strstr(file, SECRET));
    free(file);
    sk_claude_prepared_free(&r);
    dirs_free(&d);
    sk_arena_free(&A);
}

static void test_w5_chain_only_writes_the_hooks_alone_always_writes_the_relay(void)
{
    sk_arena_init(&A, 0);
    dirs d;
    dirs_make(&d);
    sk_claude_prepared r = prepare(&d, NULL, NULL, SK_STATUSLINE_CHAIN_ONLY, NULL);
    const sk_json *s = read_settings(&r);
    const sk_json *hooks = sk_json_get(s, "hooks");
    CHECK(hooks && hooks->type == SK_JSON_OBJECT && hooks->object.len == SK_CLAUDE_HOOK_EVENTS);
    sk_buf want = {0}, cmd = {0};
    sk_buf_printf(&cmd, "'%s' hook '%s'", RELAY, d.root);
    sk_jw w;
    sk_jw_init(&w, &want, 0);
    sk_buf_append_c(&want, "[{\"hooks\":[{\"type\":\"command\",\"command\":");
    sk_jw_string(&w, sk_buf_str(&cmd));
    sk_buf_append_c(&want, ",\"timeout\":1}]}]");
    for (size_t i = 0; hooks && i < hooks->object.len; i++) {
        CHECK_MEM(hooks->object.members[i].key, sk_claude_hook_events[i]);
        sk_buf got = {0};
        sk_jw g;
        sk_jw_init(&g, &got, 0);
        sk_jw_value(&g, &hooks->object.members[i].value);
        CHECK_STR(got.ptr, want.ptr);
        sk_buf_free(&got);
    }
    CHECK(sk_json_get(s, "statusLine") == NULL);
    CHECK(!r.statusline && !r.chain);
    sk_claude_prepared_free(&r);

    write_in(d.cfg, "settings.json",
             "{\"statusLine\":{\"type\":\"command\",\"command\":\"mine\",\"padding\":2,\"refreshInterval\":5,"
             "\"hideVimModeIndicator\":true}}");
    r = prepare(&d, NULL, NULL, SK_STATUSLINE_ALWAYS, NULL);
    s = read_settings(&r);
    sk_buf prefix = {0};
    sk_buf_printf(&prefix, "'%s' statusline '%s' ", RELAY, d.root);
    CHECK(!strncmp(command_of(s), prefix.ptr, prefix.len));
    CHECK_CHAIN(&r, "mine");
    double pad, refresh;
    bool hide;
    const sk_json *line = sk_json_get(s, "statusLine");
    CHECK(sk_json_as_number(sk_json_get(line, "padding"), &pad) && pad == 2);
    CHECK(sk_json_as_number(sk_json_get(line, "refreshInterval"), &refresh) && refresh == 5);
    CHECK(sk_json_as_bool(sk_json_get(line, "hideVimModeIndicator"), &hide) && hide);
    CHECK(r.statusline && r.chain);
    sk_claude_prepared_free(&r);
    sk_buf_free(&prefix);
    sk_buf_free(&want);
    sk_buf_free(&cmd);
    dirs_free(&d);
    sk_arena_free(&A);
}

/* The text is JSON.stringify(settings, null, 1), byte for byte. */
static void test_the_settings_text_is_json_stringify_with_indent_1(void)
{
    sk_arena_init(&A, 0);
    dirs d;
    dirs_make(&d);
    sk_claude_prepared r = prepare(&d, NULL, NULL, SK_STATUSLINE_ALWAYS, NULL);
    char *text = sk_test_read(r.args.ptr[1], NULL);
    sk_buf head = {0}, tail = {0};
    sk_buf_printf(&head,
                  "{\n \"hooks\": {\n  \"PreToolUse\": [\n   {\n    \"hooks\": [\n     {\n      \"type\": \"command\",\n"
                  "      \"command\": \"'%s' hook '%s'\",\n      \"timeout\": 1\n     }\n    ]\n   }\n  ],\n"
                  "  \"PostToolUse\": [\n",
                  RELAY, d.root);
    sk_buf_printf(&tail,
                  "  ]\n },\n \"statusLine\": {\n  \"type\": \"command\",\n  \"command\": \"'%s' statusline '%s' -\",\n"
                  "  \"padding\": 0\n }\n}",
                  RELAY, d.root);
    CHECK(text && !strncmp(text, head.ptr, head.len));
    CHECK(text && strlen(text) > tail.len && !strcmp(text + strlen(text) - tail.len, tail.ptr));
    /* Named for the first 16 hex digits of the text's sha256. */
    char hex[65];
    sk_sha256_hex(text, text ? strlen(text) : 0, hex);
    hex[16] = 0;
    CHECK(strstr(r.args.ptr[1], hex) != NULL);
    CHECK_STR(r.args.ptr[0], "--settings");
    free(text);
    sk_buf_free(&head);
    sk_buf_free(&tail);
    sk_claude_prepared_free(&r);
    dirs_free(&d);
    sk_arena_free(&A);
}

static void test_w6_the_file_is_mode_0600_named_for_its_text_written_once_and_kept(void)
{
    sk_arena_init(&A, 0);
    dirs d;
    dirs_make(&d);
    sk_claude_prepared r = prepare(&d, NULL, NULL, SK_STATUSLINE_ALWAYS, NULL);
    struct stat st, store;
    CHECK(stat(r.args.ptr[1], &st) == 0 && (st.st_mode & 0777) == 0600);
    CHECK(stat(d.store, &store) == 0 && (store.st_mode & 0777) == 0700);
    CHECK(!strncmp(r.args.ptr[1], d.store, strlen(d.store)) && r.args.ptr[1][strlen(d.store)] == '/' &&
          !strchr(r.args.ptr[1] + strlen(d.store) + 1, '/'));
    sk_claude_prepared again = prepare(&d, NULL, NULL, SK_STATUSLINE_ALWAYS, NULL);
    CHECK_STR(again.args.ptr[1], r.args.ptr[1]); /* the same text, the same file */
    struct stat st2;
    CHECK(stat(r.args.ptr[1], &st2) == 0 && st2.st_ino == st.st_ino); /* not written again */
    write_in(d.cfg, "settings.json", "{\"statusLine\":{\"type\":\"command\",\"command\":\"other\"}}");
    sk_claude_prepared r2 = prepare(&d, NULL, NULL, SK_STATUSLINE_ALWAYS, NULL);
    CHECK(strcmp(r2.args.ptr[1], r.args.ptr[1]) != 0); /* another chain, another file */
    CHECK(access(r.args.ptr[1], F_OK) == 0);            /* the first file stays */
    int files = 0;
    DIR *dir = opendir(d.store);
    struct dirent *e;
    while (dir && (e = readdir(dir)))
        files += e->d_name[0] != '.';
    if (dir)
        closedir(dir);
    CHECK_INT(files, 2);
    sk_claude_prepared_free(&r);
    sk_claude_prepared_free(&again);
    sk_claude_prepared_free(&r2);
    dirs_free(&d);
    sk_arena_free(&A);
}

static void test_w7_managed_settings_set_the_keys_and_managed_compaction_wins(void)
{
    sk_arena_init(&A, 0);
    dirs d;
    dirs_make(&d);
    char *managed = under(d.top, "managed");
    char *main_file = sk_path_join(managed, "managed-settings.json");
    write_in(d.cfg, "settings.json", "{\"autoCompactWindow\":600000}");
    struct {
        const char *data;
        unsigned key;
    } cases[] = {
        {"\"statusLine\":{\"type\":\"command\",\"command\":\"corp-line\"}", SK_CLAUDE_MANAGED_STATUSLINE},
        {"\"disableAllHooks\":true", SK_CLAUDE_MANAGED_DISABLE_HOOKS},
        {"\"allowManagedHooksOnly\":true", SK_CLAUDE_MANAGED_HOOKS_ONLY},
    };
    for (size_t i = 0; i < SK_LEN(cases); i++) {
        sk_buf text = {0};
        sk_buf_printf(&text, "{\"autoCompactWindow\":300000,%s}", cases[i].data);
        write_file(main_file, text.ptr);
        sk_claude_prepared r = prepare(&d, NULL, NULL, SK_STATUSLINE_ALWAYS, managed);
        CHECK_INT(r.managed, cases[i].key);
        CHECK(r.compaction.window == 300000);
        sk_claude_prepared_free(&r);
        sk_buf_free(&text);
    }
    unlink(main_file);
    write_in(managed, "managed-settings.d/10-corp.json",
             "{\"env\":{\"CLAUDE_CODE_AUTO_COMPACT_WINDOW\":\"250000\",\"ANTHROPIC_AUTH_TOKEN\":\"" SECRET "\"}}");
    sk_claude_prepared r = prepare(&d, NULL, NULL, SK_STATUSLINE_ALWAYS, managed);
    CHECK_INT(r.managed, 0);
    CHECK(r.compaction.window == 250000);
    for (size_t i = 0; i < r.args.len; i++)
        CHECK(strstr(r.args.ptr[i], SECRET) == NULL);
    sk_claude_prepared_free(&r);
    char *gone = under(d.top, "managed-gone");
    r = prepare(&d, NULL, NULL, SK_STATUSLINE_ALWAYS, gone);
    CHECK_INT(r.managed, 0);
    sk_claude_prepared_free(&r);
    sk_buf names = {0};
    sk_claude_managed_names(SK_CLAUDE_MANAGED_STATUSLINE | SK_CLAUDE_MANAGED_HOOKS_ONLY, &names);
    CHECK_STR(names.ptr, "statusLine, allowManagedHooksOnly");
    sk_buf_free(&names);
    free(gone);
    free(main_file);
    free(managed);
    dirs_free(&d);
    sk_arena_free(&A);
}

static sk_strv logs;
static void log_line(void *ud, const char *msg)
{
    (void)ud;
    sk_strv_push(&logs, msg);
}

static bool logged(const char *part)
{
    for (size_t i = 0; i < logs.len; i++)
        if (strstr(logs.ptr[i], part))
            return true;
    return false;
}

static void test_w1_a_user_settings_gives_no_arguments_the_notice_and_no_file(void)
{
    dirs d;
    dirs_make(&d);
    sk_loop *loop = sk_loop_new();
    sk_bus *bus = sk_bus_new(NULL, NULL);
    char *cmd[] = {"claude", "--settings", "/x.json", NULL};
    sk_agent_ctx c = ctx_of(&d, cmd, NULL, SK_STATUSLINE_ALWAYS, NULL);
    c.loop = loop;
    c.bus = bus;
    sk_part p = sk_provider_claude.create(&c);
    sk_strv args = {0};
    char *notice = NULL;
    CHECK(p.ops->prepare(p.self, &args, &notice) == 0);
    CHECK_INT(args.len, 0);
    CHECK_STR(notice, "meters limited: --settings given");
    CHECK(access(d.store, F_OK) != 0);
    p.ops->close(p.self);
    p.ops->free(p.self);
    free(notice);
    sk_strv_free(&args);
    sk_bus_free(bus);
    sk_loop_free(loop);
    dirs_free(&d);
}

static void test_w7_the_adapter_managed_hooks_off_give_the_notice_and_no_hook_meters(void)
{
    dirs d;
    dirs_make(&d);
    char *managed = under(d.top, "managed");
    write_in(managed, "managed-settings.json", "{\"disableAllHooks\":true}");
    sk_loop *loop = sk_loop_new();
    sk_bus *bus = sk_bus_new(NULL, NULL);
    sk_agent_ctx c = ctx_of(&d, CLAUDE, NULL, SK_STATUSLINE_ALWAYS, managed);
    c.loop = loop;
    c.bus = bus;
    c.log = log_line;
    sk_part p = sk_provider_claude.create(&c);
    sk_strv args = {0};
    char *notice = NULL;
    CHECK(p.ops->prepare(p.self, &args, &notice) == 0);
    CHECK_STR(notice, "meters limited: managed settings");
    CHECK(logged("managed settings: disableAllHooks"));
    sk_report rep;
    p.ops->report(p.self, &rep);
    CHECK_STR(rep.channels[0].name, "hooks");
    CHECK_INT(rep.channels[0].state, SK_CH_OFF);
    p.ops->close(p.self);
    p.ops->free(p.self);
    free(notice);
    sk_strv_free(&args);
    sk_strv_free(&logs);
    sk_bus_free(bus);
    sk_loop_free(loop);
    free(managed);
    dirs_free(&d);
}

const sk_test sk_suite_claude_settings[] = {
    T(test_w1_a_user_settings_gives_no_arguments_the_notice_and_no_file),
    T(test_w2_setting_sources_project_ignores_a_user_statusline),
    T(test_w3_a_statusline_in_the_git_roots_local_settings_is_chained_from_below),
    T(test_w4_a_settings_env_block_keeps_only_the_compaction_keys),
    T(test_w5_chain_only_writes_the_hooks_alone_always_writes_the_relay),
    T(test_the_settings_text_is_json_stringify_with_indent_1),
    T(test_w6_the_file_is_mode_0600_named_for_its_text_written_once_and_kept),
    T(test_w7_managed_settings_set_the_keys_and_managed_compaction_wins),
    T(test_w7_the_adapter_managed_hooks_off_give_the_notice_and_no_hook_meters),
    {0},
};
