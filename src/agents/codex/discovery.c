/* Codex rollout discovery (decision 0013), as
 * app/metrics/codex/discovery.js. The one reader of $CODEX_HOME/sessions:
 * which rollout is this session's, which rollouts its history continues
 * (decision 0026), and which are its subagents'. Routes, first hit wins:
 * a thread id in argv, a rollout the child tree holds open, then a
 * cwd-matched top-level rollout: without `resume` in argv the first one
 * created after launch, with it the one created before launch and written
 * after. `fork <id>` makes a new thread, so its id names the source,
 * never ours, and a fork takes the first route of a new session.
 * Directories are listed in strcmp order, as Node lists them. */
#include "codex.h"

#include "../../base/map.h"
#include "../../base/paths.h"
#include "../../platform/procfds.h"
#include "../../platform/file.h"
#include "../../platform/proctree.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define GRACE_MS 2000
#define ACTIVE_MS 20000
#define CHUNK (64 * 1024)
#define MAX_TREE 4096
#define MAX_BASES 16

/* What discovery reads of a rollout's first line. */
typedef struct {
    bool top;                  /* a top-level thread, not a subagent's */
    char *id, *cwd, *parent;   /* strings of the payload, else NULL */
    double created;            /* the thread's creation, NAN when unknown */
    bool has_base;             /* history_base with a byte offset */
    char *base_thread;
    int64_t base_end;
} meta;

struct sk_codex_disc {
    char *given; /* $CODEX_HOME/sessions as given */
    char *home;  /* its realpath, or its absolute path until it exists */
    bool resolved;
    sk_strv argv;
    char *cwd;
    double launched;
    sk_map lines;              /* file -> meta, once its first line is whole */
    sk_map kids;               /* file -> &OURS or &NOT_OURS */
    SK_VEC(meta *) scratch;    /* metas of lines not yet whole */
    bool have_found;
    sk_codex_found found, miss;
    char *found_file, *found_thread;
    sk_codex_tree_fn tree;
    sk_codex_files_fn files;
};

/* The values of kids: tags, compared by address. */
static char OURS, NOT_OURS;

double sk_codex_wall_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec * 1000 + (double)(ts.tv_nsec / 1000000);
}

/* Date.parse */

static bool digits(sk_str s, size_t *i, size_t n, int64_t *out)
{
    if (*i + n > s.len)
        return false;
    int64_t v = 0;
    for (size_t k = 0; k < n; k++) {
        char c = s.ptr[*i + k];
        if (c < '0' || c > '9')
            return false;
        v = v * 10 + (c - '0');
    }
    *i += n;
    *out = v;
    return true;
}

static bool next_is(sk_str s, size_t i, char c)
{
    return i < s.len && s.ptr[i] == c;
}

