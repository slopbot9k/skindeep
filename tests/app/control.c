/* The control socket (src/app/control.h): answers wait in their
 * connection, not in the loop, so a peer that reads late gets them whole
 * and holds nothing up; and the socket replaces nothing but an old
 * socket, in a folder of the user's alone. */
#include "../../src/app/control.h"
#include "../../src/base/paths.h"
#include "../test.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define BIG (4u << 20)

/* Every request answered with a string of BIG x's. */
static void big_answer(void *ud, sk_control_req *req, const sk_json *msg)
{
    (void)ud, (void)msg;
    char *json = sk_xmalloc(BIG + 2);
    json[0] = '"';
    memset(json + 1, 'x', BIG);
    json[BIG + 1] = '"';
    sk_control_answer(req, json, BIG + 2);
    free(json);
}

static int dial(const char *path)
{
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, path, strlen(path));
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK(fd >= 0 && connect(fd, (struct sockaddr *)&addr, sizeof addr) == 0);
    return fd;
}

/* The test's folder may be too deep for sun_path: a short one of its
 * own. */
static char *short_dir(void)
{
    char *d = sk_xstrdup("/tmp/skc-XXXXXX");
    CHECK(mkdtemp(d) != NULL);
    return d;
}

static void test_a_late_reader_holds_nothing_up(void)
{
    char *dir = short_dir(), *path = sk_path_join(dir, "c.sock");
    sk_loop *loop = sk_loop_new();
    char *err = NULL;
    sk_control *c = sk_control_open(loop, path, big_answer, NULL, &err);
    CHECK(c != NULL);
    if (!c) {
        free(err);
        sk_loop_free(loop);
        return;
    }
    int fd = dial(path);
    CHECK(write(fd, "{\"op\":\"x\"}\n", 11) == 11);
    /* The peer reads nothing yet: each turn of the loop comes back at
     * once, the answer waiting in the connection. */
    for (int i = 0; i < 20; i++) {
        uint64_t t0 = sk_now_ms();
        sk_loop_once(loop, 0);
        CHECK(sk_now_ms() - t0 < 200);
    }
    /* Then it reads, and gets it whole. */
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    size_t got = 0;
    bool line = false;
    char buf[65536];
    for (uint64_t until = sk_now_ms() + 10000; !line && sk_now_ms() < until;) {
        sk_loop_once(loop, 5);
        ssize_t r = read(fd, buf, sizeof buf);
        if (r > 0) {
            got += (size_t)r;
            line = buf[r - 1] == '\n';
        }
    }
    CHECK(line);
    CHECK_INT(got, BIG + 3);
    close(fd);
    sk_control_close(c);
    sk_loop_free(loop);
    rmdir(dir);
    free(path);
    free(dir);
}

static void test_the_path_is_checked(void)
{
    char *dir = short_dir(), *file = sk_path_join(dir, "notes"), *err = NULL;
    sk_loop *loop = sk_loop_new();
    FILE *f = fopen(file, "w");
    CHECK(f != NULL);
    if (f)
        fclose(f);
    /* A file there stays, and the socket is refused. */
    CHECK(sk_control_open(loop, file, big_answer, NULL, &err) == NULL);
    CHECK(err && strstr(err, "not a socket"));
    free(err);
    err = NULL;
    struct stat st;
    CHECK(lstat(file, &st) == 0 && S_ISREG(st.st_mode));
    /* A folder others may write in is refused. */
    char *open_dir = sk_path_join(dir, "open"), *sock = sk_path_join(open_dir, "c.sock");
    CHECK(mkdir(open_dir, 0700) == 0 && chmod(open_dir, 0777) == 0);
    CHECK(sk_control_open(loop, sock, big_answer, NULL, &err) == NULL);
    CHECK(err && strstr(err, "not the user's alone"));
    free(err);
    sk_loop_free(loop);
    unlink(file);
    rmdir(open_dir);
    rmdir(dir);
    free(file);
    free(open_dir);
    free(sock);
    free(dir);
}

const sk_test sk_suite_control[] = {
    T(test_a_late_reader_holds_nothing_up),
    T(test_the_path_is_checked),
    {0},
};
