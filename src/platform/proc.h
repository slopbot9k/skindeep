/* Facts about another process, from /proc on Linux and sysctl on macOS
 * (decision 0036). The relays use them to find their window by ancestry
 * (decision 0030). */
#ifndef SK_PROC_H
#define SK_PROC_H

#include "../base/base.h"

#include <sys/types.h>

typedef struct {
    pid_t ppid;
    /* When the process started, in a unit of the system's choosing: two
     * processes with one pid differ in it, so it tells a reused pid from
     * the process it once named. */
    uint64_t start;
} sk_proc_stat;

/* False when the process is gone or cannot be read. */
bool sk_proc_read(pid_t pid, sk_proc_stat *out);

#endif
