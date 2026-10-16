/* Lines are split as bytes at '\n', which no UTF-8 character contains,
 * so a character cut by the 8 MiB read stays whole without the decoder
 * state the JavaScript tail needed. */
#include "tail.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct sk_tail {
    char *file;
    sk_tail_line_fn fn;
    void *ud;
    int64_t end;         /* -1: none */
    uint64_t offset;
    uint64_t history_end; /* the bytes before it are history */
    bool polled;
    uint64_t last;
    bool stopped;
    sk_buf rest; /* a partial last line */
    sk_arena arena;
};

bool sk_tail_replaying(const sk_tail *t)
{
    return !t->stopped && t->offset < t->history_end;
}

const char *sk_tail_file(const sk_tail *t)
{
    return t->file;
}

static void deliver(sk_tail *t, const char *line, size_t n, bool history)
{
    if (!n)
        return;
    sk_json *v = sk_json_parse(&t->arena, line, n, NULL);
    if (v)
        t->fn(t->ud, v, history);
    sk_arena_reset(&t->arena);
}

/* One chunk of at most 8 MiB, up to byte `limit`. False when nothing was
 * read. */
static bool read_chunk(sk_tail *t, bool history, uint64_t limit)
{
    int fd = open(t->file, O_RDONLY);
    if (fd < 0)
        return false; /* not there yet, or rotated: the next poll tries */
    struct stat st;
    bool got = false;
    if (fstat(fd, &st) == 0) {
        uint64_t size = SK_MIN((uint64_t)st.st_size, limit);
        if (size < t->offset) {
            t->offset = 0;
            sk_buf_clear(&t->rest);
        }
        if (size > t->offset) {
            size_t want = (size_t)SK_MIN(size - t->offset, (uint64_t)SK_TAIL_CHUNK);
            size_t start = t->rest.len;
            sk_buf_reserve(&t->rest, want);
            ssize_t n = pread(fd, t->rest.ptr + start, want, (off_t)t->offset);
            if (n > 0) {
                got = true;
                t->offset += (uint64_t)n;
                t->rest.len = start + (size_t)n;
                t->rest.ptr[t->rest.len] = 0;
                /* Every whole line goes out; the partial last one waits. */
                size_t from = 0;
                for (size_t i = 0; i < t->rest.len; i++) {
                    if (t->rest.ptr[i] != '\n')
                        continue;
                    deliver(t, t->rest.ptr + from, i - from, history);
                    from = i + 1;
                    if (t->stopped)
                        break;
                }
                sk_buf_consume(&t->rest, from);
            }
        }
    }
    close(fd);
    return got;
}

/* One chunk of the history. A read that gets nothing ends it: the file
 * shrank or went away, and what comes next is read as appended. */
static void replay(sk_tail *t)
{
    if (sk_tail_replaying(t) && !read_chunk(t, true, t->history_end))
        t->history_end = 0;
}

sk_tail *sk_tail_new(const char *file, sk_tail_line_fn fn, void *ud, int64_t end)
{
    sk_tail *t = sk_xcalloc(1, sizeof *t);
    t->file = sk_xstrdup(file);
    t->fn = fn;
    t->ud = ud;
    t->end = end;
    sk_arena_init(&t->arena, 0);
    struct stat st;
    if (stat(file, &st) == 0)
        t->history_end = (uint64_t)st.st_size;
    if (end >= 0)
        t->history_end = SK_MIN(t->history_end, (uint64_t)end);
    replay(t);
    return t;
}

void sk_tail_poll(sk_tail *t, uint64_t now)
{
    if (sk_tail_replaying(t)) {
        replay(t);
        return;
    }
    if (t->stopped || t->end >= 0 || (t->polled && now - t->last < SK_TAIL_POLL_MS))
        return;
    t->polled = true;
    t->last = now;
    read_chunk(t, false, UINT64_MAX);
}

void sk_tail_stop(sk_tail *t)
{
    t->stopped = true;
}

void sk_tail_free(sk_tail *t)
{
    if (!t)
        return;
    free(t->file);
    sk_buf_free(&t->rest);
    sk_arena_free(&t->arena);
    free(t);
}
