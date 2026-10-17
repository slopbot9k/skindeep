/* The skinc program: its summary of a skin that compiles, its messages
 * and status for one that does not, its usage, and the pack -o writes. */
#include "../../src/skin/pack.h"
#include "../test.h"

#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* bin with args, stdout and stderr to files in the test's directory;
 * the exit status, or -1. */
static int run(const char *bin, const char *const *args, char **out, char **err)
{
    const char *tmp = sk_test_tmpdir();
    sk_buf o = {0}, e = {0};
    sk_buf_printf(&o, "%s/out", tmp);
    sk_buf_printf(&e, "%s/err", tmp);
    const char *argv[8] = {bin};
    for (int i = 0; args[i] && i < 6; i++)
        argv[i + 1] = args[i];
    pid_t pid = fork();
    if (pid == 0) {
        int fo = open(o.ptr, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        int fe = open(e.ptr, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fo < 0 || fe < 0 || dup2(fo, 1) < 0 || dup2(fe, 2) < 0)
            _exit(126);
        execv(bin, (char *const *)argv);
        _exit(127);
    }
    int status = 0;
    if (pid < 0 || waitpid(pid, &status, 0) != pid)
        status = -1;
    *out = sk_test_read(o.ptr, NULL);
    *err = sk_test_read(e.ptr, NULL);
    sk_buf_free(&o);
    sk_buf_free(&e);
    return status >= 0 && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static const char *bin(void)
{
    const char *b = getenv("SK_SKINC_BIN");
    if (!b || access(b, X_OK) != 0) {
        printf("    (skipped: SK_SKINC_BIN not set)\n");
        return NULL;
    }
    return b;
}

static void test_a_skin_that_compiles(void)
{
    const char *b = bin();
    if (!b)
        return;
    char *out, *err;
    const char *args[] = {"skins/phosphor", NULL};
    CHECK_INT(run(b, args, &out, &err), 0);
    CHECK(out && strncmp(out, "skin phosphor 1.0.0 (skins/phosphor)\n", 37) == 0);
    CHECK(out && strstr(out, "manifest: 9 panels, 18 widgets, 1 splits, 1 fonts, 0 sounds\n"));
    CHECK(out && strstr(out, "states: "));
    CHECK_STR(err, "");
    free(out);
    free(err);
    const char *verbose[] = {"-v", "skins/phosphor", NULL};
    CHECK_INT(run(b, verbose, &out, &err), 0);
    CHECK(out && strstr(out, "div.panel.scanlines"));
    CHECK(out && strstr(out, "animation-timing-function: steps(40, jump-end);"));
    free(out);
    free(err);
}

static void test_a_skin_that_does_not(void)
{
    const char *b = bin();
    if (!b)
        return;
    char *out, *err;
    const char *args[] = {"tests/fixtures/skins/broken-css", NULL};
    CHECK_INT(run(b, args, &out, &err), 1);
    CHECK_STR(out, "");
    CHECK_STR(err, "tests/fixtures/skins/broken-css/skin.css: line 2: @import is not allowed\n"
                   "tests/fixtures/skins/broken-css/skin.css: line 3: url(https://example.org/x.png): "
                   "\"https://example.org/x.png\": must be a path relative to the skin\n");
    free(out);
    free(err);
    const char *none[] = {"no/such/skin", NULL};
    CHECK_INT(run(b, none, &out, &err), 1);
    CHECK_STR(err, "no/such/skin: skin: directory not found\n");
    free(out);
    free(err);
}

static void test_usage(void)
{
    const char *b = bin();
    if (!b)
        return;
    char *out, *err;
    const char *nothing[] = {NULL};
    CHECK_INT(run(b, nothing, &out, &err), 2);
    CHECK_STR(err, "usage: skinc [-v] DIR [-o PACK]\n");
    free(out);
    free(err);
    const char *two[] = {"a", "b", NULL};
    CHECK_INT(run(b, two, &out, &err), 2);
    free(out);
    free(err);
    const char *no_pack[] = {"skins/deck", "-o", NULL};
    CHECK_INT(run(b, no_pack, &out, &err), 2);
    free(out);
    free(err);
}

/* -o writes the pack, quietly, and nothing for a skin that fails. */
static void test_a_pack(void)
{
    const char *b = bin();
    if (!b)
        return;
    char *out, *err;
    sk_buf pack = {0};
    sk_buf_printf(&pack, "%s/phosphor.pack", sk_test_tmpdir());
    const char *args[] = {"skins/phosphor", "-o", pack.ptr, NULL};
    CHECK_INT(run(b, args, &out, &err), 0);
    CHECK_STR(out, "");
    CHECK_STR(err, "");
    free(out);
    free(err);
    size_t n = 0;
    char *bytes = sk_test_read(pack.ptr, &n);
    sk_pack p;
    char why[300];
    CHECK(bytes && sk_pack_load((const uint8_t *)bytes, n, &p, why, sizeof why));
    if (bytes)
        CHECK_STR(p.meta.name, "phosphor");
    sk_pack_free(&p);
    free(bytes);
    sk_buf_clear(&pack);
    sk_buf_printf(&pack, "%s/broken.pack", sk_test_tmpdir());
    const char *broken[] = {"tests/fixtures/skins/broken-css", "-o", pack.ptr, NULL};
    CHECK_INT(run(b, broken, &out, &err), 1);
    CHECK(access(pack.ptr, F_OK) != 0);
    free(out);
    free(err);
    sk_buf_clear(&pack);
    sk_buf_printf(&pack, "%s/no/such/dir/x.pack", sk_test_tmpdir());
    const char *nowhere[] = {"skins/phosphor", "-o", pack.ptr, NULL};
    CHECK_INT(run(b, nowhere, &out, &err), 1);
    CHECK(err && strncmp(err, "skinc: cannot write ", 20) == 0);
    free(out);
    free(err);
    sk_buf_free(&pack);
}

const sk_test sk_suite_skinc_program[] = {
    T(test_a_skin_that_compiles),
    T(test_a_skin_that_does_not),
    T(test_usage),
    T(test_a_pack),
    {0},
};
