/* Finding the sessions the app did not start (decision 0027), as
 * test/sessions.test.js has it: the transcript of a Claude session on
 * temp profiles, with this test's own process as the session (R1-R5).
 * Then the scan, on children run under the names of the agents, the
 * ancestry, the start check, Claude Code's procStart on both systems,
 * the Codex rollout through a fake discovery, and the state. */
#include "../../src/core/agentdirs.h"
#include "../../src/platform/procinfo.h"
#include "../../src/sessions/sessions.h"
#include "../test.h"

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CWD "/work/shared"
#define ID_A "aaaaaaaa-0000-4000-8000-000000000001"
#define ID_B "bbbbbbbb-0000-4000-8000-000000000002"

/* The test's folder as realpath has it, as the sessions name files. */
static char *real_tmp(void)
{
    char *r = realpath(sk_test_tmpdir(), NULL);
    CHECK(r != NULL);
    return r;
}

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

static char *join3(const char *a, const char *b, const char *c)
{
    sk_buf p = {0};
    sk_buf_printf(&p, "%s/%s/%s", a, b, c);
    return p.ptr;
}

static char *slug_dir(const char *p)
{
    sk_buf s = {0};
    sk_claude_slug(&s, SK_STR(CWD));
    char *d = join3(p, "projects", s.ptr);
    sk_buf_free(&s);
    return d;
}

/* A profile with a projects folder for CWD, its sessions folder, and
 * session-env folders for the ids given. */
static char *profile(const char *root, const char *name, const char *const *ids)
{
    char *p = sk_path_join(root, name);
    char *d = slug_dir(p);
    CHECK(sk_mkdirs(d, 0777) == 0);
    free(d);
    d = sk_path_join(p, "sessions");
    CHECK(sk_mkdirs(d, 0777) == 0);
    free(d);
    for (; ids && *ids; ids++) {
        d = join3(p, "session-env", *ids);
        CHECK(sk_mkdirs(d, 0777) == 0);
        free(d);
    }
    return p;
}

static double now_s(void)
{
    return (double)time(NULL);
}

static char *transcript(const char *p, const char *id, double mtime_s)
{
    char *d = slug_dir(p);
    sk_buf f = {0};
    sk_buf_printf(&f, "%s/%s.jsonl", d, id);
    free(d);
    write_file(f.ptr, "{}\n");
    struct timespec ts[2] = {{(time_t)mtime_s, 0}, {(time_t)mtime_s, 0}};
    CHECK(utimensat(AT_FDCWD, f.ptr, ts, 0) == 0);
    return f.ptr;
}

/* This process's procStart as Claude Code writes it, moved by skew
 * units of the system's start (a tick, or a second). */
static void proc_start(sk_buf *out, int skew)
{
    sk_procinfo me;
    CHECK(sk_procinfo_read(getpid(), &me));
    sk_os os = sk_os_current();
    uint64_t unit = os == SK_OS_LINUX ? 1 : 1000000;
    sk_claude_proc_start(out, os, me.start + (uint64_t)((int64_t)skew * (int64_t)unit));
}

/* Claude Code's record of this process, with fields it keeps that the
 * app must not. */
static void record(const char *p, const char *session, int skew)
{
    sk_buf start = {0}, text = {0}, path = {0};
    proc_start(&start, skew);
    sk_buf_printf(&text,
                  "{\"pid\":%ld,\"sessionId\":\"%s\",\"cwd\":\"" CWD "\",\"procStart\":\"%s\","
                  "\"bridgeSessionId\":\"kept-nowhere\",\"messagingSocketPath\":\"/x.sock\"}",
                  (long)getpid(), session, start.ptr);
    sk_buf_printf(&path, "%s/sessions/%ld.json", p, (long)getpid());
    write_file(path.ptr, text.ptr);
    sk_buf_free(&start);
    sk_buf_free(&text);
    sk_buf_free(&path);
}

static double started(void)
{
    return (now_s() - 60) * 1000;
}

