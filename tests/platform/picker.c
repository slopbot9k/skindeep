/* The folder picker of a Linux desktop (platform/x11/picker.h): the
 * command lines of zenity and kdialog, their answers, the program found
 * on PATH, and whole runs on the core loop with fake pickers written in
 * sh, on every system. */
#include "../../src/platform/x11/picker.h"
#include "../../src/base/paths.h"
#include "../../src/sessions/files.h"
#include "../test.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>

static char *joined(const sk_strv *v)
{
    sk_buf b = {0};
    sk_buf_reserve(&b, 0);
    for (size_t i = 0; i < v->len; i++)
        sk_buf_printf(&b, "%s[%s]", i ? " " : "", v->ptr[i]);
    return b.ptr;
}

#define CHECK_ARGV(prog, title, start, hidden, want)                         \
    do {                                                                     \
        sk_picker_opts o_ = {title, start, hidden};                          \
        sk_strv v_ = {0};                                                    \
        sk_picker_argv(prog, &o_, &v_);                                      \
        char *got_ = joined(&v_);                                            \
        CHECK_STR(got_, want);                                               \
        free(got_);                                                          \
        sk_strv_free(&v_);                                                   \
    } while (0)

static void test_the_command_lines(void)
{
    CHECK_ARGV(SK_PICKER_ZENITY, "Folder", "/home/u/src", false,
               "[zenity] [--file-selection] [--directory] [--title=Folder] [--filename=/home/u/src/]");
    CHECK_ARGV(SK_PICKER_ZENITY, "Other profile", "/home/u/", true,
               "[zenity] [--file-selection] [--directory] [--title=Other profile] [--filename=/home/u/]");
    CHECK_ARGV(SK_PICKER_ZENITY, NULL, NULL, false, "[zenity] [--file-selection] [--directory]");
    CHECK_ARGV(SK_PICKER_KDIALOG, "Folder", "/home/u/src", false,
               "[kdialog] [--getexistingdirectory] [/home/u/src] [--title] [Folder]");
    CHECK_ARGV(SK_PICKER_KDIALOG, "Other profile", NULL, true,
               "[kdialog] [--getexistingdirectory] [--title] [Other profile]");
}

/* A wait status as waitpid gives it. */
static int exited(int code)
{
    return (code & 0xff) << 8;
}

static void check_answer(sk_picker_prog prog, const char *out, int wstatus, const char *dir, const char *err)
{
    char *d, *e;
    sk_picker_answer(prog, out, strlen(out), wstatus, &d, &e);
    if (dir)
        CHECK_STR(d, dir);
    else
        CHECK(d == NULL);
    if (err)
        CHECK_STR(e, err);
    else
        CHECK(e == NULL);
    free(d);
    free(e);
}

static void test_the_answers(void)
{
    check_answer(SK_PICKER_ZENITY, "/home/u/a b\n", exited(0), "/home/u/a b", NULL);
    check_answer(SK_PICKER_KDIALOG, "/home/u/x\r\n", exited(0), "/home/u/x", NULL);
    /* A folder's name may end in a newline of its own. */
    check_answer(SK_PICKER_ZENITY, "/home/u/odd\n\n", exited(0), "/home/u/odd\n", NULL);
    check_answer(SK_PICKER_ZENITY, "", exited(1), NULL, NULL);
    check_answer(SK_PICKER_ZENITY, "", exited(0), NULL, NULL);
    check_answer(SK_PICKER_KDIALOG, "", exited(1), NULL, NULL);
    check_answer(SK_PICKER_ZENITY, "", exited(5), NULL, "zenity failed with status 5");
    check_answer(SK_PICKER_KDIALOG, "", 9, NULL, "kdialog was killed by signal 9");
    check_answer(SK_PICKER_ZENITY, "Gtk-WARNING\n", exited(0), NULL, "zenity gave no folder: Gtk-WARNING");
    /* Reaped elsewhere: what it printed answers. */
    check_answer(SK_PICKER_ZENITY, "/x\n", -1, "/x", NULL);
}

