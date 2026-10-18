/* Ports app/board.js: decision 0029, with HTTPS through the curl command
 * (decision 0036). gh, git and curl run as children whose output the
 * core loop reads; their stderr goes to /dev/null unread, since a tool
 * may echo what it was given. */
#include "board.h"

#include "../base/paths.h"
#include "../base/spawn.h"
#include "../core/env.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

#define MAX_OUTPUT (16 * 1048576)
/* Past the last signal on Linux and macOS, as in core/session.c. */
#define MAX_SIGNAL 65
#define MAX_BODY (8 * 1048576)
/* What a child is given on its stdin is written before it starts, so it
 * must fit a pipe at once: a credential request, or curl's config. */
#define MAX_INPUT 4096

static const char *const LOOPBACK[] = {"127.0.0.1", "localhost", "[::1]"};

/* ---- Remotes ---- */

static bool is_alpha(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

static void lower(char *s)
{
    for (; *s; s++)
        if (*s >= 'A' && *s <= 'Z')
            *s = (char)(*s - 'A' + 'a');
}

static int hex_digit(char c)
{
    if (is_digit(c))
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* decodeURIComponent, false where it would throw. The UTF-8 it makes is
 * not checked: no byte past ASCII passes the name check after it. */
static bool percent_decode(sk_str s, sk_buf *out)
{
    for (size_t i = 0; i < s.len; i++) {
        if (s.ptr[i] != '%') {
            sk_buf_putc(out, s.ptr[i]);
            continue;
        }
        int hi = i + 2 < s.len ? hex_digit(s.ptr[i + 1]) : -1, lo = i + 2 < s.len ? hex_digit(s.ptr[i + 2]) : -1;
        if (hi < 0 || lo < 0)
            return false;
        sk_buf_putc(out, (char)(hi * 16 + lo));
        i += 2;
    }
    return true;
}

/* A URL path with its . and .. segments resolved, as the URL parser
 * leaves pathname. */
static void remove_dots(sk_str path, sk_buf *out)
{
    SK_VEC(size_t) at = {0};
    while (path.len) {
        size_t slash = sk_str_find(path, '/');
        sk_str seg = sk_str_sub(path, 0, slash);
        bool last = slash == SIZE_MAX;
        path = last ? sk_str_n(NULL, 0) : sk_str_sub(path, slash + 1, SIZE_MAX);
        if (sk_str_eq_c(seg, "."))
            continue;
        if (sk_str_eq_c(seg, "..")) {
            if (at.len)
                out->len = at.ptr[--at.len];
            continue;
        }
        sk_vec_push(&at, out->len);
        sk_buf_putc(out, '/');
        sk_buf_append_str(out, seg);
    }
    if (out->ptr)
        out->ptr[out->len] = 0;
    sk_vec_free(&at);
}

/* A host the URL parser accepts for a scheme with a host: none of its
 * forbidden code points, ASCII only (it would convert the rest). In
 * brackets, an IPv6 address's characters only: the host goes on a line
 * of its own to git credential, where a newline would name another. */
static bool host_ok(sk_str h, bool special)
{
    if (!h.len)
        return !special;
    if (h.ptr[0] == '[') {
        if (h.len < 4 || h.ptr[h.len - 1] != ']')
            return false;
        for (size_t i = 1; i + 1 < h.len; i++)
            if (!isxdigit((unsigned char)h.ptr[i]) && h.ptr[i] != ':' && h.ptr[i] != '.')
                return false;
        return memchr(h.ptr, ':', h.len) != NULL;
    }
    for (size_t i = 0; i < h.len; i++) {
        unsigned char c = (unsigned char)h.ptr[i];
        if (c <= ' ' || c >= 0x7f || strchr("#%/:<>?@[\\]^|", c))
            return false;
    }
    return true;
}

/* scheme://[user[:password]@]host[:port]/path, as new URL() reads it,
 * for the parts parseRemote takes. */
static bool parse_url(sk_str u, char **proto, char **host, char **port, sk_buf *path)
{
    size_t colon = sk_str_find(u, ':');
    sk_str scheme = sk_str_sub(u, 0, colon);
    sk_str rest = sk_str_sub(u, colon + 3, SIZE_MAX);
    *proto = sk_xstrndup(scheme.ptr, scheme.len);
    lower(*proto);
    bool special = !strcmp(*proto, "http") || !strcmp(*proto, "https");
    size_t end = 0;
    while (end < rest.len && rest.ptr[end] != '/' && rest.ptr[end] != '?' && rest.ptr[end] != '#' &&
           !(special && rest.ptr[end] == '\\'))
        end++;
    sk_str auth = sk_str_sub(rest, 0, end);
    for (size_t i = auth.len; i-- > 0;) {
        if (auth.ptr[i] == '@') {
            auth = sk_str_sub(auth, i + 1, SIZE_MAX);
            break;
        }
    }
    sk_str h = auth, p = sk_str_n(NULL, 0);
    size_t port_at = SIZE_MAX;
    if (auth.len && auth.ptr[0] == '[') {
        size_t close = sk_str_find(auth, ']');
        if (close == SIZE_MAX)
            return false;
        h = sk_str_sub(auth, 0, close + 1);
        if (close + 1 < auth.len) {
            if (auth.ptr[close + 1] != ':')
                return false;
            port_at = close + 2;
        }
    } else {
        size_t c = sk_str_find(auth, ':');
        if (c != SIZE_MAX) {
            h = sk_str_sub(auth, 0, c);
            port_at = c + 1;
        }
    }
    if (port_at != SIZE_MAX) {
        p = sk_str_sub(auth, port_at, SIZE_MAX);
        uint64_t n;
        if (p.len && (!sk_str_to_u64(p, &n) || n > 65535))
            return false;
    }
    if (!host_ok(h, special))
        return false;
    *host = sk_xstrndup(h.ptr, h.len);
    lower(*host);
    /* The scheme's own port is no port. */
    uint64_t n = 0;
    if (p.len)
        sk_str_to_u64(p, &n);
    sk_buf num = {0};
    if (p.len && !(!strcmp(*proto, "https") && n == 443) && !(!strcmp(*proto, "http") && n == 80))
        sk_buf_printf(&num, "%llu", (unsigned long long)n);
    *port = sk_xstrdup(sk_buf_cstr(&num));
    sk_buf_free(&num);
    size_t path_end = end;
    while (path_end < rest.len && rest.ptr[path_end] != '?' && rest.ptr[path_end] != '#')
        path_end++;
    sk_buf raw = {0};
    sk_buf_append(&raw, rest.ptr + end, path_end - end);
    if (special)
        for (size_t i = 0; i < raw.len; i++)
            if (raw.ptr[i] == '\\')
                raw.ptr[i] = '/';
    remove_dots(sk_buf_str(&raw), path);
    sk_buf_free(&raw);
    return true;
}

static bool url_form(sk_str u)
{
    if (!u.len || !is_alpha(u.ptr[0]))
        return false;
    size_t i = 1;
    while (i < u.len && (is_alpha(u.ptr[i]) || is_digit(u.ptr[i]) || strchr("+.-", u.ptr[i])))
        i++;
    return i + 2 < u.len && u.ptr[i] == ':' && u.ptr[i + 1] == '/' && u.ptr[i + 2] == '/';
}

/* /^(?:[^@/\s]+@)?([A-Za-z0-9.-]+):(?!\/)(\S+)$/ */
static bool scp_form(sk_str u, sk_str *host, sk_str *path)
{
    for (size_t i = 0; i < u.len; i++)
        if (is_space(u.ptr[i]))
            return false;
    size_t start = 0, at = sk_str_find(u, '@');
    if (at != SIZE_MAX) {
        bool user = at > 0;
        for (size_t i = 0; i < at && user; i++)
            user = u.ptr[i] != '/';
        if (user)
            start = at + 1;
    }
    size_t i = start;
    while (i < u.len && (is_alpha(u.ptr[i]) || is_digit(u.ptr[i]) || u.ptr[i] == '.' || u.ptr[i] == '-'))
        i++;
    if (i == start || i >= u.len || u.ptr[i] != ':' || i + 1 >= u.len || u.ptr[i + 1] == '/')
        return false;
    *host = sk_str_sub(u, start, i - start);
    *path = sk_str_sub(u, i + 1, SIZE_MAX);
    return true;
}

/* An owner or a repository: never . or .., which the forge's API path
 * would resolve to another, decoded from %2e after the dots were. */
static bool name_ok(sk_str s)
{
    if (!s.len || sk_str_eq_c(s, ".") || sk_str_eq_c(s, ".."))
        return false;
    for (size_t i = 0; i < s.len; i++)
        if (!(is_alpha(s.ptr[i]) || is_digit(s.ptr[i]) || s.ptr[i] == '.' || s.ptr[i] == '_' || s.ptr[i] == '-'))
            return false;
    return true;
}

bool sk_board_parse_remote(const char *url, sk_remote *out)
{
    memset(out, 0, sizeof *out);
    if (!url)
        return false;
    sk_str u = sk_str_trim(sk_str_c(url));
    if (!u.len)
        return false;
    char *proto = NULL, *host = NULL, *port = NULL;
    const char *scheme = "https";
    sk_buf path = {0};
    bool ok;
    if (url_form(u)) {
        ok = parse_url(u, &proto, &host, &port, &path);
        if (ok && !strcmp(proto, "http")) {
            ok = false;
            for (size_t i = 0; i < SK_LEN(LOOPBACK); i++)
                ok = ok || !strcmp(host, LOOPBACK[i]);
            scheme = "http";
        } else if (ok && (!strcmp(proto, "ssh") || !strcmp(proto, "git"))) {
            port[0] = 0;
        } else if (ok && strcmp(proto, "https")) {
            ok = false;
        }
        if (ok) {
            sk_buf decoded = {0};
            ok = percent_decode(sk_buf_str(&path), &decoded);
            sk_buf_free(&path);
            path = decoded;
        }
    } else {
        sk_str h, p;
        ok = scp_form(u, &h, &p);
        if (ok) {
            host = sk_xstrndup(h.ptr, h.len);
            lower(host);
            port = sk_xstrdup("");
            sk_buf_append_str(&path, p);
        }
    }
    /* Slashes off both ends, then .git off the end: owner/repo. */
    sk_str rest = sk_buf_str(&path);
    while (rest.len && rest.ptr[0] == '/')
        rest = sk_str_sub(rest, 1, SIZE_MAX);
    while (rest.len && rest.ptr[rest.len - 1] == '/')
        rest.len--;
    if (sk_str_suffix(rest, SK_STR(".git")))
        rest.len -= 4;
    size_t slash = sk_str_find(rest, '/');
    sk_str owner = sk_str_sub(rest, 0, slash);
    sk_str repo = slash == SIZE_MAX ? sk_str_n(NULL, 0) : sk_str_sub(rest, slash + 1, SIZE_MAX);
    ok = ok && slash != SIZE_MAX && name_ok(owner) && name_ok(repo);
    if (ok) {
        out->forge = !strcmp(host, "github.com") ? SK_FORGE_GITHUB : SK_FORGE_GITEA;
        out->scheme = sk_xstrdup(scheme);
        out->host = host;
        out->port = port;
        out->owner = sk_xstrndup(owner.ptr, owner.len);
        out->repo = sk_xstrndup(repo.ptr, repo.len);
        sk_buf key = {0};
        sk_buf_printf(&key, "%s://%s%s%s/%s/%s", scheme, host, *port ? ":" : "", port, out->owner, out->repo);
        out->key = key.ptr;
        host = port = NULL;
    }
    free(proto);
    free(host);
    free(port);
    sk_buf_free(&path);
    return ok;
}

void sk_remote_free(sk_remote *r)
{
    free(r->scheme);
    free(r->host);
    free(r->port);
    free(r->owner);
    free(r->repo);
    free(r->key);
    memset(r, 0, sizeof *r);
}

static void remote_copy(sk_remote *to, const sk_remote *from)
{
    to->forge = from->forge;
    to->scheme = sk_xstrdup(from->scheme);
    to->host = sk_xstrdup(from->host);
    to->port = sk_xstrdup(from->port);
    to->owner = sk_xstrdup(from->owner);
    to->repo = sk_xstrdup(from->repo);
    to->key = sk_xstrdup(from->key);
}

/* ---- Days and stamps ---- */

/* Days from 1970-01-01 to y-m-d of the proleptic Gregorian calendar. */
static int64_t days_from_civil(int64_t y, int64_t m, int64_t d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

/* Local time to ms since the epoch, fields overflowing as Date's
 * constructor lets them. */
static double local_ms(int64_t y, int mon, int64_t day, int h, int min, int s, double ms)
{
    struct tm tm;
    memset(&tm, 0, sizeof tm);
    tm.tm_year = (int)(y - 1900);
    tm.tm_mon = mon;
    tm.tm_mday = (int)day;
    tm.tm_hour = h;
    tm.tm_min = min;
    tm.tm_sec = s;
    tm.tm_isdst = -1;
    errno = 0;
    time_t t = mktime(&tm);
    return t == (time_t)-1 && errno ? NAN : (double)t * 1000 + ms;
}

static bool digits(sk_str s, size_t at, size_t n, int64_t *out)
{
    if (at + n > s.len)
        return false;
    int64_t v = 0;
    for (size_t i = 0; i < n; i++) {
        if (!is_digit(s.ptr[at + i]))
            return false;
        v = v * 10 + (s.ptr[at + i] - '0');
    }
    *out = v;
    return true;
}

/* Date.parse of ECMAScript's date time string format: YYYY[-MM[-DD]]
 * [THH:mm[:ss[.sss]]][Z|+HH:mm|-HH:mm], or a six-digit year with its
 * sign. A date alone is UTC; a time with no offset is local time. NAN
 * when it is not one. */
static double parse_date(sk_str s)
{
    size_t i = 0;
    int64_t y = 0, mo = 1, d = 1, h = 0, mi = 0, sec = 0, off = 0;
    double frac = 0;
    if (s.len && (s.ptr[0] == '+' || s.ptr[0] == '-')) {
        if (!digits(s, 1, 6, &y) || (s.ptr[0] == '-' && y == 0))
            return NAN;
        if (s.ptr[0] == '-')
            y = -y;
        i = 7;
    } else {
        if (!digits(s, 0, 4, &y))
            return NAN;
        i = 4;
    }
    if (i < s.len && s.ptr[i] == '-') {
        if (!digits(s, i + 1, 2, &mo))
            return NAN;
        i += 3;
        if (i < s.len && s.ptr[i] == '-') {
            if (!digits(s, i + 1, 2, &d))
                return NAN;
            i += 3;
        }
    }
    bool has_time = false, has_offset = false;
    if (i < s.len && (s.ptr[i] == 'T' || s.ptr[i] == 't')) {
        has_time = true;
        if (!digits(s, i + 1, 2, &h) || i + 3 >= s.len || s.ptr[i + 3] != ':' || !digits(s, i + 4, 2, &mi))
            return NAN;
        i += 6;
        if (i < s.len && s.ptr[i] == ':') {
            if (!digits(s, i + 1, 2, &sec))
                return NAN;
            i += 3;
            if (i < s.len && s.ptr[i] == '.') {
                double scale = 100;
                size_t start = ++i;
                while (i < s.len && is_digit(s.ptr[i])) {
                    frac += (s.ptr[i] - '0') * scale;
                    scale /= 10;
                    i++;
                }
                if (i == start)
                    return NAN;
                frac = floor(frac);
            }
        }
        if (i < s.len && (s.ptr[i] == 'Z' || s.ptr[i] == 'z')) {
            has_offset = true;
            i++;
        } else if (i < s.len && (s.ptr[i] == '+' || s.ptr[i] == '-')) {
            int64_t oh = 0, om = 0;
            if (!digits(s, i + 1, 2, &oh) || i + 3 >= s.len || s.ptr[i + 3] != ':' || !digits(s, i + 4, 2, &om) ||
                oh > 23 || om > 59)
                return NAN;
            off = (s.ptr[i] == '+' ? 1 : -1) * (oh * 60 + om);
            has_offset = true;
            i += 6;
        }
    }
    if (i != s.len || mo < 1 || mo > 12 || d < 1 || d > 31 || mi > 59 || sec > 59 || h > 24 ||
        (h == 24 && (mi || sec || frac)))
        return NAN;
    if (has_time && !has_offset)
        return local_ms(y, (int)mo - 1, d, (int)h, (int)mi, (int)sec, frac);
    double days = (double)days_from_civil(y, mo, d);
    return days * 86400000 + (double)(h * 3600 + mi * 60 + sec - off * 60) * 1000 + frac;
}

double sk_board_stamp(const sk_json *v)
{
    sk_str s;
    if (!sk_json_as_string(v, &s) || !s.len)
        return NAN;
    double t = parse_date(s);
    return isnan(t) || t < 86400000 ? NAN : t;
}

/* The local midnight that starts the day d days after the day of t. */
static double midnight(double t, int d)
{
    time_t secs = (time_t)floor(t / 1000);
    struct tm tm;
    if (!localtime_r(&secs, &tm))
        return NAN;
    return local_ms(tm.tm_year + 1900, tm.tm_mon, tm.tm_mday + d, 0, 0, 0, 0);
}

void sk_board_series_of(const sk_issue *issues, size_t n, double now, sk_board_series *out)
{
    memset(out, 0, sizeof *out);
    for (int k = 0; k < SK_BOARD_DAYS; k++) {
        double start = midnight(now, k - SK_BOARD_DAYS + 1);
        double end = k == SK_BOARD_DAYS - 1 ? now + 1 : midnight(now, k - SK_BOARD_DAYS + 2);
        for (size_t i = 0; i < n; i++) {
            const sk_issue *is = &issues[i];
            bool open = isnan(is->closed);
            if (is->created < end && (open || is->closed >= end))
                out->open[k]++;
            if (!open && is->closed >= start && is->closed < end)
                out->closed[k]++;
        }
        out->closed14 += out->closed[k];
    }
    out->open_now = out->open[SK_BOARD_DAYS - 1];
}

/* ---- Children ---- */

typedef struct job job;
/* status: the exit code, RUN_KILLED or RUN_CANNOT_START. */
typedef void (*job_fn)(sk_boards *b, void *owner, int status, sk_buf *out);

enum { RUN_KILLED = -1, RUN_CANNOT_START = -2 };

struct job {
    sk_boards *b;
    pid_t pid; /* -1 when it never started */
    int out_fd, watch, timer, reap;
    sk_buf out;
    size_t received; /* every byte read, kept or not */
    job_fn done;
    void *owner;
};

typedef enum { REMOTE_UNKNOWN, REMOTE_NONE, REMOTE_SET } remote_state;

typedef struct {
    char *folder;
    double at;
    job *pending;
    remote_state state;
    sk_remote remote;
} folder_entry;

typedef enum { ISSUES_UNKNOWN, ISSUES_NONE, ISSUES_SET } issues_state;

typedef SK_VEC(sk_issue) issue_list;

typedef struct {
    sk_remote remote;
    double at;
    job *pending;
    issues_state state;
    issue_list list;
    char *error;
    int fetches;
    /* The read under way: what it has so far, its page, git's password. */
    issue_list got;
    int page;
    sk_buf token;
} remote_entry;

struct sk_boards {
    sk_boards_opts o;
    SK_VEC(job *) jobs;
    SK_VEC(folder_entry *) folders;
    SK_VEC(remote_entry *) remotes;
};

static void say(sk_boards *b, const char *fmt, ...) SK_PRINTF(2, 3);

static void say(sk_boards *b, const char *fmt, ...)
{
    if (!b->o.log)
        return;
    sk_buf msg = {0};
    va_list ap;
    va_start(ap, fmt);
    sk_buf_vprintf(&msg, fmt, ap);
    va_end(ap);
    b->o.log(b->o.log_ud, msg.ptr);
    sk_buf_free(&msg);
}

/* What run() of app/board.js rejects with. */
static void run_error(sk_buf *out, const char *cmd, int status)
{
    if (status == RUN_CANNOT_START)
        sk_buf_printf(out, "%s: cannot start", cmd);
    else if (status == RUN_KILLED)
        sk_buf_printf(out, "%s: killed", cmd);
    else
        sk_buf_printf(out, "%s: exit %d", cmd, status);
}

/* The environment git and gh never prompt in: no terminal, no askpass
 * program. */
static void quiet_env(const sk_boards *b, sk_envp *env)
{
    sk_envp_from(env, b->o.env ? b->o.env : environ);
    sk_envp_set(env, "GIT_TERMINAL_PROMPT", "0");
    sk_envp_set(env, "GH_PROMPT_DISABLED", "1");
    sk_envp_set(env, "GIT_ASKPASS", NULL);
    sk_envp_set(env, "SSH_ASKPASS", NULL);
}

/* cmd found on the PATH of env, as the child would look it up. */
static char *find_program(const char *cmd, const sk_envp *env)
{
    const char *path = sk_envp_get(env, "PATH");
    sk_str rest = sk_str_c(path ? path : "/usr/bin:/bin");
    for (;;) {
        size_t colon = sk_str_find(rest, ':');
        sk_str dir = sk_str_sub(rest, 0, colon);
        sk_buf p = {0};
        sk_buf_printf(&p, "%.*s/%s", (int)dir.len, dir.len ? dir.ptr : ".", cmd);
        struct stat st;
        if (stat(p.ptr, &st) == 0 && S_ISREG(st.st_mode) && access(p.ptr, X_OK) == 0)
            return p.ptr;
        sk_buf_free(&p);
        if (colon == SIZE_MAX)
            return NULL;
        rest = sk_str_sub(rest, colon + 1, SIZE_MAX);
    }
}

static bool cloexec(int fd)
{
    return fcntl(fd, F_SETFD, FD_CLOEXEC) == 0;
}

static void job_close(job *j)
{
    sk_loop *loop = j->b->o.loop;
    if (j->watch)
        sk_loop_unwatch(loop, j->watch);
    if (j->timer)
        sk_loop_cancel(loop, j->timer);
    if (j->reap)
        sk_loop_cancel(loop, j->reap);
    j->watch = j->timer = j->reap = 0;
    if (j->out_fd >= 0)
        close(j->out_fd);
    j->out_fd = -1;
}

static void job_free(job *j)
{
    job_close(j);
    sk_buf_free(&j->out);
    free(j);
}

static void job_finish(job *j, int status)
{
    sk_boards *b = j->b;
    job_close(j);
    for (size_t i = 0; i < b->jobs.len; i++) {
        if (b->jobs.ptr[i] == j) {
            b->jobs.ptr[i] = b->jobs.ptr[--b->jobs.len];
            break;
        }
    }
    j->done(b, j->owner, status, &j->out);
    job_free(j);
}

static void on_reap(sk_loop *loop, void *ud);

/* Ends the job once the child is gone, else looks again shortly. */
static void try_reap(job *j)
{
    int ws;
    pid_t r;
    while ((r = waitpid(j->pid, &ws, WNOHANG)) < 0 && errno == EINTR)
        ;
    if (r == 0) {
        if (!j->reap)
            j->reap = sk_loop_timer(j->b->o.loop, 5, 5, on_reap, j);
        return;
    }
    int status = RUN_KILLED;
    if (r == j->pid && WIFEXITED(ws))
        status = WEXITSTATUS(ws);
    job_finish(j, status);
}

static void on_reap(sk_loop *loop, void *ud)
{
    (void)loop;
    try_reap(ud);
}

static void on_output(sk_loop *loop, int fd, short revents, void *ud)
{
    (void)loop, (void)revents;
    job *j = ud;
    char chunk[65536];
    for (;;) {
        ssize_t n = read(fd, chunk, sizeof chunk);
        if (n > 0) {
            /* As run() of app/board.js: what comes past the cap is
             * dropped, and the child goes on. */
            j->received += (size_t)n;
            if (j->received <= MAX_OUTPUT)
                sk_buf_append(&j->out, chunk, (size_t)n);
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && errno == EAGAIN)
            return;
        break;
    }
    sk_loop_unwatch(j->b->o.loop, j->watch);
    j->watch = 0;
    close(j->out_fd);
    j->out_fd = -1;
    try_reap(j);
}

static void on_timeout(sk_loop *loop, void *ud)
{
    (void)loop;
    job *j = ud;
    j->timer = 0;
    if (j->pid < 0) {
        job_finish(j, RUN_CANNOT_START);
        return;
    }
    kill(j->pid, SIGKILL);
    if (j->watch)
        sk_loop_unwatch(j->b->o.loop, j->watch);
    j->watch = 0;
    if (j->out_fd >= 0)
        close(j->out_fd);
    j->out_fd = -1;
    try_reap(j);
}

/* Starts argv with input on its stdin, stdout read, stderr dropped, and
 * killed after timeout_ms. done runs on the loop once it has ended,
 * never during this call. */
static job *run_job(sk_boards *b, const char *cmd, char *const *argv, sk_str input, int timeout_ms, job_fn done,
                    void *owner)
{
    job *j = sk_xcalloc(1, sizeof *j);
    j->b = b;
    j->pid = -1;
    j->out_fd = -1;
    j->done = done;
    j->owner = owner;
    sk_vec_push(&b->jobs, j);

    sk_envp env;
    quiet_env(b, &env);
    char *path = find_program(cmd, &env);
    int in[2] = {-1, -1}, out[2] = {-1, -1}, null_fd = -1;
    /* Marked close-on-exec before another thread spawns (base/spawn.h). */
    sk_spawn_lock();
    bool ok = path && input.len <= MAX_INPUT && pipe(in) == 0;
    ok = ok && cloexec(in[0]) && cloexec(in[1]) && pipe(out) == 0;
    ok = ok && cloexec(out[0]) && cloexec(out[1]) &&
         (null_fd = open("/dev/null", O_RDWR | O_CLOEXEC)) >= 0;
    sk_spawn_unlock();
    if (ok) {
        /* The input is in the pipe before the child starts, so writing
         * it never meets a reader that has gone. */
        int fl = fcntl(in[1], F_GETFL);
        ok = fl >= 0 && fcntl(in[1], F_SETFL, fl | O_NONBLOCK) == 0 &&
             (input.len == 0 || write(in[1], input.ptr, input.len) == (ssize_t)input.len);
        close(in[1]);
        in[1] = -1;
        fl = fcntl(out[0], F_GETFL);
        ok = ok && fl >= 0 && fcntl(out[0], F_SETFL, fl | O_NONBLOCK) == 0;
    }
    if (ok) {
        char *const *envp = sk_envp_ptr(&env);
        /* The loop's handlers would write to its pipe from the child: every
         * signal is held across the fork, then the child takes the
         * defaults and no mask, as Node's spawn gives. */
        struct sigaction dfl;
        memset(&dfl, 0, sizeof dfl);
        dfl.sa_handler = SIG_DFL;
        sigemptyset(&dfl.sa_mask);
        sigset_t all, old, none;
        sigfillset(&all);
        sigemptyset(&none);
        pthread_sigmask(SIG_SETMASK, &all, &old);
        sk_spawn_lock();
        j->pid = fork();
        if (j->pid == 0) {
            for (int sig = 1; sig < MAX_SIGNAL; sig++)
                sigaction(sig, &dfl, NULL);
            sigprocmask(SIG_SETMASK, &none, NULL);
            if (dup2(in[0], 0) < 0 || dup2(out[1], 1) < 0 || dup2(null_fd, 2) < 0)
                _exit(127);
            execve(path, argv, envp);
            _exit(127);
        }
        sk_spawn_unlock();
        pthread_sigmask(SIG_SETMASK, &old, NULL);
    }
    for (int i = 0; i < 2; i++)
        if (in[i] >= 0)
            close(in[i]);
    if (out[1] >= 0)
        close(out[1]);
    if (null_fd >= 0)
        close(null_fd);
    free(path);
    sk_envp_free(&env);
    if (j->pid > 0) {
        j->out_fd = out[0];
        j->watch = sk_loop_watch(b->o.loop, j->out_fd, POLLIN, on_output, j);
        j->timer = sk_loop_timer(b->o.loop, (uint64_t)timeout_ms, 0, on_timeout, j);
    } else {
        /* Fails on the loop, as the promise of run() rejects later. */
        if (out[0] >= 0)
            close(out[0]);
        j->pid = -1;
        j->timer = sk_loop_timer(b->o.loop, 0, 0, on_timeout, j);
    }
    return j;
}

/* ---- Reading a remote's issues ---- */

static double clock_now(const sk_boards *b)
{
    if (b->o.now)
        return b->o.now();
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec * 1000 + (double)(ts.tv_nsec / 1000000);
}

static void wipe_token(remote_entry *e)
{
    if (e->token.ptr)
        memset(e->token.ptr, 0, e->token.cap);
    sk_buf_free(&e->token);
}

static void fetch_ok(sk_boards *b, remote_entry *e)
{
    wipe_token(e);
    e->pending = NULL;
    e->state = ISSUES_SET;
    sk_vec_free(&e->list);
    for (size_t i = 0; i < e->got.len; i++)
        if (!isnan(e->got.ptr[i].created))
            sk_vec_push(&e->list, e->got.ptr[i]);
    sk_vec_free(&e->got);
    free(e->error);
    e->error = NULL;
    say(b, "board: %s: %zu issues", e->remote.key, e->list.len);
}

static void fetch_failed(sk_boards *b, remote_entry *e, const char *error)
{
    wipe_token(e);
    e->pending = NULL;
    e->state = ISSUES_NONE;
    sk_vec_free(&e->list);
    sk_vec_free(&e->got);
    free(e->error);
    e->error = sk_xstrdup(error);
    say(b, "board: %s: no access (%s)", e->remote.key, error);
}

/* Each item's two stamps, from a JSON array. */
static void take_issues(remote_entry *e, const sk_json *list, const char *created, const char *closed)
{
    for (size_t i = 0; i < list->array.len; i++) {
        const sk_json *item = &list->array.items[i];
        sk_issue is = {sk_board_stamp(sk_json_get(item, created)), sk_board_stamp(sk_json_get(item, closed))};
        sk_vec_push(&e->got, is);
    }
}

static void on_gh(sk_boards *b, void *owner, int status, sk_buf *out)
{
    remote_entry *e = owner;
    if (status != 0) {
        sk_buf msg = {0};
        run_error(&msg, "gh", status);
        fetch_failed(b, e, msg.ptr);
        sk_buf_free(&msg);
        return;
    }
    sk_arena arena;
    sk_arena_init(&arena, 0);
    const sk_json *list = sk_json_parse(&arena, out->ptr ? out->ptr : "", out->len, NULL);
    if (!list)
        fetch_failed(b, e, "gh: not JSON");
    else if (list->type != SK_JSON_ARRAY)
        fetch_failed(b, e, "gh: not a list");
    else {
        take_issues(e, list, "createdAt", "closedAt");
        fetch_ok(b, e);
    }
    sk_arena_free(&arena);
}

/* gh with the user's login, which gh keeps to itself. */
static job *github(sk_boards *b, remote_entry *e)
{
    sk_strv argv = {0};
    sk_strv_push(&argv, "gh");
    sk_strv_push(&argv, "issue");
    sk_strv_push(&argv, "list");
    sk_strv_push(&argv, "--repo");
    sk_strv_pushf(&argv, "%s/%s", e->remote.owner, e->remote.repo);
    const char *rest[] = {"--state", "all", "--json", "createdAt,closedAt", "--limit", "500", NULL};
    sk_strv_extend(&argv, (char *const *)rest);
    job *j = run_job(b, "gh", argv.ptr, sk_str_n(NULL, 0), 30000, on_gh, e);
    sk_strv_free(&argv);
    return j;
}

/* A value in curl's config syntax: in quotes, with \ and " escaped. */
static void config_value(sk_buf *out, const char *name, const char *value)
{
    sk_buf_printf(out, "%s = \"", name);
    for (const char *c = value; *c; c++) {
        if (*c == '\\' || *c == '"')
            sk_buf_putc(out, '\\');
        if (*c == '\n')
            sk_buf_append_c(out, "\\n");
        else if (*c == '\r')
            sk_buf_append_c(out, "\\r");
        else if (*c == '\t')
            sk_buf_append_c(out, "\\t");
        else
            sk_buf_putc(out, *c);
    }
    sk_buf_append_c(out, "\"\n");
}

static void on_page(sk_boards *b, void *owner, int status, sk_buf *out);

/* One GET of a page of Gitea's issues API from the remote's own host,
 * through curl. Everything, the password's header included, is in the
 * config curl reads from its stdin; its command line holds no more than
 * -q and --config -. No redirect is followed, so the header reaches no
 * other host, and no proxy is used, as with Node's https. */
static job *gitea_page(sk_boards *b, remote_entry *e)
{
    const sk_remote *r = &e->remote;
    sk_buf url = {0}, config = {0}, auth = {0};
    sk_buf_printf(&url, "%s://%s%s%s/api/v1/repos/%s/%s/issues?state=all&type=issues&limit=%d&page=%d", r->scheme,
                  r->host, *r->port ? ":" : "", r->port, r->owner, r->repo, SK_BOARD_PAGE, e->page);
    /* Room for all of it at once: a buffer that grew would leave a copy
     * of the password in the block it freed. Escaping at most doubles. */
    sk_buf_reserve(&auth, 32 + e->token.len);
    sk_buf_reserve(&config, 512 + 2 * (url.len + 32 + e->token.len));
    config_value(&config, "url", url.ptr);
    sk_buf_append_c(&config, "silent\ngloboff\nnoproxy = \"*\"\nmax-time = 15\nmax-filesize = 8388608\n");
    config_value(&config, "proto", !strcmp(r->scheme, "http") ? "=http" : "=https");
    config_value(&config, "header", "Accept: application/json");
    config_value(&config, "header", "User-Agent: claude-skins");
    sk_buf_printf(&auth, "Authorization: token %s", e->token.ptr);
    config_value(&config, "header", auth.ptr);
    config_value(&config, "write-out", "\n%{http_code}");
    char *argv[] = {"curl", "-q", "--config", "-", NULL};
    job *j = run_job(b, "curl", argv, sk_buf_str(&config), 20000, on_page, e);
    memset(auth.ptr, 0, auth.cap);
    memset(config.ptr, 0, config.cap);
    sk_buf_free(&auth);
    sk_buf_free(&config);
    sk_buf_free(&url);
    return j;
}

static void on_page(sk_boards *b, void *owner, int status, sk_buf *out)
{
    remote_entry *e = owner;
    if (status != 0) {
        /* curl's 28 is its own timeout. */
        fetch_failed(b, e, status == 28 || status == RUN_KILLED ? "timeout" : "request failed");
        return;
    }
    /* The body, then a newline and the status write-out puts after it. */
    size_t nl = out->len;
    while (nl > 0 && out->ptr[nl - 1] != '\n')
        nl--;
    uint64_t code = 0;
    if (!nl || !sk_str_to_u64(sk_str_n(out->ptr + nl, out->len - nl), &code)) {
        fetch_failed(b, e, "request failed");
        return;
    }
    size_t body = nl - 1;
    if (code < 200 || code > 299) {
        sk_buf msg = {0};
        sk_buf_printf(&msg, "HTTP %llu", (unsigned long long)code);
        fetch_failed(b, e, msg.ptr);
        sk_buf_free(&msg);
        return;
    }
    if (body > MAX_BODY) {
        fetch_failed(b, e, "request failed");
        return;
    }
    sk_arena arena;
    sk_arena_init(&arena, 0);
    const sk_json *list = sk_json_parse(&arena, out->ptr, body, NULL);
    if (!list) {
        fetch_failed(b, e, "not JSON");
    } else if (list->type != SK_JSON_ARRAY) {
        fetch_failed(b, e, "not a list");
    } else {
        take_issues(e, list, "created_at", "closed_at");
        if (list->array.len < SK_BOARD_PAGE || e->page >= SK_BOARD_PAGES) {
            fetch_ok(b, e);
        } else {
            e->page++;
            e->pending = gitea_page(b, e);
        }
    }
    sk_arena_free(&arena);
}

/* The password git's credential helper gives for the remote's host:
 * /^password=(.*)$/m of its answer. */
static bool password_of(const sk_buf *out, sk_buf *token)
{
    sk_str s = sk_buf_str(out);
    for (size_t i = 0; i < s.len;) {
        size_t end = i;
        while (end < s.len && s.ptr[end] != '\n' && s.ptr[end] != '\r')
            end++;
        sk_str line = sk_str_sub(s, i, end - i);
        if (sk_str_prefix(line, SK_STR("password="))) {
            sk_str v = sk_str_sub(line, 9, SIZE_MAX);
            if (!v.len || memchr(v.ptr, 0, v.len))
                return false;
            sk_buf_append_str(token, v);
            return true;
        }
        i = end + 1;
    }
    return false;
}

static void on_credential(sk_boards *b, void *owner, int status, sk_buf *out)
{
    remote_entry *e = owner;
    wipe_token(e);
    bool ok = status == 0 && password_of(out, &e->token);
    if (out->ptr)
        memset(out->ptr, 0, out->cap);
    if (!ok) {
        fetch_failed(b, e, "no login");
        return;
    }
    e->page = 1;
    e->pending = gitea_page(b, e);
}

/* Gitea's issues API, pages of 50 up to 10 pages, with git's password. */
static job *gitea(sk_boards *b, remote_entry *e)
{
    const sk_remote *r = &e->remote;
    sk_buf input = {0};
    sk_buf_printf(&input, "protocol=%s\nhost=%s%s%s\n\n", r->scheme, r->host, *r->port ? ":" : "", r->port);
    char *argv[] = {"git", "-c", "core.askPass=", "-c", "credential.interactive=never", "credential", "fill", NULL};
    job *j = run_job(b, "git", argv, sk_buf_str(&input), 10000, on_credential, e);
    sk_buf_free(&input);
    return j;
}

/* ---- The boards ---- */

sk_boards *sk_boards_new(const sk_boards_opts *o)
{
    sk_boards *b = sk_xcalloc(1, sizeof *b);
    b->o = *o;
    if (!b->o.refresh_ms)
        b->o.refresh_ms = SK_BOARD_REFRESH_MS;
    return b;
}

void sk_boards_free(sk_boards *b)
{
    if (!b)
        return;
    for (size_t i = 0; i < b->jobs.len; i++) {
        job *j = b->jobs.ptr[i];
        if (j->pid > 0) {
            kill(j->pid, SIGKILL);
            while (waitpid(j->pid, NULL, 0) < 0 && errno == EINTR)
                ;
        }
        job_free(j);
    }
    sk_vec_free(&b->jobs);
    for (size_t i = 0; i < b->folders.len; i++) {
        folder_entry *f = b->folders.ptr[i];
        free(f->folder);
        sk_remote_free(&f->remote);
        free(f);
    }
    sk_vec_free(&b->folders);
    for (size_t i = 0; i < b->remotes.len; i++) {
        remote_entry *e = b->remotes.ptr[i];
        wipe_token(e);
        sk_remote_free(&e->remote);
        sk_vec_free(&e->list);
        sk_vec_free(&e->got);
        free(e->error);
        free(e);
    }
    sk_vec_free(&b->remotes);
    free(b);
}

bool sk_boards_busy(const sk_boards *b)
{
    return b->jobs.len > 0;
}

static void on_remote(sk_boards *b, void *owner, int status, sk_buf *out)
{
    (void)b;
    folder_entry *f = owner;
    f->pending = NULL;
    sk_remote_free(&f->remote);
    char *url = status == 0 ? sk_xstrndup(out->ptr ? out->ptr : "", out->len) : NULL;
    f->state = url && sk_board_parse_remote(url, &f->remote) ? REMOTE_SET : REMOTE_NONE;
    free(url);
}

static void board_of(sk_boards *b, const sk_remote *r, double now, sk_board *out)
{
    remote_entry *e = NULL;
    for (size_t i = 0; i < b->remotes.len && !e; i++)
        if (!strcmp(b->remotes.ptr[i]->remote.key, r->key))
            e = b->remotes.ptr[i];
    if (!e || (!e->pending && now - e->at >= b->o.refresh_ms)) {
        if (!e) {
            e = sk_xcalloc(1, sizeof *e);
            remote_copy(&e->remote, r);
            sk_vec_push(&b->remotes, e);
        }
        e->at = now;
        e->fetches++;
        sk_vec_free(&e->got);
        e->pending = e->remote.forge == SK_FORGE_GITHUB ? github(b, e) : gitea(b, e);
    }
    memset(out, 0, sizeof *out);
    if (e->state == ISSUES_UNKNOWN) {
        out->state = SK_BOARD_LOADING;
    } else if (e->state == ISSUES_NONE) {
        out->state = SK_BOARD_NO_ACCESS;
    } else {
        out->state = SK_BOARD_OK;
        sk_board_series_of(e->list.ptr, e->list.len, now, &out->series);
    }
}

void sk_boards_get(sk_boards *b, const char *folder, sk_board *out)
{
    double now = clock_now(b);
    folder_entry *f = NULL;
    for (size_t i = 0; i < b->folders.len && !f; i++)
        if (!strcmp(b->folders.ptr[i]->folder, folder))
            f = b->folders.ptr[i];
    if (!f || (!f->pending && now - f->at >= b->o.refresh_ms)) {
        if (!f) {
            f = sk_xcalloc(1, sizeof *f);
            f->folder = sk_xstrdup(folder);
            sk_vec_push(&b->folders, f);
        }
        f->at = now;
        /* The remote origin of the folder, as git itself reads it. */
        char *argv[] = {"git", "-C", f->folder, "remote", "get-url", "origin", NULL};
        f->pending = run_job(b, "git", argv, sk_str_n(NULL, 0), 5000, on_remote, f);
    }
    memset(out, 0, sizeof *out);
    if (f->state == REMOTE_UNKNOWN)
        out->state = SK_BOARD_LOADING;
    else if (f->state == REMOTE_NONE)
        out->state = SK_BOARD_NONE;
    else
        board_of(b, &f->remote, now, out);
}

void sk_boards_report(const sk_boards *b, sk_jw *w)
{
    sk_jw_array(w);
    for (size_t i = 0; i < b->remotes.len; i++) {
        const remote_entry *e = b->remotes.ptr[i];
        sk_jw_object(w);
        sk_jw_key(w, SK_STR("key"));
        sk_jw_string(w, sk_str_c(e->remote.key));
        sk_jw_key(w, SK_STR("fetches"));
        sk_jw_int(w, e->fetches);
        sk_jw_key(w, SK_STR("error"));
        if (e->error)
            sk_jw_string(w, sk_str_c(e->error));
        else
            sk_jw_null(w);
        sk_jw_object_end(w);
    }
    sk_jw_array_end(w);
}