static void test_r1_the_record_names_the_session_not_the_newest_of_its_folder(void)
{
    char *root = real_tmp();
    char *p = profile(root, "lab", NULL);
    char *mine = transcript(p, ID_A, now_s() - 30);
    free(transcript(p, ID_B, now_s() - 1));
    record(p, ID_A, 0);
    char *profiles[] = {p, NULL};
    sk_session_channel c;
    CHECK(sk_sessions_claude_transcript(getpid(), CWD, started(), profiles, &c));
    CHECK_STR(c.file, mine);
    CHECK_STR(c.session, ID_A);
    CHECK_STR(c.profile, p);
    sk_session_channel_free(&c);
    free(mine);
    free(p);
    free(root);
}

static void test_r2_a_record_of_another_process_with_this_pid_is_ignored(void)
{
    char *root = real_tmp();
    char *p = profile(root, "lab", NULL);
    free(transcript(p, ID_A, now_s() - 30));
    char *newest = transcript(p, ID_B, now_s() - 1);
    record(p, ID_A, -1);
    char *profiles[] = {p, NULL};
    sk_claude_record r;
    CHECK(!sk_sessions_claude_record(getpid(), profiles, &r));
    sk_session_channel c;
    CHECK(sk_sessions_claude_transcript(getpid(), CWD, started(), profiles, &c));
    CHECK_STR(c.file, newest);
    CHECK_STR(c.session, ID_B);
    sk_session_channel_free(&c);
    free(newest);
    free(p);
    free(root);
}

static void test_r3_a_session_named_before_its_transcript_is_written(void)
{
    char *root = real_tmp();
    char *p = profile(root, "lab", NULL);
    free(transcript(p, ID_B, now_s() - 1));
    record(p, ID_A, 0);
    char *profiles[] = {p, NULL};
    sk_session_channel c;
    CHECK(sk_sessions_claude_transcript(getpid(), CWD, started(), profiles, &c));
    CHECK(c.file == NULL);
    CHECK_STR(c.session, ID_A);
    CHECK(c.mtime == 0);
    CHECK_STR(c.profile, p);
    sk_session_channel_free(&c);
    free(p);
    free(root);
}

static void test_r4_a_shared_sessions_folder_the_profile_holds_session_env(void)
{
    char *root = real_tmp();
    char *a = profile(root, "a", (const char *[]){ID_A, NULL});
    char *b = profile(root, "b", NULL);
    char *bs = sk_path_join(b, "sessions"), *as = sk_path_join(a, "sessions");
    CHECK(rmdir(bs) == 0);
    CHECK(symlink(as, bs) == 0);
    record(a, ID_A, 0);
    char *profiles[] = {a, b, NULL};
    sk_claude_record r;
    CHECK(sk_sessions_claude_record(getpid(), profiles, &r));
    CHECK_STR(r.session, ID_A);
    CHECK_INT(r.profiles.len, 2);
    sk_claude_record_free(&r);
    sk_session_channel c;
    CHECK(sk_sessions_claude_transcript(getpid(), CWD, started(), profiles, &c));
    CHECK_STR(c.profile, a);
    sk_session_channel_free(&c);
    free(as);
    free(bs);
    free(a);
    free(b);
    free(root);
}

static void test_r5_a_session_id_that_is_no_plain_name_is_refused(void)
{
    char *root = real_tmp();
    char *p = profile(root, "lab", NULL);
    record(p, "../../etc/passwd", 0);
    char *profiles[] = {p, NULL};
    sk_claude_record r;
    CHECK(!sk_sessions_claude_record(getpid(), profiles, &r));
    free(p);
    free(root);
}

