/* The receiver for one tab's hook and statusline relays, as
 * app/metrics/claude/server.js has it (decisions 0009, 0030, 0036): an
 * AF_UNIX socket in the tab's own directory, served by the core loop.
 * The relays find it by ancestry through ROOT/pids/<pid>, a link the
 * tab makes once its CLI has a pid. Each request is answered 204 as
 * soon as its head is read, before anything else, so a hook never has
 * an effect on the session. A body over 64 KiB, or one that is not
 * JSON, is dropped. The process ignores SIGPIPE, as the relays' callers
 * do, so a relay gone before the answer is an error, not a death. */
#include "internal.h"

#include "../../base/http.h"
#include "../../base/paths.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define MAX_CONNS 64
#define CONN_MAX_MS 10000
/* Out of descriptors, accept waits this long before it tries again. */
#define PAUSE_MS 100

typedef struct {
    struct sk_claude_server *srv;
    int fd, watch;
    sk_http_req q;
    bool answered;
    uint64_t born;
} conn;

struct sk_claude_server {
    sk_loop *loop;
    int fd, watch, pause;
    char *socket, *root, *linked;
    sk_claude_payload_fn fn;
    void *ud;
    SK_VEC(conn *) conns;
    sk_arena arena;
    bool closed;
};

static bool set_flags(int fd)
{
    int fl = fcntl(fd, F_GETFL);
    return fl >= 0 && fcntl(fd, F_SETFL, fl | O_NONBLOCK) == 0 && fcntl(fd, F_SETFD, FD_CLOEXEC) == 0;
}

static void drop(sk_claude_server *s, conn *c)
{
    for (size_t i = 0; i < s->conns.len; i++) {
        if (s->conns.ptr[i] == c) {
            s->conns.ptr[i] = s->conns.ptr[--s->conns.len];
            break;
        }
    }
    sk_loop_unwatch(s->loop, c->watch);
    close(c->fd);
    sk_http_req_free(&c->q);
    free(c);
}

/* A whole request: the connection closes, then the body goes to the
 * session when it names a route, fits and is JSON. */
static void finish(sk_claude_server *s, conn *c)
{
    bool post = sk_str_eq_c(c->q.method, "POST");
    bool hook = post && sk_str_eq_c(c->q.target, "/hook");
    bool status = post && sk_str_eq_c(c->q.target, "/statusline");
    bool keep = (hook || status) && !c->q.oversize;
    sk_buf body = c->q.body;
    memset(&c->q.body, 0, sizeof c->q.body);
    drop(s, c);
    if (keep) {
        const sk_json *v = sk_json_parse(&s->arena, body.ptr ? body.ptr : "", body.len, NULL);
        if (v)
            s->fn(s->ud, status, v);
        sk_arena_reset(&s->arena);
    }
    sk_buf_free(&body);
}

static void on_conn(sk_loop *loop, int fd, short revents, void *ud)
{
    (void)loop, (void)revents;
    conn *c = ud;
    sk_claude_server *srv = c->srv;
    char buf[16384];
    for (;;) {
        ssize_t r = read(fd, buf, sizeof buf);
        if (r < 0 && errno == EINTR)
            continue;
        if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return;
        if (r <= 0) {
            drop(srv, c);
            return;
        }
        sk_http_req_feed(&c->q, buf, (size_t)r);
        if (c->q.state == SK_HTTP_ERROR) {
            drop(srv, c);
            return;
        }
        if (c->q.state != SK_HTTP_HEAD && !c->answered) {
            c->answered = true;
            sk_buf a = {0};
            sk_http_write_204(&a);
            ssize_t w = write(fd, a.ptr, a.len);
            (void)w;
            sk_buf_free(&a);
        }
        if (c->q.state == SK_HTTP_DONE) {
            finish(srv, c);
            return;
        }
    }
}

static void on_accept(sk_loop *loop, int fd, short revents, void *ud);

static void on_resume(sk_loop *loop, void *ud)
{
    sk_claude_server *s = ud;
    s->pause = 0;
    s->watch = sk_loop_watch(loop, s->fd, POLLIN, on_accept, s);
}

