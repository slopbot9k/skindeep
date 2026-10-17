/* Files dropped on a window (decision 0002): the text the page's drop
 * handler pasted into the active tab for them. */
#ifndef SK_APP_DROP_H
#define SK_APP_DROP_H

#include "../base/str.h"

/* paths.map(quote).join(' ') + ' ': each path quoted for a shell as
 * sk_identity_quote quotes a word, joined by spaces, with a space after
 * the last, so that what is typed next stands apart. An empty path is
 * skipped, as the page's filter(Boolean) skipped it; with none left,
 * nothing is written. */
void sk_drop_text(sk_buf *out, const char *const *paths, size_t n);

#endif
