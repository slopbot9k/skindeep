/* The lock a child's spawn and an fd's making share. POSIX.1-2008 has
 * no SOCK_CLOEXEC, pipe2 or accept4, so a pipe, socket or pty is
 * close-on-exec only once fcntl marks it; a child spawned on another
 * thread in between would hold the fd, and a pipe's reader would wait
 * on it for as long as that child lives, a sound's player for minutes.
 * So each such fd is made and marked under the lock, and every fork and
 * posix_spawn is made under it. It is held for those calls only, never
 * across a wait. */
#ifndef SK_SPAWN_H
#define SK_SPAWN_H

void sk_spawn_lock(void);
void sk_spawn_unlock(void);

/* accept, socket and pipe, their fds close-on-exec as they are made.
 * -1 with errno as theirs. */
int sk_accept_cloexec(int fd);
int sk_socket_cloexec(int domain, int type);
int sk_pipe_cloexec(int p[2]);

#endif
