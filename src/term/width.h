/* How many cells a character takes (phase 4 of decision 0036): Unicode
 * 11's widths as @xterm/addon-unicode11 0.9.0 gives them, the version
 * the Electron app made active. Ported from the addon's UnicodeV11.ts,
 * (c) 2019 The xterm.js authors, MIT licence. */
#ifndef SK_TERM_WIDTH_H
#define SK_TERM_WIDTH_H

#include <stdint.h>

/* 0 for a control character or a combining mark, 2 for an East Asian
 * wide or full-width character or a wide emoji, 1 for the rest,
 * values past U+10FFFF included. */
int sk_term_wcwidth(uint32_t cp);

#endif
