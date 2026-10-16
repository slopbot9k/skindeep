/* The issue board of a session's project, for the sessions dashboard
 * (decision 0029), as app/board.js reads it. The project is the git
 * remote origin of the session's folder. Its issues are read with the
 * access the user's own Claude Code session has, through the same
 * tools: gh for github.com, which keeps its login to itself, and for any
 * other host, taken as Gitea, its API through the curl command, with the
 * password git's credential helper gives for that host. The password
 * reaches curl on its stdin, never its command line, goes to that host
 * alone, and is wiped after the request. The app keeps no token or store
 * of its own. Only counts leave this module.
 *
 * Every child runs beside the core: its output is read as the loop finds
 * it ready, and nothing waits for it. The app must ignore SIGPIPE, as
 * the relays and the tests do. */
#ifndef SK_BOARD_H
#define SK_BOARD_H

#include "../core/bus.h"
#include "../core/loop.h"

#define SK_BOARD_DAYS 14
#define SK_BOARD_REFRESH_MS 300000
#define SK_BOARD_PAGE 50
#define SK_BOARD_PAGES 10

typedef enum { SK_FORGE_GITHUB, SK_FORGE_GITEA } sk_forge;

/* A remote, with any user or password of its URL dropped. key is
 * scheme://host[:port]/owner/repo. port is "" for the scheme's own. */
typedef struct {
    sk_forge forge;
    char *scheme, *host, *port, *owner, *repo, *key;
} sk_remote;

/* An https remote, an ssh, git or scp-like one (its forge on https), or
 * an http one on the loopback host alone; false for anything else. */
bool sk_board_parse_remote(const char *url, sk_remote *out);
void sk_remote_free(sk_remote *r);

/* An issue's opening and closing, ms since the epoch; closed is NAN
 * while it is open. */
typedef struct {
    double created, closed;
} sk_issue;

typedef struct {
    int open[SK_BOARD_DAYS];   /* open at each local day's end (now, for today) */
    int closed[SK_BOARD_DAYS]; /* closed in each day */
    int open_now, closed14;
} sk_board_series;

/* The last 14 local days, today last. */
void sk_board_series_of(const sk_issue *issues, size_t n, double now, sk_board_series *out);
/* A stamp in ms, NAN for none: not a string, empty, not a date, or
 * before 1970-01-02, as gh writes a zero year for open. */
double sk_board_stamp(const sk_json *v);

typedef enum {
    SK_BOARD_NONE,      /* no project: no remote, or none a board is read from */
    SK_BOARD_LOADING,
    SK_BOARD_NO_ACCESS,
    SK_BOARD_OK,
} sk_board_state;

typedef struct {
    sk_board_state state;
    sk_board_series series; /* SK_BOARD_OK only */
} sk_board;

typedef struct {
    sk_loop *loop;
    /* ms since the epoch; NULL is the real clock. */
    double (*now)(void);
    double refresh_ms; /* 0: SK_BOARD_REFRESH_MS */
    sk_log_fn log;     /* may be NULL */
    void *log_ud;
    /* The environment the children start from, PATH included; NULL is
     * the app's own. git and gh are told never to prompt. */
    char *const *env;
} sk_boards_opts;

/* The boards of a window's sessions, each remote read at most once per
 * refresh, and only while something asks. Every session of one remote
 * shares it. */
typedef struct sk_boards sk_boards;

sk_boards *sk_boards_new(const sk_boards_opts *o);
/* Kills and reaps any child still running; before the loop is freed. */
void sk_boards_free(sk_boards *b);
/* What the page draws for folder: from what is known, starting what is
 * due, the remote first, then the remote's issues. */
void sk_boards_get(sk_boards *b, const char *folder, sk_board *out);
/* True while a child runs. */
bool sk_boards_busy(const sk_boards *b);
/* For the live checks: [{ key, fetches, error }], each remote and how
 * often it was read. */
void sk_boards_report(const sk_boards *b, sk_jw *w);

#endif
