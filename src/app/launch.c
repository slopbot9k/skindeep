#include "launch.h"

#include "../agents/claude/claude.h"
#include "../base/log.h"
#include "../base/paths.h"
#include "../core/command.h"
#include "runtime.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* SKINNED_RELAY, else the skinned-relay beside this program: the file
 * itself, past any link to it, as make install's links lead to it. */
static char *relay_path(const sk_envp *env, const char *self)
{
    const char *given = sk_envp_get(env, "SKINNED_RELAY");
    if (given && *given)
        return sk_xstrdup(given);
    char *found = strchr(self, '/') ? sk_xstrdup(self) : sk_resolve_command(self, sk_envp_get(env, "PATH"));
    char *me = found ? realpath(found, NULL) : NULL;
    free(found);
    if (!me)
        return NULL;
    char *slash = strrchr(me, '/');
    if (slash)
        *slash = 0;
    char *p = sk_path_join(me, "skinned-relay");
    free(me);
    return p;
}

char *sk_launch_named(const sk_launch *l, const char *name, const char *suffix)
{
    sk_buf base = {0};
    sk_buf_printf(&base, "%s-%ld.%s", name, (long)getpid(), suffix);
    char *p = sk_path_join(l->state, base.ptr);
    sk_buf_free(&base);
    return p;
}

/* The launch's log, <state>/<stamp>-[<name>-]<pid>.log in local time,
 * as the JavaScript app's launcher named it: the names sort by age, so
 * the rotation keeps the newest. */
static char *log_path(const sk_launch *l, const char *name)
{
    time_t now = time(NULL);
    struct tm tm;
    char stamp[32] = "00000000-000000";
    if (localtime_r(&now, &tm))
        strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tm);
    sk_buf base = {0};
    sk_buf_printf(&base, "%s-%s%s%ld.log", stamp, name ? name : "", name ? "-" : "", (long)getpid());
    char *p = sk_path_join(l->state, base.ptr);
    sk_buf_free(&base);
    return p;
}

int sk_launch_open(sk_launch *l, const sk_options *o, const sk_envp *env, const char *self, const char *name)
{
    memset(l, 0, sizeof *l);
    l->log_fd = -1;
    l->file = sk_resolve_command(o->cmd[0], sk_envp_get(env, "PATH"));
    if (!l->file) {
        sk_tell("%s: command not found", o->cmd[0]);
        return SK_EXIT_NOT_FOUND;
    }
    sk_env e;
    sk_env_current(&e);
    l->state = sk_path_state(&e);
    l->root = sk_path_runtime_base(&e);
    l->store = sk_runtime_settings_dir(&e);
    l->run_dir = sk_runtime_create(&e, sk_envp_get(env, "SKINNED_RUNTIME_DIR"));
    if (!l->state || !l->root || !l->store || !l->run_dir || sk_mkdirs(l->state, 0700) != 0) {
        sk_tell("cannot make the app's directories: %s", strerror(errno));
        sk_launch_close(l);
        return SK_EXIT_APP;
    }
    l->log_path = log_path(l, name);
    l->log_fd = open(l->log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (l->log_fd < 0) {
        sk_tell("cannot write %s: %s", l->log_path, strerror(errno));
        sk_launch_close(l);
        return SK_EXIT_APP;
    }
    sk_log_set_fd(l->log_fd);
    if (sk_runtime_rotate_logs(&e) != 0)
        sk_log("logs: %s", strerror(errno));
    l->relay = relay_path(env, self);
    int moved = sk_claude_migrate_settings(l->store, l->relay);
    if (moved)
        sk_log("settings: %d file%s of the JavaScript app now run%s skinned-relay", moved, moved == 1 ? "" : "s",
               moved == 1 ? "s" : "");
    /* As process.cwd() throws in app/main.js: a folder that is gone is
     * not the root's. */
    if (!getcwd(l->cwd, sizeof l->cwd)) {
        sk_tell("cannot open the tab: %s", strerror(errno));
        sk_launch_close(l);
        return SK_EXIT_APP;
    }
    return 0;
}

void sk_launch_close(sk_launch *l)
{
    if (l->log_fd >= 0) {
        sk_log_set_fd(2);
        close(l->log_fd);
    }
    if (l->run_dir)
        sk_runtime_remove(l->run_dir);
    free(l->file);
    free(l->state);
    free(l->root);
    free(l->store);
    free(l->run_dir);
    free(l->log_path);
    free(l->relay);
    memset(l, 0, sizeof *l);
    l->log_fd = -1;
}
