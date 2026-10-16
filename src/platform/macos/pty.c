/* The pty parts of platform/pty.h on macOS. A session leader takes its
 * terminal only through TIOCSCTTY, and the descriptors to close come
 * from libproc, so a raised descriptor limit costs nothing. */
#include "../pty.h"

#include <libproc.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <termios.h>
#include <unistd.h>

/* As exec-clean.py's os.closerange(3, 1 << 20). */
#define CLOSE_LIMIT (1 << 20)

int sk_pty_set_ctty(int fd)
{
    return ioctl(fd, TIOCSCTTY, 0);
}

int sk_pty_set_size(int fd, int cols, int rows)
{
    struct winsize ws = {0};
    ws.ws_col = (unsigned short)cols;
    ws.ws_row = (unsigned short)rows;
    return ioctl(fd, TIOCSWINSZ, &ws);
}

int sk_pty_get_size(int fd, int *cols, int *rows)
{
    struct winsize ws = {0};
    if (ioctl(fd, TIOCGWINSZ, &ws) != 0 || !ws.ws_col || !ws.ws_row)
        return -1;
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
}

int sk_pty_set_modes(int fd)
{
    struct termios t;
    if (tcgetattr(fd, &t) != 0)
        return -1;
    t.c_iflag |= BRKINT | IXANY | IMAXBEL | IUTF8;
    if (cfsetispeed(&t, B38400) != 0 || cfsetospeed(&t, B38400) != 0)
        return -1;
    return tcsetattr(fd, TCSANOW, &t);
}

static void close_each(int low, int keep)
{
    struct rlimit rl;
    int limit = CLOSE_LIMIT;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY &&
        rl.rlim_cur < (rlim_t)CLOSE_LIMIT)
        limit = (int)rl.rlim_cur;
    for (int fd = low; fd < limit; fd++)
        if (fd != keep)
            close(fd);
}

void sk_pty_close_from(int low, int keep)
{
    /* A full buffer may leave some out, so list again until none is
     * left; the bound only guards against a list that never shrinks. */
    for (int round = 0; round < 64; round++) {
        struct proc_fdinfo fds[256];
        int bytes = proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, fds, (int)sizeof fds);
        if (bytes <= 0)
            break;
        int n = bytes / (int)sizeof fds[0], closed = 0;
        for (int i = 0; i < n; i++) {
            if (fds[i].proc_fd >= low && fds[i].proc_fd != keep) {
                close(fds[i].proc_fd);
                closed++;
            }
        }
        if (!closed)
            return;
    }
    close_each(low, keep);
}