/* <tmp>/name, made, normalized, as TMPDIR may end in a slash. */
static char *dir_in(const char *name)
{
    char *j = sk_path_join(sk_test_tmpdir(), name), *d = sk_files_resolve(j);
    free(j);
    CHECK_INT(sk_mkdirs(d, 0700), 0);
    return d;
}

/* A fake picker in dir: it writes its arguments to <dir>/args, one a
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

static void test_the_program_found_on_path(void)
{
    char *z = dir_in("z"), *k = dir_in("k"), *none = dir_in("none");
    fake(z, "zenity", "", 0);
    fake(k, "kdialog", "", 0);
    char *file = NULL;
    sk_buf both = {0};
    sk_buf_printf(&both, "%s:%s", k, z);
    CHECK_INT(sk_picker_find(both.ptr, &file), SK_PICKER_ZENITY);
    char *want = sk_path_join(z, "zenity");
    CHECK_STR(file, want);
    free(want);
    free(file);
    CHECK_INT(sk_picker_find(k, &file), SK_PICKER_KDIALOG);
    free(file);
    CHECK_INT(sk_picker_find(none, &file), SK_PICKER_NONE);
    CHECK(file == NULL);
    CHECK_INT(sk_picker_find(NULL, &file), SK_PICKER_NONE);
    sk_buf_free(&both);
    free(z);
    free(k);
    free(none);
}

typedef struct {
    bool done;
    char *dir, *err;
} answer;

static void answered(void *ud, char *dir, char *err)
{
    answer *a = ud;
    a->done = true;
    a->dir = dir;
    a->err = err;
}

/* A whole run on a loop, waited for at most 10 s. */
static void run(const char *path, const char *title, const char *start, answer *a)
{
    memset(a, 0, sizeof *a);
    sk_loop *loop = sk_loop_new();
    sk_picker_opts o = {title, start, false};
    sk_picker_run(loop, &o, path, answered, a);
    for (int i = 0; i < 1000 && !a->done; i++)
        sk_loop_once(loop, 10);
    CHECK(a->done);
    sk_loop_free(loop);
}

static void test_a_run_answers_with_the_folder(void)
{
    char *z = dir_in("z");
    fake(z, "zenity", "/picked/folder one\n", 0);
    answer a;
    run(z, "Folder", "/start/here", &a);
    CHECK_STR(a.dir, "/picked/folder one");
    CHECK(a.err == NULL);
    char *args = sk_path_join(z, "args");
    char *got = sk_test_read(args, NULL);
    CHECK_STR(got, "--file-selection\n--directory\n--title=Folder\n--filename=/start/here/\n");
    free(got);
    free(args);
    free(a.dir);
    free(a.err);
    free(z);
}

static void test_a_run_cancelled_or_failed(void)
{
    char *z = dir_in("z"), *k = dir_in("k"), *none = dir_in("none");
    fake(z, "zenity", "", 1);
    fake(k, "kdialog", "", 4);
    answer a;
    run(z, "Folder", NULL, &a);
    CHECK(a.dir == NULL && a.err == NULL);
    run(k, "Other profile", "/h", &a);
    CHECK(a.dir == NULL);
    CHECK_STR(a.err, "kdialog failed with status 4");
    free(a.err);
    char *args = sk_path_join(k, "args");
    char *got = sk_test_read(args, NULL);
    CHECK_STR(got, "--getexistingdirectory\n/h\n--title\nOther profile\n");
    free(got);
    free(args);
    /* Neither on PATH: said at once, no child run. */
    run(none, "Folder", NULL, &a);
    CHECK(a.dir == NULL);
    CHECK_STR(a.err, SK_PICKER_MISSING);
    CHECK_STR(a.err, "no folder picker: install zenity or kdialog, or type the folder");
    free(a.err);
    free(z);
    free(k);
    free(none);
}

const sk_test sk_suite_picker[] = {
    T(test_the_command_lines),
    T(test_the_answers),
    T(test_the_program_found_on_path),
    T(test_a_run_answers_with_the_folder),
    T(test_a_run_cancelled_or_failed),
    {0},
};
