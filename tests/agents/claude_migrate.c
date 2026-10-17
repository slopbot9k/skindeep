/* The settings files of the JavaScript app carried over the cutover
 * (sk_claude_migrate_settings): each command that ran a JavaScript relay
 * that is gone runs skinned-relay, the file otherwise as
 * JSON.stringify(settings, null, 1) wrote it; anything else is left. */
#include "../../src/agents/claude/claude.h"
#include "../../src/base/paths.h"
#include "../../src/sessions/files.h"
#include "../test.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

/* s inside a JSON string: these commands need only \ and " escaped. */
static char *esc(const char *s)
{
    sk_buf b = {0};
    sk_buf_reserve(&b, 0);
    for (; *s; s++) {
        if (*s == '\\' || *s == '"')
            sk_buf_putc(&b, '\\');
        sk_buf_putc(&b, *s);
    }
    return b.ptr;
}

/* A settings file as settings.js wrote it, with one hook. */
static char *settings(const char *hook_cmd, const char *line_cmd)
{
    char *hook = esc(hook_cmd), *line = line_cmd ? esc(line_cmd) : NULL;
    sk_buf b = {0};
    sk_buf_printf(&b,
                  "{\n \"hooks\": {\n  \"Stop\": [\n   {\n    \"hooks\": [\n     {\n      \"type\": \"command\",\n"
                  "      \"command\": \"%s\",\n      \"timeout\": 1\n     }\n    ]\n   }\n  ]\n }",
                  hook);
    if (line)
        sk_buf_printf(&b, ",\n \"statusLine\": {\n  \"type\": \"command\",\n  \"command\": \"%s\",\n  \"padding\": 0\n }",
                      line);
    sk_buf_append_c(&b, "\n}");
    free(hook);
    free(line);
    return b.ptr;
}

typedef struct {
    char *top, *store, *relay, *repo;
} dirs;

static void put(const char *path, const char *text, mode_t mode)
{
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    if (f) {
        fputs(text, f);
        fclose(f);
    }
    chmod(path, mode);
}

static void dirs_make(dirs *d)
{
    d->top = sk_files_resolve(sk_test_tmpdir());
    d->store = sk_path_join(d->top, "settings");
    d->relay = sk_path_join(d->top, "skinned-relay");
    d->repo = sk_path_join(d->top, "repo");
    CHECK_INT(sk_mkdirs(d->store, 0700), 0);
    put(d->relay, "#!/bin/sh\n", 0755);
}

static void dirs_free(dirs *d)
{
    free(d->top);
    free(d->store);
    free(d->relay);
    free(d->repo);
}

static char *in_store(const dirs *d, const char *name)
{
    return sk_path_join(d->store, name);
}

static void check_file(const dirs *d, const char *name, const char *want)
{
    char *p = in_store(d, name), *got = sk_test_read(p, NULL);
    CHECK_STR(got, want);
    free(got);
    free(p);
}

static void test_the_javascript_relays_gone_run_skinned_relay(void)
{
    dirs d;
    dirs_make(&d);
    char *js = settings("'/usr/bin/python3' -I -S '/gone/app/relay/hook.py' '/run/user/1000/claude-skins'",
                        "ELECTRON_RUN_AS_NODE=1 '/gone/node_modules/electron/dist/electron' "
                        "'/gone/app/relay/statusline.js' '/run/user/1000/claude-skins' Y2NzdGF0dXM");
    char *p = in_store(&d, "0123456789abcdef.json");
    put(p, js, 0600);
    CHECK_INT(sk_claude_migrate_settings(d.store, d.relay), 1);
    sk_buf hook = {0}, line = {0};
    sk_buf_printf(&hook, "'%s' hook '/run/user/1000/claude-skins'", d.relay);
    sk_buf_printf(&line, "'%s' statusline '/run/user/1000/claude-skins' Y2NzdGF0dXM", d.relay);
    char *want = settings(hook.ptr, line.ptr);
    check_file(&d, "0123456789abcdef.json", want);
    struct stat st;
    CHECK(stat(p, &st) == 0 && (st.st_mode & 0777) == 0600);
    /* Done once: the second launch finds nothing to do. */
    CHECK_INT(sk_claude_migrate_settings(d.store, d.relay), 0);
    check_file(&d, "0123456789abcdef.json", want);
    free(want);
    sk_buf_free(&hook);
    sk_buf_free(&line);
    free(js);
    free(p);
    dirs_free(&d);
}

