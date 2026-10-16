/* Codex rollout discovery at its interface, as
 * test/codex-discovery.test.js has it: a temporary CODEX_HOME, a real
 * child holding a rollout open for the fd route, mtimes set relative to
 * the real clock (decisions 0013, 0026). Then the fd route through a
 * faked process tree, Date.parse, and the adapter reading a fork's
 * sources first. */
#include "../../src/agents/codex/codex.h"
#include "../../src/base/paths.h"
#include "../test.h"

#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CWD "/work/project"
#define DAY 86400000.0

static int serial;
static pid_t kids[16];
static size_t n_kids;

typedef struct {
    char s[37];
} ident;

static ident uuid(void)
{
    ident u;
    snprintf(u.s, sizeof u.s, "01a12c5b-c2a0-7221-b9e6-%012d", ++serial);
    return u;
}

/* The test's directory with every link resolved, as the fd links name it. */
static char *root(void)
{
    return realpath(sk_test_tmpdir(), NULL);
}

static char *home(void)
{
    char *r = root();
    sk_buf h = {0};
    sk_buf_printf(&h, "%s/home-%d", r, ++serial);
    char *sessions = sk_path_join(h.ptr, "sessions");
    CHECK_INT(sk_mkdirs(sessions, 0700), 0);
    free(sessions);
    free(r);
    return h.ptr;
}

static double now_ms(void)
{
    return sk_codex_wall_ms();
}

static void local_tm(double ms, struct tm *tm)
{
    time_t t = (time_t)floor(ms / 1000);
    localtime_r(&t, tm);
}

/* The local date dir `back` days before now. */
typedef struct {
    char s[16];
} day_t;

