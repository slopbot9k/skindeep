#include "control.h"

#include "../base/arena.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

/* A request longer than this ends its connection. */
#define MAX_LINE (1 << 20)

typedef struct conn {
    struct sk_control *c;
    int id, fd, watch;
    sk_buf in;
    /* Requests not yet answered, and whether the peer has stopped
     * sending: a peer that sends and then shuts its side still gets
     * every answer. */
    int pending;
    bool eof;
} conn;

struct sk_control {
    sk_loop *loop;
    int fd, watch, next_id;
    char *path;
    sk_control_fn fn;
    void *ud;
    SK_VEC(conn *) conns;
};

struct sk_control_req {
    sk_control *c;
    int conn;
};

static conn *find(sk_control *c, int id)
{
    for (size_t i = 0; i < c->conns.len; i++)
        if (c->conns.ptr[i]->id == id)
            return c->conns.ptr[i];
    return NULL;
}

static void drop(sk_control *c, conn *k)
{
    sk_loop_unwatch(c->loop, k->watch);
    close(k->fd);
    sk_buf_free(&k->in);
    for (size_t i = 0; i < c->conns.len; i++)
        if (c->conns.ptr[i] == k) {
            c->conns.ptr[i] = c->conns.ptr[--c->conns.len];
            break;
        }
    free(k);
}

/* The socket is non-blocking for reads; an answer waits a second at
 * most for room. */
static bool write_all(int fd, const char *p, size_t n)
{
    while (n) {
        ssize_t w = write(fd, p, n);
        if (w < 0 && errno == EINTR)
            continue;
        if (w < 0 && errno == EAGAIN) {
            struct pollfd pf = {fd, POLLOUT, 0};
            if (poll(&pf, 1, 1000) <= 0)
                return false;
            continue;
        }
        if (w <= 0)
            return false;
        p += w;
        n -= (size_t)w;
    }
    return true;
}

typedef struct {
    sk_control *c;
    int conn;
    size_t n;
    char line[];
} answer;

/* On the loop's thread: the connection may have gone meanwhile. */
static void do_answer(void *ud)
{
    answer *a = ud;
    conn *k = find(a->c, a->conn);
    if (k) {
        k->pending--;
        if (!write_all(k->fd, a->line, a->n) || (k->eof && !k->pending))
            drop(a->c, k);
    }
    free(a);
}

void sk_control_answer(sk_control_req *req, const char *json, size_t n)
{
    answer *a = sk_xmalloc(sizeof *a + n + 1);
    a->c = req->c;
    a->conn = req->conn;
    memcpy(a->line, json, n);
    a->line[n] = '\n';
    a->n = n + 1;
    sk_loop_post(req->c->loop, do_answer, a);
    free(req);
}

void sk_control_ok(sk_control_req *req, const char *why)
{
    sk_buf b = {0};
    sk_jw w;
    sk_jw_init(&w, &b, 0);
    sk_jw_object(&w);
    sk_jw_key(&w, SK_STR("ok"));
    sk_jw_bool(&w, why == NULL);
    if (why) {
        sk_jw_key(&w, SK_STR("error"));
        sk_jw_string(&w, sk_str_c(why));
    }
    sk_jw_object_end(&w);
    sk_control_answer(req, b.ptr, b.len);
    sk_buf_free(&b);
}

static void request(sk_control *c, conn *k, const char *line, size_t n)
{
    sk_arena a;
    sk_arena_init(&a, 0);
    const sk_json *msg = sk_json_parse(&a, line, n, NULL);
    sk_control_req *req = sk_xmalloc(sizeof *req);
    req->c = c;
    req->conn = k->id;
    k->pending++;
    if (!msg || msg->type != SK_JSON_OBJECT)
        sk_control_ok(req, "not a JSON object");
    else
        c->fn(c->ud, req, msg);
    sk_arena_free(&a);
}

static void on_conn(sk_loop *loop, int fd, short revents, void *ud)
{
    (void)loop, (void)revents;
    conn *k = ud;
    sk_control *c = k->c;
    char buf[65536];
    ssize_t r = read(fd, buf, sizeof buf);
    if (r < 0 && (errno == EAGAIN || errno == EINTR))
        return;
    if (r <= 0) {
        if (!k->pending) {
            drop(c, k);
            return;
        }
        sk_loop_unwatch(c->loop, k->watch);
        k->watch = 0;
        k->eof = true;
        return;
    }
    sk_buf_append(&k->in, buf, (size_t)r);
    /* Each whole line is a request; the handler may not drop k, since
     * answers go through the loop. */
    size_t start = 0;
    for (size_t i = 0; i < k->in.len; i++) {
        if (k->in.ptr[i] != '\n')
            continue;
        if (i > start)
            request(c, k, k->in.ptr + start, i - start);
        start = i + 1;
    }
    sk_buf_consume(&k->in, start);
    if (k->in.len > MAX_LINE)
        drop(c, k);
}

static void on_accept(sk_loop *loop, int fd, short revents, void *ud)
{
    (void)revents;
    sk_control *c = ud;
    for (;;) {
        int cfd = accept(fd, NULL, NULL);
        if (cfd < 0) {
            if (errno == EINTR)
                continue;
            return;
        }
        int fl = fcntl(cfd, F_GETFL);
        if (fl < 0 || fcntl(cfd, F_SETFL, fl | O_NONBLOCK) != 0 || fcntl(cfd, F_SETFD, FD_CLOEXEC) != 0) {
            close(cfd);
            continue;
        }
        conn *k = sk_xcalloc(1, sizeof *k);
        k->c = c;
        k->id = ++c->next_id;
        k->fd = cfd;
        k->watch = sk_loop_watch(loop, cfd, POLLIN, on_conn, k);
        sk_vec_push(&c->conns, k);
    }
}

sk_control *sk_control_open(sk_loop *loop, const char *path, sk_control_fn fn, void *ud, char **err)
{
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    size_t n = strlen(path);
    if (n >= sizeof addr.sun_path) {
        *err = sk_xstrdup("the control socket's path is too long");
        return NULL;
    }
    memcpy(addr.sun_path, path, n);
    unlink(path);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    mode_t old = umask(077);
    bool ok = fd >= 0 && fcntl(fd, F_SETFD, FD_CLOEXEC) == 0 && fcntl(fd, F_SETFL, O_NONBLOCK) == 0 &&
              bind(fd, (struct sockaddr *)&addr, sizeof addr) == 0 && listen(fd, 8) == 0;
    umask(old);
    if (!ok) {
        sk_buf why = {0};
        sk_buf_printf(&why, "cannot listen on %s: %s", path, strerror(errno));
        *err = why.ptr;
        if (fd >= 0)
            close(fd);
        return NULL;
    }
    sk_control *c = sk_xcalloc(1, sizeof *c);
    c->loop = loop;
    c->fd = fd;
    c->path = sk_xstrdup(path);
    c->fn = fn;
    c->ud = ud;
    c->watch = sk_loop_watch(loop, fd, POLLIN, on_accept, c);
    return c;
}

void sk_control_close(sk_control *c)
{
    if (!c)
        return;
    while (c->conns.len)
        drop(c, c->conns.ptr[c->conns.len - 1]);
    sk_vec_free(&c->conns);
    sk_loop_unwatch(c->loop, c->watch);
    close(c->fd);
    unlink(c->path);
    free(c->path);
    free(c);
}
