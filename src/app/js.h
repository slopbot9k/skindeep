/* What the app's ported modules need of JavaScript's own library, so
 * they give what the JavaScript app gave: String's trim, length and
 * slice, which count UTF-16 code units, the whitespace trim and \s
 * take, and Node's posix path functions. Text is UTF-8; an invalid
 * sequence counts as the one U+FFFD Node decodes it to. */
#ifndef SK_APP_JS_H
#define SK_APP_JS_H

#include "../base/str.h"

/* WhiteSpace or LineTerminator: what trim strips and \s matches. */
bool sk_js_space(uint32_t cp);
/* s.trim(). */
sk_str sk_js_trim(sk_str s);
/* s.length. */
size_t sk_js_units(sk_str s);
/* Appends s.slice(0, units). Invalid sequences come out as U+FFFD, and
 * so does a character cut in half, where slice leaves a lone surrogate
 * that Node writes as U+FFFD. */
void sk_js_cut(sk_buf *out, sk_str s, size_t units);

/* path.basename(p) and path.dirname(p): views of p, or of a literal. */
sk_str sk_js_basename(sk_str p);
sk_str sk_js_dirname(sk_str p);
/* Appends path.normalize(p). */
void sk_js_normalize(sk_buf *out, sk_str p);
/* Appends path.join(a, b). */
void sk_js_join(sk_buf *out, sk_str a, sk_str b);
/* Appends path.resolve(p), a relative p taken from the working
 * directory. */
void sk_js_resolve(sk_buf *out, sk_str p);

#endif
