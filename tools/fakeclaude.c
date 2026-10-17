/* A replay fake named claude (decisions 0019, 0036), built as
 * build/<mode>/fake/claude: run as a tab's CLI, it finds the settings the
 * app injected and plays the recorded fixtures through the real relays,
 * as Claude Code would. The hook events of tests/fixtures/claude/
 * hooks.jsonl go through the hooks' command, statusline.json through the
 * statusLine command, and both name a copy of transcript-read.jsonl, so
 * the transcript tail has a file to follow. It spends no tokens.
 *
 *   PATH=build/release/fake:$PATH skinned --headless -- claude
 *   skinned --skin ~/my-skin -- build/release/fake/claude
 *
 * FAKE_CLAUDE_PAUSE: seconds between events (default 0.3).
 * FAKE_CLAUDE_LINGER: seconds it stays after the last (default 2).
 * FAKE_CLAUDE_FIXTURES: the fixtures' folder, else the checkout's. */
#include "../src/base/json.h"
#include "../src/base/paths.h"
#include "../src/sessions/files.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

/* The checkout's, as the build knows it. */
#ifndef SK_FAKE_FIXTURES
#define SK_FAKE_FIXTURES "tests/fixtures/claude"
#endif

#define CAP (16u << 20)

static void nap(double seconds)
{
    if (!(seconds > 0))
        return;
    struct timespec t = {(time_t)seconds, (long)((seconds - floor(seconds)) * 1e9)};
    while (nanosleep(&t, &t) != 0 && errno == EINTR)
        ;
}

static double seconds_of(const char *name, double dflt)
{
    const char *v = getenv(name);
    char *end;
    double d = v ? strtod(v, &end) : dflt;
    return v && (end == v || *end) ? dflt : d;
}

/* The command of the first hook and of the statusLine in the settings
 * the app wrote: one command serves every event. */
static bool commands(const char *settings, char **hook, char **status)
{
    sk_buf text = {0};
    *hook = *status = NULL;
    if (!sk_files_read(settings, CAP, &text))
        return false;
    sk_arena a;
    sk_arena_init(&a, 0);
    const sk_json *root = sk_json_parse(&a, text.ptr ? text.ptr : "", text.len, NULL);
    const sk_json *hooks = sk_json_get(root, "hooks");
    sk_str s = {0};
    if (hooks && hooks->type == SK_JSON_OBJECT && hooks->object.len &&
        sk_json_as_string(
            sk_json_get(sk_json_at(sk_json_get(sk_json_at(&hooks->object.members[0].value, 0), "hooks"), 0), "command"),
            &s))
        *hook = sk_xstrndup(s.ptr, s.len);
    if (sk_json_as_string(sk_json_dig(root, "statusLine", "command", NULL), &s))
        *status = sk_xstrndup(s.ptr, s.len);
    sk_arena_free(&a);
    sk_buf_free(&text);
    return *hook != NULL;
}

/* line with its transcript_path the copy's, into out. */
static void place(sk_str line, const char *transcript, sk_buf *out)
{
    sk_buf_clear(out);
    const char *key = "\"transcript_path\"";
    size_t klen = strlen(key);
    const char *at = NULL, *end = line.ptr + line.len;
    /* Within the line alone: the rest of the buffer holds the others. */
    for (const char *p = line.ptr; !at && p + klen <= end; p++)
        if (!memcmp(p, key, klen))
            at = p;
    if (at) {
        const char *p = at + strlen(key);
        while (p < end && (*p == ' ' || *p == ':'))
            p++;
        if (p < end && *p == '"') {
            const char *q = p + 1;
            while (q < end && *q != '"')
                q += *q == '\\' && q + 1 < end ? 2 : 1;
            if (q < end) {
                sk_buf_append(out, line.ptr, (size_t)(p - line.ptr));
                sk_jw w;
                sk_jw_init(&w, out, 0);
                sk_jw_string(&w, sk_str_c(transcript));
                sk_buf_append(out, q + 1, (size_t)(end - q - 1));
                return;
            }
        }
    }
    sk_buf_append_str(out, line);
}

/* cmd run by sh with input on its stdin, as Claude Code runs a hook; its
 * stdout dropped when quiet. */
