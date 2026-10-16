/* xterm.js's EscapeSequenceParser, OscParser and DcsParser
 * (src/common/parser/) and its Utf8ToUtf32 (src/common/input/
 * TextDecoder.ts), ported from xterm.js 6.0.0 (MIT, third_party/
 * xterm.js). The transition table is built as xterm.js builds it, and
 * the read-ahead loops stop where xterm.js's stop, which is not always
 * where the table would. */
#include "parser.h"

#include "../base/utf8.h"

#include <pthread.h>
#include <string.h>

enum {
    GROUND,
    ESCAPE,
    ESCAPE_INTERMEDIATE,
    CSI_ENTRY,
    CSI_PARAM,
    CSI_INTERMEDIATE,
    CSI_IGNORE,
    SOS_PM_APC_STRING,
    OSC_STRING,
    DCS_ENTRY,
    DCS_PARAM,
    DCS_IGNORE,
    DCS_INTERMEDIATE,
    DCS_PASSTHROUGH,
    N_STATES
};

enum {
    A_IGNORE,
    A_ERROR,
    A_PRINT,
    A_EXECUTE,
    A_OSC_START,
    A_OSC_PUT,
    A_OSC_END,
    A_CSI_DISPATCH,
    A_PARAM,
    A_COLLECT,
    A_ESC_DISPATCH,
    A_CLEAR,
    A_DCS_HOOK,
    A_DCS_PUT,
    A_DCS_UNHOOK,
};

enum { OSC_START, OSC_ID, OSC_PAYLOAD, OSC_ABORT };

/* What a non-ASCII printable character looks up in the table. */
#define NON_ASCII 0xA0
/* xterm.js's PAYLOAD_LIMIT, in UTF-16 units as JavaScript counts. */
#define PAYLOAD_LIMIT 10000000u
/* The bytes decoded at once; xterm.js's parse buffer holds 131072. */
#define CHUNK 65536

static uint8_t table[N_STATES << 8];
static pthread_once_t table_once = PTHREAD_ONCE_INIT;

static void add(int code, int state, int action, int next)
{
    table[state << 8 | code] = (uint8_t)(action << 4 | next);
}

static void add_range(int lo, int hi, int state, int action, int next)
{
    for (int c = lo; c < hi; c++)
        add(c, state, action, next);
}

/* The executables: 0x00 to 0x17, 0x19, 0x1c to 0x1f. */
static void add_exec(int state, int action, int next)
{
    add_range(0x00, 0x18, state, action, next);
    add(0x19, state, action, next);
    add_range(0x1c, 0x20, state, action, next);
}

