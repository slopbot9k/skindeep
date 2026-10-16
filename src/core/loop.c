#include "loop.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* POSIX names no count of signals; Linux has 64, macOS 31. */
#define MAX_SIGNAL 65

typedef struct {
    int id, fd;
    short events;
    sk_io_cb cb;
    void *ud;
    bool live;
} watcher;

typedef struct {
    int id;
    uint64_t due, repeat, round;
    sk_timer_cb cb;
    void *ud;
    bool live;
} timer;

typedef struct task {
    struct task *next;
    sk_task_cb cb;
    void *ud;
} task;

struct sk_loop {
    SK_VEC(watcher) watchers;
    SK_VEC(timer) timers;
    SK_VEC(struct pollfd) fds;
    SK_VEC(int) fd_ids;
    int next_id;
    uint64_t round;
    bool stopped;
    int wake[2];
    pthread_mutex_t lock;
    task *head, *tail;
    struct {
        sk_signal_cb cb;
        void *ud;
    } signals[MAX_SIGNAL];
};

/* Signals reach the loop through this pipe: the handler only writes the
 * signal's number to it. */
static int sig_pipe[2] = {-1, -1};
static sk_loop *sig_loop;

uint64_t sk_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static int nonblocking_pipe(int p[2])
{
    if (pipe(p) != 0)
        return -1;
    for (int i = 0; i < 2; i++) {
        int fl = fcntl(p[i], F_GETFL);
        if (fl < 0 || fcntl(p[i], F_SETFL, fl | O_NONBLOCK) != 0 ||
            fcntl(p[i], F_SETFD, FD_CLOEXEC) != 0) {
            close(p[0]);
            close(p[1]);
            return -1;
        }
    }
    return 0;
}

static void drain(int fd)
{
    char buf[256];
    while (read(fd, buf, sizeof buf) > 0)
        ;
}

static void run_tasks(sk_loop *loop)
{
    pthread_mutex_lock(&loop->lock);
    task *t = loop->head;
    loop->head = loop->tail = NULL;
    pthread_mutex_unlock(&loop->lock);
    while (t) {
        task *next = t->next;
        t->cb(t->ud);
        free(t);
        t = next;
    }
}

static void on_wake(sk_loop *loop, int fd, short revents, void *ud)
{
    (void)revents, (void)ud;
    drain(fd);
    run_tasks(loop);
}

sk_loop *sk_loop_new(void)
{
    sk_loop *loop = sk_xcalloc(1, sizeof *loop);
    if (nonblocking_pipe(loop->wake) != 0)
        sk_die("cannot make the loop's wake pipe");
    if (pthread_mutex_init(&loop->lock, NULL) != 0)
        sk_die("cannot make the loop's lock");
    loop->next_id = 1;
    sk_loop_watch(loop, loop->wake[0], POLLIN, on_wake, NULL);
    return loop;
}

void sk_loop_free(sk_loop *loop)
{
    if (!loop)
        return;
    for (int s = 1; s < MAX_SIGNAL; s++)
        if (loop->signals[s].cb)
            signal(s, SIG_DFL);
    if (sig_loop == loop) {
        close(sig_pipe[0]);
        close(sig_pipe[1]);
        sig_pipe[0] = sig_pipe[1] = -1;
        sig_loop = NULL;
    }
    for (task *t = loop->head; t;) {
        task *next = t->next;
        free(t);
        t = next;
    }
    close(loop->wake[0]);
    close(loop->wake[1]);
    pthread_mutex_destroy(&loop->lock);
    sk_vec_free(&loop->watchers);
    sk_vec_free(&loop->timers);
    sk_vec_free(&loop->fds);
    sk_vec_free(&loop->fd_ids);
    free(loop);
}

static int new_id(sk_loop *loop)
{
    if (loop->next_id == INT_MAX)
        loop->next_id = 1;
    return loop->next_id++;
}

int sk_loop_watch(sk_loop *loop, int fd, short events, sk_io_cb cb, void *ud)
{
    watcher w = {new_id(loop), fd, events, cb, ud, true};
    sk_vec_push(&loop->watchers, w);
    return w.id;
}

void sk_loop_unwatch(sk_loop *loop, int id)
{
    for (size_t i = 0; i < loop->watchers.len; i++)
        if (loop->watchers.ptr[i].id == id)
            loop->watchers.ptr[i].live = false;
}

int sk_loop_timer(sk_loop *loop, uint64_t delay_ms, uint64_t repeat_ms, sk_timer_cb cb, void *ud)
{
    timer t = {new_id(loop), sk_now_ms() + delay_ms, repeat_ms, 0, cb, ud, true};
    sk_vec_push(&loop->timers, t);
    return t.id;
}

void sk_loop_cancel(sk_loop *loop, int id)
{
    for (size_t i = 0; i < loop->timers.len; i++)
        if (loop->timers.ptr[i].id == id)
            loop->timers.ptr[i].live = false;
}

static void on_signal(int signo)
{
    int saved = errno;
    unsigned char b = (unsigned char)signo;
    ssize_t w = write(sig_pipe[1], &b, 1);
    (void)w;
    errno = saved;
}

