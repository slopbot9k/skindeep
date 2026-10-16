/* The MilkDrop renderer's worker pool (milkint.h): the warp, the
 * composite and the stretch to the pane are each a pass over rows that
 * do not depend on one another, split into bands that the pool's
 * threads and the caller take in turn. The pool is made the first time
 * a pass runs and lives as long as the process. */
#include "milkint.h"

#include <pthread.h>
#include <stdint.h>
#include <unistd.h>

/* Threads in all, the caller among them: more than this gains little
 * on a pane's few hundred rows. */
#define MAX_THREADS 8
/* Bands per thread, so a slow band does not hold the rest up. */
#define BANDS_PER_THREAD 4

static struct {
    pthread_once_t once;
    /* One job at a time: a caller holds run for the whole of its job. */
    pthread_mutex_t run;
    pthread_mutex_t lock;
    pthread_cond_t wake, done;
    /* The threads made, the caller counted, and how many a job may use
     * (sk_milk_set_threads), 0 for all. */
    int threads, limit;
    /* The job, under lock. gen counts jobs, so a worker that wakes late
     * knows whether there is one it has not seen; workers numbered
     * active or more sit it out. */
    sk_milk_job fn;
    void *ctx;
    int n, band, next, left, active;
    unsigned gen;
} pool = {
    .once = PTHREAD_ONCE_INIT,
    .run = PTHREAD_MUTEX_INITIALIZER,
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .wake = PTHREAD_COND_INITIALIZER,
    .done = PTHREAD_COND_INITIALIZER,
};

/* Takes bands of the job under way until none is left; called and
 * returning with lock held. */
static void work(void)
{
    while (pool.next < pool.n) {
        int lo = pool.next, hi = SK_MIN(pool.n, lo + pool.band);
        pool.next = hi;
        sk_milk_job fn = pool.fn;
        void *ctx = pool.ctx;
        pthread_mutex_unlock(&pool.lock);
        fn(ctx, lo, hi);
        pthread_mutex_lock(&pool.lock);
        if (--pool.left == 0)
            pthread_cond_broadcast(&pool.done);
    }
}

/* Worker number arg, from 1: the caller is 0. */
static void *worker(void *arg)
{
    int me = (int)(intptr_t)arg;
    unsigned seen = 0;
    pthread_mutex_lock(&pool.lock);
    for (;;) {
        while (pool.gen == seen)
            pthread_cond_wait(&pool.wake, &pool.lock);
        seen = pool.gen;
        if (me < pool.active)
            work();
    }
    return NULL;
}

static void start(void)
{
    long cpus = 1;
#ifdef _SC_NPROCESSORS_ONLN
    cpus = sysconf(_SC_NPROCESSORS_ONLN);
#endif
    int want = (int)(cpus < 1 ? 1 : cpus > MAX_THREADS ? MAX_THREADS : cpus);
    /* A worker that cannot start leaves the work to the others. */
    pool.threads = 1;
    for (int i = 1; i < want; i++) {
        pthread_t t;
        pthread_attr_t attr;
        if (pthread_attr_init(&attr) != 0)
            break;
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        bool ok = pthread_create(&t, &attr, worker, (void *)(intptr_t)pool.threads) == 0;
        pthread_attr_destroy(&attr);
        if (!ok)
            break;
        pool.threads++;
    }
}

void sk_milk_set_threads(int n)
{
    pthread_mutex_lock(&pool.run);
    pool.limit = n < 0 ? 0 : n;
    pthread_mutex_unlock(&pool.run);
}

void sk_milk_parallel(sk_milk_job fn, void *ctx, int n)
{
    if (n <= 0)
        return;
    pthread_once(&pool.once, start);
    pthread_mutex_lock(&pool.run);
    int use = pool.limit ? SK_MIN(pool.limit, pool.threads) : pool.threads;
    if (use < 2 || n < 2) {
        fn(ctx, 0, n);
        pthread_mutex_unlock(&pool.run);
        return;
    }
    pthread_mutex_lock(&pool.lock);
    pool.fn = fn;
    pool.ctx = ctx;
    pool.n = n;
    pool.active = use;
    pool.band = SK_MAX(1, (n + use * BANDS_PER_THREAD - 1) / (use * BANDS_PER_THREAD));
    pool.next = 0;
    pool.left = (n + pool.band - 1) / pool.band;
    pool.gen++;
    pthread_cond_broadcast(&pool.wake);
    work();
    while (pool.left > 0)
        pthread_cond_wait(&pool.done, &pool.lock);
    pthread_mutex_unlock(&pool.lock);
    pthread_mutex_unlock(&pool.run);
}
