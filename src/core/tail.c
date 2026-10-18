/* Lines are split as bytes at '\n', which no UTF-8 character contains,
 * so a character cut by the 8 MiB read stays whole without the decoder
 * state the JavaScript tail needed. */
#include "tail.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

size_t sk_tail_max_line = SK_TAIL_MAX_LINE;

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
    bool skipping; /* within a line longer than sk_tail_max_line */
    bool known;    /* dev and ino are the file's as last read */
    dev_t dev;
    ino_t ino;
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
    /* Non-blocking: a fifo there would hold the thread until written. */
    int fd = open(t->file, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0)
        return false; /* not there yet, or rotated: the next poll tries */
    struct stat st;
    bool got = false;
    if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode)) {
        /* Another file in its place: read from its start, as appended. */
        if (t->known && (st.st_dev != t->dev || st.st_ino != t->ino)) {
            t->dev = st.st_dev;
            t->ino = st.st_ino;
            t->offset = 0;
            t->history_end = 0;
            t->skipping = false;
            sk_buf_clear(&t->rest);
            close(fd);
            return false;
        }
        t->known = true;
        t->dev = st.st_dev;
        t->ino = st.st_ino;
        uint64_t size = SK_MIN((uint64_t)st.st_size, limit);
        if (size < t->offset) {
            t->offset = 0;
            t->skipping = false;
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
                size_t from = 0;
                /* The rest of a line too long to keep, up to its end. */
                if (t->skipping) {
                    const char *nl = memchr(t->rest.ptr, '\n', t->rest.len);
                    from = nl ? (size_t)(nl - t->rest.ptr) + 1 : t->rest.len;
                    t->skipping = !nl;
                }
                /* Every whole line goes out; the partial last one waits. */
                for (size_t i = from; i < t->rest.len; i++) {
                    if (t->rest.ptr[i] != '\n')
                        continue;
                    deliver(t, t->rest.ptr + from, i - from, history);
                    from = i + 1;
                    if (t->stopped)
                        break;
                }
                sk_buf_consume(&t->rest, from);
                if (t->rest.len > sk_tail_max_line) {
                    sk_buf_clear(&t->rest);
                    t->skipping = true;
                }
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
