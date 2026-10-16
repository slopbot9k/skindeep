/* A stub until the session work of phase 2 fills it in. */
#include "../proctree.h"

size_t sk_proc_tree(pid_t pid, pid_t *out, size_t max)
{
    if (!max)
        return 0;
    out[0] = pid;
    return 1;
}

bool sk_proc_tree_usage(pid_t pid, uint64_t *cpu_ms, uint64_t *rss_bytes)
{
    (void)pid;
    *cpu_ms = *rss_bytes = 0;
    return false;
}
