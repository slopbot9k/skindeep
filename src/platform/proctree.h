/* A process and every process below it: /proc/<pid>/task/<tid>/children on
 * Linux, libproc on macOS (decisions 0014, 0036). */
#ifndef SK_PROCTREE_H
#define SK_PROCTREE_H

#include "../base/base.h"

#include <sys/types.h>

/* pid and its descendants, pid first, at most max of them. Returns how
 * many; 0 when pid is gone. */
size_t sk_proc_tree(pid_t pid, pid_t *out, size_t max);
/* The CPU time, user and system, in ms, and the resident memory in bytes
 * of pid and its descendants. False when pid is gone. */
bool sk_proc_tree_usage(pid_t pid, uint64_t *cpu_ms, uint64_t *rss_bytes);

#endif
