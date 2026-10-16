/* Recorded sessions replayed (phase 4 of decision 0036): each
 * tests/fixtures/term/NAME.rec, as skinned --record wrote it from a real
 * program on a pty, is fed to a terminal of its size, its resizes at the
 * offsets NAME.rec.resizes names, and the screen it leaves is compared
 * with tests/golden/term/rec_NAME.txt. Fed again in pieces of 7 bytes,
 * splitting sequences and characters wherever they fall, it must leave
 * the very same screen. */
#include "../../src/term/term.h"
#include "screen.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>

#define DIR_ "tests/fixtures/term"

typedef struct {
    unsigned long long at;
    int cols, rows;
} resize;

/* The recording's bytes after its header, its size, and its resizes. */
static bool load(const char *name, char **data, size_t *n, size_t *skip, int *cols, int *rows, resize **rs,
                 size_t *nrs)
{
    sk_buf path = {0};
    sk_buf_printf(&path, "%s/%s", DIR_, name);
    *data = sk_test_read(path.ptr, n);
    bool ok = *data && sscanf(*data, "skinned-record 1 %d %d", cols, rows) == 2;
    char *nl = ok ? memchr(*data, '\n', *n) : NULL;
    ok = ok && nl;
    if (ok)
        *skip = (size_t)(nl - *data) + 1;
    sk_buf_printf(&path, ".resizes");
    char *side = sk_test_read(path.ptr, NULL);
    *rs = NULL;
    *nrs = 0;
    for (char *line = side; ok && line && *line;) {
        resize r;
        if (sscanf(line, "%llu %d %d", &r.at, &r.cols, &r.rows) == 3) {
            *rs = sk_xreallocarray(*rs, *nrs + 1, sizeof **rs);
            (*rs)[(*nrs)++] = r;
        }
        char *next = strchr(line, '\n');
        line = next ? next + 1 : NULL;
    }
    free(side);
    sk_buf_free(&path);
    return ok;
}

/* The recording fed in pieces of at most piece bytes. */
static sk_term *replay(const char *data, size_t n, int cols, int rows, const resize *rs, size_t nrs, size_t piece)
{
    sk_term *t = sk_term_new(cols, rows, NULL);
    size_t at = 0, next = 0;
    while (at < n) {
        while (next < nrs && rs[next].at <= at)
            sk_term_resize(t, rs[next].cols, rs[next].rows), next++;
        size_t end = SK_MIN(n, at + piece);
        if (next < nrs && rs[next].at < end)
            end = (size_t)rs[next].at;
        sk_term_feed(t, data + at, end - at);
        at = end;
    }
    for (; next < nrs; next++)
        sk_term_resize(t, rs[next].cols, rs[next].rows);
    return t;
}

static void test_recordings_replay_to_their_screens(void)
{
    DIR *d = opendir(DIR_);
    CHECK(d != NULL);
    int seen = 0;
    for (struct dirent *e; d && (e = readdir(d));) {
        size_t len = strlen(e->d_name);
        if (len < 5 || strcmp(e->d_name + len - 4, ".rec"))
            continue;
        char *data;
        size_t n, skip = 0, nrs;
        int cols, rows;
        resize *rs;
        if (!load(e->d_name, &data, &n, &skip, &cols, &rows, &rs, &nrs)) {
            sk_test_fail(__FILE__, __LINE__, "%s: not a skinned recording", e->d_name);
            free(data);
            continue;
        }
        sk_term *whole = replay(data + skip, n - skip, cols, rows, rs, nrs, n);
        sk_term *pieces = replay(data + skip, n - skip, cols, rows, rs, nrs, 7);
        char *a = tt_dump(whole), *b = tt_dump(pieces);
        if (strcmp(a, b))
            sk_test_fail(__FILE__, __LINE__, "%s: fed in pieces, the screen differs:\n%s\n---\n%s", e->d_name, a,
                         b);
        sk_buf golden = {0};
        sk_buf_printf(&golden, "rec_%.*s", (int)(len - 4), e->d_name);
        CHECK_GOLDEN_SCREEN(golden.ptr, whole);
        /* A full-screen program puts the normal screen back as it quits:
         * its last screen is the one just before it leaves the alternate
         * buffer. */
        const char *body = data + skip, *leave = NULL;
        for (const char *p = body; (p = memchr(p, '\033', (size_t)(body + (n - skip) - p))); p++)
            if ((size_t)(body + (n - skip) - p) >= 8 && !memcmp(p, "\033[?1049l", 8))
                leave = p;
        if (leave) {
            sk_term *shown = replay(body, (size_t)(leave - body), cols, rows, rs, nrs, n);
            sk_buf_printf(&golden, "_shown");
            CHECK_GOLDEN_SCREEN(golden.ptr, shown);
            sk_term_free(shown);
        }
        sk_buf_free(&golden);
        free(a);
        free(b);
        sk_term_free(whole);
        sk_term_free(pieces);
        free(rs);
        free(data);
        seen++;
    }
    if (d)
        closedir(d);
    CHECK(seen > 0);
}

const sk_test sk_suite_term_recorded[] = {
    T(test_recordings_replay_to_their_screens),
    {0},
};
