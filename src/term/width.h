/* Character widths in terminal cells, as xterm.js's Unicode 11 provider
 * (@xterm/addon-unicode11 0.9.0) gives them, the widths the Electron app
 * used. */
#ifndef SK_TERM_WIDTH_H
#define SK_TERM_WIDTH_H

#include <stdint.h>

/* 0 for C0 and C1 controls and for combining marks, 2 for East Asian Wide
 * and Fullwidth characters and wide emoji, 1 for everything else. */
int sk_term_wcwidth(uint32_t cp);

#endif
