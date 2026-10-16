/* This launch's runtime dir and the logs of every launch (decision
 * 0016), as app/runtime.js keeps them. The runtime root is
 * sk_path_runtime_base: one dir per running launch, named for its pid,
 * and pids/, one link per tab's CLI to its hook socket (decision 0030).
 * The logs and the kept Claude settings live in sk_path_state. */
#ifndef SK_APP_RUNTIME_H
#define SK_APP_RUNTIME_H

#include "../base/paths.h"

#define SK_RUNTIME_KEEP_LOGS 10

/* Makes the root, mode 0700, removes the dirs of launches whose pid is
 * dead and sweeps pids/, then makes this launch's dir, mode 0700, and
 * returns it, malloc'd. That is given, the dir the launcher picked
 * (SKINNED_RUNTIME_DIR), when it lies directly in the root, else
 * <root>/<pid>. NULL with errno when a dir cannot be made, listed or
 * removed. */
char *sk_runtime_create(const sk_env *e, const char *given);
/* Removes each link of root/pids whose socket is gone. */
void sk_runtime_sweep_links(const char *root);
/* rm -rf: path and all under it, links not followed. 0, or -1 with
 * errno at the first failure; nothing there, or NULL, is no failure. */
int sk_runtime_remove(const char *path);

/* Where the launcher writes one log per launch, <stamp>-<pid>.log. */
char *sk_runtime_log_dir(const sk_env *e);
/* The generated Claude settings (decision 0030): they outlive the
 * window, so they live beside the logs. */
char *sk_runtime_settings_dir(const sk_env *e);
/* Keeps the newest 10 logs by name. 0, or -1 with errno at the first log
 * that cannot be removed. */
int sk_runtime_rotate_logs(const sk_env *e);
/* Removes the logs of launches that have ended (decision 0022): a live
 * launch's launcher still runs, so its log stays. The count removed, or
 * -1 with errno at the first that cannot be. */
int sk_runtime_clear_logs(const sk_env *e);

#endif
