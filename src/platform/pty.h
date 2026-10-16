/* The parts of a session's pty that POSIX leaves to each system
 * (decision 0036): the controlling terminal, the window size, the modes
 * node-pty gives a new terminal, and closing what a child inherited, as
 * app/exec-clean.py does. core/session.c opens the pty itself with
 * posix_openpt. */
#ifndef SK_PTY_H
#define SK_PTY_H

#include "../base/base.h"

/* Makes fd, a pty's slave opened by a session leader, the controlling
 * terminal. Async-signal-safe, for a child between fork and exec. */
int sk_pty_set_ctty(int fd);
/* The window size of the pty fd, master or slave. */
int sk_pty_set_size(int fd, int cols, int rows);
/* The window size of the terminal fd; -1 when it has none, or reports
 * zero. */
int sk_pty_get_size(int fd, int *cols, int *rows);
/* The input modes and speed node-pty sets on top of the system's
 * defaults: BRKINT, IXANY, IMAXBEL and IUTF8, 38400 baud. */
int sk_pty_set_modes(int fd);
/* Closes every descriptor from low up but keep (-1: none).
 * Async-signal-safe, for a child between fork and exec. */
void sk_pty_close_from(int low, int keep);

#endif
