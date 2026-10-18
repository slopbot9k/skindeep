/* A followed JSONL file, a Claude transcript or a Codex rollout, as
 * app/metrics/tail.js follows it. The file as it is at start comes first,
 * every line of it flagged history, then the appended lines, read when
 * the owner polls. The history is read one 8 MiB chunk at start and one
 * per poll, so a large file never holds the core for long. A line that is
 * not JSON is skipped. The tail holds no timer. */
#ifndef SK_TAIL_H
#define SK_TAIL_H

#include "../base/json.h"

#define SK_TAIL_CHUNK (8 * 1024 * 1024)
#define SK_TAIL_POLL_MS 500
/* A line longer than this is skipped, not kept whole: a file with no
 * newline would grow the partial line by a chunk every poll. A test may
 * lower it. */
#define SK_TAIL_MAX_LINE ((size_t)64 << 20)
extern size_t sk_tail_max_line;

/* One line's value, valid during the call only. */
typedef void (*sk_tail_line_fn)(void *ud, const sk_json *obj, bool history);

typedef struct sk_tail sk_tail;

/* Reads the first chunk of history at once. end >= 0: read the file to
 * that byte as history, and nothing after it, ever (decision 0026). */
sk_tail *sk_tail_new(const char *file, sk_tail_line_fn fn, void *ud, int64_t end);
/* The next chunk of history while there is one, else the appended lines,
 * at most once per 500 ms of the caller's clock. */
void sk_tail_poll(sk_tail *t, uint64_t now);
/* True until the history is read. */
bool sk_tail_replaying(const sk_tail *t);
const char *sk_tail_file(const sk_tail *t);
void sk_tail_stop(sk_tail *t);
void sk_tail_free(sk_tail *t);

#endif