static void build(void)
{
    memset(table, A_ERROR << 4 | GROUND, sizeof table);
    add_range(0x20, 0x7f, GROUND, A_PRINT, GROUND);
    for (int s = 0; s < N_STATES; s++) {
        add(0x18, s, A_EXECUTE, GROUND);
        add(0x1a, s, A_EXECUTE, GROUND);
        add(0x99, s, A_EXECUTE, GROUND);
        add(0x9a, s, A_EXECUTE, GROUND);
        add_range(0x80, 0x90, s, A_EXECUTE, GROUND);
        add_range(0x90, 0x98, s, A_EXECUTE, GROUND);
        add(0x9c, s, A_IGNORE, GROUND);
        add(0x1b, s, A_CLEAR, ESCAPE);
        add(0x9d, s, A_OSC_START, OSC_STRING);
        add(0x98, s, A_IGNORE, SOS_PM_APC_STRING);
        add(0x9e, s, A_IGNORE, SOS_PM_APC_STRING);
        add(0x9f, s, A_IGNORE, SOS_PM_APC_STRING);
        add(0x9b, s, A_CLEAR, CSI_ENTRY);
        add(0x90, s, A_CLEAR, DCS_ENTRY);
    }
    add_exec(GROUND, A_EXECUTE, GROUND);
    add_exec(ESCAPE, A_EXECUTE, ESCAPE);
    add(0x7f, ESCAPE, A_IGNORE, ESCAPE);
    add_exec(OSC_STRING, A_IGNORE, OSC_STRING);
    add_exec(CSI_ENTRY, A_EXECUTE, CSI_ENTRY);
    add(0x7f, CSI_ENTRY, A_IGNORE, CSI_ENTRY);
    add_exec(CSI_PARAM, A_EXECUTE, CSI_PARAM);
    add(0x7f, CSI_PARAM, A_IGNORE, CSI_PARAM);
    add_exec(CSI_IGNORE, A_EXECUTE, CSI_IGNORE);
    add_exec(CSI_INTERMEDIATE, A_EXECUTE, CSI_INTERMEDIATE);
    add(0x7f, CSI_INTERMEDIATE, A_IGNORE, CSI_INTERMEDIATE);
    add_exec(ESCAPE_INTERMEDIATE, A_EXECUTE, ESCAPE_INTERMEDIATE);
    add(0x7f, ESCAPE_INTERMEDIATE, A_IGNORE, ESCAPE_INTERMEDIATE);
    /* osc */
    add(0x5d, ESCAPE, A_OSC_START, OSC_STRING);
    add_range(0x20, 0x7f, OSC_STRING, A_OSC_PUT, OSC_STRING);
    add(0x7f, OSC_STRING, A_OSC_PUT, OSC_STRING);
    add(0x9c, OSC_STRING, A_OSC_END, GROUND);
    add(0x1b, OSC_STRING, A_OSC_END, GROUND);
    add(0x18, OSC_STRING, A_OSC_END, GROUND);
    add(0x1a, OSC_STRING, A_OSC_END, GROUND);
    add(0x07, OSC_STRING, A_OSC_END, GROUND);
    add_range(0x1c, 0x20, OSC_STRING, A_IGNORE, OSC_STRING);
    /* sos, pm and apc do nothing */
    add(0x58, ESCAPE, A_IGNORE, SOS_PM_APC_STRING);
    add(0x5e, ESCAPE, A_IGNORE, SOS_PM_APC_STRING);
    add(0x5f, ESCAPE, A_IGNORE, SOS_PM_APC_STRING);
    add_range(0x20, 0x7f, SOS_PM_APC_STRING, A_IGNORE, SOS_PM_APC_STRING);
    add_exec(SOS_PM_APC_STRING, A_IGNORE, SOS_PM_APC_STRING);
    add(0x9c, SOS_PM_APC_STRING, A_IGNORE, GROUND);
    add(0x7f, SOS_PM_APC_STRING, A_IGNORE, SOS_PM_APC_STRING);
    /* csi */
    add(0x5b, ESCAPE, A_CLEAR, CSI_ENTRY);
    add_range(0x40, 0x7f, CSI_ENTRY, A_CSI_DISPATCH, GROUND);
    add_range(0x30, 0x3c, CSI_ENTRY, A_PARAM, CSI_PARAM);
    add_range(0x3c, 0x40, CSI_ENTRY, A_COLLECT, CSI_PARAM);
    add_range(0x30, 0x3c, CSI_PARAM, A_PARAM, CSI_PARAM);
    add_range(0x40, 0x7f, CSI_PARAM, A_CSI_DISPATCH, GROUND);
    add_range(0x3c, 0x40, CSI_PARAM, A_IGNORE, CSI_IGNORE);
    add_range(0x20, 0x40, CSI_IGNORE, A_IGNORE, CSI_IGNORE);
    add(0x7f, CSI_IGNORE, A_IGNORE, CSI_IGNORE);
    add_range(0x40, 0x7f, CSI_IGNORE, A_IGNORE, GROUND);
    add_range(0x20, 0x30, CSI_ENTRY, A_COLLECT, CSI_INTERMEDIATE);
    add_range(0x20, 0x30, CSI_INTERMEDIATE, A_COLLECT, CSI_INTERMEDIATE);
    add_range(0x30, 0x40, CSI_INTERMEDIATE, A_IGNORE, CSI_IGNORE);
    add_range(0x40, 0x7f, CSI_INTERMEDIATE, A_CSI_DISPATCH, GROUND);
    add_range(0x20, 0x30, CSI_PARAM, A_COLLECT, CSI_INTERMEDIATE);
    /* esc */
    add_range(0x20, 0x30, ESCAPE, A_COLLECT, ESCAPE_INTERMEDIATE);
    add_range(0x20, 0x30, ESCAPE_INTERMEDIATE, A_COLLECT, ESCAPE_INTERMEDIATE);
    add_range(0x30, 0x7f, ESCAPE_INTERMEDIATE, A_ESC_DISPATCH, GROUND);
    add_range(0x30, 0x50, ESCAPE, A_ESC_DISPATCH, GROUND);
    add_range(0x51, 0x58, ESCAPE, A_ESC_DISPATCH, GROUND);
    add(0x59, ESCAPE, A_ESC_DISPATCH, GROUND);
    add(0x5a, ESCAPE, A_ESC_DISPATCH, GROUND);
    add(0x5c, ESCAPE, A_ESC_DISPATCH, GROUND);
    add_range(0x60, 0x7f, ESCAPE, A_ESC_DISPATCH, GROUND);
    /* dcs */
    add(0x50, ESCAPE, A_CLEAR, DCS_ENTRY);
    add_exec(DCS_ENTRY, A_IGNORE, DCS_ENTRY);
    add(0x7f, DCS_ENTRY, A_IGNORE, DCS_ENTRY);
    add_range(0x1c, 0x20, DCS_ENTRY, A_IGNORE, DCS_ENTRY);
    add_range(0x20, 0x30, DCS_ENTRY, A_COLLECT, DCS_INTERMEDIATE);
    add_range(0x30, 0x3c, DCS_ENTRY, A_PARAM, DCS_PARAM);
    add_range(0x3c, 0x40, DCS_ENTRY, A_COLLECT, DCS_PARAM);
    add_exec(DCS_IGNORE, A_IGNORE, DCS_IGNORE);
    add_range(0x20, 0x80, DCS_IGNORE, A_IGNORE, DCS_IGNORE);
    add_range(0x1c, 0x20, DCS_IGNORE, A_IGNORE, DCS_IGNORE);
    add_exec(DCS_PARAM, A_IGNORE, DCS_PARAM);
    add(0x7f, DCS_PARAM, A_IGNORE, DCS_PARAM);
    add_range(0x1c, 0x20, DCS_PARAM, A_IGNORE, DCS_PARAM);
    add_range(0x30, 0x3c, DCS_PARAM, A_PARAM, DCS_PARAM);
    add_range(0x3c, 0x40, DCS_PARAM, A_IGNORE, DCS_IGNORE);
    add_range(0x20, 0x30, DCS_PARAM, A_COLLECT, DCS_INTERMEDIATE);
    add_exec(DCS_INTERMEDIATE, A_IGNORE, DCS_INTERMEDIATE);
    add(0x7f, DCS_INTERMEDIATE, A_IGNORE, DCS_INTERMEDIATE);
    add_range(0x1c, 0x20, DCS_INTERMEDIATE, A_IGNORE, DCS_INTERMEDIATE);
    add_range(0x20, 0x30, DCS_INTERMEDIATE, A_COLLECT, DCS_INTERMEDIATE);
    add_range(0x30, 0x40, DCS_INTERMEDIATE, A_IGNORE, DCS_IGNORE);
    add_range(0x40, 0x7f, DCS_INTERMEDIATE, A_DCS_HOOK, DCS_PASSTHROUGH);
    add_range(0x40, 0x7f, DCS_PARAM, A_DCS_HOOK, DCS_PASSTHROUGH);
    add_range(0x40, 0x7f, DCS_ENTRY, A_DCS_HOOK, DCS_PASSTHROUGH);
    add_exec(DCS_PASSTHROUGH, A_DCS_PUT, DCS_PASSTHROUGH);
    add_range(0x20, 0x7f, DCS_PASSTHROUGH, A_DCS_PUT, DCS_PASSTHROUGH);
    add(0x7f, DCS_PASSTHROUGH, A_IGNORE, DCS_PASSTHROUGH);
    add(0x1b, DCS_PASSTHROUGH, A_DCS_UNHOOK, GROUND);
    add(0x9c, DCS_PASSTHROUGH, A_DCS_UNHOOK, GROUND);
    add(0x18, DCS_PASSTHROUGH, A_DCS_UNHOOK, GROUND);
    add(0x1a, DCS_PASSTHROUGH, A_DCS_UNHOOK, GROUND);
    /* non-ASCII printables */
    add(NON_ASCII, GROUND, A_PRINT, GROUND);
    add(NON_ASCII, OSC_STRING, A_OSC_PUT, OSC_STRING);
    add(NON_ASCII, CSI_IGNORE, A_IGNORE, CSI_IGNORE);
    add(NON_ASCII, DCS_IGNORE, A_IGNORE, DCS_IGNORE);
    add(NON_ASCII, DCS_PASSTHROUGH, A_DCS_PUT, DCS_PASSTHROUGH);
}

