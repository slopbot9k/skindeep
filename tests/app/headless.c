/* skinned --headless end to end, the gate of phase 2 on the recorded
 * fixtures: the real program runs the fake claude (tools/fakeclaude.c),
 * whose hook and statusline commands are the real skinned-relay, and
 * the metrics file must show the session's meters and its live
 * channels. The app's state and runtime dirs are temporary. */
#include "../../src/app/runtime.h"
#include "../../src/base/strv.h"
#include "../../src/core/loop.h"
#include "../test.h"

#include <dirent.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

/* run() starts skinned with fds 0 to 2 closed, as some launchers do. */
static bool no_stdio;
/* run() gives this argv[0] instead of the file, when set. */
static const char *argv0;
/* run() starts skinned in this folder, removed first, when set. */
static const char *in_gone;

/* Runs skinned with argv after the program, its environment the test's
 * plus extra; returns its exit status. */
static int run(const char *bin, const char *const *args, const char *const *extra, const char *out)
{
    sk_strv argv = {0}, env = {0};
    sk_strv_push(&argv, argv0 ? argv0 : bin);
    for (; *args; args++)
        sk_strv_push(&argv, *args);
    for (char **e = environ; *e; e++) {
        bool replaced = false;
        for (const char *const *x = extra; *x && !replaced; x++) {
            size_t n = strcspn(*x, "=");
            replaced = !strncmp(*e, *x, n + 1);
        }
        if (!replaced)
            sk_strv_push(&env, *e);
    }
    for (; *extra; extra++)
        sk_strv_push(&env, *extra);
    pid_t pid = fork();
    if (pid == 0) {
        /* skinned's own lines go there too, out of the test's output. */
        if (!freopen(out, "w", stdout) || dup2(1, 2) < 0 || !freopen("/dev/null", "r", stdin))
            _exit(98);
        if (no_stdio)
            close(0), close(1), close(2);
        if (in_gone && (chdir(in_gone) != 0 || rmdir(in_gone) != 0))
            _exit(97);
        execve(bin, sk_strv_argv(&argv), sk_strv_argv(&env));
        _exit(99);
    }
    /* A skinned that hangs fails the test, not the run. */
    int ws = 0;
    uint64_t until = sk_now_ms() + 30000;
    while (waitpid(pid, &ws, WNOHANG) == 0) {
        if (sk_now_ms() > until) {
            kill(pid, SIGKILL);
            waitpid(pid, &ws, 0);
            break;
        }
        sk_test_sleep_ms(20);
    }
    sk_strv_free(&argv);
    sk_strv_free(&env);
    return WIFEXITED(ws) ? WEXITSTATUS(ws) : 128 + WTERMSIG(ws);
}

static void test_the_fake_claude_session_shows_its_meters(void)
{
    const char *bin = getenv("SK_SKINNED_BIN"), *fake = getenv("SK_FAKE_DIR");
    if (!bin || access(bin, X_OK) != 0 || !fake || !*fake) {
        printf("    (skipped: SK_SKINNED_BIN or SK_FAKE_DIR not set)\n");
        return;
    }
    const char *tmp = sk_test_tmpdir();
    char cwd[1024];
    CHECK(getcwd(cwd, sizeof cwd) != NULL);
    sk_buf metrics = {0}, out = {0}, path = {0}, state = {0}, run_dir = {0};
    sk_buf_printf(&metrics, "%s/metrics.jsonl", tmp);
    sk_buf_printf(&out, "%s/pty.out", tmp);
    /* The fake's folder made absolute: the app runs in another. */
    sk_buf_printf(&path, "PATH=%s%s%s:%s", *fake == '/' ? "" : cwd, *fake == '/' ? "" : "/", fake, getenv("PATH"));
    sk_buf_printf(&state, "XDG_STATE_HOME=%s/state", tmp);
    /* The hook socket's path must fit sun_path, 104 bytes on macOS,
     * which the test's own temporary dir is too deep for. */
    char short_run[] = "/tmp/skr-XXXXXX";
    CHECK(mkdtemp(short_run) != NULL);
    sk_buf_printf(&run_dir, "XDG_RUNTIME_DIR=%s", short_run);
    sk_buf headless = {0};
    sk_buf_printf(&headless, "--headless=%s", metrics.ptr);
    const char *args[] = {headless.ptr, "--", "claude", NULL};
    const char *extra[] = {path.ptr, state.ptr, run_dir.ptr, "FAKE_CLAUDE_PAUSE=0.05",
                           "FAKE_CLAUDE_LINGER=1.5", NULL};
    CHECK_INT(run(bin, args, extra, out.ptr), 0);

    char *pty = sk_test_read(out.ptr, NULL);
    CHECK(pty && strstr(pty, "fake claude: done"));
    char *text = sk_test_read(metrics.ptr, NULL);
    CHECK(text != NULL);
    if (text) {
        CHECK(strstr(text, "\"agent\":\"claude\""));
        CHECK(strstr(text, "{\"name\":\"hooks\",\"state\":\"live\""));
        CHECK(strstr(text, "{\"name\":\"statusline\",\"state\":\"live\""));
        CHECK(strstr(text, "{\"name\":\"transcript\",\"state\":\"live\""));
        CHECK(strstr(text, "\"model.name\":\"Opus 5.5\""));
        CHECK(strstr(text, "\"context.window\":1000000"));
        CHECK(strstr(text, "\"limits.five_hour_pct\":1"));
        CHECK(strstr(text, "\"event.prompt\":"));
    }
    sk_runtime_remove(short_run);
    free(pty);
    free(text);
    sk_buf_free(&metrics);
    sk_buf_free(&out);
    sk_buf_free(&path);
    sk_buf_free(&state);
    sk_buf_free(&run_dir);
    sk_buf_free(&headless);
}

