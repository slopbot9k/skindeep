/* resolveCommand of app/cli.js, which no JavaScript test covers: the
 * cases follow its code, with Node's path.join and path.resolve. Run
 * from the repository root, where bin/skinned is executable. */
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

static void test_relative_entries(void)
{
    char *got = sk_resolve_command("skinned", "bin");
    CHECK_STR(got, "bin/skinned");
    free(got);
    got = sk_resolve_command("skinned", "./bin/");
    CHECK_STR(got, "bin/skinned");
    free(got);
    /* An empty entry is not the working directory. */
    CHECK(sk_resolve_command("install.sh", ":") == NULL);
    got = sk_resolve_command("install.sh", ".");
    CHECK_STR(got, "install.sh");
    free(got);
}

static void test_a_path_is_resolved(void)
{
    char here[1024], want[1100];
    CHECK(getcwd(here, sizeof here) != NULL);
    snprintf(want, sizeof want, "%s/bin/skinned", here);
    char *got = sk_resolve_command("bin/skinned", NULL);
    CHECK_STR(got, want);
    free(got);
    got = sk_resolve_command("./bin/../bin//skinned", "/nonexistent");
    CHECK_STR(got, want);
    free(got);
    got = sk_resolve_command("/bin/../bin//sh", NULL);
    CHECK_STR(got, "/bin/sh");
    free(got);
    CHECK(sk_resolve_command("/bin", NULL) == NULL); /* a directory */
    CHECK(sk_resolve_command("/nonexistent/sh", "/bin") == NULL);
    CHECK(sk_resolve_command("README.md", NULL) == NULL);
    CHECK(sk_resolve_command("./README.md", NULL) == NULL); /* not executable */
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
