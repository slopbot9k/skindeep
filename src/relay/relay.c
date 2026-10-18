#include "relay.h"

#include "../base/base64.h"
#include "../base/http.h"
#include "../base/paths.h"
#include "../base/str.h"
#include "../core/loop.h"
#include "../platform/proc.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

char *sk_relay_window(const char *root, pid_t from)
{
    /* Only a root of this user's alone: another's could link a pid to a
     * socket that takes the hooks' payloads. */
    if (!root || !*root || !sk_path_private_dir(root, false))
        return NULL;
    uid_t me = geteuid();
    pid_t pid = from;
    for (int i = 0; i < SK_RELAY_MAX_DEPTH && pid > 1; i++) {
        sk_buf p = {0};
        sk_buf_printf(&p, "%s/pids/%ld", root, (long)pid);
        struct stat st;
        /* Follows the link: a dead one fails; a live one is the
         * window's socket, this user's. */
        if (stat(p.ptr, &st) == 0 && S_ISSOCK(st.st_mode) && st.st_uid == me)
            return p.ptr;
        sk_buf_free(&p);
        sk_proc_stat ps;
        if (!sk_proc_read(pid, &ps))
            return NULL;
        pid = ps.ppid;
    }
    return NULL;
}

static bool set_flags(int fd)
{
    int fl = fcntl(fd, F_GETFL);
    return fl >= 0 && fcntl(fd, F_SETFL, fl | O_NONBLOCK) == 0 && fcntl(fd, F_SETFD, FD_CLOEXEC) == 0;
}

/* One POST as a state machine, so the statusline relay can run it beside
 * its command. fd is -1 once it has finished, either way. */
typedef struct {
    int fd;
    bool connected, answered;
    sk_buf req;
    size_t sent;
    uint64_t deadline;
} post;

static void post_end(post *p)
{
    if (p->fd >= 0)
        close(p->fd);
    p->fd = -1;
}

static void post_begin(post *p, const char *sock, const char *path, const char *body, size_t n,
                       int timeout_ms)
{
    memset(p, 0, sizeof *p);
    p->deadline = sk_now_ms() + (uint64_t)timeout_ms;
    sk_http_write_post(&p->req, path, n);
    sk_buf_append(&p->req, body, n);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    p->fd = -1;
    if (strlen(sock) >= sizeof addr.sun_path)
        return;
    memcpy(addr.sun_path, sock, strlen(sock));
    p->fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (p->fd < 0)
        return;
    if (!set_flags(p->fd)) {
        post_end(p);
        return;
    }
    if (connect(p->fd, (struct sockaddr *)&addr, sizeof addr) == 0)
        p->connected = true;
    else if (errno != EINPROGRESS && errno != EAGAIN && errno != EINTR)
        post_end(p);
}

static short post_events(const post *p)
{
    return p->connected && p->sent == p->req.len ? POLLIN : POLLOUT;
}

