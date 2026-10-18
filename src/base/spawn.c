#include "spawn.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void sk_spawn_lock(void)
{
    pthread_mutex_lock(&lock);
}

void sk_spawn_unlock(void)
{
    pthread_mutex_unlock(&lock);
}

/* fd marked, or closed and -1 with fcntl's errno. */
static int marked(int fd)
{
    if (fd < 0 || fcntl(fd, F_SETFD, FD_CLOEXEC) == 0)
        return fd;
    int e = errno;
    close(fd);
    errno = e;
    return -1;
}

int sk_accept_cloexec(int fd)
{
    sk_spawn_lock();
    int r = marked(accept(fd, NULL, NULL));
    int e = errno;
    sk_spawn_unlock();
    errno = e;
    return r;
}

int sk_socket_cloexec(int domain, int type)
{
    sk_spawn_lock();
    int r = marked(socket(domain, type, 0));
    int e = errno;
    sk_spawn_unlock();
    errno = e;
    return r;
}

int sk_pipe_cloexec(int p[2])
{
    sk_spawn_lock();
    int r = pipe(p);
    /* marked closes the one it cannot mark; the other is closed here. */
    if (r == 0 && ((p[0] = marked(p[0])) < 0 || (p[1] = marked(p[1])) < 0)) {
        int e = errno, other = p[0] < 0 ? p[1] : p[0];
        close(other);
        errno = e;
        r = -1;
    }
    int e = errno;
    sk_spawn_unlock();
    errno = e;
    return r;
}
