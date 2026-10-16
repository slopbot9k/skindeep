/* What the skinc suites share: a throwaway skin in the test's own
 * directory, compiled by the front end. */
#ifndef SK_TESTS_SKINC_UTIL_H
#define SK_TESTS_SKINC_UTIL_H

#include "../../skinc/front.h"

/* Writes a skin into a fresh directory under the test's tmpdir: a
 * manifest of a 1000 x 700 window (800 x 500 at least), the menu at
 * the top right, the terminal below y 300, with the panels and widgets
 * JSON arrays given (NULL for none) and css as skin.css. Returns the
 * directory, which lives until the test ends. */
const char *skt_skin(const char *panels, const char *widgets, const char *css);
/* The same, then the front end run over it. True when it compiled. */
bool skt_compile(sk_front *f, sk_diags *d, const char *panels, const char *widgets, const char *css);
/* skt_compile where the skin must compile: a failure names its messages. */
#define COMPILES(f, d, panels, widgets, css)                                                                           \
    do {                                                                                                               \
        if (!skt_compile(f, d, panels, widgets, css)) {                                                                \
            sk_test_fail(__FILE__, __LINE__, "the skin did not compile");                                             \
            for (size_t i_ = 0; i_ < (d)->items.len; i_++)                                                             \
                fprintf(stderr, "      %s\n", (d)->items.ptr[i_]);                                                    \
        }                                                                                                              \
    } while (0)
/* Writes text to dir/name, making the directories it needs. */
void skt_write(const char *dir, const char *name, const char *text);

/* A parsed sheet's first rule, for the value and selector suites. */
sk_css_rule *skt_rule(sk_css_sheet *sheet, const char *css, sk_diags *d);

/* The text of a longhand of a style, in a buffer the next call reuses. */
const char *skt_prop(const sk_css_style *s, uint16_t prop);
/* A value parsed as a longhand in a default context: its text, or NULL
 * when it does not parse. */
const char *skt_value(const char *prop, const char *value);

/* Whether any message contains needle. */
bool skt_said(const sk_diags *d, const char *needle);

#endif