/* Params, as xterm.js's: ZDM for parameters, -1 for an empty
 * sub-parameter, values clamped to 2^31 - 1. */

static void params_reset(sk_vt_params *p)
{
    p->n = 0;
    p->nsub = 0;
    p->reject_digits = p->reject_sub = p->digit_is_sub = false;
}

static void add_param(sk_vt_params *p, int32_t v)
{
    p->digit_is_sub = false;
    if (p->n >= SK_VT_MAX_PARAMS) {
        p->reject_digits = true;
        return;
    }
    p->subidx[p->n] = (uint16_t)(p->nsub << 8 | p->nsub);
    p->v[p->n++] = v;
}

static void add_sub(sk_vt_params *p, int32_t v)
{
    p->digit_is_sub = true;
    if (!p->n)
        return;
    if (p->reject_digits || p->nsub >= SK_VT_MAX_PARAMS) {
        p->reject_sub = true;
        return;
    }
    p->sub[p->nsub++] = v;
    p->subidx[p->n - 1]++;
}

static void add_digit(sk_vt_params *p, int d)
{
    int len = p->digit_is_sub ? p->nsub : p->n;
    if (p->reject_digits || !len || (p->digit_is_sub && p->reject_sub))
        return;
    int32_t *store = p->digit_is_sub ? p->sub : p->v;
    int32_t cur = store[len - 1];
    store[len - 1] = cur != -1 ? (int32_t)SK_MIN((int64_t)cur * 10 + d, (int64_t)0x7FFFFFFF) : d;
}

