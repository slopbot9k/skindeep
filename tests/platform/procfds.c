/* The files a process holds open, on this process and on a child, and
 * the mtime to the fraction of a ms (decisions 0013, 0036). */
#include "../../src/base/paths.h"
#include "../../src/platform/file.h"
#include "../../src/platform/procfds.h"
#include "../test.h"

#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static bool listed(pid_t pid, const char *path)
{
    sk_strv v = {0};
    sk_proc_open_files(pid, &v);
    bool found = false;
    for (size_t i = 0; i < v.len; i++)
        found = found || !strcmp(v.ptr[i], path);
    sk_strv_free(&v);
    return found;
}

/* The path the system names a file by: links resolved, as both systems
 * report an open file's path. */
static char *real_file(const char *name)
{
    char *dir = realpath(sk_test_tmpdir(), NULL);
    char *p = sk_path_join(dir, name);
    free(dir);
    return p;
}

static void test_this_process_lists_a_file_it_opened(void)
{
    char *path = real_file("held.txt");
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    CHECK(fd >= 0);
    CHECK(listed(getpid(), path));
    close(fd);
    CHECK(!listed(getpid(), path));
    free(path);
}

static void test_a_child_lists_what_it_inherited(void)
{
    char *path = real_file("child.txt");
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    CHECK(fd >= 0);
    pid_t pid = fork();
    if (pid == 0) {
        for (;;)
            pause();
    }
    close(fd);
    CHECK(pid > 0);
    CHECK(listed(pid, path));
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
    /* A reaped child is gone: nothing, and no error. */
    CHECK(!listed(pid, path));
    free(path);
}

static void test_descriptors_in_the_order_node_lists_them(void)
{
    char *paths[12];
    int fds[12];
    for (int i = 0; i < 12; i++) {
        char name[32];
        snprintf(name, sizeof name, "order-%02d.txt", i);
        paths[i] = real_file(name);
        fds[i] = open(paths[i], O_WRONLY | O_CREAT | O_TRUNC, 0600);
    }
    sk_strv v = {0};
    sk_proc_open_files(getpid(), &v);
    /* Ordered by the descriptor as a string: "10" before "9". */
    char prev[16] = "";
    size_t seen = 0;
    for (size_t i = 0; i < v.len; i++) {
        for (int k = 0; k < 12; k++) {
            if (strcmp(v.ptr[i], paths[k]))
                continue;
            char num[16];
            snprintf(num, sizeof num, "%d", fds[k]);
            CHECK(strcmp(prev, num) < 0);
            snprintf(prev, sizeof prev, "%s", num);
            seen++;
        }
    }
    CHECK_INT(seen, 12);
    sk_strv_free(&v);
    for (int i = 0; i < 12; i++) {
        close(fds[i]);
        free(paths[i]);
    }
}

static void test_mtime_keeps_its_fraction(void)
{
    char *path = real_file("stamped.txt");
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    CHECK(fd >= 0);
    close(fd);
    struct timespec times[2];
    times[0].tv_sec = times[1].tv_sec = 1700000000;
    times[0].tv_nsec = times[1].tv_nsec = 123456789;
    CHECK_INT(utimensat(AT_FDCWD, path, times, 0), 0);
    struct stat st;
    CHECK_INT(stat(path, &st), 0);
    double ms = sk_stat_mtime_ms(&st);
    /* To the microsecond: a double this large holds no finer. */
    CHECK(fabs(ms - 1700000000123.456789) < 0.001);
    free(path);
}

const sk_test sk_suite_procfds[] = {
    T(test_this_process_lists_a_file_it_opened),
    T(test_a_child_lists_what_it_inherited),
    T(test_descriptors_in_the_order_node_lists_them),
    T(test_mtime_keeps_its_fraction),
    {0},
};
