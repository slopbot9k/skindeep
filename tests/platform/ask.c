/* The question of a Linux desktop (platform/x11/ask.h): the command
 * lines of zenity and kdialog, what their answers say, and whole runs
 * with fake ones written in sh, on every system. */
#include "../../src/platform/x11/ask.h"
#include "../../src/base/paths.h"
#include "../../src/sessions/files.h"
#include "../test.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

static const char *const SKINS[] = {"deck", "~/skins/coffee"};

static char *joined(const sk_strv *v)
{
    sk_buf b = {0};
    sk_buf_reserve(&b, 0);
    for (size_t i = 0; i < v->len; i++)
        sk_buf_printf(&b, "%s[%s]", i ? " " : "", v->ptr[i]);
    return b.ptr;
}

static void check_argv(sk_picker_prog prog, const char *want)
{
    sk_strv v = {0};
    sk_ask_argv(prog, "T", "Which?", "Pick one.", SKINS, 2, &v);
    char *got = joined(&v);
    CHECK_STR(got, want);
    free(got);
    sk_strv_free(&v);
}

static void test_the_command_lines(void)
{
    check_argv(SK_PICKER_ZENITY, "[zenity] [--list] [--radiolist] [--title=T] [--text=Which?\n\nPick one.] "
                                 "[--column=] [--column=] [--column=Skin] [--hide-column=2] [--print-column=2] "
                                 "[--hide-header] [--width=460] [--height=280] "
                                 "[TRUE] [0] [deck] [FALSE] [1] [~/skins/coffee]");
    check_argv(SK_PICKER_KDIALOG, "[kdialog] [--title] [T] [--radiolist] [Which?\n\nPick one.] "
                                  "[0] [deck] [on] [1] [~/skins/coffee] [off]");
}

/* A wait status as waitpid gives it. */
static int exited(int code)
{
    return (code & 0xff) << 8;
}

static int answer(const char *out, int wstatus, size_t n)
{
    return sk_ask_answer(out, strlen(out), wstatus, n);
}

static void test_the_answers(void)
{
    CHECK_INT(answer("1\n", exited(0), 2), 1);
    CHECK_INT(answer("0\n", exited(0), 2), 0);
    CHECK_INT(answer("12\r\n", exited(0), 13), 12);
    CHECK_INT(answer("2", exited(0), 3), 2);
    /* A cancel, a failure, a death: the first. */
    CHECK_INT(answer("", exited(1), 2), 0);
    CHECK_INT(answer("1\n", exited(5), 2), 0);
    CHECK_INT(answer("1\n", 9, 2), 0);
    /* OK with nothing chosen, or what names no choice. */
    CHECK_INT(answer("", exited(0), 2), 0);
    CHECK_INT(answer("2\n", exited(0), 2), 0);
    CHECK_INT(answer("99999999999999999999999\n", exited(0), 2), 0);
    CHECK_INT(answer("deck\n", exited(0), 2), 0);
    CHECK_INT(answer("1|0\n", exited(0), 2), 0);
    CHECK_INT(answer("-1\n", exited(0), 2), 0);
}

/* <tmp>/name, made, normalized, as TMPDIR may end in a slash. */
static char *dir_in(const char *name)
{
    char *j = sk_path_join(sk_test_tmpdir(), name), *d = sk_files_resolve(j);
    free(j);
    CHECK_INT(sk_mkdirs(d, 0700), 0);
    return d;
}

/* A fake dialog in dir: it writes its arguments to <dir>/args, one a
 * line, prints out and exits with code. */
static void fake(const char *dir, const char *name, const char *out, int code)
{
    char *p = sk_path_join(dir, name);
    FILE *f = fopen(p, "w");
    CHECK(f != NULL);
    if (f) {
        fprintf(f, "#!/bin/sh\nprintf '%%s\\n' \"$@\" > \"$(dirname \"$0\")/args\"\nprintf '%%s' '%s'\nexit %d\n", out,
                code);
        fclose(f);
    }
    chmod(p, 0755);
    free(p);
}

static int ask(const char *path)
{
    return sk_ask_run(path, "claude-skins", "Which?", "Pick one.", SKINS, 2);
}

static void test_a_run_answers_with_the_choice(void)
{
    char *z = dir_in("z"), *k = dir_in("k");
    fake(z, "zenity", "1\n", 0);
    fake(k, "kdialog", "1\n", 0);
    CHECK_INT(ask(z), 1);
    char *args = sk_path_join(z, "args");
    char *got = sk_test_read(args, NULL);
    CHECK_STR(got, "--list\n--radiolist\n--title=claude-skins\n--text=Which?\n\nPick one.\n--column=\n--column=\n"
                   "--column=Skin\n--hide-column=2\n--print-column=2\n--hide-header\n--width=460\n--height=280\n"
                   "TRUE\n0\ndeck\nFALSE\n1\n~/skins/coffee\n");
    free(got);
    free(args);
    CHECK_INT(ask(k), 1);
    args = sk_path_join(k, "args");
    got = sk_test_read(args, NULL);
    CHECK_STR(got, "--title\nclaude-skins\n--radiolist\nWhich?\n\nPick one.\n0\ndeck\non\n1\n~/skins/coffee\noff\n");
    free(got);
    free(args);
    free(z);
    free(k);
}

static void test_a_run_cancelled_or_with_no_dialog(void)
{
    char *z = dir_in("z"), *none = dir_in("none");
    fake(z, "zenity", "", 1);
    CHECK_INT(ask(z), 0);
    /* Neither on PATH: no child run, and the caller takes the first. */
    CHECK_INT(ask(none), -1);
    CHECK_INT(ask(NULL), -1);
    free(z);
    free(none);
}

const sk_test sk_suite_ask[] = {
    T(test_the_command_lines),
    T(test_the_answers),
    T(test_a_run_answers_with_the_choice),
    T(test_a_run_cancelled_or_with_no_dialog),
    {0},
};
