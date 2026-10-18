/* The runtime dir and the logs (decision 0016) from app/runtime.js,
 * which has no JavaScript tests of its own: dirs of dead pids swept,
 * the launcher's dir taken, dangling hook links removed, the newest 10
 * logs kept and the logs of ended launches cleared. */
#include "../../src/app/runtime.h"
#include "../../src/base/str.h"
#include "../test.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* A pid that names no process: a child already reaped. */
static pid_t dead_pid(void)
{
    pid_t pid = fork();
    if (pid == 0)
        _exit(0);
    waitpid(pid, NULL, 0);
    return pid;
}

static char *path(const char *dir, const char *fmt, ...) SK_PRINTF(2, 3);
static char *path(const char *dir, const char *fmt, ...)
{
    sk_buf b = {0};
    sk_buf_printf(&b, "%s/", dir);
    va_list ap;
    va_start(ap, fmt);
    sk_buf_vprintf(&b, fmt, ap);
    va_end(ap);
    return b.ptr;
}

static void touch(const char *p)
{
    int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    CHECK(fd >= 0);
    if (fd >= 0)
        close(fd);
}

static bool exists(const char *p)
{
    struct stat st;
    return lstat(p, &st) == 0;
}

static sk_env env_in(const char *tmp)
{
    sk_env e = {SK_OS_LINUX, "/nonexistent-home", NULL, NULL, NULL, NULL, 1000};
    e.xdg_runtime_dir = tmp;
    e.xdg_state_home = tmp;
    return e;
}

static void test_create_makes_a_private_dir_and_sweeps_dead_pids(void)
{
    const char *tmp = sk_test_tmpdir();
    sk_env e = env_in(tmp);
    char *root = path(tmp, "claude-skins");
    char *dead = path(root, "%ld", (long)dead_pid());
    char *dead_deep = path(dead, "tab-1/electron");
    char *live = path(root, "%ld", (long)getppid());
    char *other = path(root, "not-a-pid");
    char *huge = path(root, "99999999999");
    CHECK_INT(sk_mkdirs(dead_deep, 0700), 0);
    CHECK_INT(sk_mkdirs(live, 0700), 0);
    CHECK_INT(sk_mkdirs(other, 0700), 0);
    CHECK_INT(sk_mkdirs(huge, 0700), 0);
    char *file = path(dead_deep, "f");
    touch(file);
    char *dir = sk_runtime_create(&e);
    char *want = path(root, "%ld", (long)getpid());
    CHECK_STR(dir, want);
    struct stat st;
    CHECK(stat(dir, &st) == 0 && S_ISDIR(st.st_mode));
    CHECK_INT(st.st_mode & 0777, 0700);
    CHECK(stat(root, &st) == 0 && (st.st_mode & 0777) == 0700);
    CHECK(!exists(dead));
    CHECK(exists(live));
    CHECK(exists(other));
    CHECK(!exists(huge)); /* beyond a pid, as Node's process.kill refused it */
    /* Again: its own dir stays. */
    char *again = sk_runtime_create(&e);
    CHECK_STR(again, want);
    CHECK(exists(dir));
    CHECK_INT(sk_runtime_remove(dir), 0);
    CHECK(!exists(dir));
    CHECK_INT(sk_runtime_remove(dir), 0); /* nothing there is no failure */
    CHECK_INT(sk_runtime_remove(NULL), 0);
    free(root), free(dead), free(dead_deep), free(live), free(other), free(huge), free(file);
    free(dir), free(want), free(again);
}

static void test_a_root_that_cannot_be_made_fails(void)
{
    const char *tmp = sk_test_tmpdir();
    char *blocker = path(tmp, "file");
    touch(blocker);
    sk_env e = env_in(blocker);
    errno = 0;
    CHECK(sk_runtime_create(&e) == NULL);
    CHECK(errno != 0);
    free(blocker);
}

/* A root that is not this user's alone is not used (decision 0016): a
 * link another user made on a shared /tmp, here one to a folder of this
 * user's whose numbered dirs the sweep would remove. One of this user's
 * with loose bits is made 0700. */
static void test_a_root_not_of_this_user_alone_is_refused(void)
{
    const char *tmp = sk_test_tmpdir();
    char *victim = path(tmp, "victim"), *kept = path(victim, "%ld", (long)dead_pid());
    char *base = path(tmp, "linked");
    char *root = path(base, "claude-skins");
    CHECK_INT(sk_mkdirs(kept, 0700), 0);
    CHECK_INT(sk_mkdirs(base, 0700), 0);
    CHECK(symlink(victim, root) == 0);
    sk_env e = env_in(base);
    errno = 0;
    CHECK(sk_runtime_create(&e) == NULL);
    CHECK(errno == EPERM);
    CHECK(exists(kept));
    /* This user's own, made 0755: tightened, and used. */
    char *loose_base = path(tmp, "loose"), *loose = path(loose_base, "claude-skins");
    CHECK_INT(sk_mkdirs(loose, 0700), 0);
    CHECK(chmod(loose, 0755) == 0);
    e = env_in(loose_base);
    char *dir = sk_runtime_create(&e);
    CHECK(dir != NULL);
    struct stat st;
    CHECK(lstat(loose, &st) == 0 && (st.st_mode & 0777) == 0700);
    CHECK_INT(sk_runtime_remove(dir), 0);
    free(dir), free(victim), free(kept), free(base), free(root), free(loose_base), free(loose);
}