static void on_accept(sk_loop *loop, int fd, short revents, void *ud)
{
    (void)revents;
    sk_claude_server *s = ud;
    for (;;) {
        int cfd = accept(fd, NULL, NULL);
        if (cfd < 0) {
            if (errno == EINTR)
                continue;
            /* The connection stays queued, and poll would report it at
             * once, round after round: the listener rests a while. A
             * relay waits 500 ms before it gives up. */
            if (errno == EMFILE || errno == ENFILE || errno == ENOBUFS || errno == ENOMEM) {
                sk_loop_unwatch(loop, s->watch);
                s->watch = 0;
                s->pause = sk_loop_timer(loop, PAUSE_MS, 0, on_resume, s);
            }
            return;
        }
        if (s->conns.len >= MAX_CONNS || !set_flags(cfd)) {
            close(cfd);
            continue;
        }
        conn *c = sk_xcalloc(1, sizeof *c);
        c->srv = s;
        c->fd = cfd;
        c->born = sk_now_ms();
        sk_http_req_init(&c->q, SK_HTTP_MAX_BODY);
        c->watch = sk_loop_watch(loop, cfd, POLLIN, on_conn, c);
        sk_vec_push(&s->conns, c);
    }
}

sk_claude_server *sk_claude_server_start(sk_loop *loop, const char *dir, const char *root,
                                         sk_claude_payload_fn fn, void *ud)
{
    if (!loop || !dir || !root) {
        errno = EINVAL;
        return NULL;
    }
    char *path = sk_path_join(dir, "hooks.sock");
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof addr.sun_path) {
        free(path);
        errno = ENAMETOOLONG;
        return NULL;
    }
    memcpy(addr.sun_path, path, strlen(path));
    unlink(path);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0 || !set_flags(fd) || bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0 || listen(fd, 64) != 0) {
        int saved = errno;
        if (fd >= 0)
            close(fd);
        free(path);
        errno = saved;
        return NULL;
    }
    sk_claude_server *s = sk_xcalloc(1, sizeof *s);
    s->loop = loop;
    s->fd = fd;
    s->socket = path;
    s->root = sk_xstrdup(root);
    s->fn = fn;
    s->ud = ud;
    sk_arena_init(&s->arena, 0);
    s->watch = sk_loop_watch(loop, fd, POLLIN, on_accept, s);
    return s;
}

const char *sk_claude_server_socket(const sk_claude_server *s)
{
    return s->socket;
}

int sk_claude_server_link(sk_claude_server *s, pid_t pid)
{
    if (s->closed) {
        errno = EINVAL;
        return -1;
    }
    char *pids = sk_path_join(s->root, "pids");
    int rc = sk_mkdirs(pids, 0700);
    sk_buf p = {0};
    sk_buf_printf(&p, "%s/%ld", pids, (long)pid);
    free(pids);
    if (rc == 0) {
        unlink(p.ptr);
        rc = symlink(s->socket, p.ptr);
    }
    if (rc != 0) {
        int saved = errno;
        sk_buf_free(&p);
        errno = saved;
        return -1;
    }
    free(s->linked);
    s->linked = p.ptr;
    return 0;
}

void sk_claude_server_sweep(sk_claude_server *s, uint64_t now)
{
    for (size_t i = s->conns.len; i > 0; i--) {
        conn *c = s->conns.ptr[i - 1];
        if (now - c->born > CONN_MAX_MS)
            drop(s, c);
    }
}

void sk_claude_server_close(sk_claude_server *s)
{
    if (!s || s->closed)
        return;
    s->closed = true;
    if (s->linked) {
        /* Only while the link still names this socket. */
        char target[4096];
        ssize_t n = readlink(s->linked, target, sizeof target - 1);
        if (n >= 0) {
            target[n] = 0;
            if (!strcmp(target, s->socket))
                unlink(s->linked);
        }
        free(s->linked);
        s->linked = NULL;
    }
    sk_loop_unwatch(s->loop, s->watch);
    sk_loop_cancel(s->loop, s->pause);
    close(s->fd);
    while (s->conns.len)
        drop(s, s->conns.ptr[s->conns.len - 1]);
    unlink(s->socket);
}

void sk_claude_server_free(sk_claude_server *s)
{
    if (!s)
        return;
    sk_claude_server_close(s);
    sk_vec_free(&s->conns);
    sk_arena_free(&s->arena);
    free(s->socket);
    free(s->root);
    free(s);
}
