/* The C tests' checks. A file defines its tests as static functions and
 * lists them: const sk_test sk_suite_NAME[] = {T(test_a), T(test_b), {0}};
 * then names the suite in suites.h. A failed check reports and the test
 * goes on. */
#ifndef SK_TEST_H
#define SK_TEST_H

#include "../src/base/str.h"
#include "suites.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *name;
    void (*fn)(void);
} sk_test;

#define T(fn) {#fn, fn}
#define SK_DECLARE_SUITE(name) extern const sk_test sk_suite_##name[];
SK_SUITES(SK_DECLARE_SUITE)

void sk_test_fail(const char *file, int line, const char *fmt, ...) SK_PRINTF(3, 4);
/* A fresh directory for this test, removed with everything in it when
 * the test ends. */
const char *sk_test_tmpdir(void);
/* The whole file, NUL-terminated, or NULL. */
char *sk_test_read(const char *path, size_t *len);

#define CHECK(c)                                                             \
    do {                                                                     \
        if (!(c))                                                            \
            sk_test_fail(__FILE__, __LINE__, "%s", #c);                      \
    } while (0)

#define CHECK_INT(a, b)                                                      \
    do {                                                                     \
        long long a_ = (long long)(a), b_ = (long long)(b);                  \
        if (a_ != b_)                                                        \
            sk_test_fail(__FILE__, __LINE__, "%s == %s: %lld != %lld", #a,   \
                         #b, a_, b_);                                        \
    } while (0)

#define CHECK_STR(a, b)                                                      \
    do {                                                                     \
        const char *a_ = (a), *b_ = (b);                                     \
        if (!a_ || !b_ || strcmp(a_, b_) != 0)                               \
            sk_test_fail(__FILE__, __LINE__, "%s == %s:\n  \"%s\"\n  \"%s\"",  \
                         #a, #b, a_ ? a_ : "(null)", b_ ? b_ : "(null)");    \
    } while (0)

/* An sk_str against a C string. */
#define CHECK_MEM(s, lit)                                                    \
    do {                                                                     \
        sk_str s_ = (s);                                                     \
        const char *l_ = (lit);                                              \
        if (s_.len != strlen(l_) || (s_.len && memcmp(s_.ptr, l_, s_.len)))  \
            sk_test_fail(__FILE__, __LINE__, "%s == %s:\n  \"%.*s\"\n  \"%s\"", \
                         #s, #lit, (int)s_.len, s_.ptr ? s_.ptr : "", l_);   \
    } while (0)

#endif
