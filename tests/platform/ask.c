/* The question of a Linux desktop (platform/x11/ask.h): the command
 * lines of zenity and kdialog, what their answers say, and whole runs
 * with fake ones written in sh, on every system. */
#include "../../src/platform/x11/ask.h"
#include "../../src/base/paths.h"
#include "../../src/sessions/files.h"
#include "../../src/core/loop.h"
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

static const sk_ask_question CLOSE = {"claude-skins", "Close 2 tabs?", "Each tab's CLI <is> hung up & gone.", "Close tabs",
                                       "Cancel"};

static void test_the_confirm_s_command_lines(void)
{
    sk_strv v = {0};
    sk_ask_confirm_argv(SK_PICKER_ZENITY, &CLOSE, &v);
    char *got = joined(&v);
    /* zenity's text is markup. */
    CHECK_STR(got, "[zenity] [--question] [--title=claude-skins] "
                   "[--text=Close 2 tabs?\n\nEach tab's CLI &lt;is&gt; hung up &amp; gone.] [--ok-label=Close tabs] "
                   "[--cancel-label=Cancel]");
    free(got);
    sk_strv_free(&v);
    sk_ask_confirm_argv(SK_PICKER_KDIALOG, &CLOSE, &v);
    got = joined(&v);
    CHECK_STR(got, "[kdialog] [--title] [claude-skins] [--yes-label] [Close tabs] [--no-label] [Cancel] [--yesno] "
                   "[Close 2 tabs?\n\nEach tab's CLI <is> hung up & gone.]");
    free(got);
    sk_strv_free(&v);
    CHECK(sk_ask_confirm_answer(exited(0)));
    CHECK(!sk_ask_confirm_answer(exited(1)));
    CHECK(!sk_ask_confirm_answer(exited(5)));
    CHECK(!sk_ask_confirm_answer(9));
}

typedef struct {
    bool done;
    int answer;
} ran;

static void on_ran(void *ud, int answer)
{
    ran *r = ud;
    r->done = true;
    r->answer = answer;
}

/* A whole confirm on a loop, waited for at most 10 s. */
static int confirm(const char *path)
{
    ran r = {false, 99};
    sk_loop *loop = sk_loop_new();
    sk_ask_confirm_run(loop, path, &CLOSE, on_ran, &r);
    for (int i = 0; i < 1000 && !r.done; i++)
        sk_loop_once(loop, 10);
    CHECK(r.done);
    sk_loop_free(loop);
    return r.answer;
}

static void test_a_confirm_run(void)
{
    char *z = dir_in("z"), *k = dir_in("k"), *none = dir_in("none");
    fake(z, "zenity", "", 0);
    fake(k, "kdialog", "", 1);
    CHECK_INT(confirm(z), 1);
    char *args = sk_path_join(z, "args");
    char *got = sk_test_read(args, NULL);
    CHECK(got && strstr(got, "--question\n--title=claude-skins\n"));
    free(got);
    free(args);
    CHECK_INT(confirm(k), 0);
    /* Neither on PATH: answered at once, no child run. */
    CHECK_INT(confirm(none), -1);
    free(z);
    free(k);
    free(none);
}

const sk_test sk_suite_ask[] = {
    T(test_the_command_lines),
    T(test_the_answers),
    T(test_a_run_answers_with_the_choice),
    T(test_a_run_cancelled_or_with_no_dialog),
    T(test_the_confirm_s_command_lines),
    T(test_a_confirm_run),
    {0},
};
