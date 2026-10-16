/* CLI-agnostic meters (decision 0014), as app/metrics/generic.js has
 * them: the child's process tree CPU and memory, the pty rates, idle,
 * and the session clock. The seam sets one beside every adapter. */
#ifndef SK_GENERIC_H
#define SK_GENERIC_H

#include "agent.h"

sk_part sk_generic_create(const sk_agent_ctx *ctx);
/* Terminal output of n bytes at now (ms). */
void sk_generic_on_output(void *self, size_t n, uint64_t now);
/* One input write: keys or a paste. */
void sk_generic_on_input(void *self);
/* The session's start, for the clock: by default when it was made. A
 * session the app did not start counts from its own start (0027). */
void sk_generic_set_start(void *self, uint64_t start);

#endif
