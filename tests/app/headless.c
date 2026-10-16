/* skinned --headless end to end, the gate of phase 2 on the recorded
 * fixtures: the real program runs tools/fake/claude, whose hook and
 * statusline commands are the real skinned-relay, and the metrics file
 * must show the session's meters and its live channels. The app's state
 * and runtime dirs are temporary. */
#include "../../src/app/runtime.h"
#include "../../src/base/strv.h"
#include "../test.h"

#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

/* Runs skinned with argv after the program, its environment the test's
 * plus extra; returns its exit status. */
static int run(const char *bin, const char *const *args, const char *const *extra, const char *out)
{
    sk_strv argv = {0}, env = {0};
    sk_strv_push(&argv, bin);
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
        execve(bin, sk_strv_argv(&argv), sk_strv_argv(&env));
        _exit(99);
    }
    int ws = 0;
    waitpid(pid, &ws, 0);
    sk_strv_free(&argv);
    sk_strv_free(&env);
    return WIFEXITED(ws) ? WEXITSTATUS(ws) : 128 + WTERMSIG(ws);
}

static void test_the_fake_claude_session_shows_its_meters(void)
{
    const char *bin = getenv("SK_SKINNED_BIN");
    if (!bin || access(bin, X_OK) != 0) {
        printf("    (skipped: SK_SKINNED_BIN not set)\n");
        return;
    }
    const char *tmp = sk_test_tmpdir();
    char cwd[1024];
    CHECK(getcwd(cwd, sizeof cwd) != NULL);
    sk_buf metrics = {0}, out = {0}, path = {0}, state = {0}, run_dir = {0};
    sk_buf_printf(&metrics, "%s/metrics.jsonl", tmp);
    sk_buf_printf(&out, "%s/pty.out", tmp);
    sk_buf_printf(&path, "PATH=%s/tools/fake:%s", cwd, getenv("PATH"));
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
    const char *no_window[] = {"--", "sh", NULL};
    CHECK_INT(run(bin, no_window, extra, out.ptr), 125);
    sk_buf_free(&out);
    sk_buf_free(&state);
    sk_buf_free(&run_dir);
    sk_buf_free(&metrics);
}

const sk_test sk_suite_headless[] = {
    T(test_the_fake_claude_session_shows_its_meters),
    T(test_statuses),
    {0},
};
