/* The command line of New tab... split into words (decision 0025), as
 * app/argv.js splits it: as a shell splits words, with no shell. Blanks
 * separate words, '...' is literal, "..." keeps \" \\ \$ \` as escapes,
 * and a backslash outside quotes takes the next character. Nothing
 * expands: no ~, $VAR, glob or redirection. It is the inverse of
 * sk_identity_quote for every word. */
#ifndef SK_APP_ARGV_H
#define SK_APP_ARGV_H

#include "../base/strv.h"

/* Appends the words of line to argv and returns NULL, or returns why
 * the line is refused, appending nothing. */
const char *sk_argv_split(const char *line, sk_strv *argv);

#endif
