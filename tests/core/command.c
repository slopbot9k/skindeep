/* resolveCommand of the JavaScript app's cli.js, which no JavaScript
 * test covered: the cases follow its code, with Node's path.join and
 * path.resolve. */
#include "../../src/core/command.h"
#include "../test.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static void put(const char *dir, const char *name, mode_t mode)
{
    char path[600];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    if (f) {
        fputs("#!/bin/sh\n", f);
        fclose(f);
    }
    CHECK(chmod(path, mode) == 0);
}

/* The temp dir without the doubled slash TMPDIR's own trailing one
 * gives it, as the resolved path has it. */
static void collapse(char *p)
{
    char *w = p;
    for (const char *r = p; *r; r++)
        if (!(*r == '/' && r[1] == '/'))
            *w++ = *r;
    *w = 0;
}

static void test_found_on_path(void)
{
    const char *tmp = sk_test_tmpdir();
    char a[600], b[600], c[600], d[600], path[3000], want[700];
    snprintf(a, sizeof a, "%s/a", tmp);
    snprintf(b, sizeof b, "%s/b", tmp);
    snprintf(c, sizeof c, "%s/c", tmp);
    snprintf(d, sizeof d, "%s/d", tmp);
    mkdir(a, 0700);
    mkdir(b, 0700);
    mkdir(c, 0700);
    mkdir(d, 0700);
    snprintf(path, sizeof path, "%s/tool", a);
    mkdir(path, 0700);              /* a directory is no command */
    put(b, "tool", 0644);           /* nor is a file without x */
    put(c, "tool", 0755);
    put(d, "tool", 0755);
    /* Empty entries are skipped, and the first match wins, normalized. */
    snprintf(path, sizeof path, "%s::%s:%s//c/./:%s", a, b, tmp, d);
    snprintf(want, sizeof want, "%s/c/tool", tmp);
    collapse(want);
    char *got = sk_resolve_command("tool", path);
    CHECK_STR(got, want);
    free(got);
    snprintf(path, sizeof path, "%s:%s", a, b);
    CHECK(sk_resolve_command("tool", path) == NULL);
    CHECK(sk_resolve_command("tool", NULL) == NULL);
    CHECK(sk_resolve_command("", "/bin") == NULL);
}

/* Entries relative to the working directory, made the test's own folder
 * for the while: bin/tool, and top beside it. */
static void test_relative_entries(void)
{
    char cwd[4096], dir[600], bin[700];
    CHECK(getcwd(cwd, sizeof cwd) != NULL);
    snprintf(dir, sizeof dir, "%s/rel", sk_test_tmpdir());
    snprintf(bin, sizeof bin, "%s/bin", dir);
    mkdir(dir, 0700);
    mkdir(bin, 0700);
    put(bin, "tool", 0755);
    put(dir, "top", 0755);
    CHECK(chdir(dir) == 0);
    char *got = sk_resolve_command("tool", "bin");
    CHECK_STR(got, "bin/tool");
    free(got);
    got = sk_resolve_command("tool", "./bin/");
    CHECK_STR(got, "bin/tool");
    free(got);
    /* An empty entry is not the working directory. */
    CHECK(sk_resolve_command("top", ":") == NULL);
    got = sk_resolve_command("top", ".");
    CHECK_STR(got, "top");
    free(got);
    CHECK(chdir(cwd) == 0);
}

/* A path with a slash is the file, resolved, PATH not looked at: from
 * the test's own folder, made the working directory for the while. */
static void test_a_path_is_resolved(void)
{
    char cwd[4096], dir[600], bin[700], here[1024], want[1100];
    CHECK(getcwd(cwd, sizeof cwd) != NULL);
    snprintf(dir, sizeof dir, "%s/abs", sk_test_tmpdir());
    snprintf(bin, sizeof bin, "%s/bin", dir);
    mkdir(dir, 0700);
    mkdir(bin, 0700);
    put(bin, "tool", 0755);
    put(dir, "notes.md", 0644);
    CHECK(chdir(dir) == 0);
    CHECK(getcwd(here, sizeof here) != NULL);
    snprintf(want, sizeof want, "%s/bin/tool", here);
    char *got = sk_resolve_command("bin/tool", NULL);
    CHECK_STR(got, want);
    free(got);
    got = sk_resolve_command("./bin/../bin//tool", "/nonexistent");
    CHECK_STR(got, want);
    free(got);
    got = sk_resolve_command("/bin/../bin//sh", NULL);
    CHECK_STR(got, "/bin/sh");
    free(got);
    CHECK(sk_resolve_command("/bin", NULL) == NULL); /* a directory */
    CHECK(sk_resolve_command("/nonexistent/sh", "/bin") == NULL);
    CHECK(sk_resolve_command("notes.md", NULL) == NULL);
    CHECK(sk_resolve_command("./notes.md", NULL) == NULL); /* not executable */
    CHECK(chdir(cwd) == 0);
}

static void test_exit_codes(void)
{
    CHECK_INT(SK_EXIT_APP, 125);
    CHECK_INT(SK_EXIT_CANNOT_EXEC, 126);
    CHECK_INT(SK_EXIT_NOT_FOUND, 127);
}

const sk_test sk_suite_command[] = {
    T(test_found_on_path),
    T(test_relative_entries),
    T(test_a_path_is_resolved),
    T(test_exit_codes),
    {0},
};