static void post_step(post *p, short revents)
{
    if (!p->connected) {
        int err = 0;
        socklen_t len = sizeof err;
        if (getsockopt(p->fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err != 0) {
            post_end(p);
            return;
        }
        p->connected = true;
    }
    if (p->sent < p->req.len) {
        ssize_t w = write(p->fd, p->req.ptr + p->sent, p->req.len - p->sent);
        if (w > 0)
            p->sent += (size_t)w;
        else if (errno != EAGAIN && errno != EINTR)
            post_end(p);
        return;
    }
    if (revents & (POLLIN | POLLHUP | POLLERR)) {
        /* The app answers 204 and closes; any answer will do. */
        char buf[256];
        ssize_t r = read(p->fd, buf, sizeof buf);
        if (r > 0)
            p->answered = true;
        if (r == 0 || (r < 0 && errno != EAGAIN && errno != EINTR))
            post_end(p);
    }
}

static int remaining(uint64_t deadline)
{
    uint64_t now = sk_now_ms();
    return deadline > now ? (int)SK_MIN(deadline - now, (uint64_t)60000) : 0;
}

int sk_relay_post(const char *sock, const char *path, const char *body, size_t n, int timeout_ms)
{
    post p;
    post_begin(&p, sock, path, body, n, timeout_ms);
    while (p.fd >= 0) {
        int wait = remaining(p.deadline);
        if (wait == 0)
            break;
        struct pollfd pfd = {p.fd, post_events(&p), 0};
        int r = poll(&pfd, 1, wait);
        if (r > 0)
            post_step(&p, pfd.revents);
        else if (r < 0 && errno != EINTR)
            break;
    }
    post_end(&p);
    sk_buf_free(&p.req);
    return p.answered ? 0 : -1;
}

/* All of fd, keeping at most cap bytes; *over when there was more. */
static void read_all(int fd, sk_buf *b, size_t cap, bool *over)
{
    char chunk[65536];
    *over = false;
    for (;;) {
        ssize_t r = read(fd, chunk, sizeof chunk);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return;
        if (b->len + (size_t)r > cap)
            *over = true;
        else
            sk_buf_append(b, chunk, (size_t)r);
    }
}

int sk_relay_hook(const char *root, int in_fd, pid_t from)
{
    sk_buf body = {0};
    bool over;
    read_all(in_fd, &body, SK_HTTP_MAX_BODY, &over);
    if (root && body.len && !over) {
        char *sock = sk_relay_window(root, from);
        if (sock)
            sk_relay_post(sock, "/hook", body.ptr, body.len, SK_RELAY_HOOK_MS);
        free(sock);
    }
    sk_buf_free(&body);
    return 0;
}

static volatile sig_atomic_t term_child;

static void on_term(int signo)
{
    (void)signo;
    if (term_child > 0)
        kill((pid_t)term_child, SIGTERM);
    _exit(143);
}

/* The command in chain, or NULL for none. */
static char *decode_chain(const char *chain)
{
    if (!chain || !strcmp(chain, "-"))
        return NULL;
    sk_buf cmd = {0};
    if (!sk_base64url_decode(&cmd, sk_str_c(chain)) || cmd.len == 0 || memchr(cmd.ptr, 0, cmd.len)) {
        sk_buf_free(&cmd);
        return NULL;
    }
    return cmd.ptr;
}

static pid_t start_command(const char *cmd, int out_fd, int *to_child)
{
    int p[2];
    if (pipe(p) != 0)
        return -1;
    pid_t pid = fork();
    if (pid < 0) {
        close(p[0]);
        close(p[1]);
        return -1;
    }
    if (pid == 0) {
        signal(SIGPIPE, SIG_DFL);
        if (dup2(p[0], 0) < 0 || (out_fd != 1 && dup2(out_fd, 1) < 0))
            _exit(127);
        close(p[0]);
        close(p[1]);
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    close(p[0]);
    set_flags(p[1]);
    *to_child = p[1];
    return pid;
}

int sk_relay_statusline(const char *root, const char *chain, int in_fd, int out_fd, pid_t from)
{
    sk_buf body = {0};
    bool over;
    read_all(in_fd, &body, SIZE_MAX / 2, &over);

    char *cmd = decode_chain(chain);
    int to_child = -1, status = 0;
    pid_t child = -1;
    struct sigaction old_term, sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_term;
    sigemptyset(&sa.sa_mask);
    if (cmd) {
        child = start_command(cmd, out_fd, &to_child);
        if (child < 0) {
            status = 1;
        } else {
            term_child = child;
            sigaction(SIGTERM, &sa, &old_term);
        }
    }

    post p = {.fd = -1};
    char *sock = sk_relay_window(root, from);
    if (sock)
        post_begin(&p, sock, "/statusline", body.ptr ? body.ptr : "", body.len,
                   SK_RELAY_STATUSLINE_MS);
    size_t written = 0;
    while (p.fd >= 0 || to_child >= 0) {
        struct pollfd fds[2];
        int n = 0, post_at = -1, child_at = -1, wait = -1;
        if (p.fd >= 0) {
            wait = remaining(p.deadline);
            if (wait == 0) {
                post_end(&p);
                continue;
            }
            post_at = n;
            fds[n++] = (struct pollfd){p.fd, post_events(&p), 0};
        }
        if (to_child >= 0) {
            child_at = n;
            fds[n++] = (struct pollfd){to_child, POLLOUT, 0};
        }
        int r = poll(fds, (nfds_t)n, wait);
        if (r < 0 && errno != EINTR)
            break;
        if (r <= 0)
            continue;
        if (post_at >= 0 && fds[post_at].revents)
            post_step(&p, fds[post_at].revents);
        if (child_at >= 0 && fds[child_at].revents) {
            ssize_t w = written < body.len ? write(to_child, body.ptr + written, body.len - written) : 0;
            if (w > 0)
                written += (size_t)w;
            /* Done, or the command stopped reading: either way its stdin
             * ends here. */
            if (written == body.len || (w < 0 && errno != EAGAIN && errno != EINTR) ||
                (fds[child_at].revents & (POLLERR | POLLHUP))) {
                close(to_child);
                to_child = -1;
            }
        }
    }
    post_end(&p);
    if (to_child >= 0)
        close(to_child);

    if (child > 0) {
        int ws;
        pid_t w;
        while ((w = waitpid(child, &ws, 0)) < 0 && errno == EINTR)
            ;
        if (w == child)
            status = WIFEXITED(ws) ? WEXITSTATUS(ws) : 128;
        term_child = 0;
        sigaction(SIGTERM, &old_term, NULL);
    }
    free(sock);
    free(cmd);
    sk_buf_free(&p.req);
    sk_buf_free(&body);
    return status;
}