static void run(const char *cmd, const sk_buf *input, bool quiet)
{
    int p[2];
    if (pipe(p) != 0)
        return;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, p[0], 0);
    posix_spawn_file_actions_addclose(&fa, p[1]);
    if (quiet)
        posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    char *argv[] = {"sh", "-c", (char *)cmd, NULL};
    pid_t pid;
    int e = posix_spawn(&pid, "/bin/sh", &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(p[0]);
    for (size_t done = 0; !e && done < input->len;) {
        ssize_t w = write(p[1], input->ptr + done, input->len - done);
        if (w < 0 && errno == EINTR)
            continue;
        if (w <= 0)
            break;
        done += (size_t)w;
    }
    close(p[1]);
    while (!e && waitpid(pid, NULL, 0) < 0 && errno == EINTR)
        ;
}

/* The event a hook line names, into out. */
static void event_of(sk_str line, sk_buf *out)
{
    sk_buf_clear(out);
    sk_arena a;
    sk_arena_init(&a, 0);
    sk_str s = {0};
    if (sk_json_as_string(sk_json_get(sk_json_parse(&a, line.ptr, line.len, NULL), "hook_event_name"), &s))
        sk_buf_append_str(out, s);
    sk_arena_free(&a);
}

int main(int argc, char **argv)
{
    const char *settings = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--settings") && i + 1 < argc)
            settings = argv[++i];
        else if (!strncmp(argv[i], "--settings=", 11))
            settings = argv[i] + 11;
    }
    char *hook, *status;
    if (!settings || !commands(settings, &hook, &status)) {
        printf("fake claude: no --settings given, nothing to relay\n");
        return 0;
    }
    const char *dir = getenv("FAKE_CLAUDE_FIXTURES");
    dir = dir && *dir ? dir : SK_FAKE_FIXTURES;
    char *hooks_path = sk_path_join(dir, "hooks.jsonl"), *line_path = sk_path_join(dir, "statusline.json");
    char *read_path = sk_path_join(dir, "transcript-read.jsonl");
    sk_buf hooks = {0}, line = {0}, transcript = {0}, placed = {0}, name = {0};
    if (!sk_files_read(hooks_path, CAP, &hooks) || !sk_files_read(line_path, CAP, &line) ||
        !sk_files_read(read_path, CAP, &transcript)) {
        fprintf(stderr, "fake claude: cannot read the fixtures in %s\n", dir);
        return 1;
    }
    /* A copy of the transcript the payloads name. */
    const char *tmp = getenv("TMPDIR");
    sk_buf tdir = {0};
    sk_buf_printf(&tdir, "%s/fake-claude.XXXXXX", tmp && *tmp ? tmp : "/tmp");
    if (!mkdtemp(tdir.ptr)) {
        fprintf(stderr, "fake claude: cannot make a folder: %s\n", strerror(errno));
        return 1;
    }
    char *copy = sk_path_join(tdir.ptr, "transcript.jsonl");
    FILE *f = fopen(copy, "w");
    if (f) {
        fwrite(transcript.ptr, 1, transcript.len, f);
        fclose(f);
    }
    double pause = seconds_of("FAKE_CLAUDE_PAUSE", 0.3), linger = seconds_of("FAKE_CLAUDE_LINGER", 2);

    printf("\033[1mfake claude\033[0m: replaying the fixtures through %s\r\n", hook);
    fflush(stdout);
    place(sk_buf_str(&line), copy, &placed);
    if (status)
        run(status, &placed, true);
    for (size_t at = 0; at < hooks.len;) {
        const char *nl = memchr(hooks.ptr + at, '\n', hooks.len - at);
        size_t end = nl ? (size_t)(nl - hooks.ptr) : hooks.len;
        sk_str one = sk_str_n(hooks.ptr + at, end - at);
        at = nl ? end + 1 : hooks.len;
        if (!one.len)
            continue;
        event_of(one, &name);
        printf("  %s\r\n", sk_buf_cstr(&name));
        fflush(stdout);
        place(one, copy, &placed);
        run(hook, &placed, false);
        nap(pause);
    }
    place(sk_buf_str(&line), copy, &placed);
    if (status)
        run(status, &placed, true);
    printf("fake claude: done\r\n");
    fflush(stdout);
    nap(linger);
    unlink(copy);
    rmdir(tdir.ptr);
    free(copy);
    sk_buf_free(&tdir);
    sk_buf_free(&hooks);
    sk_buf_free(&line);
    sk_buf_free(&transcript);
    sk_buf_free(&placed);
    sk_buf_free(&name);
    free(hooks_path);
    free(line_path);
    free(read_path);
    free(hook);
    free(status);
    return 0;
}