static void test_a_record_that_is_not_json_or_names_another_pid_is_ignored(void)
{
    char *root = real_tmp();
    char *p = profile(root, "lab", NULL);
    char *profiles[] = {p, NULL};
    sk_buf path = {0}, start = {0}, text = {0};
    sk_buf_printf(&path, "%s/sessions/%ld.json", p, (long)getpid());
    proc_start(&start, 0);
    sk_claude_record r;
    write_file(path.ptr, "{\"pid\":");
    CHECK(!sk_sessions_claude_record(getpid(), profiles, &r));
    sk_buf_printf(&text, "{\"pid\":%ld,\"sessionId\":\"" ID_A "\",\"procStart\":\"%s\"}", (long)getpid() + 1,
                  start.ptr);
    write_file(path.ptr, text.ptr);
    CHECK(!sk_sessions_claude_record(getpid(), profiles, &r));
    /* A repeated key holds its last value, as JSON.parse keeps it. */
    sk_buf_clear(&text);
    sk_buf_printf(&text, "{\"pid\":\"x\",\"sessionId\":\"../x\",\"procStart\":\"%s\",\"pid\":%ld,\"sessionId\":\"" ID_A "\"}",
                  start.ptr, (long)getpid());
    write_file(path.ptr, text.ptr);
    CHECK(sk_sessions_claude_record(getpid(), profiles, &r) && !strcmp(r.session, ID_A));
    sk_claude_record_free(&r);
    /* On Linux a procStart written as a number counts, as String() of
     * it is the same digits. */
    if (sk_os_current() == SK_OS_LINUX) {
        sk_buf_clear(&text);
        sk_buf_printf(&text, "{\"pid\":%ld,\"sessionId\":\"" ID_A "\",\"procStart\":%s}", (long)getpid(), start.ptr);
        write_file(path.ptr, text.ptr);
        CHECK(sk_sessions_claude_record(getpid(), profiles, &r));
        sk_claude_record_free(&r);
    }
    sk_buf_free(&path);
    sk_buf_free(&start);
    sk_buf_free(&text);
    free(p);
    free(root);
}

static void test_claude_proc_start_on_each_system(void)
{
    sk_buf b = {0};
    sk_claude_proc_start(&b, SK_OS_LINUX, 123456);
    CHECK_STR(b.ptr, "123456");
    CHECK(sk_claude_proc_start_is(SK_STR("123456"), SK_OS_LINUX, 123456));
    CHECK(!sk_claude_proc_start_is(SK_STR("123455"), SK_OS_LINUX, 123456));
    CHECK(!sk_claude_proc_start_is(SK_STR(" 123456"), SK_OS_LINUX, 123456));
    /* Claude Code 2.1.301 on macOS for a process started at
     * 1792161882.123456 s (measured 2026-10-16). */
    sk_buf_clear(&b);
    sk_claude_proc_start(&b, SK_OS_MACOS, 1792161882123456u);
    CHECK_STR(b.ptr, "Fri Oct 16 14:44:42 2026");
    CHECK(sk_claude_proc_start_is(SK_STR("Fri Oct 16 14:44:42 2026"), SK_OS_MACOS, 1792161882123456u));
    CHECK(!sk_claude_proc_start_is(SK_STR("Fri Oct 16 14:44:41 2026"), SK_OS_MACOS, 1792161882123456u));
    CHECK(!sk_claude_proc_start_is(SK_STR(""), SK_OS_MACOS, 1792161882123456u));
    sk_buf_clear(&b);
    sk_claude_proc_start(&b, SK_OS_MACOS, 1791556747000000u);
    CHECK_STR(b.ptr, "Fri Oct  9 14:39:07 2026");
    CHECK(sk_claude_proc_start_is(SK_STR("Fri Oct 9 14:39:07 2026"), SK_OS_MACOS, 1791556747000000u));
    CHECK(sk_claude_proc_start_is(SK_STR("Fri Oct  9 14:39:07 2026"), SK_OS_MACOS, 1791556747000000u));
    sk_buf_free(&b);
}

/* A shell that execs nothing more: macOS's /bin/sh runs bash in its
 * place, which renames the process. */
static const char *shell(void)
{
    return access("/bin/bash", X_OK) == 0 ? "/bin/bash" : "/bin/sh";
}

typedef struct {
    pid_t pid;
    int hold;
} child;

/* dir/bin/<name>, a link to a shell, run in dir with argv; it says
 * ready, then waits on its stdin. */
static child run_as(const char *dir, const char *name, char *const *argv)
{
    char *bin = sk_path_join(dir, "bin");
    sk_mkdirs(bin, 0777);
    char *link = sk_path_join(bin, name);
    if (access(link, F_OK) != 0)
        CHECK(symlink(shell(), link) == 0);
    int ready[2], in[2];
    CHECK(pipe(ready) == 0 && pipe(in) == 0);
    /* The children after this one must not hold its stdin open. */
    fcntl(in[1], F_SETFD, FD_CLOEXEC);
    fcntl(ready[0], F_SETFD, FD_CLOEXEC);
    child c = {fork(), in[1]};
    if (c.pid == 0) {
        if (chdir(dir) != 0 || dup2(in[0], 0) < 0 || dup2(ready[1], 1) < 0)
            _exit(127);
        close(in[1]);
        close(ready[0]);
        execv(link, argv);
        _exit(127);
    }
    close(in[0]);
    close(ready[1]);
    char buf[8];
    CHECK_INT(read(ready[0], buf, sizeof buf), 6);
    close(ready[0]);
    free(link);
    free(bin);
    return c;
}

