/* UTF-8 by the WHATWG decoder, the one Node uses: every maximal invalid
 * subsequence becomes one U+FFFD. So C reads a transcript's text as the
 * JavaScript app did. */
#ifndef SK_UTF8_H
#define SK_UTF8_H

#include "str.h"

#define SK_UTF8_REPLACEMENT 0xFFFDu

/* Decodes the character at s[0], n > 0 bytes available. Returns the bytes
 * it takes, at least 1. An invalid or cut-off sequence gives U+FFFD and
 * the length of its maximal subpart, and sets *bad when bad is not NULL. */
size_t sk_utf8_decode(const char *s, size_t n, uint32_t *cp, bool *bad);
/* Writes cp's encoding to out and returns its length, 1 to 4.
 * Surrogates and values past U+10FFFF encode as U+FFFD. */
size_t sk_utf8_encode(uint32_t cp, char out[4]);
void sk_utf8_append(sk_buf *b, uint32_t cp);
bool sk_utf8_valid(const char *s, size_t n);
/* The length of the longest prefix of s that does not end inside a
 * character that more bytes could still complete: what a chunked reader
 * can decode now, holding the rest for the next chunk. */
size_t sk_utf8_whole(const char *s, size_t n);
/* Appends s with every invalid sequence replaced by U+FFFD. */
void sk_utf8_sanitize(sk_buf *b, const char *s, size_t n);

#endif
