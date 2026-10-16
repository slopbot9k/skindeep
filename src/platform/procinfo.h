/* What the sessions scan reads of other processes (decision 0027), as
 * app/sessions.js, app/identity.js and app/main.js read /proc: every
 * process with its owner, a process's parent and start, the name it was
 * run by, its argv and its folder. /proc on Linux; libproc and sysctl on
 * macOS (decision 0036). A process's environment is never read. */
#ifndef SK_PROCINFO_H
#define SK_PROCINFO_H

#include "../base/strv.h"

#include <sys/types.h>

typedef struct {
    pid_t pid;
    uid_t uid; /* the effective uid, the owner of /proc/<pid> on Linux */
} sk_procinfo_owner;

typedef SK_VEC(sk_procinfo_owner) sk_procinfo_owners;

typedef struct {
    pid_t ppid;
    uid_t uid;
    /* As sk_proc_stat's start: clock ticks since boot on Linux, the
     * field Claude Code records; microseconds since the epoch on macOS. */
    uint64_t start;
    /* When it started, in ms since the Unix epoch. */
    uint64_t started_ms;
} sk_procinfo;

/* Every process on the machine, appended to out. False when the table
 * cannot be read. */
bool sk_procinfo_list(sk_procinfo_owners *out);
/* False when pid is gone or cannot be read. */
bool sk_procinfo_read(pid_t pid, sk_procinfo *out);
/* The name pid was run by, the last part of the path exec was given,
 * which is how the kernel names it on Linux (comm, at most 15 bytes).
 * Not the file a link leads to: Claude Code's installer links claude to
 * a file named for its version. Appends to out; false when unreadable. */
bool sk_procinfo_name(pid_t pid, sk_buf *out);
/* pid's argv, appended to out; false when unreadable. */
bool sk_procinfo_argv(pid_t pid, sk_strv *out);
/* pid's working directory, malloc'd, or NULL. */
char *sk_procinfo_cwd(pid_t pid);

#endif