static void test_a_quote_in_the_root_and_no_chain_or_statusline(void)
{
    dirs d;
    dirs_make(&d);
    char *a = settings("'python3' -I -S '/gone/app/relay/hook.py' '/tmp/it'\\''s'",
                       "'node' '/gone/app/relay/statusline.js' '/tmp/it'\\''s' -");
    char *b = settings("'/usr/bin/python3' -I -S '/gone/app/relay/hook.py' '/r'", NULL);
    char *pa = in_store(&d, "a.json"), *pb = in_store(&d, "b.json");
    put(pa, a, 0600);
    put(pb, b, 0600);
    CHECK_INT(sk_claude_migrate_settings(d.store, d.relay), 2);
    sk_buf hook = {0}, line = {0}, hook_b = {0};
    sk_buf_printf(&hook, "'%s' hook '/tmp/it'\\''s'", d.relay);
    sk_buf_printf(&line, "'%s' statusline '/tmp/it'\\''s' -", d.relay);
    sk_buf_printf(&hook_b, "'%s' hook '/r'", d.relay);
    char *want_a = settings(hook.ptr, line.ptr), *want_b = settings(hook_b.ptr, NULL);
    check_file(&d, "a.json", want_a);
    check_file(&d, "b.json", want_b);
    free(want_a);
    free(want_b);
    sk_buf_free(&hook);
    sk_buf_free(&line);
    sk_buf_free(&hook_b);
    free(a);
    free(b);
    free(pa);
    free(pb);
    dirs_free(&d);
}

static void test_files_left_alone(void)
{
    dirs d;
    dirs_make(&d);
    /* The JavaScript relays still there: the JavaScript app's. */
    char *relays = sk_path_join(d.repo, "app/relay"), *hook_py = sk_path_join(relays, "hook.py"),
         *line_js = sk_path_join(relays, "statusline.js");
    CHECK_INT(sk_mkdirs(relays, 0700), 0);
    put(hook_py, "", 0644);
    put(line_js, "", 0644);
    sk_buf hook = {0}, line = {0};
    sk_buf_printf(&hook, "'python3' -I -S '%s' '/r'", hook_py);
    sk_buf_printf(&line, "'node' '%s' '/r' -", line_js);
    char *live = settings(hook.ptr, line.ptr);
    /* skinned's own; a shape settings.js never wrote; a chain that is
     * not base64url; a user's own hook.py elsewhere. */
    char *own = settings("'/opt/sk/skinned-relay' hook '/r'", "'/opt/sk/skinned-relay' statusline '/r' -");
    char *odd = settings("python3 /gone/app/relay/hook.py /r; true", "'node' '/gone/app/relay/statusline.js' '/r'");
    char *chain = settings("'/usr/bin/python3' -I '/gone/app/relay/hook.py' '/r'",
                           "'node' '/gone/app/relay/statusline.js' '/r' $(id)");
    struct {
        const char *name, *text;
    } files[] = {
        {"live.json", live},
        {"own.json", own},
        {"odd.json", odd},
        {"chain.json", chain},
        {"broken.json", "{\"hooks\": "},
        {"array.json", "[1, 2]"},
        {"notes.txt", odd},
    };
    for (size_t i = 0; i < SK_LEN(files); i++) {
        char *p = in_store(&d, files[i].name);
        put(p, files[i].text, 0600);
        free(p);
    }
    CHECK_INT(sk_claude_migrate_settings(d.store, d.relay), 0);
    for (size_t i = 0; i < SK_LEN(files); i++)
        check_file(&d, files[i].name, files[i].text);
    /* A relay that cannot run, or no store: nothing. */
    char *js = settings("'python3' -I -S '/gone/app/relay/hook.py' '/r'", NULL);
    char *p = in_store(&d, "gone.json");
    put(p, js, 0600);
    chmod(d.relay, 0644);
    CHECK_INT(sk_claude_migrate_settings(d.store, d.relay), 0);
    check_file(&d, "gone.json", js);
    CHECK_INT(sk_claude_migrate_settings(NULL, d.relay), 0);
    char *missing = sk_path_join(d.top, "missing");
    chmod(d.relay, 0755);
    CHECK_INT(sk_claude_migrate_settings(missing, d.relay), 0);
    free(missing);
    free(p);
    free(js);
    free(live);
    free(own);
    free(odd);
    free(chain);
    sk_buf_free(&hook);
    sk_buf_free(&line);
    free(relays);
    free(hook_py);
    free(line_js);
    dirs_free(&d);
}

const sk_test sk_suite_claude_migrate[] = {
    T(test_the_javascript_relays_gone_run_skinned_relay),
    T(test_a_quote_in_the_root_and_no_chain_or_statusline),
    T(test_files_left_alone),
    {0},
};
