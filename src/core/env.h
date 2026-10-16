/* An environment as a list of NAME=value strings it owns, ready to pass
 * as envp. The app hands each tab a copy of its own, edited. */
#ifndef SK_ENV_H
#define SK_ENV_H

#include "../base/strv.h"

typedef struct {
    sk_strv vars;
} sk_envp;

/* A copy of src, such as environ. */
void sk_envp_from(sk_envp *e, char *const *src);
/* The value, or NULL when unset. An empty value is "", not NULL. */
const char *sk_envp_get(const sk_envp *e, const char *name);
/* Sets name, replacing it; a NULL value unsets it. */
void sk_envp_set(sk_envp *e, const char *name, const char *value);
char *const *sk_envp_ptr(sk_envp *e);
void sk_envp_free(sk_envp *e);

#endif