static day_t day(int back)
{
    struct tm tm;
    local_tm(now_ms() - back * DAY, &tm);
    day_t d;
    snprintf(d.s, sizeof d.s, "%04d/%02d/%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return d;
}

/* new Date(ms).toISOString() */
static void iso(sk_buf *out, double ms)
{
    double whole = floor(ms / 1000);
    time_t t = (time_t)whole;
    struct tm tm;
    gmtime_r(&t, &tm);
    sk_buf_printf(out, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, (int)(ms - whole * 1000));
}

static void touch(const char *path, double ms)
{
    double sec = floor(ms / 1000);
    struct timespec ts[2];
    ts[0].tv_sec = ts[1].tv_sec = (time_t)sec;
    ts[0].tv_nsec = ts[1].tv_nsec = (long)((ms - sec * 1000) * 1e6);
    CHECK_INT(utimensat(AT_FDCWD, path, ts, 0), 0);
}

/* A rollout whose first line is session_meta. stamp: the thread's
 * creation, in the payload and the name (0: now). written: when the line
 * was written (0: stamp). mtime: the last write, when set. bulk: bytes
 * of instructions that lengthen the first line. base: the thread a fork
 * continues, to base_end. */
typedef struct {
    const char *id, *cwd, *source, *parent;
    double stamp, written, mtime;
    size_t bulk;
    const char *base;
    double base_end;
} opts;

static char *rollout(const char *h, day_t dir, opts o)
{
    ident fresh = uuid();
    const char *id = o.id ? o.id : fresh.s;
    double stamp = o.stamp ? o.stamp : now_ms();
    double written = o.written ? o.written : stamp;
    sk_buf d = {0};
    sk_buf_printf(&d, "%s/sessions/%s", h, dir.s);
    CHECK_INT(sk_mkdirs(d.ptr, 0700), 0);
    struct tm tm;
    local_tm(stamp, &tm);
    sk_buf file = {0};
    sk_buf_printf(&file, "%s/rollout-%04d-%02d-%02dT%02d-%02d-%02d-%s.jsonl", d.ptr,
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, id);
    sk_buf text = {0};
    sk_buf_append_c(&text, "{\"timestamp\":\"");
    iso(&text, written);
    sk_buf_printf(&text, "\",\"type\":\"session_meta\",\"payload\":{\"id\":\"%s\",\"timestamp\":\"", id);
    iso(&text, stamp);
    sk_buf_printf(&text, "\",\"cwd\":\"%s\",\"source\":", o.cwd ? o.cwd : CWD);
    if (o.parent)
        sk_buf_printf(&text, "{\"subagent\":{\"thread_spawn\":{\"parent_thread_id\":\"%s\",\"depth\":1}}}",
                      o.parent);
    else
        sk_buf_printf(&text, "\"%s\"", o.source ? o.source : "cli");
    sk_buf_append_c(&text, ",\"originator\":\"codex-tui\",\"cli_version\":\"0.162.1\"");
    if (o.bulk) {
        sk_buf_append_c(&text, ",\"instructions\":\"");
        for (size_t i = 0; i < o.bulk; i++)
            sk_buf_putc(&text, 'x');
        sk_buf_putc(&text, '"');
    }
    if (o.base) {
        sk_buf_printf(&text, ",\"history_mode\":\"paginated\",\"history_base\":{\"thread_id\":\"%s\","
                             "\"end_byte_offset\":",
                      o.base);
        sk_json_format_number(&text, o.base_end);
        sk_buf_append_c(&text, ",\"end_ordinal_exclusive\":2}");
    }
    sk_buf_append_c(&text, "}}\n{\"timestamp\":\"");
    iso(&text, stamp);
    sk_buf_append_c(&text, "\",\"type\":\"event_msg\",\"payload\":{\"type\":\"task_started\"}}\n");
    FILE *f = fopen(file.ptr, "wb");
    CHECK(f != NULL);
    if (f) {
        fwrite(text.ptr, 1, text.len, f);
        fclose(f);
    }
    if (o.mtime)
        touch(file.ptr, o.mtime);
    sk_buf_free(&text);
    sk_buf_free(&d);
    return file.ptr;
}

static void touch_day(const char *h, day_t dir, double ms)
{
    sk_buf d = {0};
    sk_buf_printf(&d, "%s/sessions/%s", h, dir.s);
    touch(d.ptr, ms);
    sk_buf_free(&d);
}

/* A real child holding file open. Returns its pid. */
static pid_t hold(const char *file)
{
    int fd = open(file, O_WRONLY | O_APPEND);
    CHECK(fd >= 0);
    pid_t pid = fork();
    if (pid == 0) {
        for (;;)
            pause();
    }
    close(fd);
    CHECK(pid > 0);
    if (pid > 0 && n_kids < SK_LEN(kids))
        kids[n_kids++] = pid;
    return pid;
}

static void end_kids(void)
{
    for (size_t i = 0; i < n_kids; i++) {
        kill(kids[i], SIGKILL);
        waitpid(kids[i], NULL, 0);
    }
    n_kids = 0;
}

#define ARGV(...) ((char *const[]){__VA_ARGS__, NULL})
#define NO_ARGV ((char *const[]){NULL})

static sk_codex_disc *disc(const char *h, char *const *argv, double launched)
{
    return sk_codex_disc_new(h, argv, CWD, launched);
}

static void check_found(const sk_codex_found *r, const char *file, const char *how, bool exact)
{
    CHECK_STR(r->file, file);
    CHECK_STR(r->how, how);
    CHECK(r->exact == exact);
}

static void check_miss(const sk_codex_found *r, const char *t0, const char *t1, const char *t2)
{
    const char *want[] = {t0, t1, t2};
    size_t n = t2 ? 3 : t1 ? 2 : 1;
    CHECK(r->file == NULL);
    CHECK_STR(r->how, "none");
    CHECK_INT(r->n_tried, n);
    for (size_t i = 0; i < n && i < r->n_tried; i++)
        CHECK_STR(r->tried[i], want[i]);
}

/* The file of a fresh discovery's find, freed with it. */
static void check_file(const char *h, char *const *argv, double launched, pid_t pid, const char *file)
{
    sk_codex_disc *d = disc(h, argv, launched);
    const sk_codex_found *r = sk_codex_disc_find(d, pid);
    if (file)
        CHECK_STR(r->file, file);
    else
        CHECK(r->file == NULL);
    sk_codex_disc_free(d);
}

static void test_t1_resume_id_finds_the_rollout_in_any_date_dir(void)
{
    int backs[] = {100, 120};
    for (size_t i = 0; i < SK_LEN(backs); i++) {
        char *h = home();
        ident id = uuid();
        double old = now_ms() - backs[i] * DAY;
        char *f = rollout(h, day(backs[i]), (opts){.id = id.s, .cwd = "/elsewhere", .stamp = old, .mtime = old});
        touch_day(h, day(backs[i]), old);
        sk_codex_disc *d = disc(h, ARGV("-s", "read-only", "resume", id.s), now_ms());
        const sk_codex_found *r = sk_codex_disc_find(d, 0);
        check_found(r, f, "argv", true);
        CHECK_STR(r->thread, id.s);
        sk_codex_disc_free(d);
        free(f);
        free(h);
    }
}

static void test_t2_route_order_argv_then_fd_then_the_cwd_route(void)
{
    char *h = home();
    double launched = now_ms() - 5000;
    ident id = uuid();
    char *by_id = rollout(h, day(0), (opts){.id = id.s, .cwd = "/elsewhere", .stamp = launched + 1000});
    char *held = rollout(h, day(0), (opts){.cwd = "/elsewhere", .source = "vscode", .stamp = launched + 1000});
    char *fresh = rollout(h, day(0), (opts){.stamp = launched + 1000});
    pid_t pid = hold(held);
    sk_codex_disc *d = disc(h, ARGV("resume", id.s), launched);
    check_found(sk_codex_disc_find(d, pid), by_id, "argv", true);
    sk_codex_disc_free(d);
    d = disc(h, NO_ARGV, launched);
    check_found(sk_codex_disc_find(d, pid), held, "fd", true);
    sk_codex_disc_free(d);
    d = disc(h, NO_ARGV, launched);
    check_found(sk_codex_disc_find(d, 0), fresh, "new", false);
    sk_codex_disc_free(d);
    end_kids();
    free(by_id);
    free(held);
    free(fresh);
    free(h);
}

static void test_t3_a_subagent_rollout_is_never_chosen(void)
{
    char *h = home();
    double launched = now_ms() - 5000;
    ident parent = uuid();
    char *sub = rollout(h, day(0), (opts){.parent = parent.s, .stamp = launched + 1000});
    pid_t pid = hold(sub);
    check_file(h, NO_ARGV, launched, pid, NULL);
    end_kids();
    free(sub);
    free(h);
}

static void test_t4_the_launch_cut(void)
{
    double launched = now_ms();
    struct {
        double ago;
        bool chosen;
    } cases[] = {{3000, false}, {1000, true}};
    for (size_t i = 0; i < SK_LEN(cases); i++) {
        char *h = home();
        char *f = rollout(h, day(0), (opts){.stamp = launched - cases[i].ago});
        check_file(h, NO_ARGV, launched, 0, cases[i].chosen ? f : NULL);
        free(f);
        free(h);
    }
}

static void test_t5_a_miss_reports_none_and_the_routes_tried(void)
{
    char *r = root();
    char *absent = sk_path_join(r, "absent");
    ident id = uuid();
    sk_codex_disc *d = disc(absent, ARGV("resume", id.s), now_ms());
    check_miss(sk_codex_disc_find(d, getpid()), "argv", "fd", "resumed");
    sk_codex_disc_free(d);
    char *h = home();
    d = disc(h, ARGV("resume", "--last"), now_ms());
    check_miss(sk_codex_disc_find(d, getpid()), "fd", "resumed", NULL);
    sk_codex_disc_free(d);
    free(h);
    h = home();
    d = disc(h, NO_ARGV, now_ms());
    check_miss(sk_codex_disc_find(d, 0), "new", NULL, NULL);
    sk_codex_disc_free(d);
    free(h);
    free(absent);
    free(r);
}

static void test_t6_a_symlinked_codex_home_the_fd_route_finds_the_held_rollout(void)
{
    char *real = home();
    char *r = root();
    sk_buf link = {0};
    sk_buf_printf(&link, "%s/link-%d", r, ++serial);
    CHECK_INT(symlink(real, link.ptr), 0);
    double launched = now_ms() - 5000;
    char *f = rollout(real, day(0), (opts){.cwd = "/elsewhere", .stamp = launched + 1000});
    pid_t pid = hold(f);
    sk_codex_disc *d = disc(link.ptr, NO_ARGV, launched);
    check_found(sk_codex_disc_find(d, pid), f, "fd", true);
    sk_codex_disc_free(d);
    end_kids();
    free(f);
    sk_buf_free(&link);
    free(r);
    free(real);
}

static void test_t7_dash_c_dir_finds_the_rollout_of_dir(void)
{
    char *const *argvs[] = {
        ARGV("-C", "/work/other"), ARGV("--cd", "/work/other"), ARGV("--cd=/work/other"),
        ARGV("-C/work/other"),     ARGV("-C", "../other"),
    };
    for (size_t i = 0; i < SK_LEN(argvs); i++) {
        char *h = home();
        char *f = rollout(h, day(0), (opts){.cwd = "/work/other"});
        check_file(h, argvs[i], now_ms() - 5000, 0, f);
        free(f);
        free(h);
    }
}

static void test_t8_a_first_line_over_64_kib_is_read(void)
{
    char *h = home();
    char *f = rollout(h, day(0), (opts){.bulk = 80 * 1024});
    size_t n;
    char *text = sk_test_read(f, &n);
    CHECK(text && (size_t)(strchr(text, '\n') - text) > 64 * 1024);
    free(text);
    check_file(h, NO_ARGV, now_ms() - 5000, 0, f);
    free(f);
    free(h);
}

static void check_subagents(sk_codex_disc *d, const char *thread, int spawned, int active)
{
    int s = -1, a = -1;
    sk_codex_disc_subagents(d, thread, now_ms(), &s, &a);
    CHECK_INT(s, spawned);
    CHECK_INT(a, active);
}

static void test_t10_subagents_spawned_once_active_while_written_within_20_s(void)
{
    char *h = home();
    double launched = now_ms() - 60000;
    ident thread = uuid();
    char *top = rollout(h, day(0), (opts){.id = thread.s, .stamp = launched + 1000});
    char *kid = rollout(h, day(0), (opts){.parent = thread.s, .stamp = now_ms() - 1000, .mtime = now_ms() - 1000});
    sk_codex_disc *d = disc(h, NO_ARGV, launched);
    check_subagents(d, thread.s, 1, 1);
    touch(kid, now_ms() - 21000);
    check_subagents(d, thread.s, 0, 0);
    sk_codex_disc_free(d);
    free(top);
    free(kid);
    free(h);
}

static void test_t9_a_subagent_in_another_date_dir_created_after_launch_is_counted(void)
{
    char *h = home();
    double launched = now_ms() - 60000;
    ident thread = uuid();
    char *kid = rollout(h, day(1), (opts){.parent = thread.s, .stamp = now_ms() - 1000, .mtime = now_ms() - 1000});
    sk_codex_disc *d = disc(h, NO_ARGV, launched);
    check_subagents(d, thread.s, 1, 1);
    sk_codex_disc_free(d);
    free(kid);
    free(h);
}

static void test_t11_a_subagent_of_our_subagent_is_not_counted(void)
{
    char *h = home();
    double launched = now_ms() - 60000;
    ident thread = uuid(), child = uuid();
    char *a = rollout(h, day(0), (opts){.id = child.s, .parent = thread.s, .stamp = now_ms() - 2000, .mtime = now_ms() - 2000});
    char *b = rollout(h, day(0), (opts){.parent = child.s, .stamp = now_ms() - 1000, .mtime = now_ms() - 1000});
    sk_codex_disc *d = disc(h, NO_ARGV, launched);
    check_subagents(d, thread.s, 1, 1);
    sk_codex_disc_free(d);
    free(a);
    free(b);
    free(h);
}

static void test_t12_fresh_an_older_session_written_after_launch_is_not_chosen(void)
{
    char *h = home();
    double launched = now_ms() - 10000;
    char *ours = rollout(h, day(0), (opts){.stamp = launched + 1000, .mtime = launched + 2000});
    char *older = rollout(h, day(0), (opts){.stamp = launched - 3 * 3600000.0, .mtime = launched + 2500});
    sk_codex_disc *d = disc(h, NO_ARGV, launched);
    check_found(sk_codex_disc_find(d, 0), ours, "new", false);
    sk_codex_disc_free(d);
    free(ours);
    free(older);
    free(h);
}

static void test_t18_fresh_created_before_launch_and_prompted_after_it_is_not_chosen(void)
{
    char *h = home();
    double launched = now_ms() - 10000;
    char *before = rollout(h, day(0), (opts){.stamp = launched - 60000, .written = launched + 1000, .mtime = launched + 1000});
    char *ours = rollout(h, day(0), (opts){.stamp = launched + 500, .written = launched + 3000, .mtime = launched + 3000});
    sk_codex_disc *d = disc(h, NO_ARGV, launched);
    check_found(sk_codex_disc_find(d, 0), ours, "new", false);
    sk_codex_disc_free(d);
    free(before);
    free(ours);
    free(h);
}

static void test_t13_fresh_of_two_stamped_after_launch_the_earlier_is_chosen(void)
{
    char *h = home();
    double launched = now_ms() - 10000;
    char *first = rollout(h, day(0), (opts){.stamp = launched + 1000, .mtime = launched + 3000});
    char *second = rollout(h, day(0), (opts){.stamp = launched + 2000, .mtime = launched + 4000});
    check_file(h, NO_ARGV, launched, 0, first);
    free(first);
    free(second);
    free(h);
}

static void test_t14_resume_last_finds_a_rollout_10_days_back_appended_after_launch(void)
{
    char *h = home();
    double old = now_ms() - 10 * DAY;
    char *f = rollout(h, day(10), (opts){.stamp = old, .mtime = now_ms()});
    touch_day(h, day(10), old);
    sk_codex_disc *d = disc(h, ARGV("resume", "--last"), now_ms() - 5000);
    check_found(sk_codex_disc_find(d, 0), f, "resumed", false);
    sk_codex_disc_free(d);
    free(f);
    free(h);
}

static void test_t15_resume_last_a_fresh_unrelated_session_is_not_chosen(void)
{
    char *h = home();
    double launched = now_ms() - 10000;
    double old = now_ms() - 10 * DAY;
    char *resumed = rollout(h, day(10), (opts){.stamp = old, .mtime = old});
    touch_day(h, day(10), old);
    char *other = rollout(h, day(0), (opts){.stamp = launched + 5000});
    sk_codex_disc *d = disc(h, ARGV("resume", "--last"), launched);
    check_miss(sk_codex_disc_find(d, 0), "resumed", NULL, NULL);
    touch(resumed, now_ms());
    check_found(sk_codex_disc_find(d, 0), resumed, "resumed", false);
    sk_codex_disc_free(d);
    free(resumed);
    free(other);
    free(h);
}

static void test_t16_resume_name_takes_the_resumed_route(void)
{
    char *h = home();
    double launched = now_ms() - 10000;
    double old = now_ms() - 3 * DAY;
    char *f = rollout(h, day(3), (opts){.stamp = old, .mtime = now_ms()});
    touch_day(h, day(3), old);
    char *other = rollout(h, day(0), (opts){.stamp = launched + 1000});
    sk_codex_disc *d = disc(h, ARGV("resume", "my-session"), launched);
    check_found(sk_codex_disc_find(d, 0), f, "resumed", false);
    sk_codex_disc_free(d);
    char *empty = home();
    d = disc(empty, ARGV("resume", "my-session"), now_ms());
    check_miss(sk_codex_disc_find(d, 0), "resumed", NULL, NULL);
    sk_codex_disc_free(d);
    free(empty);
    free(f);
    free(other);
    free(h);
}

static void test_t19_fork_id_the_source_is_not_followed_the_new_thread_is(void)
{
    char *h = home();
    double launched = now_ms() - 10000;
    ident id = uuid();
    char *source = rollout(h, day(2), (opts){.id = id.s, .stamp = now_ms() - 2 * DAY, .mtime = launched + 3000});
    char *ours = rollout(h, day(0), (opts){.stamp = launched + 800, .mtime = launched + 900});
    sk_codex_disc *d = disc(h, ARGV("fork", id.s), launched);
    check_found(sk_codex_disc_find(d, 0), ours, "forked", false);
    sk_codex_disc_free(d);
    char *empty = home();
    d = disc(empty, ARGV("fork", id.s), now_ms());
    check_miss(sk_codex_disc_find(d, 0), "forked", NULL, NULL);
    sk_codex_disc_free(d);
    free(empty);
    free(source);
    free(ours);
    free(h);
}

static void test_t17_latch_after_a_hit_a_newer_candidate_does_not_change_the_answer(void)
{
    char *h = home();
    double launched = now_ms() - 10000;
    char *first = rollout(h, day(0), (opts){.stamp = launched + 1000, .mtime = launched + 1000});
    sk_codex_disc *d = disc(h, NO_ARGV, launched);
    const sk_codex_found *a = sk_codex_disc_find(d, 0);
    CHECK_STR(a->file, first);
    char *newer = rollout(h, day(0), (opts){.stamp = launched + 2000});
    const sk_codex_found *b = sk_codex_disc_find(d, hold(newer));
    CHECK(a == b);
    CHECK_STR(b->file, first);
    sk_codex_disc_free(d);
    end_kids();
    free(first);
    free(newer);
    free(h);
}

/* A rollout whose first line references from's history up to end, as
 * Codex 0.162.1 writes a fork. */
static char *fork_of(const char *h, const char *from, double end, const char *id)
{
    return rollout(h, day(0), (opts){.id = id, .base = from, .base_end = end});
}

static long long size_of(const char *file)
{
    struct stat st;
    return stat(file, &st) == 0 ? (long long)st.st_size : -1;
}

/* The id a rollout's name ends with. */
static ident id_of(const char *file)
{
    ident u;
    size_t n = strlen(file);
    snprintf(u.s, sizeof u.s, "%.36s", file + n - 42);
    return u;
}

static void test_t20_a_fork_of_a_fork_its_sources_oldest_first(void)
{
    char *h = home();
    ident aid = uuid(), bid = uuid(), cid = uuid();
    char *a = rollout(h, day(1), (opts){.id = aid.s});
    ident named = id_of(a);
    CHECK_STR(named.s, aid.s);
    char *b = fork_of(h, aid.s, (double)size_of(a), bid.s);
    char *c = fork_of(h, bid.s, 100, cid.s);
    sk_codex_disc *d = disc(h, ARGV("fork", bid.s), now_ms());
    sk_codex_bases bases = {0};
    sk_codex_disc_bases(d, c, &bases);
    CHECK_INT(bases.len, 2);
    if (bases.len == 2) {
        CHECK_STR(bases.ptr[0].file, a);
        CHECK_INT(bases.ptr[0].end, size_of(a));
        CHECK_STR(bases.ptr[1].file, b);
        CHECK_INT(bases.ptr[1].end, 100);
    }
    sk_codex_bases_free(&bases);
    sk_codex_disc_free(d);
    d = disc(h, NO_ARGV, now_ms());
    sk_codex_disc_bases(d, a, &bases);
    CHECK_INT(bases.len, 0);
    sk_codex_bases_free(&bases);
    sk_codex_disc_free(d);
    free(a);
    free(b);
    free(c);
    free(h);
}

static size_t bases_of(const char *h, const char *file)
{
    sk_codex_disc *d = disc(h, NO_ARGV, now_ms());
    sk_codex_bases bases = {0};
    sk_codex_disc_bases(d, file, &bases);
    size_t n = bases.len;
    sk_codex_bases_free(&bases);
    sk_codex_disc_free(d);
    return n;
}

static void test_t21_a_missing_source_a_fork_naming_itself_or_a_bad_offset_give_none(void)
{
    char *h = home();
    ident missing = uuid(), self = uuid(), aid = uuid();
    char *f1 = fork_of(h, missing.s, 10, NULL);
    CHECK_INT(bases_of(h, f1), 0);
    char *f2 = fork_of(h, self.s, 10, self.s);
    CHECK_INT(bases_of(h, f2), 0);
    char *a = rollout(h, day(0), (opts){.id = aid.s});
    char *f3 = fork_of(h, id_of(a).s, -1, NULL);
    CHECK_INT(bases_of(h, f3), 0);
    free(f1);
    free(f2);
    free(f3);
    free(a);
    free(h);
}

/* The fd route through a faked tree: the rollout is held by a
 * descendant, not by the child itself. */
static const char *fake_held;

static size_t fake_tree(pid_t pid, pid_t *out, size_t max)
{
    size_t n = 0;
    pid_t tree[] = {pid, 1001, 1002};
    for (size_t i = 0; i < SK_LEN(tree) && n < max; i++)
        out[n++] = tree[i];
    return n;
}

static void fake_files(pid_t pid, sk_strv *paths)
{
    sk_strv_push(paths, "/dev/null");
    if (pid == 1002) {
        sk_buf deleted = {0};
        sk_buf_printf(&deleted, "%s (deleted)", fake_held);
        sk_strv_push(paths, deleted.ptr);
        sk_strv_push(paths, fake_held);
        sk_buf_free(&deleted);
    }
}

static void test_the_fd_route_looks_in_every_process_of_the_tree(void)
{
    char *h = home();
    double launched = now_ms() - 5000;
    char *f = rollout(h, day(0), (opts){.cwd = "/elsewhere", .stamp = launched + 1000});
    fake_held = f;
    sk_codex_disc *d = disc(h, NO_ARGV, launched);
    sk_codex_disc_set_procs(d, fake_tree, fake_files);
    const sk_codex_found *r = sk_codex_disc_find(d, 1000);
    check_found(r, f, "fd", true);
    ident id = id_of(f);
    CHECK_STR(r->thread, id.s);
    sk_codex_disc_free(d);
    free(f);
    free(h);
}

static void test_date_parse_reads_the_iso_format(void)
{
    struct {
        const char *s;
        double ms;
    } cases[] = {
        {"2026-10-11T19:06:16.907Z", 1791745576907.0},
        {"2026-10-11T19:06:16Z", 1791745576000.0},
        {"2026-10-11T19:06Z", 1791745560000.0},
        {"2026-10-11T21:06:16.907+02:00", 1791745576907.0},
        {"2026-10-11T19:06:16.9071234Z", 1791745576907.0},
        {"2026-10-11T19:06:16.9Z", 1791745576900.0},
        {"2026-10-11", 1791676800000.0},
        {"2026-10", 1790812800000.0},
        {"2026", 1767225600000.0},
        {"1970-01-01T00:00:00.000Z", 0},
        {"+002026-10-11T19:06:16.907Z", 1791745576907.0},
        {"2026-10-12T24:00:00Z", 1791849600000.0},
    };
    for (size_t i = 0; i < SK_LEN(cases); i++) {
        double got = sk_codex_date_parse(sk_str_c(cases[i].s));
        if (got != cases[i].ms)
            sk_test_fail(__FILE__, __LINE__, "%s: %.0f != %.0f", cases[i].s, got, cases[i].ms);
    }
    const char *bad[] = {"", "2026-13-01", "2026-10-32", "2026-10-11T25:00Z", "-000000-01-01",
                         "2026-10-11T24:00:01Z", "2026-10-11T19:60Z", "x"};
    for (size_t i = 0; i < SK_LEN(bad); i++)
        CHECK(isnan(sk_codex_date_parse(sk_str_c(bad[i]))));
    /* A time with no offset is local time. */
    struct tm tm;
    memset(&tm, 0, sizeof tm);
    tm.tm_year = 2026 - 1900;
    tm.tm_mon = 9;
    tm.tm_mday = 11;
    tm.tm_hour = 19;
    tm.tm_min = 6;
    tm.tm_isdst = -1;
    CHECK(sk_codex_date_parse(SK_STR("2026-10-11T19:06")) == (double)mktime(&tm) * 1000);
}

/* The adapter reads a fork's source to the byte it was forked at, as
 * history, then the fork's own lines (decision 0026). */
static void collect(void *ud, const char *msg)
{
    sk_strv_push(ud, msg);
}

static void test_the_adapter_reads_a_forks_source_first(void)
{
    char *h = home();
    ident aid = uuid(), bid = uuid();
    char *a = rollout(h, day(1), (opts){.id = aid.s});
    long long forked_at = size_of(a);
    /* The source goes on after the fork; that part is not ours. */
    FILE *more = fopen(a, "ab");
    CHECK(more != NULL);
    if (more) {
        fputs("{\"type\":\"event_msg\",\"payload\":{\"type\":\"task_started\"}}\n", more);
        fclose(more);
    }
    char *b = fork_of(h, aid.s, (double)forked_at, bid.s);
    sk_bus *bus = sk_bus_new(NULL, NULL);
    sk_envp env;
    sk_envp_from(&env, NULL);
    sk_envp_set(&env, "CODEX_HOME", h);
    sk_strv logs = {0};
    char *cmd[] = {"codex", "resume", bid.s, NULL};
    sk_agent_ctx ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.cmd = cmd;
    ctx.cwd = CWD;
    ctx.env = &env;
    ctx.bus = bus;
    ctx.log = collect;
    ctx.log_ud = &logs;
    sk_agent *ag = sk_agent_open(&ctx);
    CHECK_STR(sk_agent_kind(ag), "codex");
    sk_strv args = {0};
    char *notice = NULL;
    sk_agent_prepare(ag, &args, &notice);
    sk_agent_attach(ag, getpid());
    sk_agent_tick(ag, sk_now_ms());
    sk_report r;
    sk_agent_report(ag, &r);
    CHECK_STR(r.channels[0].name, "rollout");
    CHECK_STR(r.channels[0].target, b);
    CHECK_STR(r.channels[0].how, "argv");
    CHECK_STR(r.session, bid.s);
    CHECK(r.notice == NULL);
    CHECK(!r.replaying);
    const sk_val *prompts = sk_bus_get(bus, "event.prompt");
    CHECK(prompts && prompts->num == 2); /* one of the source's, one of the fork's */
    CHECK_INT(logs.len, 2);
    if (logs.len == 2)
        CHECK(strstr(logs.ptr[1], "continues 1 rollout(s), read first as history") != NULL);
    sk_agent_free(ag);
    sk_strv_free(&args);
    free(notice);
    sk_strv_free(&logs);
    sk_envp_free(&env);
    sk_bus_free(bus);
    free(a);
    free(b);
    free(h);
}

const sk_test sk_suite_codex_discovery[] = {
    T(test_t1_resume_id_finds_the_rollout_in_any_date_dir),
    T(test_t2_route_order_argv_then_fd_then_the_cwd_route),
    T(test_t3_a_subagent_rollout_is_never_chosen),
    T(test_t4_the_launch_cut),
    T(test_t5_a_miss_reports_none_and_the_routes_tried),
    T(test_t6_a_symlinked_codex_home_the_fd_route_finds_the_held_rollout),
    T(test_t7_dash_c_dir_finds_the_rollout_of_dir),
    T(test_t8_a_first_line_over_64_kib_is_read),
    T(test_t10_subagents_spawned_once_active_while_written_within_20_s),
    T(test_t9_a_subagent_in_another_date_dir_created_after_launch_is_counted),
    T(test_t11_a_subagent_of_our_subagent_is_not_counted),
    T(test_t12_fresh_an_older_session_written_after_launch_is_not_chosen),
    T(test_t18_fresh_created_before_launch_and_prompted_after_it_is_not_chosen),
    T(test_t13_fresh_of_two_stamped_after_launch_the_earlier_is_chosen),
    T(test_t14_resume_last_finds_a_rollout_10_days_back_appended_after_launch),
    T(test_t15_resume_last_a_fresh_unrelated_session_is_not_chosen),
    T(test_t16_resume_name_takes_the_resumed_route),
    T(test_t19_fork_id_the_source_is_not_followed_the_new_thread_is),
    T(test_t17_latch_after_a_hit_a_newer_candidate_does_not_change_the_answer),
    T(test_t20_a_fork_of_a_fork_its_sources_oldest_first),
    T(test_t21_a_missing_source_a_fork_naming_itself_or_a_bad_offset_give_none),
    T(test_the_fd_route_looks_in_every_process_of_the_tree),
    T(test_date_parse_reads_the_iso_format),
    T(test_the_adapter_reads_a_forks_source_first),
    {0},
};