static void test_dangling_hook_links_are_swept(void)
{
    const char *tmp = sk_test_tmpdir();
    char *pids = path(tmp, "pids");
    CHECK_INT(sk_mkdirs(pids, 0700), 0);
    char *sock = path(tmp, "hook.sock");
    touch(sock);
    char *kept = path(pids, "100");
    char *gone = path(pids, "101");
    CHECK(symlink(sock, kept) == 0);
    CHECK(symlink("/nonexistent/hook.sock", gone) == 0);
    sk_runtime_sweep_links(tmp);
    CHECK(exists(kept));
    CHECK(!exists(gone));
    sk_runtime_sweep_links("/nonexistent"); /* no pids/ is fine */
    free(pids), free(sock), free(kept), free(gone);
}

static void test_the_logs_and_the_settings_live_in_the_state_dir(void)
{
    sk_env e = {SK_OS_LINUX, "/home/u", NULL, NULL, NULL, NULL, 1000};
    char *logs = sk_runtime_log_dir(&e), *settings = sk_runtime_settings_dir(&e);
    CHECK_STR(logs, "/home/u/.local/state/claude-skins");
    CHECK_STR(settings, "/home/u/.local/state/claude-skins/settings");
    free(logs), free(settings);
    sk_env mac = {SK_OS_MACOS, "/Users/u", NULL, NULL, NULL, NULL, 501};
    settings = sk_runtime_settings_dir(&mac);
    CHECK_STR(settings, "/Users/u/Library/Application Support/claude-skins/settings");
    free(settings);
    sk_env none = {SK_OS_LINUX, NULL, NULL, NULL, NULL, NULL, 1};
    CHECK(sk_runtime_settings_dir(&none) == NULL);
    CHECK_INT(sk_runtime_rotate_logs(&none), 0);
    CHECK_INT(sk_runtime_clear_logs(&none), 0);
}

static void test_rotate_keeps_the_newest_ten_logs(void)
{
    const char *tmp = sk_test_tmpdir();
    sk_env e = env_in(tmp);
    char *dir = path(tmp, "claude-skins");
    CHECK_INT(sk_mkdirs(dir, 0700), 0);
    CHECK_INT(sk_runtime_rotate_logs(&e), 0); /* fewer than ten */
    for (int i = 0; i < 13; i++) {
        char *p = path(dir, "202610%02d-120000-%d.log", i + 1, 100 + i);
        touch(p);
        free(p);
    }
    char *other = path(dir, "settings.json");
    touch(other);
    CHECK_INT(sk_runtime_rotate_logs(&e), 0);
    for (int i = 0; i < 13; i++) {
        char *p = path(dir, "202610%02d-120000-%d.log", i + 1, 100 + i);
        CHECK(exists(p) == (i >= 3));
        free(p);
    }
    CHECK(exists(other));
    free(dir), free(other);
}

static void test_clear_removes_the_logs_of_ended_launches(void)
{
    const char *tmp = sk_test_tmpdir();
    sk_env e = env_in(tmp);
    char *dir = path(tmp, "claude-skins");
    CHECK_INT(sk_mkdirs(dir, 0700), 0);
    char *dead = path(dir, "20261001-120000-%ld.log", (long)dead_pid());
    char *live = path(dir, "20261002-120000-%ld.log", (long)getpid());
    char *nopid = path(dir, "notes.log");
    char *letters = path(dir, "x-12a.log");
    char *huge = path(dir, "x-99999999999.log");
    const char *const all[] = {dead, live, nopid, letters, huge};
    for (size_t i = 0; i < SK_LEN(all); i++)
        touch(all[i]);
    CHECK_INT(sk_runtime_clear_logs(&e), 2);
    CHECK(!exists(dead));
    CHECK(exists(live));
    CHECK(exists(nopid));
    CHECK(exists(letters));
    CHECK(!exists(huge));
    CHECK_INT(sk_runtime_clear_logs(&e), 0);
    free(dir), free(dead), free(live), free(nopid), free(letters), free(huge);
}

const sk_test sk_suite_runtime[] = {
    T(test_create_makes_a_private_dir_and_sweeps_dead_pids),
    T(test_a_root_that_cannot_be_made_fails),
    T(test_a_root_not_of_this_user_alone_is_refused),
    T(test_dangling_hook_links_are_swept),
    T(test_the_logs_and_the_settings_live_in_the_state_dir),
    T(test_rotate_keeps_the_newest_ten_logs),
    T(test_clear_removes_the_logs_of_ended_launches),
    {0},
};
