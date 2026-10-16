/* Runs the C tests: all of them, or those whose "suite.test" name begins
 * with an argument, as in `run json` or `run json.parse_strings`. Run it
 * from the repository root, where the fixtures are. */
#include "test.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define SK_SUITE_ENTRY(name) {#name, sk_suite_##name},

static const struct {
    const char *name;
    const sk_test *tests;
} suites[] = {SK_SUITES(SK_SUITE_ENTRY)};

static int failures;
static char *tmpdir;

void sk_test_fail(const char *file, int line, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "    %s:%d: ", file, line);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    failures++;
}

static void remove_tree(const char *path)
{
    struct stat st;
    if (lstat(path, &st) != 0)
        return;
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        struct dirent *e;
        while (d && (e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
                continue;
            sk_buf p = {0};
            sk_buf_printf(&p, "%s/%s", path, e->d_name);
            remove_tree(p.ptr);
            sk_buf_free(&p);
        }
        if (d)
            closedir(d);
        rmdir(path);
    } else {
        unlink(path);
    }
}

const char *sk_test_tmpdir(void)
{
    if (!tmpdir) {
        const char *base = getenv("TMPDIR");
        sk_buf p = {0};
        sk_buf_printf(&p, "%s/sk-test-XXXXXX", base && *base ? base : "/tmp");
        if (!mkdtemp(p.ptr)) {
            perror("mkdtemp");
            exit(2);
        }
        tmpdir = p.ptr;
    }
    return tmpdir;
}

char *sk_test_read(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    sk_buf b = {0};
    char chunk[65536];
    size_t n;
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0)
        sk_buf_append(&b, chunk, n);
    fclose(f);
    if (len)
        *len = b.len;
    if (!b.ptr)
        sk_buf_reserve(&b, 0);
    return b.ptr;
}

static bool selected(int argc, char **argv, const char *full)
{
    if (argc < 2)
        return true;
    for (int i = 1; i < argc; i++)
        if (strncmp(full, argv[i], strlen(argv[i])) == 0)
            return true;
    return false;
}

int main(int argc, char **argv)
{
    int run = 0, failed = 0;
    for (size_t s = 0; s < SK_LEN(suites); s++) {
        for (const sk_test *t = suites[s].tests; t->name; t++) {
            const char *short_name = strncmp(t->name, "test_", 5) == 0 ? t->name + 5 : t->name;
            sk_buf full = {0};
            sk_buf_printf(&full, "%s.%s", suites[s].name, short_name);
            if (selected(argc, argv, full.ptr)) {
                failures = 0;
                t->fn();
                if (tmpdir) {
                    remove_tree(tmpdir);
                    free(tmpdir);
                    tmpdir = NULL;
                }
                run++;
                if (failures) {
                    failed++;
                    fprintf(stderr, "FAIL %s\n", full.ptr);
                }
            }
            sk_buf_free(&full);
        }
    }
    printf("%d tests, %d failed\n", run, failed);
    return failed ? 1 : (run ? 0 : 2);
}