static void clear(sk_vt_parser *p)
{
    params_reset(&p->params);
    add_param(&p->params, 0);
    p->collect = 0;
}

bool sk_vt_has_sub(const sk_vt_params *p, int i)
{
    return (p->subidx[i] & 0xFF) > (p->subidx[i] >> 8);
}

int sk_vt_subs(const sk_vt_params *p, int i, const int32_t **out)
{
    int start = p->subidx[i] >> 8, end = p->subidx[i] & 0xFF;
    *out = p->sub + start;
    return end > start ? end - start : 0;
}

/* OSC and DCS payloads, kept as UTF-8 with their UTF-16 length. */
static void put(sk_buf *b, size_t *units, bool *hit, const uint32_t *cp, size_t n)
{
    if (*hit)
        return;
    for (size_t i = 0; i < n; i++) {
        sk_utf8_append(b, cp[i]);
        *units += cp[i] > 0xFFFF ? 2 : 1;
    }
    if (*units > PAYLOAD_LIMIT) {
        sk_buf_clear(b);
        *units = 0;
        *hit = true;
    }
}

static void payload_reset(sk_buf *b, size_t *units, bool *hit)
{
    sk_buf_clear(b);
    *units = 0;
    *hit = false;
}

static void osc_reset(sk_vt_parser *p)
{
    payload_reset(&p->osc_data, &p->osc_units, &p->osc_hit);
    p->osc_keep = false;
    p->osc_id = -1;
    p->osc_state = OSC_START;
}

