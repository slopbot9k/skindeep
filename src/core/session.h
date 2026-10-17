/* One tab's CLI child on a pty, as app/session.js runs it through
 * app/exec-clean.py: the argv it execs, the spawn, input and resize, one
 * kill policy and its exit status (decisions 0003, 0016), on the core
 * loop. The identity strip, the view and the last screen are the UI's;
 * it reads what it needs through the getters here.
 *
 * A window runs one session per tab (decision 0023). The loop reaps each
 * child (sk_loop_child), so any number of sessions, and anything else
 * that runs children, share one SIGCHLD. */
#ifndef SK_SESSION_H
#define SK_SESSION_H

#include "../agents/agent.h"
#include "env.h"
#include "loop.h"

#include <sys/types.h>

/* A close of the window, or a signal to the app, before the CLI started:
 * the status a hung-up child usually reports. */
#define SK_EXIT_BEFORE_SPAWN 129
/* From SIGHUP to SIGKILL. */
#define SK_SESSION_GRACE_MS 2000

typedef enum {
    SK_SESSION_IDLE,
    SK_SESSION_PREPARING,
    SK_SESSION_RUNNING,
    SK_SESSION_ENDING, /* hung up, the child not yet reaped */
    SK_SESSION_ENDED,
} sk_session_state;

typedef struct sk_session sk_session;

/* What a session is made from. The strings and lists are copied. Every
 * callback may be NULL and gets ud. A callback may call any function
 * here but sk_session_free, which only on_end may call. */
typedef struct {
    sk_loop *loop;
    /* The file to exec, as sk_resolve_command found it. */
    const char *file;
    /* The command as the user gave it, NULL-terminated. */
    char *const *argv;
    /* NULL: the app's own. */
    const char *cwd;
    /* The tab's environment; NULL is an empty one. */
    const sk_envp *env;
    const char *label;
    /* The tab's adapters, or NULL. The session prepares, attaches, feeds
     * and closes it; the caller frees it after the end. */
    sk_agent *agent;
    /* The agent's clock in ms, for its output; NULL is sk_now_ms. */
    uint64_t (*now)(void);
    /* 0 is SK_SESSION_GRACE_MS. */
    uint64_t grace_ms;
    /* The child exists, its pid known; the agent is attached. */
    void (*on_spawn)(void *ud, pid_t pid);
    /* Bytes from the pty, as they came. */
    void (*on_output)(void *ud, const char *bytes, size_t n);
    /* The end, from the loop after the call that ended the session has
     * returned. The session is not touched after it. */
    void (*on_end)(void *ud, int status);
    /* The app's log. */
    sk_log_fn log;
    /* The user's terminal (decision 0003): "cannot start CMD: REASON". */
    sk_log_fn tell;
    void *ud;
} sk_session_opts;

sk_session *sk_session_new(const sk_session_opts *o);
/* Prepares the agent, then spawns the child on a cols by rows pty. Once,
 * from idle; never fails: a spawn that fails tells why and ends the
 * session with SK_EXIT_APP. A hang-up during the prepare, from one of
 * its callbacks, ends it without a spawn (decision 0016). */
void sk_session_start(sk_session *s, int cols, int rows);
/* Queued and written without blocking, while the child lives. */
void sk_session_input(sk_session *s, const void *bytes, size_t n);
/* 0, or -1 when there is no child or the size is out of range. */
int sk_session_resize(sk_session *s, int cols, int rows);
/* As closing a terminal: SIGHUP to the child's process group, SIGKILL
 * after the grace, the end when the child is reaped. Before the spawn it
 * ends the session at once with SK_EXIT_BEFORE_SPAWN; once the child is
 * reaped, with the child's status. A second call changes nothing. */
void sk_session_hangup(sk_session *s);

sk_session_state sk_session_get_state(const sk_session *s);
/* The child's pid, kept after the end; 0 when none was spawned. */
pid_t sk_session_pid(const sk_session *s);
/* The exit status: the child's code, 128+n for death by signal n; -1
 * until the end. */
int sk_session_status(const sk_session *s);
/* The adapters' notice from the prepare, or NULL. */
const char *sk_session_notice(const sk_session *s);
/* Why the child could not start, as the user is told after "cannot
 * start CMD: ", or NULL. */
const char *sk_session_failure(const sk_session *s);
/* The argv exec'd, the adapters' arguments after argv[0]; NULL before
 * the spawn. */
char *const *sk_session_argv(const sk_session *s);
const char *sk_session_label(const sk_session *s);
/* Whether the end came from sk_session_hangup, not from the CLI
 * exiting by itself. */
bool sk_session_hung_up(const sk_session *s);
/* A child still running is killed and reaped first: the app leaves no
 * child behind (decision 0016). The agent is closed, not freed. */
void sk_session_free(sk_session *s);

/* As an internal error ends the app (decisions 0003, 0016): every
 * child alive hung up, its process group, waited for grace_ms at most,
 * the rest killed. From any thread, with no lock or allocation. */
void sk_session_hangup_every(uint64_t grace_ms);
/* A wait status as the session reports it: the code, or 128+n. */
int sk_session_exit_status(int wait_status);
/* The child's environment: env with TERM and COLORTERM set, without
 * the variables of the app and of Chromium. out is overwritten. */
void sk_session_child_env(const sk_envp *env, sk_envp *out);

#endif
