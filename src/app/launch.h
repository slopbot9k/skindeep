/* What every way of running the app sets up before its first tab
 * (decision 0036): the command found, the state, runtime and settings
 * directories, the log, the relay beside the program, and the folder
 * the first tab starts in. skinned --headless and the window share it. */
#ifndef SK_APP_LAUNCH_H
#define SK_APP_LAUNCH_H

#include "../core/env.h"
#include "options.h"

#include <limits.h>

typedef struct {
    char *file;              /* the command, o->cmd[0], resolved */
    char *state, *root, *store, *run_dir;
    char *log_path;
    char *relay;             /* NULL when none was found */
    int log_fd;
    char cwd[PATH_MAX];
} sk_launch;

/* 0 with *l set and the log going to <state>/<stamp>-<pid>.log, as
 * decision 0016 names it, the newest 10 kept; or the exit status of
 * decision 0003 (127 for a command not found, else 125) after telling
 * the user why, *l closed. self: argv[0], to find skinned-relay beside
 * it. name: NULL for the app's window, else a mode's name, which goes
 * in its log's: <stamp>-<name>-<pid>.log. */
int sk_launch_open(sk_launch *l, const sk_options *o, const sk_envp *env, const char *self, const char *name);
/* <state>/<name>-<pid>.<suffix>, malloc'd. */
char *sk_launch_named(const sk_launch *l, const char *name, const char *suffix);
/* The log back on stderr, the runtime dir removed, all of it freed. */
void sk_launch_close(sk_launch *l);

#endif