/* The command's own status comes back, and a missing one is 127. */
static void test_statuses(void)
{
    const char *bin = getenv("SK_SKINNED_BIN");
    if (!bin || access(bin, X_OK) != 0)
        return;
    const char *tmp = sk_test_tmpdir();
    sk_buf out = {0}, state = {0}, run_dir = {0}, metrics = {0};
    sk_buf_printf(&out, "%s/out", tmp);
    sk_buf_printf(&state, "XDG_STATE_HOME=%s/state", tmp);
    sk_buf_printf(&run_dir, "XDG_RUNTIME_DIR=%s/run", tmp);
    sk_buf_printf(&metrics, "--headless=%s/m.jsonl", tmp);
    const char *extra[] = {state.ptr, run_dir.ptr, NULL};
    const char *three[] = {metrics.ptr, "--", "sh", "-c", "exit 3", NULL};
    CHECK_INT(run(bin, three, extra, out.ptr), 3);
    const char *missing[] = {metrics.ptr, "--", "no-such-command-anywhere", NULL};
    CHECK_INT(run(bin, missing, extra, out.ptr), 127);
    /* With no stdio, the files skinned opens must not take fds 0 to 2:
     * the tab's output to fd 1 would go into the metrics. */
    no_stdio = true;
    const char *says[] = {metrics.ptr, "--", "sh", "-c", "echo said-it; exit 4", NULL};
    CHECK_INT(run(bin, says, extra, out.ptr), 4);
    no_stdio = false;
    char *m = sk_test_read(metrics.ptr + strlen("--headless="), NULL);
    CHECK(m && !strstr(m, "said-it"));
    free(m);
    /* As process.cwd() throws in app/main.js: a folder that is gone
     * opens no tab. */
    sk_buf gone = {0};
    sk_buf_printf(&gone, "%s/gone", tmp);
    CHECK(mkdir(gone.ptr, 0700) == 0);
    char whole_bin[PATH_MAX]; /* bin is relative to the folder left */
    CHECK(realpath(bin, whole_bin) != NULL);
    in_gone = gone.ptr;
    const char *nowhere[] = {metrics.ptr, "--", "sh", "-c", "exit 0", NULL};
    CHECK_INT(run(whole_bin, nowhere, extra, out.ptr), 125);
    in_gone = NULL;
    sk_buf_free(&gone);
    sk_buf_free(&out);
    sk_buf_free(&state);
    sk_buf_free(&run_dir);
    sk_buf_free(&metrics);
}

/* Run by its bare name through a link on the PATH, skinned takes the
 * relay beside the file the link leads to, as make install's links
 * lead to it: its log names it. */