static void stop(child c)
{
    close(c.hold);
    waitpid(c.pid, NULL, 0);
}

static const sk_session *find(const sk_sessions *all, pid_t pid)
{
    for (size_t i = 0; i < all->len; i++)
        if (all->ptr[i].pid == pid)
            return &all->ptr[i];
    return NULL;
}

static void test_the_scan_agents_by_name_or_script_not_daemons_nor_inner_processes(void)
{
    char *dir = real_tmp();
    char *script_dir = sk_path_join(dir, "js");
    CHECK(sk_mkdirs(script_dir, 0777) == 0);
    char *script = sk_path_join(script_dir, "claude");
    write_file(script, "echo ready; read x\n");
    char *codex = join3(dir, "bin", "codex");
    sk_buf nested = {0};
    sk_buf_printf(&nested, "'%s' -c 'echo ready; read x'; exit 0", codex);

    child claude = run_as(dir, "claude", (char *[]){"claude", "-c", "echo ready; read x", "--model", "opus", NULL});
    child daemon = run_as(dir, "codex", (char *[]){"codex", "-c", "echo ready; read x", "app-server", NULL});
    child outer = run_as(dir, "codex", (char *[]){"codex", "-c", nested.ptr, NULL});
    child npm = run_as(dir, "node", (char *[]){"node", script, NULL});
    child other = run_as(dir, "bash-not-an-agent", (char *[]){"claude", "-c", "echo ready; read x", NULL});

    sk_sessions all = {0};
    sk_sessions_scan(geteuid(), &all);
    const sk_session *s = find(&all, claude.pid);
    CHECK(s && s->kind == SK_PROFILE_CLAUDE && s->ppid == getpid());
    if (s) {
        CHECK_STR(s->cwd, dir);
        CHECK_INT(s->argv.len, 5);
        CHECK_STR(s->argv.ptr[4], "opus");
        CHECK(sk_sessions_same(s->pid, s->started));
        CHECK(!sk_sessions_same(s->pid, s->started - 5000));
    }
    CHECK(find(&all, daemon.pid) == NULL);
    s = find(&all, outer.pid);
    CHECK(s && s->kind == SK_PROFILE_CODEX);
    for (size_t i = 0; i < all.len; i++)
        CHECK(all.ptr[i].ppid != outer.pid);
    s = find(&all, npm.pid);
    CHECK(s && s->kind == SK_PROFILE_CLAUDE);
    CHECK(find(&all, other.pid) == NULL);
    for (size_t i = 1; i < all.len; i++)
        CHECK(all.ptr[i - 1].started >= all.ptr[i].started);

    pid_t up[8];
    size_t n = sk_sessions_ancestors(claude.pid, up, SK_LEN(up));
    CHECK(n >= 2);
    CHECK_INT(up[0], getpid());
    CHECK_INT(up[1], getppid());
    CHECK_INT(sk_sessions_ancestors(claude.pid, up, 1), 1);

    sk_sessions_free(&all);
    stop(claude);
    stop(daemon);
    stop(outer);
    stop(npm);
    stop(other);
    CHECK(!sk_sessions_same(claude.pid, 0));
    CHECK_INT(sk_sessions_ancestors(claude.pid, up, SK_LEN(up)), 0);
    sk_buf_free(&nested);
    free(codex);
    free(script);
    free(script_dir);
    free(dir);
}

/* The fake discovery: a hit only in the home named by ud. */
typedef struct {
    const char *hit_home, *file;
    int calls;
    sk_buf seen;
} fake_discovery;

