/* The skins a window can show (decisions 0008, 0022, 0037), as
 * app/skins/registry.js and main.js's start keep them: the shipped
 * skins, linked in as packs (skin/shipped.h), in name order; a skin
 * opened by path, compiled by skinc as a child into the cache
 * (skin/compile.h); which is current; and the spec the last-skin
 * records keep (lastskin.h).
 *
 * Identity: a skin's id is its name. A skin opened by path takes its
 * manifest's name, and when a shipped skin has that name the path skin
 * replaces it for the rest of the run, the list showing its folder.
 * Opening the shipped folder itself restores the shipped skin.
 *
 * Threads: one at a time. Opening or selecting a skin by path may run
 * skinc, a child process that takes as long as the skin takes to
 * compile, so the core thread or a worker calls these, not the UI
 * thread; the current pack (view.h's sk_skinpack) is then posted to
 * the windows, which hold their own references. */
#ifndef SK_APP_SKINS_H
#define SK_APP_SKINS_H

#include "../base/str.h"
#include "../skin/view.h"

#include <sys/types.h>

/* decision 0008's default skin. */
#define SK_SKINS_DEFAULT "deck"

typedef struct sk_skins sk_skins;

typedef struct {
    /* skinc, as sk_skinc_beside finds it; NULL: a skin opened by path
     * cannot be compiled, and says so. */
    const char *skinc;
    /* decision 0036's cache directory; NULL for this system's. */
    const char *cache;
    /* The shipped skins' own folders (the repository's skins/), whose
     * opening restores a shipped skin; NULL when there are none. */
    const char *shipped_dir;
    /* The user's home, to show a path with ~; NULL for none. */
    const char *home;
} sk_skins_opts;

sk_skins *sk_skins_new(const sk_skins_opts *o);
void sk_skins_free(sk_skins *r);

/* registry.js's start(): spec NULL is the default, a spec with a '/'
 * is a folder (open), anything else a shipped skin's name. False with
 * the messages, one a line, appended to errors; nothing changes. */
bool sk_skins_start(sk_skins *r, const char *spec, sk_buf *errors);
/* main.js's startRemembered(): the remembered spec (lastskin.h's), or
 * when it is NULL or no longer loads the default. A spec refused gives
 * a problem for Housekeeping, *title and *detail malloc'd (NULL when
 * none). False only when the default fails too, its messages in
 * errors. */
bool sk_skins_start_remembered(sk_skins *r, const char *spec, char **title, char **detail, sk_buf *errors);

/* The skins in the menu's order: the shipped, then those opened by
 * path. location: the folder of a skin opened by path, else NULL;
 * current: it is the one shown. Valid until the next call. */
typedef struct {
    const char *id;
    const char *location;
    bool current;
} sk_skins_entry;

size_t sk_skins_count(const sk_skins *r);
sk_skins_entry sk_skins_at(const sk_skins *r, size_t i);

/* The skin with id, loaded or compiled again (the current one too:
 * decision 0008's reload). False with the messages; the current skin
 * stays. */
bool sk_skins_select(sk_skins *r, const char *id, sk_buf *errors);
/* A folder: selected when listed, else compiled and made current under
 * its name. */
bool sk_skins_open(sk_skins *r, const char *dir, sk_buf *errors);

/* The current skin's pack, NULL before a start; the registry's
 * reference, which a window refs to keep. */
sk_skinpack *sk_skins_current(const sk_skins *r);
const char *sk_skins_current_id(const sk_skins *r);
/* What start() takes to open the current skin again: its name, or the
 * folder of a skin opened by path; NULL before a start. */
const char *sk_skins_spec(const sk_skins *r);
/* The current skin recorded as pid's last (decision 0022); 0, or -1
 * with errno. */
int sk_skins_remember(const sk_skins *r, const char *last_dir, pid_t pid, double now_ms);

#endif