static void on_signal_pipe(sk_loop *loop, int fd, short revents, void *ud)
{
    (void)revents, (void)ud;
    unsigned char buf[64];
    ssize_t n;
    while ((n = read(fd, buf, sizeof buf)) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            int s = buf[i];
            if (s < MAX_SIGNAL && loop->signals[s].cb)
                loop->signals[s].cb(loop, s, loop->signals[s].ud);
        }
    }
}

int sk_loop_signal(sk_loop *loop, int signo, sk_signal_cb cb, void *ud)
{
    if (signo <= 0 || signo >= MAX_SIGNAL)
        return -1;
    if (sig_loop && sig_loop != loop)
        sk_die("signals already belong to another loop");
    if (!sig_loop) {
        if (nonblocking_pipe(sig_pipe) != 0)
            return -1;
        sig_loop = loop;
        sk_loop_watch(loop, sig_pipe[0], POLLIN, on_signal_pipe, NULL);
    }
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (sigaction(signo, &sa, NULL) != 0)
        return -1;
    loop->signals[signo].cb = cb;
    loop->signals[signo].ud = ud;
    return 0;
}

void sk_loop_post(sk_loop *loop, sk_task_cb cb, void *ud)
{
    task *t = sk_xmalloc(sizeof *t);
    t->next = NULL;
    t->cb = cb;
    t->ud = ud;
    pthread_mutex_lock(&loop->lock);
    if (loop->tail)
        loop->tail->next = t;
    else
        loop->head = t;
    loop->tail = t;
    pthread_mutex_unlock(&loop->lock);
    sk_loop_wake(loop);
}

void sk_loop_wake(sk_loop *loop)
{
    char b = 1;
    ssize_t w = write(loop->wake[1], &b, 1);
    (void)w; /* a full pipe already wakes the loop */
}

static int wait_ms(const sk_loop *loop, int timeout_ms)
{
    uint64_t now = sk_now_ms();
    int64_t best = timeout_ms;
    for (size_t i = 0; i < loop->timers.len; i++) {
        const timer *t = &loop->timers.ptr[i];
        if (!t->live)
            continue;
        uint64_t d = t->due > now ? t->due - now : 0;
        if (d > INT_MAX)
            d = INT_MAX;
        if (best < 0 || (int64_t)d < best)
            best = (int64_t)d;
    }
    return (int)best;
}

static void run_timers(sk_loop *loop)
{
    uint64_t now = sk_now_ms();
    uint64_t round = ++loop->round;
    /* Due timers run in order of their due time, each at most once a
     * round, timers made during the round included only in the next. */
    size_t count = loop->timers.len;
    for (;;) {
        size_t pick = SIZE_MAX;
        for (size_t i = 0; i < count; i++) {
            const timer *t = &loop->timers.ptr[i];
            if (t->live && t->round != round && t->due <= now &&
                (pick == SIZE_MAX || t->due < loop->timers.ptr[pick].due))
                pick = i;
        }
        if (pick == SIZE_MAX)
            return;
        timer *t = &loop->timers.ptr[pick];
        t->round = round;
        if (t->repeat) {
            t->due += t->repeat;
            if (t->due <= now)
                t->due = now + t->repeat; /* skip the rounds a stall missed */
        } else {
            t->live = false;
        }
        sk_timer_cb cb = t->cb;
        void *ud = t->ud;
        cb(loop, ud);
    }
}

static void compact(sk_loop *loop)
{
    size_t n = 0;
    for (size_t i = 0; i < loop->watchers.len; i++)
        if (loop->watchers.ptr[i].live)
            loop->watchers.ptr[n++] = loop->watchers.ptr[i];
    loop->watchers.len = n;
    n = 0;
    for (size_t i = 0; i < loop->timers.len; i++)
        if (loop->timers.ptr[i].live)
            loop->timers.ptr[n++] = loop->timers.ptr[i];
    loop->timers.len = n;
}

void sk_loop_once(sk_loop *loop, int timeout_ms)
{
    compact(loop);
    loop->fds.len = loop->fd_ids.len = 0;
    for (size_t i = 0; i < loop->watchers.len; i++) {
        const watcher *w = &loop->watchers.ptr[i];
        struct pollfd p = {w->fd, w->events, 0};
        sk_vec_push(&loop->fds, p);
        sk_vec_push(&loop->fd_ids, w->id);
    }
    int r = poll(loop->fds.ptr, (nfds_t)loop->fds.len, wait_ms(loop, timeout_ms));
    if (r < 0 && errno != EINTR && errno != EAGAIN)
        sk_die("poll failed: %s", strerror(errno));
    for (size_t i = 0; r > 0 && i < loop->fds.len; i++) {
        short revents = loop->fds.ptr[i].revents;
        if (!revents)
            continue;
        /* A callback may have removed this watcher, or others. */
        for (size_t k = 0; k < loop->watchers.len; k++) {
            watcher *w = &loop->watchers.ptr[k];
            if (w->id == loop->fd_ids.ptr[i] && w->live) {
                sk_io_cb cb = w->cb;
                void *ud = w->ud;
                cb(loop, w->fd, revents, ud);
                break;
            }
        }
    }
    run_timers(loop);
}

void sk_loop_run(sk_loop *loop)
{
    loop->stopped = false;
    while (!loop->stopped)
        sk_loop_once(loop, -1);
}

void sk_loop_stop(sk_loop *loop)
{
    loop->stopped = true;
}
