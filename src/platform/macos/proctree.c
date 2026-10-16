/* The process tree through libproc (decisions 0014, 0036), walked as
 * app/metrics/proc.js walks /proc. CPU time is the task's total user
 * and system time, in Mach absolute time units, which are not
 * nanoseconds on Apple Silicon; memory is its resident size. */
#include "../proctree.h"

#include <errno.h>
#include <libproc.h>
#include <mach/mach_time.h>
#include <signal.h>
#include <sys/proc_info.h>

/* As proc.js caps the walk. */
#define MAX_TREE 4096

typedef SK_VEC(pid_t) pid_vec;

static bool exists(pid_t pid)
{
    return pid > 0 && (kill(pid, 0) == 0 || errno == EPERM);
}

/* A full buffer may have cut the list, so it grows until one is not. */
static void push_children(pid_t pid, pid_vec *stack, pid_vec *kids)
{
    for (;;) {
        if (kids->cap < 64)
            kids->ptr = sk_vec_grow_(kids->ptr, &kids->cap, 64, sizeof *kids->ptr);
        int max = (int)SK_MIN(kids->cap, (size_t)MAX_TREE);
        int n = proc_listchildpids(pid, kids->ptr, max * (int)sizeof *kids->ptr);
        if (n <= 0)
            return;
        if (n < max || max == MAX_TREE) {
            for (int i = 0; i < n; i++)
                if (kids->ptr[i] > 0)
                    sk_vec_push(stack, kids->ptr[i]);
            return;
        }
        kids->ptr = sk_vec_grow_(kids->ptr, &kids->cap, kids->cap * 2, sizeof *kids->ptr);
    }
}

size_t sk_proc_tree(pid_t pid, pid_t *out, size_t max)
{
    if (!max || !exists(pid))
        return 0;
    pid_vec stack = {0}, kids = {0};
    sk_vec_push(&stack, pid);
    size_t n = 0;
    while (stack.len && n < max) {
        pid_t p = stack.ptr[--stack.len];
        out[n++] = p;
        push_children(p, &stack, &kids);
    }
    sk_vec_free(&stack);
    sk_vec_free(&kids);
    return n;
}

bool sk_proc_tree_usage(pid_t pid, uint64_t *cpu_ms, uint64_t *rss_bytes)
{
    *cpu_ms = *rss_bytes = 0;
    pid_t *pids = sk_xmalloc(MAX_TREE * sizeof *pids);
    size_t n = sk_proc_tree(pid, pids, MAX_TREE);
    uint64_t units = 0, rss = 0;
    for (size_t i = 0; i < n; i++) {
        /* A zombie or a process of another user has no task info. */
        struct proc_taskinfo ti;
        if (proc_pidinfo(pids[i], PROC_PIDTASKINFO, 0, &ti, (int)sizeof ti) == (int)sizeof ti) {
            units += ti.pti_total_user + ti.pti_total_system;
            rss += ti.pti_resident_size;
        }
    }
    free(pids);
    if (!n)
        return false;
    mach_timebase_info_data_t tb;
    if (mach_timebase_info(&tb) != KERN_SUCCESS || !tb.denom)
        tb.numer = tb.denom = 1;
    uint64_t ns = units / tb.denom * tb.numer + units % tb.denom * tb.numer / tb.denom;
    *cpu_ms = ns / 1000000u;
    *rss_bytes = rss;
    return true;
}
