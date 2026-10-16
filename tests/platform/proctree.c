/* The process tree and its usage, against this process and children it
 * forks: one that sleeps with a child of its own, one that spins, so
 * the CPU reading moves. */
#include "../../src/core/loop.h"
#include "../../src/platform/proctree.h"
#include "../test.h"

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX 4096

static bool has(const pid_t *pids, size_t n, pid_t pid)
{
    for (size_t i = 0; i < n; i++)
        if (pids[i] == pid)
            return true;
    return false;
}

static void stop_child(pid_t pid)
{
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
}

/* A child that forks a grandchild, both sleeping; *grand is learned
 * through a pipe. */
static pid_t sleeper(pid_t *grand)
{
    int p[2];
    CHECK(pipe(p) == 0);
    pid_t child = fork();
    if (child == 0) {
        close(p[0]);
        pid_t g = fork();
        if (g == 0) {
            pause();
            _exit(0);
        }
        ssize_t w = write(p[1], &g, sizeof g);
        (void)w;
        pause();
        kill(g, SIGKILL);
        _exit(0);
    }
    close(p[1]);
    *grand = 0;
    ssize_t r = read(p[0], grand, sizeof *grand);
    CHECK(r == (ssize_t)sizeof *grand);
    close(p[0]);
    return child;
}

static void test_tree_of_this_process(void)
{
    pid_t *pids = sk_xmalloc(MAX * sizeof *pids);
    pid_t grand, child = sleeper(&grand);
    size_t n = sk_proc_tree(getpid(), pids, MAX);
    CHECK(n >= 3);
    CHECK_INT(pids[0], getpid());
    CHECK(has(pids, n, child));
    CHECK(has(pids, n, grand));
    n = sk_proc_tree(child, pids, MAX);
    CHECK_INT(n, 2);
    CHECK_INT(pids[0], child);
    CHECK_INT(pids[1], grand);
    CHECK_INT(sk_proc_tree(getpid(), pids, 1), 1);
    CHECK_INT(sk_proc_tree(getpid(), pids, 0), 0);
    kill(grand, SIGKILL);
    stop_child(child);
    free(pids);
}

static void test_gone(void)
{
    pid_t child = fork();
    if (child == 0)
        _exit(0);
    waitpid(child, NULL, 0);
    pid_t pids[4];
    CHECK_INT(sk_proc_tree(child, pids, 4), 0);
    uint64_t cpu = 1, rss = 1;
    CHECK(!sk_proc_tree_usage(child, &cpu, &rss));
    CHECK_INT(cpu, 0);
    CHECK_INT(rss, 0);
}

static void test_usage_moves_with_a_busy_child(void)
{
    /* A child just forked may have no task info yet (macOS), so it says
     * when it runs. */
    int p[2];
    CHECK(pipe(p) == 0);
    pid_t child = fork();
    if (child == 0) {
        ssize_t w = write(p[1], "x", 1);
        (void)w;
        volatile unsigned long x = 0;
        for (;;)
            x++;
    }
    char c;
    CHECK(read(p[0], &c, 1) == 1);
    close(p[0]);
    close(p[1]);
    uint64_t cpu0, rss0, self0, self_rss;
    CHECK(sk_proc_tree_usage(child, &cpu0, &rss0));
    CHECK(sk_proc_tree_usage(getpid(), &self0, &self_rss));
    CHECK(rss0 > 0);
    CHECK(self_rss >= rss0); /* the tree holds the child */
    uint64_t start = sk_now_ms();
    sk_test_sleep_ms(400);
    uint64_t took = sk_now_ms() - start;
    uint64_t cpu1, rss1, self1;
    CHECK(sk_proc_tree_usage(child, &cpu1, &rss1));
    CHECK(sk_proc_tree_usage(getpid(), &self1, &self_rss));
    /* A spinning child gets most of a core; allow a loaded machine. */
    CHECK(cpu1 - cpu0 >= 100);
    CHECK(cpu1 - cpu0 <= took + 50);
    CHECK(self1 - self0 >= 100);
    stop_child(child);
}

const sk_test sk_suite_proctree[] = {
    T(test_tree_of_this_process),
    T(test_gone),
    T(test_usage_moves_with_a_busy_child),
    {0},
};