static void osc_begin(sk_vt_parser *p)
{
    p->osc_keep = p->h.osc_wanted(p->h.ud, p->osc_id);
    payload_reset(&p->osc_data, &p->osc_units, &p->osc_hit);
}

static void osc_put(sk_vt_parser *p, const uint32_t *cp, size_t start, size_t end)
{
    if (p->osc_state == OSC_ABORT)
        return;
    if (p->osc_state == OSC_ID) {
        while (start < end) {
            uint32_t c = cp[start++];
            if (c == ';') {
                p->osc_state = OSC_PAYLOAD;
                osc_begin(p);
                break;
            }
            if (c < '0' || c > '9') {
                p->osc_state = OSC_ABORT;
                return;
            }
            if (p->osc_id == -1)
                p->osc_id = 0;
            /* Past any registered id it stays unregistered. */
            if (p->osc_id < ((int64_t)1 << 40))
                p->osc_id = p->osc_id * 10 + (c - '0');
        }
    }
    if (p->osc_state == OSC_PAYLOAD && end > start && p->osc_keep)
        put(&p->osc_data, &p->osc_units, &p->osc_hit, cp + start, end - start);
}

static void osc_end(sk_vt_parser *p, bool success)
{
    if (p->osc_state == OSC_START)
        return;
    if (p->osc_state != OSC_ABORT) {
        if (p->osc_state == OSC_ID)
            osc_begin(p);
        if (p->osc_keep && success && !p->osc_hit)
            p->h.osc(p->h.ud, p->osc_id, sk_buf_cstr(&p->osc_data), p->osc_data.len);
    }
    osc_reset(p);
}

static void dcs_reset(sk_vt_parser *p)
{
    payload_reset(&p->dcs_data, &p->dcs_units, &p->dcs_hit);
    p->dcs_keep = false;
    p->dcs_ident = 0;
}

static void dcs_hook(sk_vt_parser *p, uint32_t ident)
{
    dcs_reset(p);
    p->dcs_ident = ident;
    p->dcs_keep = p->h.dcs_wanted(p->h.ud, ident);
    p->dcs_params = p->params;
}

static void dcs_unhook(sk_vt_parser *p, bool success)
{
    if (p->dcs_keep && success && !p->dcs_hit)
        p->h.dcs(p->h.ud, p->dcs_ident, &p->dcs_params, sk_buf_cstr(&p->dcs_data), p->dcs_data.len);
    dcs_reset(p);
}

void sk_vt_init(sk_vt_parser *p, const sk_vt_handler *h)
{
    pthread_once(&table_once, build);
    memset(p, 0, sizeof *p);
    p->h = *h;
    p->state = GROUND;
    clear(p);
    osc_reset(p);
    dcs_reset(p);
}

void sk_vt_free(sk_vt_parser *p)
{
    sk_buf_free(&p->osc_data);
    sk_buf_free(&p->dcs_data);
    free(p->cp);
    p->cp = NULL;
}

void sk_vt_reset(sk_vt_parser *p)
{
    p->state = GROUND;
    osc_reset(p);
    dcs_reset(p);
    clear(p);
    p->join = 0;
}

size_t sk_vt_decode(sk_vt_parser *p, const unsigned char *in, size_t n, uint32_t *out)
{
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned b = in[i];
        if (p->u8_need) {
            if ((b & 0xC0) == 0x80) {
                p->u8_cp = p->u8_cp << 6 | (b & 0x3F);
                if (--p->u8_need)
                    continue;
                uint32_t cp = p->u8_cp;
                if (p->u8_len == 2 ? cp >= 0x80
                    : p->u8_len == 3 ? cp >= 0x800 && (cp < 0xD800 || cp > 0xDFFF) && cp != 0xFEFF
                                     : cp >= 0x10000 && cp <= 0x10FFFF)
                    out[k++] = cp;
                continue;
            }
            /* A wrong continuation drops the sequence; b starts afresh. */
            p->u8_need = 0;
        }
        if (b < 0x80) {
            out[k++] = b;
        } else if ((b & 0xE0) == 0xC0) {
            p->u8_cp = b & 0x1F;
            p->u8_need = 1;
            p->u8_len = 2;
        } else if ((b & 0xF0) == 0xE0) {
            p->u8_cp = b & 0x0F;
            p->u8_need = 2;
            p->u8_len = 3;
        } else if ((b & 0xF8) == 0xF0) {
            p->u8_cp = b & 0x07;
            p->u8_need = 3;
            p->u8_len = 4;
        }
    }
    return k;
}