static int64_t days_from_civil(int64_t y, int64_t m, int64_t d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

double sk_codex_date_parse(sk_str s)
{
    size_t i = 0;
    int64_t year = 0, month = 1, day = 1, hour = 0, minute = 0, second = 0, ms = 0, oh = 0, om = 0;
    int sign = 0;
    if (next_is(s, 0, '+') || next_is(s, 0, '-')) {
        int ys = s.ptr[0] == '-' ? -1 : 1;
        i = 1;
        if (!digits(s, &i, 6, &year) || (ys < 0 && year == 0))
            return NAN;
        year *= ys;
    } else if (!digits(s, &i, 4, &year)) {
        return NAN;
    }
    if (next_is(s, i, '-')) {
        i++;
        if (!digits(s, &i, 2, &month))
            return NAN;
        if (next_is(s, i, '-')) {
            i++;
            if (!digits(s, &i, 2, &day))
                return NAN;
        }
    }
    bool timed = next_is(s, i, 'T'), offset = false;
    if (timed) {
        i++;
        if (!digits(s, &i, 2, &hour) || !next_is(s, i, ':'))
            return NAN;
        i++;
        if (!digits(s, &i, 2, &minute))
            return NAN;
        if (next_is(s, i, ':')) {
            i++;
            if (!digits(s, &i, 2, &second))
                return NAN;
            if (next_is(s, i, '.')) {
                /* Any number of digits, as V8 reads them: the first three count. */
                i++;
                size_t start = i;
                while (i < s.len && s.ptr[i] >= '0' && s.ptr[i] <= '9') {
                    if (i - start < 3)
                        ms = ms * 10 + (s.ptr[i] - '0');
                    i++;
                }
                if (i == start)
                    return NAN;
                for (size_t k = i - start; k < 3; k++)
                    ms *= 10;
            }
        }
        if (next_is(s, i, 'Z')) {
            i++;
            offset = true;
        } else if (next_is(s, i, '+') || next_is(s, i, '-')) {
            sign = s.ptr[i] == '-' ? -1 : 1;
            i++;
            if (!digits(s, &i, 2, &oh) || !next_is(s, i, ':'))
                return NAN;
            i++;
            if (!digits(s, &i, 2, &om) || oh > 23 || om > 59)
                return NAN;
            offset = true;
        }
    }
    /* V8 takes any day to 31, so 30 February rolls over into March. */
    if (i != s.len || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour > 24 || minute > 59 || second > 59 ||
        (hour == 24 && (minute || second || ms)))
        return NAN;
    double t;
    if (timed && !offset) {
        struct tm tm;
        memset(&tm, 0, sizeof tm);
        tm.tm_year = (int)(year - 1900);
        tm.tm_mon = (int)(month - 1);
        tm.tm_mday = (int)day;
        tm.tm_hour = (int)hour;
        tm.tm_min = (int)minute;
        tm.tm_sec = (int)second;
        tm.tm_isdst = -1;
        t = (double)mktime(&tm) * 1000 + (double)ms;
    } else {
        t = (double)days_from_civil(year, month, day) * 86400000.0 +
            (double)(hour * 3600000 + minute * 60000 + second * 1000 + ms) -
            (double)(sign * (oh * 3600000 + om * 60000));
    }
    return fabs(t) <= 8.64e15 ? t : NAN;
}

/* Paths */

/* path.resolve(base, p): p against base, base against the process's cwd,
 * with no ".", "..", doubled or trailing slash. */
static char *resolve_path(const char *base, const char *p)
{
    sk_buf joined = {0};
    if (!(p && p[0] == '/')) {
        if (!(base && base[0] == '/')) {
            char cwd[4096];
            sk_buf_append_c(&joined, getcwd(cwd, sizeof cwd) ? cwd : "/");
        }
        if (base && *base) {
            sk_buf_putc(&joined, '/');
            sk_buf_append_c(&joined, base);
        }
    }
    if (p && *p) {
        sk_buf_putc(&joined, '/');
        sk_buf_append_c(&joined, p);
    }
    sk_buf out = {0};
    const char *s = sk_buf_cstr(&joined);
    while (*s) {
        while (*s == '/')
            s++;
        const char *e = strchr(s, '/');
        size_t n = e ? (size_t)(e - s) : strlen(s);
        if (n == 2 && s[0] == '.' && s[1] == '.') {
            while (out.len && out.ptr[out.len - 1] != '/')
                out.len--;
            if (out.len)
                out.len--;
            if (out.ptr)
                out.ptr[out.len] = 0;
        } else if (n && !(n == 1 && s[0] == '.')) {
            sk_buf_putc(&out, '/');
            sk_buf_append(&out, s, n);
        }
        s += n;
    }
    if (!out.len)
        sk_buf_putc(&out, '/');
    sk_buf_free(&joined);
    return out.ptr;
}

static bool starts_with(const char *s, const char *prefix)
{
    return !strncmp(s, prefix, strlen(prefix));
}

static bool ends_with(const char *s, const char *suffix)
{
    size_t n = strlen(s), m = strlen(suffix);
    return n >= m && !memcmp(s + n - m, suffix, m);
}

/* A JavaScript regex's `.`: anything but a line terminator. */
static bool line_break_at(const char *s, size_t i, size_t n)
{
    unsigned char c = (unsigned char)s[i];
    if (c == '\n' || c == '\r')
        return true;
    return c == 0xE2 && i + 2 < n && (unsigned char)s[i + 1] == 0x80 &&
           ((unsigned char)s[i + 2] == 0xA8 || (unsigned char)s[i + 2] == 0xA9);
}

/* /rollout-.*\.jsonl$/ */
static bool rollout_path(const char *s)
{
    size_t n = strlen(s);
    if (n < 14 || !ends_with(s, ".jsonl"))
        return false;
    /* The last "rollout-" that ends before ".jsonl" leaves the least between. */
    for (size_t i = n - 14 + 1; i-- > 0;) {
        if (memcmp(s + i, "rollout-", 8))
            continue;
        for (size_t k = i + 8; k < n - 6; k++)
            if (line_break_at(s, k, n))
                return false;
        return true;
    }
    return false;
}

static bool is_uuid(const char *s)
{
    if (!s || strlen(s) != 36)
        return false;
    for (size_t i = 0; i < 36; i++) {
        char c = s[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-')
                return false;
        } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return true;
}

static bool n_digits(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (s[i] < '0' || s[i] > '9')
            return false;
    return s[n] == 0;
}

static bool year_name(const char *s)
{
    return n_digits(s, 4);
}

static bool two_digits(const char *s)
{
    return n_digits(s, 2);
}

static bool rollout_name(const char *s)
{
    return starts_with(s, "rollout-") && ends_with(s, ".jsonl");
}

static int by_name(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* The names in dir that want takes, sorted; none when dir cannot be read. */
static void names(const char *dir, bool (*want)(const char *), sk_strv *out)
{
    DIR *d = opendir(dir);
    if (!d)
        return;
    struct dirent *e;
    while ((e = readdir(d)))
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..") && want(e->d_name))
            sk_strv_push(out, e->d_name);
    closedir(d);
    if (out->len)
        qsort(out->ptr, out->len, sizeof *out->ptr, by_name);
}

typedef struct {
    char *dir;
    double mtime;
} date_dir;

typedef SK_VEC(date_dir) date_dirs;

/* Every YYYY/MM/DD date dir under the sessions dir, with its mtime.
 * Creating a rollout bumps its date dir's mtime, appending to one does not. */
static void list_date_dirs(const char *root, date_dirs *out)
{
    if (!root)
        return;
    sk_strv ys = {0};
    names(root, year_name, &ys);
    for (size_t y = 0; y < ys.len; y++) {
        char *ydir = sk_path_join(root, ys.ptr[y]);
        sk_strv ms = {0};
        names(ydir, two_digits, &ms);
        for (size_t m = 0; m < ms.len; m++) {
            char *mdir = sk_path_join(ydir, ms.ptr[m]);
            sk_strv ds = {0};
            names(mdir, two_digits, &ds);
            for (size_t k = 0; k < ds.len; k++) {
                char *dir = sk_path_join(mdir, ds.ptr[k]);
                struct stat st;
                if (stat(dir, &st) == 0) {
                    date_dir dd = {dir, sk_stat_mtime_ms(&st)};
                    sk_vec_push(out, dd);
                } else {
                    free(dir);
                }
            }
            sk_strv_free(&ds);
            free(mdir);
        }
        sk_strv_free(&ms);
        free(ydir);
    }
    sk_strv_free(&ys);
}

static void free_date_dirs(date_dirs *dirs)
{
    for (size_t i = 0; i < dirs->len; i++)
        free(dirs->ptr[i].dir);
    sk_vec_free(dirs);
}

/* The rollouts of one date dir, as paths. */
static void rollouts(const char *dir, sk_strv *out)
{
    sk_strv ns = {0};
    names(dir, rollout_name, &ns);
    for (size_t i = 0; i < ns.len; i++) {
        char *p = sk_path_join(dir, ns.ptr[i]);
        sk_strv_push(out, p);
        free(p);
    }
    sk_strv_free(&ns);
}

/* First lines */

static char *string_or_null(const sk_json *v)
{
    sk_str s;
    return sk_json_as_string(v, &s) ? sk_xstrndup(s.ptr, s.len) : NULL;
}

static double stamp_of(const sk_json *v)
{
    sk_str s;
    return sk_json_as_string(v, &s) ? sk_codex_date_parse(s) : NAN;
}

static void meta_free(meta *m)
{
    if (!m)
        return;
    free(m->id);
    free(m->cwd);
    free(m->parent);
    free(m->base_thread);
    free(m);
}

/* NULL when the line is not JSON, or is null. */
static meta *meta_of(const sk_json *v)
{
    if (!v || v->type == SK_JSON_NULL)
        return NULL;
    meta *m = sk_xcalloc(1, sizeof *m);
    const sk_json *p = sk_json_get(v, "payload");
    /* The source string varies (cli, vscode when the TUI runs through the
     * shared app-server daemon); a subagent's is an object. */
    const sk_json *s = sk_json_get(p, "source");
    bool object = s && (s->type == SK_JSON_OBJECT || s->type == SK_JSON_ARRAY);
    const sk_json *sub = object ? sk_json_get(s, "subagent") : NULL;
    m->top = sk_codex_truthy(s) && !sk_codex_truthy(sub);
    const sk_json *spawn = sk_codex_truthy(sub) ? sk_json_get(sub, "thread_spawn") : NULL;
    const sk_json *parent = sk_codex_truthy(spawn) ? sk_json_get(spawn, "parent_thread_id") : NULL;
    if (sk_codex_truthy(parent))
        m->parent = string_or_null(parent);
    m->id = string_or_null(sk_json_get(p, "id"));
    m->cwd = string_or_null(sk_json_get(p, "cwd"));
    /* The payload's stamp also names the file. The line's own is when it
     * was written, which Codex 0.162 does at the first prompt. */
    m->created = stamp_of(sk_json_get(p, "timestamp"));
    if (isnan(m->created))
        m->created = stamp_of(sk_json_get(v, "timestamp"));
    const sk_json *b = sk_json_get(p, "history_base");
    double end;
    if (sk_json_as_number(sk_json_get(b, "end_byte_offset"), &end) && end >= 0 && end == floor(end)) {
        m->has_base = true;
        m->base_end = end >= 9.2e18 ? INT64_MAX : (int64_t)end;
        m->base_thread = string_or_null(sk_json_get(b, "thread_id"));
    }
    return m;
}

/* The first line, read to its newline or to the end of the file.
 * *complete once the newline is written. */
static meta *first_line(const char *file, bool *complete)
{
    *complete = false;
    int fd = open(file, O_RDONLY);
    if (fd < 0)
        return NULL;
    sk_buf line = {0};
    char *chunk = sk_xmalloc(CHUNK);
    off_t pos = 0;
    bool failed = false;
    for (;;) {
        ssize_t n = pread(fd, chunk, CHUNK, pos);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            failed = n < 0;
            break;
        }
        const char *nl = memchr(chunk, '\n', (size_t)n);
        sk_buf_append(&line, chunk, nl ? (size_t)(nl - chunk) : (size_t)n);
        if (nl) {
            *complete = true;
            break;
        }
        pos += n;
    }
    free(chunk);
    close(fd);
    meta *m = NULL;
    if (failed) {
        *complete = false;
    } else {
        sk_arena a;
        sk_arena_init(&a, 0);
        m = meta_of(sk_json_parse(&a, sk_buf_cstr(&line), line.len, NULL));
        sk_arena_free(&a);
    }
    sk_buf_free(&line);
    return m;
}

/* The first line of file, read once for the discovery's life once its
 * newline is written. */
static meta *meta_get(sk_codex_disc *d, const char *file)
{
    sk_str key = sk_str_c(file);
    if (sk_map_has(&d->lines, key))
        return sk_map_get(&d->lines, key);
    bool complete;
    meta *m = first_line(file, &complete);
    if (complete)
        *sk_map_put(&d->lines, key) = m;
    else if (m)
        sk_vec_push(&d->scratch, m);
    return m;
}

/* Ends a public call: metas of lines not yet whole are read again next time. */
static void drop_scratch(sk_codex_disc *d)
{
    for (size_t i = 0; i < d->scratch.len; i++)
        meta_free(d->scratch.ptr[i]);
    d->scratch.len = 0;
}

/* Discovery */

/* The session's working root: `-C <dir>` or `--cd <dir>` resolved against
 * the app's cwd, else the app's cwd. The last one given wins. */
static char *session_cwd(char *const *argv, const char *cwd)
{
    const char *dir = NULL;
    for (size_t i = 0; argv && argv[i]; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--"))
            break;
        if ((!strcmp(a, "-C") || !strcmp(a, "--cd")) && argv[i + 1])
            dir = argv[++i];
        else if (starts_with(a, "--cd="))
            dir = a + 5;
        else if (a[0] == '-' && a[1] == 'C' && a[2] && !line_break_at(a, 2, strlen(a)))
            dir = a + 2;
    }
    return dir ? resolve_path(cwd, dir) : sk_xstrdup(cwd);
}

sk_codex_disc *sk_codex_disc_new(const char *home, char *const *argv, const char *cwd,
                                 double launched)
{
    sk_codex_disc *d = sk_xcalloc(1, sizeof *d);
    if (home)
        d->given = sk_path_join(home, "sessions");
    sk_strv_extend(&d->argv, argv);
    char here[4096];
    if (!cwd)
        cwd = getcwd(here, sizeof here) ? here : "/";
    d->cwd = session_cwd(argv, cwd);
    d->launched = launched;
    sk_map_init(&d->lines);
    sk_map_init(&d->kids);
    d->tree = sk_proc_tree;
    d->files = sk_proc_open_files;
    return d;
}

void sk_codex_disc_free(sk_codex_disc *d)
{
    if (!d)
        return;
    size_t i = 0;
    sk_str k;
    void *v;
    while (sk_map_next(&d->lines, &i, &k, &v))
        meta_free(v);
    sk_map_free(&d->lines);
    sk_map_free(&d->kids);
    drop_scratch(d);
    sk_vec_free(&d->scratch);
    sk_strv_free(&d->argv);
    free(d->given);
    free(d->home);
    free(d->cwd);
    free(d->found_file);
    free(d->found_thread);
    free(d);
}

void sk_codex_disc_set_launched(sk_codex_disc *d, double launched)
{
    d->launched = launched;
}

void sk_codex_disc_set_procs(sk_codex_disc *d, sk_codex_tree_fn tree, sk_codex_files_fn files)
{
    d->tree = tree;
    d->files = files;
}

/* The sessions dir, resolved once with realpath, since the fd links name
 * real paths. A dir that does not exist yet is taken as given and
 * resolved again on the next call. */
static void resolve(sk_codex_disc *d)
{
    if (d->resolved || !d->given)
        return;
    char *real = realpath(d->given, NULL);
    free(d->home);
    if (real) {
        d->home = sk_xstrdup(real);
        free(real);
        d->resolved = true;
    } else {
        d->home = resolve_path(NULL, d->given);
    }
}

static bool argv_has(const sk_codex_disc *d, const char *word)
{
    for (size_t i = 0; i < d->argv.len; i++)
        if (!strcmp(d->argv.ptr[i], word))
            return true;
    return false;
}

/* A thread id named in argv, as in `codex resume <id>`. */
static const char *argv_thread(const sk_codex_disc *d)
{
    size_t i = 0;
    while (i < d->argv.len && strcmp(d->argv.ptr[i], "resume"))
        i++;
    for (i++; i < d->argv.len; i++)
        if (is_uuid(d->argv.ptr[i]))
            return d->argv.ptr[i];
    return NULL;
}

/* A top-level rollout of the session's cwd. */
static bool mine(const sk_codex_disc *d, const meta *m)
{
    return m && m->top && m->cwd && !strcmp(m->cwd, d->cwd);
}

static const sk_codex_found *hit(sk_codex_disc *d, const char *file, const char *thread,
                                 const char *how, bool exact)
{
    d->found_file = sk_xstrdup(file);
    d->found_thread = thread ? sk_xstrdup(thread) : NULL;
    d->found = (sk_codex_found){d->found_file, d->found_thread, how, exact, {NULL}, 0};
    d->have_found = true;
    return &d->found;
}

char *sk_codex_disc_by_id(sk_codex_disc *d, const char *id)
{
    resolve(d);
    if (!is_uuid(id))
        return NULL;
    sk_buf suffix = {0};
    sk_buf_printf(&suffix, "-%s.jsonl", id);
    date_dirs dirs = {0};
    list_date_dirs(d->home, &dirs);
    char *file = NULL;
    for (size_t i = 0; i < dirs.len && !file; i++) {
        sk_strv rs = {0};
        rollouts(dirs.ptr[i].dir, &rs);
        for (size_t k = 0; k < rs.len && !file; k++)
            if (ends_with(rs.ptr[k], suffix.ptr))
                file = sk_xstrdup(rs.ptr[k]);
        sk_strv_free(&rs);
    }
    free_date_dirs(&dirs);
    sk_buf_free(&suffix);
    return file;
}

/* A top-level rollout under the sessions dir that the child tree holds
 * open: its path, malloc'd, and its first line. */
static char *held(sk_codex_disc *d, pid_t pid, const meta **found)
{
    if (!d->home)
        return NULL;
    sk_buf prefix = {0};
    sk_buf_printf(&prefix, "%s/", d->home);
    pid_t *pids = sk_xcalloc(MAX_TREE, sizeof *pids);
    size_t n = d->tree(pid, pids, MAX_TREE);
    char *file = NULL;
    for (size_t i = 0; i < n && !file; i++) {
        sk_strv paths = {0};
        d->files(pids[i], &paths);
        for (size_t k = 0; k < paths.len && !file; k++) {
            const char *target = paths.ptr[k];
            if (!starts_with(target, prefix.ptr) || !rollout_path(target))
                continue;
            const meta *m = meta_get(d, target);
            if (m && m->top) {
                file = sk_xstrdup(target);
                *found = m;
            }
        }
        sk_strv_free(&paths);
    }
    free(pids);
    sk_buf_free(&prefix);
    return file;
}

/* A fresh session's rollout: the one of our cwd whose session_meta is
 * stamped at or after the launch cut, the earliest such. Only date dirs
 * whose mtime is at or after the cut can hold it. */
static char *fresh(sk_codex_disc *d, const meta **found)
{
    double cut = d->launched - GRACE_MS, best = 0;
    char *file = NULL;
    date_dirs dirs = {0};
    list_date_dirs(d->home, &dirs);
    for (size_t i = 0; i < dirs.len; i++) {
        if (dirs.ptr[i].mtime < cut)
            continue;
        sk_strv rs = {0};
        rollouts(dirs.ptr[i].dir, &rs);
        for (size_t k = 0; k < rs.len; k++) {
            const meta *m = meta_get(d, rs.ptr[k]);
            if (!mine(d, m) || !(m->created >= cut))
                continue;
            if (!file || m->created < best) {
                free(file);
                file = sk_xstrdup(rs.ptr[k]);
                *found = m;
                best = m->created;
            }
        }
        sk_strv_free(&rs);
    }
    free_date_dirs(&dirs);
    return file;
}

/* A resumed session's rollout: the one of our cwd stamped before the
 * launch cut and last written at or after it, the most recently written.
 * Every rollout of every date dir is stat'ed, since a resumed rollout is
 * appended in an old date dir. */
static char *resumed(sk_codex_disc *d, const meta **found)
{
    double cut = d->launched - GRACE_MS, best = 0;
    char *file = NULL;
    date_dirs dirs = {0};
    list_date_dirs(d->home, &dirs);
    for (size_t i = 0; i < dirs.len; i++) {
        sk_strv rs = {0};
        rollouts(dirs.ptr[i].dir, &rs);
        for (size_t k = 0; k < rs.len; k++) {
            struct stat st;
            if (stat(rs.ptr[k], &st) != 0)
                continue;
            double mtime = sk_stat_mtime_ms(&st);
            if (mtime < cut)
                continue;
            const meta *m = meta_get(d, rs.ptr[k]);
            if (!mine(d, m) || !(m->created < cut))
                continue;
            if (!file || mtime > best) {
                free(file);
                file = sk_xstrdup(rs.ptr[k]);
                *found = m;
                best = mtime;
            }
        }
        sk_strv_free(&rs);
    }
    free_date_dirs(&dirs);
    return file;
}

const sk_codex_found *sk_codex_disc_find(sk_codex_disc *d, pid_t pid)
{
    if (d->have_found)
        return &d->found;
    resolve(d);
    sk_codex_found *miss = &d->miss;
    memset(miss, 0, sizeof *miss);
    const sk_codex_found *r = NULL;
    const char *id = argv_thread(d);
    if (id) {
        miss->tried[miss->n_tried++] = "argv";
        char *file = sk_codex_disc_by_id(d, id);
        if (file)
            r = hit(d, file, id, "argv", true);
        free(file);
    }
    if (!r && pid) {
        miss->tried[miss->n_tried++] = "fd";
        const meta *m = NULL;
        char *file = held(d, pid, &m);
        if (file)
            r = hit(d, file, m->id, "fd", true);
        free(file);
    }
    if (!r) {
        const char *how = argv_has(d, "resume") ? "resumed" : argv_has(d, "fork") ? "forked" : "new";
        miss->tried[miss->n_tried++] = how;
        const meta *m = NULL;
        char *file = !strcmp(how, "resumed") ? resumed(d, &m) : fresh(d, &m);
        if (file)
            r = hit(d, file, m->id, how, false);
        free(file);
    }
    if (!r) {
        miss->how = "none";
        r = miss;
    }
    drop_scratch(d);
    return r;
}

void sk_codex_disc_bases(sk_codex_disc *d, const char *file, sk_codex_bases *out)
{
    sk_codex_bases chain = {0}; /* newest first */
    sk_strv seen = {0};
    sk_strv_push(&seen, file);
    const char *f = file;
    while (chain.len < MAX_BASES) {
        const meta *m = meta_get(d, f);
        if (!m || !m->has_base)
            break;
        int64_t end = m->base_end;
        char *from = m->base_thread ? sk_codex_disc_by_id(d, m->base_thread) : NULL;
        bool again = false;
        for (size_t i = 0; from && i < seen.len; i++)
            again = again || !strcmp(seen.ptr[i], from);
        if (!from || again) {
            free(from);
            break;
        }
        sk_strv_push(&seen, from);
        sk_codex_base b = {from, end};
        sk_vec_push(&chain, b);
        f = from;
    }
    for (size_t i = chain.len; i-- > 0;)
        sk_vec_push(out, chain.ptr[i]);
    sk_vec_free(&chain);
    sk_strv_free(&seen);
    drop_scratch(d);
}

void sk_codex_bases_free(sk_codex_bases *b)
{
    for (size_t i = 0; i < b->len; i++)
        free(b->ptr[i].file);
    sk_vec_free(b);
}

void sk_codex_disc_subagents(sk_codex_disc *d, const char *thread, double now, int *spawned,
                             int *active)
{
    resolve(d);
    double cut = d->launched - GRACE_MS;
    *spawned = *active = 0;
    date_dirs dirs = {0};
    list_date_dirs(d->home, &dirs);
    for (size_t i = 0; i < dirs.len; i++) {
        if (dirs.ptr[i].mtime < cut)
            continue;
        sk_strv rs = {0};
        rollouts(dirs.ptr[i].dir, &rs);
        for (size_t k = 0; k < rs.len; k++) {
            sk_str f = sk_str_c(rs.ptr[k]);
            if (sk_map_has(&d->kids, f))
                continue;
            const meta *m = meta_get(d, rs.ptr[k]);
            if (!sk_map_has(&d->lines, f))
                continue;
            if (!m || !m->parent || strcmp(m->parent, thread)) {
                *sk_map_put(&d->kids, f) = &NOT_OURS;
                continue;
            }
            struct stat st;
            if (stat(rs.ptr[k], &st) != 0 || sk_stat_mtime_ms(&st) < cut)
                continue;
            *sk_map_put(&d->kids, f) = &OURS;
            (*spawned)++;
        }
        sk_strv_free(&rs);
    }
    free_date_dirs(&dirs);
    size_t i = 0;
    sk_str f;
    void *v;
    while (sk_map_next(&d->kids, &i, &f, &v)) {
        if (v != &OURS)
            continue;
        char *path = sk_xstrndup(f.ptr, f.len);
        struct stat st;
        if (stat(path, &st) == 0 && now - sk_stat_mtime_ms(&st) < ACTIVE_MS)
            (*active)++;
        free(path);
    }
    drop_scratch(d);
}
