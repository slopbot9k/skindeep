#include "../../src/core/loop.h"
#include "../test.h"

#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

static void sleep_ms(long ms)
{
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000};
    while (nanosleep(&ts, &ts) != 0)
        ;
}

typedef struct {
    int count;
    char order[16];
    size_t n;
    uint64_t at;
} record;

static void on_timer(sk_loop *loop, void *ud)
{
    (void)loop;
    record *r = ud;
    r->count++;
    r->at = sk_now_ms();
}

static void test_timer_waits(void)
{
    sk_loop *loop = sk_loop_new();
    record r = {0};
    uint64_t start = sk_now_ms();
    sk_loop_timer(loop, 30, 0, on_timer, &r);
    for (int i = 0; i < 20 && !r.count; i++)
        sk_loop_once(loop, -1);
    CHECK_INT(r.count, 1);
    CHECK(r.at - start >= 30);
    sk_loop_once(loop, 20);
    CHECK_INT(r.count, 1); /* once only */
    sk_loop_free(loop);
}

typedef struct {
    record *r;
    char tag;
} tagged;

static void on_tagged(sk_loop *loop, void *ud)
{
    (void)loop;
    tagged *t = ud;
    t->r->order[t->r->n++] = t->tag;
}

static void test_due_timers_run_in_due_order(void)
{
    sk_loop *loop = sk_loop_new();
    record r = {0};
    tagged c = {&r, 'c'}, a = {&r, 'a'}, b = {&r, 'b'};
    sk_loop_timer(loop, 15, 0, on_tagged, &c);
    sk_loop_timer(loop, 5, 0, on_tagged, &a);
    sk_loop_timer(loop, 10, 0, on_tagged, &b);
    sleep_ms(30);
    sk_loop_once(loop, 0);
    CHECK_STR(r.order, "abc");
    sk_loop_free(loop);
}

static void stop_after_three(sk_loop *loop, void *ud)
{
    record *r = ud;
    if (++r->count == 3)
        sk_loop_stop(loop);
}

static void test_repeat_and_stop(void)
{
    sk_loop *loop = sk_loop_new();
    record r = {0};
    int id = sk_loop_timer(loop, 1, 5, stop_after_three, &r);
    sk_loop_run(loop);
    CHECK_INT(r.count, 3);
    sk_loop_cancel(loop, id);
    sk_loop_once(loop, 30);
    CHECK_INT(r.count, 3);
    sk_loop_free(loop);
}

typedef struct {
    int reads, id;
    char got[8];
} reader;

static void on_readable(sk_loop *loop, int fd, short revents, void *ud)
{
    reader *rd = ud;
    CHECK(revents & POLLIN);
    ssize_t n = read(fd, rd->got, sizeof rd->got - 1);
    CHECK(n > 0);
    rd->reads++;
    sk_loop_unwatch(loop, rd->id); /* from inside its own callback */
}

static void test_watch_and_unwatch(void)
{
    sk_loop *loop = sk_loop_new();
    int p[2];
    CHECK(pipe(p) == 0);
    reader rd = {0};
    rd.id = sk_loop_watch(loop, p[0], POLLIN, on_readable, &rd);
    sk_loop_once(loop, 0);
    CHECK_INT(rd.reads, 0);
    CHECK(write(p[1], "hi", 2) == 2);
    sk_loop_once(loop, 1000);
    CHECK_INT(rd.reads, 1);
    CHECK_STR(rd.got, "hi");
    CHECK(write(p[1], "again", 5) == 5);
    sk_loop_once(loop, 20);
    CHECK_INT(rd.reads, 1); /* unwatched */
    close(p[0]);
    close(p[1]);
    sk_loop_free(loop);
}

typedef struct {
    sk_loop *loop;
    pthread_t on;
    int runs;
} posted;

static void on_task(void *ud)
{
    posted *p = ud;
    p->on = pthread_self();
    p->runs++;
    sk_loop_stop(p->loop);
}

static void *poster(void *ud)
{
    posted *p = ud;
    sleep_ms(20);
    sk_loop_post(p->loop, on_task, p);
    return NULL;
}

static void test_post_from_another_thread(void)
{
    sk_loop *loop = sk_loop_new();
    posted p = {loop, 0, 0};
    pthread_t t;
    CHECK(pthread_create(&t, NULL, poster, &p) == 0);
    sk_loop_run(loop); /* returns only once the task ran */
    pthread_join(t, NULL);
    CHECK_INT(p.runs, 1);
    CHECK(pthread_equal(p.on, pthread_self()));
    sk_loop_free(loop);
}

static void on_usr1(sk_loop *loop, int signo, void *ud)
{
    (void)loop;
    int *got = ud;
    *got = signo;
}

static void test_signal_becomes_an_event(void)
{
    sk_loop *loop = sk_loop_new();
    int got = 0;
    CHECK_INT(sk_loop_signal(loop, SIGUSR1, on_usr1, &got), 0);
    raise(SIGUSR1);
    for (int i = 0; i < 10 && !got; i++)
        sk_loop_once(loop, 100);
    CHECK_INT(got, SIGUSR1);
    sk_loop_free(loop); /* puts SIGUSR1 back to its default */
    CHECK_INT(sk_loop_signal(NULL, 0, NULL, NULL), -1);
}

const sk_test sk_suite_loop[] = {
    T(test_timer_waits),
    T(test_due_timers_run_in_due_order),
    T(test_repeat_and_stop),
    T(test_watch_and_unwatch),
    T(test_post_from_another_thread),
    T(test_signal_becomes_an_event),
    {0},
};
