/* Which links the system's opener may be given (decision 0002): http,
 * https and file URLs, as main.js's open-link had them. A file URL that
 * names a program is refused: an executable file, an application
 * bundle, or a launcher (.desktop, .terminal). A click on a link a CLI
 * printed, whose text may hide where it points, must never start one. */
#ifndef SK_APP_OPENER_H
#define SK_APP_OPENER_H

#include "../base/str.h"

/* Whether uri may be opened; false with *why, static text, when not. A
 * file URL's path is decoded and looked at as it is now. */
bool sk_opener_allows(const char *uri, const char **why);
/* The path of a file URL with an empty or localhost host, decoded, into
 * out; false for any other URI, a bad escape, an escaped NUL or a
 * fragment. */
bool sk_opener_file_path(const char *uri, sk_buf *out);

#endif
