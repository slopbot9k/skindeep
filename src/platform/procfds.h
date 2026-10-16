/* The files a process holds open, as app/metrics/codex/discovery.js
 * reads them from /proc/<pid>/fd (decisions 0013, 0036): the links of
 * /proc/<pid>/fd on Linux, libproc on macOS. Codex discovery looks among
 * them for the rollout a session writes. */
#ifndef SK_PROCFDS_H
#define SK_PROCFDS_H

#include "../base/strv.h"

#include <sys/stat.h>
#include <sys/types.h>

/* Appends the path of each file pid holds open, in the order of its
 * descriptors' numbers compared as strings ("0", "1", "10", "2"), the
 * order Node lists /proc/<pid>/fd in. Nothing when pid is gone or cannot
 * be read. */
void sk_proc_open_files(pid_t pid, sk_strv *paths);

#endif
