/* The VT parser of the terminal: xterm.js 6.0.0's EscapeSequenceParser,
 * the DEC ANSI state machine of vt100.net with sub-parameters, and its
 * UTF-8 decoder, ported from xterm.js (MIT, third_party/xterm.js). The
 * screen (term.c) gives it a handler and feeds it bytes. */
#ifndef SK_TERM_PARSER_H
#define SK_TERM_PARSER_H

#include "../base/str.h"

/* 32 parameters and 32 sub-parameters, as xterm.js's Params keeps. An
 * empty parameter is 0, an empty sub-parameter -1, any value at most
 * 2^31 - 1. */
#define SK_VT_MAX_PARAMS 32

typedef struct {
    int32_t v[SK_VT_MAX_PARAMS];
    int n;
    int32_t sub[SK_VT_MAX_PARAMS];
    int nsub;
    uint16_t subidx[SK_VT_MAX_PARAMS]; /* start << 8 | end into sub */
    bool reject_digits, reject_sub, digit_is_sub;
} sk_vt_params;

bool sk_vt_has_sub(const sk_vt_params *p, int i);
/* The sub-parameters of parameter i: their count, and *out at the first. */
int sk_vt_subs(const sk_vt_params *p, int i, const int32_t **out);

/* An identifier is the collected prefix and intermediate bytes, then the
 * final byte, one byte each from the high end: CSI ? 1 $ p is
 * '?' << 16 | '$' << 8 | 'p'. */
typedef struct {
    void *ud;
    /* A run of printable characters; the screen reads and sets join. */
    void (*print)(void *ud, const uint32_t *cp, size_t n);
    void (*execute)(void *ud, uint32_t code);
    void (*csi)(void *ud, uint32_t ident, const sk_vt_params *p);
    void (*esc)(void *ud, uint32_t ident);
    /* Whether the screen handles OSC id or DCS ident: only those payloads
     * are kept. */
    bool (*osc_wanted)(void *ud, int64_t id);
    bool (*dcs_wanted)(void *ud, uint32_t ident);
    /* A whole OSC or DCS string, ended by BEL or ST, within the payload
     * limit. data is UTF-8, NUL-terminated. */
    void (*osc)(void *ud, int64_t id, const char *data, size_t len);
    void (*dcs)(void *ud, uint32_t ident, const sk_vt_params *p, const char *data, size_t len);
} sk_vt_handler;

typedef struct {
    sk_vt_handler h;
    uint8_t state;
    uint32_t collect;
    sk_vt_params params;
    /* xterm.js's precedingJoinState: the Unicode properties of the last
     * printed character, 0 after any control. print and REP use it. */
    uint32_t join;

    int osc_state;
    int64_t osc_id;
    bool osc_keep, osc_hit;
    sk_buf osc_data;
    size_t osc_units; /* the payload's length in UTF-16 units */

    uint32_t dcs_ident;
    bool dcs_keep, dcs_hit;
    sk_vt_params dcs_params;
    sk_buf dcs_data;
    size_t dcs_units;

    /* A UTF-8 sequence cut by the end of a feed. */
    uint32_t u8_cp;
    int u8_need, u8_len;

    uint32_t *cp;
    size_t cp_cap;
} sk_vt_parser;

void sk_vt_init(sk_vt_parser *p, const sk_vt_handler *h);
void sk_vt_free(sk_vt_parser *p);
/* Back to the ground state, as ESC c does; the UTF-8 decoder keeps its
 * state. */
void sk_vt_reset(sk_vt_parser *p);
void sk_vt_feed(sk_vt_parser *p, const char *data, size_t n);
/* Decodes UTF-8 as xterm.js's Utf8ToUtf32 decodes a whole stream: an
 * invalid byte, a cut sequence, an overlong form, a surrogate and U+FEFF
 * are dropped. Returns the codepoints written to out, at most n. */
size_t sk_vt_decode(sk_vt_parser *p, const unsigned char *in, size_t n, uint32_t *out);

#endif
