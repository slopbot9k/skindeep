#include "../../src/base/paths.h"
#include "../test.h"

#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>

#define CHECK_PATH(expr, want)                                               \
    do {                                                                     \
        char *got_ = (expr);                                                 \
        CHECK_STR(got_, want);                                               \
        free(got_);                                                          \
    } while (0)

static void test_linux_defaults(void)
{
    sk_env e = {SK_OS_LINUX, "/home/u", NULL, NULL, NULL, NULL, 1000};
    CHECK_PATH(sk_path_state(&e), "/home/u/.local/state/claude-skins");
    CHECK_PATH(sk_path_cache(&e), "/home/u/.cache/claude-skins");
    CHECK_PATH(sk_path_runtime_base(&e), "/tmp/claude-skins-1000");
}

static void test_linux_xdg(void)
{
    sk_env e = {SK_OS_LINUX, "/home/u", "/s", "/c/", "/run/user/1000", "/t", 1000};
    CHECK_PATH(sk_path_state(&e), "/s/claude-skins");
    CHECK_PATH(sk_path_cache(&e), "/c/claude-skins");
    CHECK_PATH(sk_path_runtime_base(&e), "/run/user/1000/claude-skins");
    e.xdg_runtime_dir = "";
    CHECK_PATH(sk_path_runtime_base(&e), "/t/claude-skins-1000"); /* empty counts as unset */
}

static void test_macos(void)
{
    sk_env e = {SK_OS_MACOS, "/Users/u", NULL, NULL, NULL, "/var/folders/x/T/", 501};
    CHECK_PATH(sk_path_state(&e), "/Users/u/Library/Application Support/claude-skins");
    CHECK_PATH(sk_path_cache(&e), "/Users/u/Library/Caches/claude-skins");
    CHECK_PATH(sk_path_runtime_base(&e), "/var/folders/x/T/claude-skins-501");
    e.xdg_state_home = "/s";
    CHECK_PATH(sk_path_state(&e), "/s/claude-skins");
}

static void test_no_home(void)
{
    sk_env e = {SK_OS_LINUX, NULL, NULL, NULL, NULL, NULL, 7};
    CHECK(sk_path_state(&e) == NULL);
    CHECK(sk_path_cache(&e) == NULL);
    CHECK_PATH(sk_path_runtime_base(&e), "/tmp/claude-skins-7");
}

static void test_join(void)
{
    CHECK_PATH(sk_path_join("/a", "b"), "/a/b");
    CHECK_PATH(sk_path_join("/a/", "/b"), "/a/b");
    CHECK_PATH(sk_path_join("", "b"), "b");
}

static void test_current_env(void)
{
    sk_env e;
    sk_env_current(&e);
#ifdef __APPLE__
    CHECK_INT(e.os, SK_OS_MACOS);
#else
    CHECK_INT(e.os, SK_OS_LINUX);
#endif
    char *state = sk_path_state(&e);
    CHECK(state != NULL);
    free(state);
}

static void test_mkdirs(void)
{
    char *deep = sk_path_join(sk_test_tmpdir(), "a/b/c/");
    CHECK_INT(sk_mkdirs(deep, 0700), 0);
    CHECK_INT(sk_mkdirs(deep, 0700), 0); /* again: already there */
    struct stat st;
    CHECK(stat(deep, &st) == 0 && S_ISDIR(st.st_mode));
    CHECK_INT(st.st_mode & 0777, 0700);
    char *file = sk_path_join(sk_test_tmpdir(), "a/file");
    FILE *f = fopen(file, "w");
    CHECK(f != NULL);
    if (f)
        fclose(f);
    char *under = sk_path_join(file, "x");
    errno = 0;
    CHECK_INT(sk_mkdirs(under, 0700), -1);
    CHECK_INT(errno, ENOTDIR);
    errno = 0;
    CHECK_INT(sk_mkdirs("", 0700), -1);
    CHECK_INT(errno, ENOENT);
    free(deep);
    free(file);
    free(under);
}

const sk_test sk_suite_paths[] = {
    T(test_linux_defaults),
    T(test_linux_xdg),
    T(test_macos),
    T(test_no_home),
    T(test_join),
    T(test_current_env),
    T(test_mkdirs),
    {0},
};
