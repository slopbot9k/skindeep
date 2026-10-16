/* $DISPLAY and the Unix socket it leads to. */
#include "x11.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static bool digits(const char *s, size_t n)
{
    if (n == 0 || n > 9)
        return false;
    for (size_t i = 0; i < n; i++)
        if (s[i] < '0' || s[i] > '9')
            return false;
    return true;
}

static bool fail(char **err, const char *name, const char *what)
{
    sk_buf b = {0};
    sk_buf_printf(&b, "DISPLAY %s %s", name, what);
    *err = b.ptr;
    return false;
}

bool sk_x11_display_parse(const char *name, sk_x11_display *d, char **err)
{
    memset(d, 0, sizeof *d);
    if (!name || !*name) {
        *err = sk_xstrdup("DISPLAY is not set");
        return false;
    }
    const char *colon = strrchr(name, ':');
    if (!colon)
        return fail(err, name, "has no display number");
    const char *num = colon + 1, *dot = strchr(num, '.');
    size_t num_len = dot ? (size_t)(dot - num) : strlen(num);
    if (!digits(num, num_len) || (dot && !digits(dot + 1, strlen(dot + 1))))
        return fail(err, name, "has no display number");
    memcpy(d->number, num, num_len);
    d->screen = dot ? atoi(dot + 1) : 0;
    size_t host_len = (size_t)(colon - name);
    if (name[0] == '/') {
        d->sockets[d->n_sockets++] = sk_xstrndup(name, host_len);
        d->sockets[d->n_sockets++] = sk_xstrndup(name, dot ? (size_t)(dot - name) : strlen(name));
        return true;
    }
    if (host_len != 0 && !(host_len == 4 && !memcmp(name, "unix", 4)))
        return fail(err, name, "names a host; X over TCP is not spoken, only a local display");
    sk_buf b = {0};
#ifdef __linux__
    sk_buf_printf(&b, "@/tmp/.X11-unix/X%s", d->number);
    d->sockets[d->n_sockets++] = b.ptr;
    b = (sk_buf){0};
#endif
    sk_buf_printf(&b, "/tmp/.X11-unix/X%s", d->number);
    d->sockets[d->n_sockets++] = b.ptr;
    return true;
}

void sk_x11_display_free(sk_x11_display *d)
{
    for (size_t i = 0; i < d->n_sockets; i++)
        free(d->sockets[i]);
    memset(d, 0, sizeof *d);
}

static int connect_one(const char *path, int *err_no)
{
    struct sockaddr_un sa;
    memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    size_t n = strlen(path);
    if (n >= sizeof sa.sun_path) {
        *err_no = ENAMETOOLONG;
        return -1;
    }
    socklen_t len;
    if (path[0] == '@') {
        /* Linux's abstract namespace: a NUL, then the name, unterminated. */
        memcpy(sa.sun_path + 1, path + 1, n - 1);
        len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + n);
    } else {
        memcpy(sa.sun_path, path, n);
        len = (socklen_t)sizeof sa;
    }
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        *err_no = errno;
        return -1;
    }
    int r;
    while ((r = connect(fd, (struct sockaddr *)&sa, len)) != 0 && errno == EINTR)
        ;
    int fl = r == 0 ? fcntl(fd, F_GETFL) : -1;
    if (r != 0 || fl < 0 || fcntl(fd, F_SETFL, fl | O_NONBLOCK) != 0 ||
        fcntl(fd, F_SETFD, FD_CLOEXEC) != 0) {
        *err_no = errno;
        close(fd);
        return -1;
    }
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    return fd;
}

int sk_x11_display_connect(const sk_x11_display *d, char **err)
{
    int last = ENOENT;
    for (size_t i = 0; i < d->n_sockets; i++) {
        int fd = connect_one(d->sockets[i], &last);
        if (fd >= 0)
            return fd;
    }
    sk_buf b = {0};
    sk_buf_printf(&b, "cannot reach the X server at %s: %s",
                  d->n_sockets ? d->sockets[d->n_sockets - 1] : "(nothing)", strerror(last));
    *err = b.ptr;
    return -1;
}
