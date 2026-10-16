/* procinfo.h on this process and on a child it starts with a known argv
 * and folder. */
#include "../../src/platform/proc.h"
#include "../../src/platform/procinfo.h"
#include "../test.h"

#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static uint64_t epoch_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static bool listed(pid_t pid, uid_t uid)
{
    sk_procinfo_owners all = {0};
    CHECK(sk_procinfo_list(&all));
    bool found = false;
    for (size_t i = 0; i < all.len; i++)
        if (all.ptr[i].pid == pid)
            found = all.ptr[i].uid == uid;
    sk_vec_free(&all);
    return found;
}

static void test_self(void)
{
    sk_procinfo me;
    CHECK(sk_procinfo_read(getpid(), &me));
    CHECK_INT(me.ppid, getppid());
    CHECK_INT(me.uid, geteuid());
    sk_proc_stat st;
    CHECK(sk_proc_read(getpid(), &st));
    CHECK(st.start == me.start);
    uint64_t now = epoch_ms();
    CHECK(me.started_ms <= now + 1000 && me.started_ms + 86400000u > now);
    CHECK(listed(getpid(), geteuid()));

    sk_buf name = {0};
    CHECK(sk_procinfo_name(getpid(), &name));
    CHECK_STR(sk_buf_cstr(&name), "run");
    sk_buf_free(&name);

    sk_strv argv = {0};
    CHECK(sk_procinfo_argv(getpid(), &argv));
    CHECK(argv.len >= 1 && sk_str_suffix(sk_str_c(argv.ptr[0]), SK_STR("run")));
    sk_strv_free(&argv);

    char here[PATH_MAX];
    CHECK(getcwd(here, sizeof here) != NULL);
    char *cwd = sk_procinfo_cwd(getpid());
    CHECK_STR(cwd, here);
    free(cwd);
}

/* A shell that execs nothing more: macOS's /bin/sh runs bash in its
 * place, which renames the process. */
static const char *shell(void)
{
    return access("/bin/bash", X_OK) == 0 ? "/bin/bash" : "/bin/sh";
}

/* A shell run as dir/probe, a link, in dir with a known argv, waiting on
 * its stdin; closing *hold ends it. */
static pid_t start_child(const char *dir, int *hold)
{
    char link[PATH_MAX];
    snprintf(link, sizeof link, "%s/probe", dir);
    CHECK(symlink(shell(), link) == 0);
    int ready[2], in[2];
    if (pipe(ready) != 0 || pipe(in) != 0)
        return -1;
    pid_t pid = fork();
    if (pid == 0) {
        if (chdir(dir) != 0 || dup2(in[0], 0) < 0 || dup2(ready[1], 1) < 0)
            _exit(127);
        close(in[1]);
        close(ready[0]);
        execl(link, "sh", "-c", "echo ready; read x", "probe", "", "two words", (char *)NULL);
        _exit(127);
    }
    close(in[0]);
    close(ready[1]);
    char buf[8];
    ssize_t n = read(ready[0], buf, sizeof buf);
    CHECK_INT(n, 6);
    close(ready[0]);
    *hold = in[1];
    return pid;
}

static void test_child(void)
{
    char real[PATH_MAX];
    CHECK(realpath(sk_test_tmpdir(), real) != NULL);
    int hold = -1;
    pid_t child = start_child(sk_test_tmpdir(), &hold);
    CHECK(child > 0);

    sk_procinfo me, it;
    CHECK(sk_procinfo_read(getpid(), &me));
    CHECK(sk_procinfo_read(child, &it));
    CHECK_INT(it.ppid, getpid());
    CHECK_INT(it.uid, geteuid());
    CHECK(it.started_ms >= me.started_ms);
    CHECK(it.started_ms <= epoch_ms() + 1000);
    sk_proc_stat st;
    CHECK(sk_proc_read(child, &st) && st.start == it.start);
    CHECK(listed(child, geteuid()));

    sk_buf name = {0};
    CHECK(sk_procinfo_name(child, &name));
    /* The link's name, not the shell's, as Linux names a process. */
    CHECK_STR(sk_buf_cstr(&name), "probe");
    sk_buf_free(&name);

    sk_strv argv = {0};
    CHECK(sk_procinfo_argv(child, &argv));
    const char *want[] = {"sh", "-c", "echo ready; read x", "probe", "", "two words"};
    CHECK_INT(argv.len, SK_LEN(want));
    for (size_t i = 0; i < SK_LEN(want) && i < argv.len; i++)
        CHECK_STR(argv.ptr[i], want[i]);
    sk_strv_free(&argv);

    char *cwd = sk_procinfo_cwd(child);
    CHECK_STR(cwd, real);
    free(cwd);

    close(hold);
    waitpid(child, NULL, 0);
    CHECK(!sk_procinfo_read(child, &it));
    sk_buf gone = {0};
    CHECK(!sk_procinfo_name(child, &gone));
    sk_buf_free(&gone);
    CHECK(sk_procinfo_cwd(child) == NULL);
}

const sk_test sk_suite_procinfo[] = {T(test_self), T(test_child), {0}};
