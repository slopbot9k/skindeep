/* The pty parts of platform/pty.h on Linux. Opening the slave already
 * made it the session leader's terminal, so TIOCSCTTY only confirms it.
 * close_range (Linux 5.9) closes the inherited descriptors in one call;
 * an older kernel gets a loop up to the descriptor limit. */
#define _DEFAULT_SOURCE /* syscall() */
#include "../pty.h"

#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
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

#ifdef SYS_close_range
static bool close_range_(int low, int high)
{
    return high < low || syscall(SYS_close_range, (unsigned)low, (unsigned)high, 0u) == 0;
}
#endif

void sk_pty_close_from(int low, int keep)
{
#ifdef SYS_close_range
    int top = (int)(~0u >> 1);
    if (keep < low ? close_range_(low, top)
                   : close_range_(low, keep - 1) && close_range_(keep + 1, top))
        return;
#endif
    struct rlimit rl;
    int limit = CLOSE_LIMIT;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY &&
        rl.rlim_cur < (rlim_t)CLOSE_LIMIT)
        limit = (int)rl.rlim_cur;
    for (int fd = low; fd < limit; fd++)
        if (fd != keep)
            close(fd);
}
