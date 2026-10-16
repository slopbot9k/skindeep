/* The core thread's event loop (decision 0036): one poll() over every
 * descriptor the app watches, timers on the monotonic clock, signals
 * turned into ordinary events through a pipe, and a queue other threads
 * post work to. Only sk_loop_wake and sk_loop_post may be called from
 * another thread. */
#ifndef SK_LOOP_H
#define SK_LOOP_H

#include "../base/base.h"

#include <sys/types.h>

typedef struct sk_loop sk_loop;

/* revents: the poll() events that fired. */
typedef void (*sk_io_cb)(sk_loop *loop, int fd, short revents, void *ud);
typedef void (*sk_timer_cb)(sk_loop *loop, void *ud);
typedef void (*sk_signal_cb)(sk_loop *loop, int signo, void *ud);
typedef void (*sk_task_cb)(void *ud);

sk_loop *sk_loop_new(void);
/* Frees the loop. Watched descriptors are not closed; signals it took
 * go back to their default action. */
void sk_loop_free(sk_loop *loop);

/* Milliseconds on the monotonic clock. */
uint64_t sk_now_ms(void);

/* Calls cb when fd has any of events (POLLIN, POLLOUT). Returns an id
 * for sk_loop_unwatch. Changing a watcher's events: unwatch, watch. */
int sk_loop_watch(sk_loop *loop, int fd, short events, sk_io_cb cb, void *ud);
void sk_loop_unwatch(sk_loop *loop, int id);

/* Calls cb after delay_ms, then every repeat_ms when it is not 0. */
int sk_loop_timer(sk_loop *loop, uint64_t delay_ms, uint64_t repeat_ms, sk_timer_cb cb, void *ud);
void sk_loop_cancel(sk_loop *loop, int id);

/* Delivers signo to cb on the loop's thread. One loop per process may
 * take signals; a second sk_loop_signal for the same signo replaces the
 * first. Returns 0, or -1 when the signal cannot be caught. */
int sk_loop_signal(sk_loop *loop, int signo, sk_signal_cb cb, void *ud);

/* The exit of a child, reaped by the loop. wstatus is waitpid's status,
 * or -1 when the child was reaped elsewhere and its status is lost. */
typedef void (*sk_child_cb)(sk_loop *loop, pid_t pid, int wstatus, void *ud);

/* Reaps pid when it exits and calls cb, once. The loop takes SIGCHLD
 * for itself the first time, so no other code may take SIGCHLD; children
 * nobody watches are left for whoever made them. A child that exited
 * before the call is reaped on the next round. -1 when SIGCHLD cannot be
 * caught. */
int sk_loop_child(sk_loop *loop, pid_t pid, sk_child_cb cb, void *ud);
/* Stops watching pid, for an owner that reaps it itself. */
void sk_loop_unchild(sk_loop *loop, pid_t pid);

/* Thread-safe: runs cb(ud) on the loop's thread, in the order posted. */
void sk_loop_post(sk_loop *loop, sk_task_cb cb, void *ud);
/* Thread-safe: makes a poll() in progress return. */
void sk_loop_wake(sk_loop *loop);

/* One round: waits at most timeout_ms (-1: until something happens),
 * then runs whatever is due. */
void sk_loop_once(sk_loop *loop, int timeout_ms);
/* Rounds until sk_loop_stop. */
void sk_loop_run(sk_loop *loop);
void sk_loop_stop(sk_loop *loop);

#endif
