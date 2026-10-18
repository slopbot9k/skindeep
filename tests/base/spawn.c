/* The spawn lock's fds: each close-on-exec as it is made, so no child
 * another thread spawns holds one. */
#include "../../src/base/paths.h"
#include "../../src/base/spawn.h"
#include "../test.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static bool cloexec(int fd)
{
    int fl = fcntl(fd, F_GETFD);
    return fl >= 0 && (fl & FD_CLOEXEC);
}

static void test_pipes_sockets_and_accepts_are_close_on_exec(void)
{
    int p[2];
    CHECK_INT(sk_pipe_cloexec(p), 0);
    CHECK(cloexec(p[0]) && cloexec(p[1]));
    close(p[0]);
    close(p[1]);
    int l = sk_socket_cloexec(AF_UNIX, SOCK_STREAM);
    CHECK(l >= 0 && cloexec(l));
    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    char *path = sk_path_join(sk_test_tmpdir(), "s");
    CHECK(strlen(path) < sizeof a.sun_path);
    memcpy(a.sun_path, path, strlen(path));
    CHECK(bind(l, (struct sockaddr *)&a, sizeof a) == 0 && listen(l, 1) == 0);
    int c = socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK(connect(c, (struct sockaddr *)&a, sizeof a) == 0);
    int s = sk_accept_cloexec(l);
    CHECK(s >= 0 && cloexec(s));
    close(s);
    close(c);
    close(l);
    free(path);
    /* The lock is taken and given back. */
    sk_spawn_lock();
    sk_spawn_unlock();
}

const sk_test sk_suite_spawn[] = {
    T(test_pipes_sockets_and_accepts_are_close_on_exec),
    {0},
};