static bool stops_print(uint32_t c)
{
    return c < 0x20 || (c > 0x7e && c < NON_ASCII);
}

static void parse(sk_vt_parser *p, const uint32_t *data, size_t length)
{
    const sk_vt_handler *h = &p->h;
    for (size_t i = 0; i < length; i++) {
        uint32_t code = data[i];
        uint8_t transition = table[p->state << 8 | (code < NON_ASCII ? code : NON_ASCII)];
        switch (transition >> 4) {
        case A_PRINT: {
            size_t j = i + 1;
            while (j < length && !stops_print(data[j]))
                j++;
            h->print(h->ud, data + i, j - i);
            i = j - 1;
            break;
        }
        case A_EXECUTE:
            h->execute(h->ud, code);
            p->join = 0;
            break;
        case A_IGNORE:
        case A_ERROR:
            break;
        case A_CSI_DISPATCH:
            h->csi(h->ud, p->collect << 8 | code, &p->params);
            p->join = 0;
            break;
        case A_PARAM:
            do {
                if (code == ';')
                    add_param(&p->params, 0);
                else if (code == ':')
                    add_sub(&p->params, -1);
                else
                    add_digit(&p->params, (int)(code - '0'));
            } while (++i < length && (code = data[i]) > 0x2f && code < 0x3c);
            i--;
            break;
        case A_COLLECT:
            p->collect = p->collect << 8 | code;
            break;
        case A_ESC_DISPATCH:
            h->esc(h->ud, p->collect << 8 | code);
            p->join = 0;
            break;
        case A_CLEAR:
            clear(p);
            break;
        case A_DCS_HOOK:
            dcs_hook(p, p->collect << 8 | code);
            break;
        case A_DCS_PUT: {
            size_t j = i + 1;
            while (j < length && (code = data[j]) != 0x18 && code != 0x1a && code != 0x1b &&
                   !(code > 0x7f && code < NON_ASCII))
                j++;
            if (p->dcs_keep)
                put(&p->dcs_data, &p->dcs_units, &p->dcs_hit, data + i, j - i);
            i = j - 1;
            break;
        }
        case A_DCS_UNHOOK:
            dcs_unhook(p, code != 0x18 && code != 0x1a);
            if (code == 0x1b)
                transition |= ESCAPE;
            clear(p);
            p->join = 0;
            break;
        case A_OSC_START:
            osc_reset(p);
            p->osc_state = OSC_ID;
            break;
        case A_OSC_PUT: {
            size_t j = i + 1;
            while (j < length && (code = data[j]) >= 0x20 && !(code > 0x7f && code < NON_ASCII))
                j++;
            osc_put(p, data, i, j);
            i = j - 1;
            break;
        }
        case A_OSC_END:
            osc_end(p, code != 0x18 && code != 0x1a);
            if (code == 0x1b)
                transition |= ESCAPE;
            clear(p);
            p->join = 0;
            break;
        }
        p->state = transition & 15;
    }
}

void sk_vt_feed(sk_vt_parser *p, const char *data, size_t n)
{
    if (!p->cp) {
        p->cp_cap = CHUNK;
        p->cp = sk_xreallocarray(NULL, p->cp_cap, sizeof *p->cp);
    }
    while (n) {
        size_t take = SK_MIN(n, (size_t)CHUNK);
        size_t k = sk_vt_decode(p, (const unsigned char *)data, take, p->cp);
        parse(p, p->cp, k);
        data += take;
        n -= take;
    }
}