static void test_the_relay_is_found_past_a_link(void)
{
    const char *bin = getenv("SK_SKINNED_BIN");
    if (!bin || access(bin, X_OK) != 0)
        return;
    const char *tmp = sk_test_tmpdir();
    char real_bin[PATH_MAX];
    CHECK(realpath(bin, real_bin) != NULL);
    sk_buf links = {0}, link = {0}, path = {0}, state = {0}, run_dir = {0}, metrics = {0}, out = {0}, want = {0};
    sk_buf_printf(&links, "%s/links", tmp);
    sk_buf_printf(&link, "%s/skinned", links.ptr);
    CHECK(mkdir(links.ptr, 0700) == 0);
    CHECK(symlink(real_bin, link.ptr) == 0);
    sk_buf_printf(&path, "PATH=%s:%s", links.ptr, getenv("PATH"));
    sk_buf_printf(&state, "XDG_STATE_HOME=%s/state", tmp);
    sk_buf_printf(&run_dir, "XDG_RUNTIME_DIR=%s/run", tmp);
    sk_buf_printf(&metrics, "--headless=%s/m.jsonl", tmp);
    sk_buf_printf(&out, "%s/out", tmp);
    const char *extra[] = {path.ptr, state.ptr, run_dir.ptr, NULL};
    const char *args[] = {metrics.ptr, "--", "sh", "-c", "exit 0", NULL};
    argv0 = "skinned";
    CHECK_INT(run(link.ptr, args, extra, out.ptr), 0);
    argv0 = NULL;
    *strrchr(real_bin, '/') = 0;
    sk_buf_printf(&want, "relay %s/skinned-relay", real_bin);
    sk_buf logs = {0};
    sk_buf_printf(&logs, "%s/state/claude-skins", tmp);
    DIR *d = opendir(logs.ptr);
    CHECK(d != NULL);
    bool named = false;
    for (struct dirent *e; d && (e = readdir(d));) {
        if (!strstr(e->d_name, "-headless-") || !strstr(e->d_name, ".log"))
            continue;
        sk_buf p = {0};
        sk_buf_printf(&p, "%s/%s", logs.ptr, e->d_name);
        char *text = sk_test_read(p.ptr, NULL);
        named = named || (text && strstr(text, want.ptr));
        free(text);
        sk_buf_free(&p);
    }
    if (d)
        closedir(d);
    if (!named)
        sk_test_fail(__FILE__, __LINE__, "no log names \"%s\"", want.ptr);
    sk_buf *bufs[] = {&links, &link, &path, &state, &run_dir, &metrics, &out, &want, &logs};
    for (size_t i = 0; i < SK_LEN(bufs); i++)
        sk_buf_free(bufs[i]);
}

/* --record keeps the tab's bytes as they came, after a header naming the
 * size the tab started at, for the terminal's tests to replay. */
static void test_record_keeps_the_bytes(void)
{
    const char *bin = getenv("SK_SKINNED_BIN");
    if (!bin || access(bin, X_OK) != 0)
        return;
    const char *tmp = sk_test_tmpdir();
    sk_buf state = {0}, run_dir = {0}, metrics = {0}, out = {0}, record = {0}, rec_path = {0}, side = {0};
    sk_buf_printf(&state, "XDG_STATE_HOME=%s/state", tmp);
    sk_buf_printf(&run_dir, "XDG_RUNTIME_DIR=%s/run", tmp);
    sk_buf_printf(&metrics, "--headless=%s/m.jsonl", tmp);
    sk_buf_printf(&out, "%s/out", tmp);
    sk_buf_printf(&rec_path, "%s/session.rec", tmp);
    sk_buf_printf(&record, "--record=%s", rec_path.ptr);
    sk_buf_printf(&side, "%s.resizes", rec_path.ptr);
    const char *extra[] = {state.ptr, run_dir.ptr, NULL};
    const char *args[] = {metrics.ptr, record.ptr, "--", "sh", "-c", "printf '\\033[1mbold\\033[0m\\n'", NULL};
    CHECK_INT(run(bin, args, extra, out.ptr), 0);
    size_t n = 0;
    char *rec = sk_test_read(rec_path.ptr, &n);
    /* Not a terminal: the size is 80 by 24. The pty turns \n into \r\n. */
    const char want[] = "skinned-record 1 80 24\n\033[1mbold\033[0m\r\n";
    CHECK(rec && n == sizeof want - 1 && !memcmp(rec, want, n));
    char *resizes = sk_test_read(side.ptr, NULL);
    CHECK(resizes && !*resizes);
    free(rec);
    free(resizes);
    const char *bad[] = {metrics.ptr, "--record=/nonexistent/dir/x.rec", "--", "sh", "-c", "exit 0", NULL};
    CHECK_INT(run(bin, bad, extra, out.ptr), 125);
    sk_buf *bufs[] = {&state, &run_dir, &metrics, &out, &record, &rec_path, &side};
    for (size_t i = 0; i < SK_LEN(bufs); i++)
        sk_buf_free(bufs[i]);
}

const sk_test sk_suite_headless[] = {
    T(test_the_fake_claude_session_shows_its_meters),
    T(test_statuses),
    T(test_the_relay_is_found_past_a_link),
    T(test_record_keeps_the_bytes),
    {0},
};
