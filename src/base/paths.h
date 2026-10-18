/* Where the app keeps its files (decisions 0016, 0036), on each system.
 * The rules are pure functions of an sk_env, so a test can check both
 * systems' rules on either. */
#ifndef SK_PATHS_H
#define SK_PATHS_H

#include "base.h"

#include <sys/types.h>

#define SK_APP_NAME "claude-skins"

typedef enum { SK_OS_LINUX, SK_OS_MACOS } sk_os;

typedef struct {
    sk_os os;
    const char *home; /* NULL or "" when unknown */
    const char *xdg_state_home, *xdg_cache_home, *xdg_runtime_dir, *tmpdir;
    unsigned long uid;
} sk_env;

/* The running system and its environment. */
sk_os sk_os_current(void);
void sk_env_current(sk_env *e);

/* Each returns a malloc'd path, or NULL when the environment gives no
 * home and the rule needs one. An empty variable counts as unset. */
/* Logs, settings, profiles, the last skins: Linux
 * ${XDG_STATE_HOME:-~/.local/state}/claude-skins; macOS the same when
 * XDG_STATE_HOME is set, else ~/Library/Application Support/claude-skins. */
char *sk_path_state(const sk_env *e);
/* Compiled skin packs: Linux ${XDG_CACHE_HOME:-~/.cache}/claude-skins;
 * macOS the same when XDG_CACHE_HOME is set, else
 * ~/Library/Caches/claude-skins. */
char *sk_path_cache(const sk_env *e);
/* The runtime root, holding one directory per running instance:
 * $XDG_RUNTIME_DIR/claude-skins, else ${TMPDIR:-/tmp}/claude-skins-<uid>. */
char *sk_path_runtime_base(const sk_env *e);

char *sk_path_join(const char *a, const char *b);
/* mkdir -p, each directory it makes with mode; 0 or -1 with errno. */
int sk_mkdirs(const char *path, mode_t mode);
/* Whether path is a directory of this user's alone: no link, owned by
 * the effective uid, and no bit for group or others. On a shared /tmp
 * anyone may make a path first, so the runtime root and what the relays
 * follow in it must be such a directory (decision 0016). When tighten,
 * one of this user's with looser bits is made 0700 first. */
bool sk_path_private_dir(const char *path, bool tighten);

#endif
