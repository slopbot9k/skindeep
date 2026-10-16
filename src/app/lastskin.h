/* The last skin used (decision 0022), as app/skins/last.js keeps it.
 * Each window writes one file of its own, <dir>/<pid>, at launch and at
 * each switch, by write and rename, so no two windows ever write one
 * file. A file holds the time in ms and the skin's spec: a shipped
 * name, or the folder of a skin opened by path. */
#ifndef SK_APP_LASTSKIN_H
#define SK_APP_LASTSKIN_H

#include "../base/paths.h"
#include "../base/strv.h"

#include <sys/types.h>

#define SK_LAST_RACE_MS 5000.0

typedef struct {
    pid_t pid;
    double t;
    char *spec;
} sk_last_record;
typedef SK_VEC(sk_last_record) sk_last_records;

/* <state>/last-skins, malloc'd; NULL without a home. */
char *sk_last_dir(const sk_env *e);
/* spec as pid's record at now, by write and rename. 0, or -1 with
 * errno. */
int sk_last_write(const char *dir, pid_t pid, const char *spec, double now);
/* Every window's record, newest first, appended to out. A file another
 * window is renaming, or one that is not a record, is skipped. */
void sk_last_read(const char *dir, sk_last_records *out);
void sk_last_records_free(sk_last_records *r);

/* What a bare launch opens: false with no record; else the newest's
 * spec in *spec (the record's string), or, when windows recorded
 * different skins within race_ms of the newest, copies of those specs
 * pushed to choices, the newest first, each once, and *spec NULL. */
bool sk_last_pick(const sk_last_records *r, double race_ms, const char **spec, sk_strv *choices);
/* Records older than race_ms before the newest can never be picked
 * again: removed at each launch. */
void sk_last_prune(const char *dir, const sk_last_records *r, double race_ms);
/* Every record removed. 0, or -1 with errno for the first that could
 * not be. */
int sk_last_forget(const char *dir);

#endif
