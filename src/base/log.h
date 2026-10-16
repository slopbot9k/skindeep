/* The app's own messages: one line each, stamped in UTC, written to one
 * descriptor with a single write so lines from threads never interleave.
 * The launcher points it at the caller's stderr (decision 0003). */
#ifndef SK_LOG_H
#define SK_LOG_H

#include "base.h"

/* stderr until set. */
void sk_log_set_fd(int fd);
void sk_log(const char *fmt, ...) SK_PRINTF(1, 2);

#endif
