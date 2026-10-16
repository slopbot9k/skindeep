/* base64url (RFC 4648 section 5), as Node's 'base64url' encoding writes
 * it: no padding. The statusline relay carries the profile's own
 * statusline command this way (decision 0010). */
#ifndef SK_BASE64_H
#define SK_BASE64_H

#include "str.h"

void sk_base64url_encode(sk_buf *out, const void *data, size_t n);
/* Appends the decoded bytes. Takes either alphabet, with or without '='
 * padding; false, appending nothing, on any other character or an
 * impossible length. */
bool sk_base64url_decode(sk_buf *out, sk_str text);

#endif
