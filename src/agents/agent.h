/* The agent adapter seam (decision 0021), as app/metrics/agent.js has
 * it. A provider (Claude, Codex, plain) makes one part for a session;
 * the seam sets the Generic part beside it and drives both: prepare
 * once before the spawn, attach once after it, poll on one clock, a
 * report for the strip and the live checks, close at every end. */
#ifndef SK_AGENT_H
#define SK_AGENT_H

#include "../base/strv.h"
#include "../core/bus.h"
#include "../core/env.h"
#include "../core/loop.h"

#include <sys/types.h>

#define SK_AGENT_TICK_MS 250
#define SK_AGENT_FAILED "meters limited: adapter failed"

typedef enum { SK_CH_OFF, SK_CH_WAITING, SK_CH_LIVE } sk_ch_state;

/* One channel of the report: hooks, statusline, transcript, rollout,
 * pty, proc. target and how may be NULL. */
typedef struct {
    const char *name;
    sk_ch_state state;
    const char *target, *how;
} sk_channel;

#define SK_MAX_CHANNELS 6

/* What a part follows and by which channels. The strings belong to the
 * part and stay valid until its next call. replaying: the history of
 * the file followed is still being read, so its totals are not yet
 * whole. title: the session's name in the CLI's own record (decision
 * 0032). */
typedef struct {
    const char *session, *title, *notice;
    sk_channel channels[SK_MAX_CHANNELS];
    size_t n_channels;
    bool replaying;
} sk_report;

/* The statusline mode of --statusline (decision 0020). */
typedef enum { SK_STATUSLINE_ALWAYS, SK_STATUSLINE_CHAIN_ONLY } sk_statusline_mode;

/* What a part is made from. Every pointer outlives the part. */
typedef struct {
    char *const *cmd;    /* the command, NULL-terminated */
    const char *cwd;
    const sk_envp *env;  /* the tab's environment */
    const char *run_dir; /* the tab's own directory, for its hook socket */
    const char *root;    /* the runtime root the relays search */
    const char *store;   /* where generated Claude settings go */
    const char *relay;   /* the path of skinned-relay */
    sk_statusline_mode statusline;
    sk_bus *bus;
    sk_loop *loop;       /* the core loop, for sockets; may be NULL in tests */
    sk_log_fn log;
    void *log_ud;
    const char *managed_dir; /* NULL: the system's managed settings */
    /* For a session the app did not start (decision 0027), its start in
     * ms on the seam's clock, else 0. */
    uint64_t launched;
    /* The seam's clock in ms; NULL is sk_now_ms. Tests give their own.
     * It is monotonic, not the wall clock: code that compares with file
     * times, such as Codex discovery, converts at the point of use. */
    uint64_t (*now)(void);
    /* No timer even with a loop: the caller ticks, as fake-clock tests
     * that need the loop for sockets do. */
    bool manual_ticks;
} sk_agent_ctx;

/* ctx's clock. */
uint64_t sk_agent_now(const sk_agent_ctx *ctx);

/* A part's operations. All but prepare and rename are required. */
typedef struct {
    /* Before the spawn: appends the arguments to inject to args and may
     * set *notice to a malloc'd strip notice. -1 when it failed: the seam
     * closes the part and the session runs without its arguments. */
    int (*prepare)(void *self, sk_strv *args, char **notice);
    void (*attach)(void *self, pid_t pid);
    /* Each tick of the seam's clock, now in ms. */
    void (*poll)(void *self, uint64_t now);
    void (*report)(void *self, sk_report *out);
    /* A rename made in the tab, into the CLI's record; NULL when the CLI
     * keeps none. True when recorded. */
    bool (*rename)(void *self, const char *name);
    /* Idempotent, valid at any point: nothing of the part runs after. */
    void (*close)(void *self);
    void (*free)(void *self);
    /* Keyboard input to the session; NULL when the part takes none. */
    void (*input)(void *self);
} sk_part_ops;

typedef struct {
    const sk_part_ops *ops;
    void *self;
} sk_part;

/* A provider is a row of the dispatch table (decision 0021). */
typedef struct {
    const char *kind;
    /* base: the command's basename. */
    bool (*matches)(const char *base);
    sk_part (*create)(const sk_agent_ctx *ctx);
    /* The arguments a new tab carries, and those it drops (decision
     * 0023); argv without the program. */
    void (*carry)(char *const *argv, sk_strv *args, sk_strv *dropped);
} sk_provider;

extern const sk_provider sk_provider_claude;
extern const sk_provider sk_provider_codex;
extern const sk_provider sk_provider_plain;

/* The provider of cmd by its basename: claude, codex, else plain. */
const sk_provider *sk_agent_provider(char *const *cmd);
/* What a new tab carries of cmd's arguments, by its provider. */
void sk_agent_carry(char *const *cmd, sk_strv *args, sk_strv *dropped);

/* One session's adapter and Generic behind the one seam. */
typedef struct sk_agent sk_agent;

/* Makes the parts and, when ctx->loop is set, the one timer that ticks
 * them every 250 ms. Writes nothing to the bus: prepare does. */
sk_agent *sk_agent_open(const sk_agent_ctx *ctx);
/* The same with a provider of the caller's choice, for tests. */
sk_agent *sk_agent_open_with(const sk_agent_ctx *ctx, const sk_provider *p);
/* A session the app did not start (decision 0027), read only, as
 * agent.js watch(): the given watch part, such as sk_claude_watch's or a
 * Codex part made with ctx->launched, beside Generic counting from
 * ctx->launched. kind names it in reports and logs. */
sk_agent *sk_agent_watch(const sk_agent_ctx *ctx, const char *kind, sk_part adapter);
const char *sk_agent_kind(const sk_agent *a);
/* Once, before the spawn. Never fails: a part whose prepare fails is
 * closed, the notice becomes SK_AGENT_FAILED, and the session runs
 * without its arguments. *notice is malloc'd or NULL. */
void sk_agent_prepare(sk_agent *a, sk_strv *args, char **notice);
/* Once, after the spawn, with the child's pid. */
void sk_agent_attach(sk_agent *a, pid_t pid);
/* n bytes of terminal output, now on the agent's own clock. */
void sk_agent_on_output(sk_agent *a, size_t n);
void sk_agent_on_input(sk_agent *a);
/* Polls every part, then recomputes the bus's rates. The timer calls
 * it; tests call it with their own clock. */
void sk_agent_tick(sk_agent *a, uint64_t now);
/* The adapter's report with Generic's channels after its own. Valid
 * until the next call on the agent. */
void sk_agent_report(sk_agent *a, sk_report *out);
bool sk_agent_rename(sk_agent *a, const char *name);
/* Idempotent: no part is left running. */
void sk_agent_close(sk_agent *a);
void sk_agent_free(sk_agent *a);
/* The Generic part, for the session to feed and tests to drive. */
void *sk_agent_generic(sk_agent *a);

#endif