static bool fake_find(void *ud, pid_t pid, const char *home, char *const *args, const char *cwd,
                      double launched, sk_codex_rollout *out)
{
    fake_discovery *f = ud;
    f->calls++;
    sk_buf_printf(&f->seen, "%ld %s %s %.0f:", (long)pid, home, cwd, launched);
    for (; *args; args++)
        sk_buf_printf(&f->seen, " %s", *args);
    sk_buf_putc(&f->seen, '\n');
    if (strcmp(home, f->hit_home))
        return false;
    out->file = sk_xstrdup(f->file);
    out->thread = sk_xstrdup("t-1");
    out->how = "new";
    return true;
}

static void test_codex_rollout_one_try_per_sessions_folder_through_the_discovery(void)
{
    char *root = real_tmp();
    char *h1 = sk_path_join(root, "h1"), *h2 = sk_path_join(root, "h2"), *h3 = sk_path_join(root, "h3");
    char *s1 = sk_path_join(h1, "sessions"), *s2 = sk_path_join(h2, "sessions"), *s3 = sk_path_join(h3, "sessions");
    CHECK(sk_mkdirs(s1, 0777) == 0 && sk_mkdirs(h2, 0777) == 0 && sk_mkdirs(s3, 0777) == 0);
    CHECK(symlink(s1, s2) == 0);
    char *file = sk_path_join(s3, "rollout.jsonl");
    write_file(file, "{}\n");
    struct timespec ts[2] = {{1000, 0}, {1000, 0}};
    CHECK(utimensat(AT_FDCWD, file, ts, 0) == 0);

    sk_session s = {4242, 1, SK_PROFILE_CODEX, 5000, "/w", {0}};
    sk_strv_push(&s.argv, "codex");
    sk_strv_push(&s.argv, "resume");
    char *homes[] = {h1, h2, h3, NULL};
    fake_discovery f = {h3, file, 0, {0}};
    sk_session_channel c;
    CHECK(sk_sessions_codex_rollout(&s, homes, fake_find, &f, &c));
    CHECK_INT(f.calls, 2);
    sk_buf want = {0};
    sk_buf_printf(&want, "4242 %s /w 5000: resume\n4242 %s /w 5000: resume\n", h1, h3);
    CHECK_STR(sk_buf_cstr(&f.seen), want.ptr);
    CHECK_STR(c.file, file);
    CHECK_STR(c.session, "t-1");
    CHECK(c.mtime == 1000000);
    CHECK_STR(c.profile, h3);
    CHECK_STR(c.home, h3);
    CHECK_STR(c.how, "new");
    sk_session_channel_free(&c);

    /* A hit in a folder two homes share names no profile. */
    f.hit_home = h1;
    CHECK(sk_sessions_codex_rollout(&s, homes, fake_find, &f, &c));
    CHECK(c.profile == NULL);
    CHECK_STR(c.home, h1);
    sk_session_channel_free(&c);
    f.hit_home = "/nowhere";
    CHECK(!sk_sessions_codex_rollout(&s, homes, fake_find, &f, &c));

    sk_buf_free(&want);
    sk_buf_free(&f.seen);
    sk_strv_free(&s.argv);
    free(file);
    free(s1);
    free(s2);
    free(s3);
    free(h1);
    free(h2);
    free(h3);
    free(root);
}

static void test_state_from_when_the_channel_was_written(void)
{
    CHECK_STR(sk_sessions_state_of(0, 100000), "unknown");
    CHECK_STR(sk_sessions_state_of(95000, 100000), "working");
    CHECK_STR(sk_sessions_state_of(90000, 100000), "waiting");
    CHECK_STR(sk_sessions_state_of(1000, 700000), "idle");
}

const sk_test sk_suite_sessions[] = {
    T(test_r1_the_record_names_the_session_not_the_newest_of_its_folder),
    T(test_r2_a_record_of_another_process_with_this_pid_is_ignored),
    T(test_r3_a_session_named_before_its_transcript_is_written),
    T(test_r4_a_shared_sessions_folder_the_profile_holds_session_env),
    T(test_r5_a_session_id_that_is_no_plain_name_is_refused),
    T(test_a_record_that_is_not_json_or_names_another_pid_is_ignored),
    T(test_claude_proc_start_on_each_system),
    T(test_the_scan_agents_by_name_or_script_not_daemons_nor_inner_processes),
    T(test_codex_rollout_one_try_per_sessions_folder_through_the_discovery),
    T(test_state_from_when_the_channel_was_written),
    {0},
};
