/* The options a new tab carries from the window's command (decision
 * 0023), as app/metrics/carry.js picks them. An option the table lacks is
 * dropped, and so is every operand: a prompt, a session id, a
 * subcommand, all that follows --. Values are taken as the CLIs take
 * them: a SK_ARITY_ONE option takes the next token whatever it is,
 * SK_ARITY_OPTIONAL and SK_ARITY_MANY the tokens that do not start with
 * -. --name=value and a short option's attached value (-mo3) stay one
 * token. */
#ifndef SK_CARRY_H
#define SK_CARRY_H

#include "../base/strv.h"

typedef enum { SK_ARITY_FLAG, SK_ARITY_ONE, SK_ARITY_OPTIONAL, SK_ARITY_MANY } sk_arity;

typedef struct {
    const char *name; /* "--model", "-m" */
    sk_arity arity;
} sk_option;

/* argv: the command's arguments after the program, NULL-terminated. */
void sk_carry_pick(char *const *argv, const sk_option *table, size_t n, sk_strv *args, sk_strv *dropped);

#endif
