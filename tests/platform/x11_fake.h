/* A fake X server in the test process, for the X11 tests on any system
 * (decision 0036). It listens on a Unix socket in the test's directory
 * and serves one client from a thread: a scripted setup, atoms, a US
 * keyboard with AltGr, its modifiers, RESOURCE_MANAGER, BIG-REQUESTS but
 * not MIT-SHM, windows, pixmaps and their pixels, properties and
 * selections, as a server does. It records every request. Another client
 * can be played: one that owns a selection, or one that asks for it. */
#ifndef SK_TEST_X11_FAKE_H
#define SK_TEST_X11_FAKE_H

#include "../../src/base/base.h"

#define SK_XFAKE_ROOT 0x100u
#define SK_XFAKE_VISUAL 0x21u
/* The other client's windows. */
#define SK_XFAKE_OWNER 0xe00001u
#define SK_XFAKE_REQUESTOR 0xe00002u

typedef struct sk_xfake sk_xfake;

typedef struct {
    const char *resources; /* RESOURCE_MANAGER on the root; NULL for none */
    bool big_requests;     /* offer BIG-REQUESTS */
    uint16_t max_request;  /* the setup's maximum request length in words; 0 for 65535 */
    const char *refuse;    /* fail the setup with this reason */
} sk_xfake_opts;

sk_xfake *sk_xfake_start(const sk_xfake_opts *o);
void sk_xfake_stop(sk_xfake *f);
/* The DISPLAY that reaches it, "<dir>/X7:7". */
const char *sk_xfake_display(const sk_xfake *f);
/* The authorization the client sent, malloc'd: its name, a space, its
 * data in hex. NULL before the setup. */
char *sk_xfake_auth(sk_xfake *f);

/* Requests of major, and minor (-1: any), so far. */
size_t sk_xfake_count(sk_xfake *f, int major, int minor);
/* A copy of the nth (from 0) such request as it came, or NULL. */
uint8_t *sk_xfake_request(sk_xfake *f, int major, int minor, size_t nth, size_t *len);
/* Waits up to ms for count such requests. */
bool sk_xfake_wait(sk_xfake *f, int major, int minor, size_t count, int ms);
/* Requests that came in BIG-REQUESTS's form. */
size_t sk_xfake_big_count(sk_xfake *f);

uint32_t sk_xfake_atom(sk_xfake *f, const char *name);
/* Sends an event to the client, its sequence number filled in. */
void sk_xfake_event(sk_xfake *f, const uint8_t ev[32]);
/* The window takes a new size, and the client hears of it. */
void sk_xfake_configure(sk_xfake *f, uint32_t win, int w, int h);
/* A copy of a drawable's pixels, malloc'd, or NULL. */
uint32_t *sk_xfake_pixels(sk_xfake *f, uint32_t drawable, int *w, int *h);
/* A property's bytes as stored, malloc'd, or NULL; its type in *type. */
uint8_t *sk_xfake_property(sk_xfake *f, uint32_t win, uint32_t atom, size_t *len, uint32_t *type);

/* The other client owns selection and answers UTF8_STRING (and STRING)
 * with text; by INCR in chunks of chunk bytes when chunk is not 0 and
 * the text is longer. A NULL text never answers. */
void sk_xfake_own(sk_xfake *f, uint32_t selection, const char *text, size_t chunk);
/* The other client asks the selection's owner for target, into a property
 * on its own window, following INCR. */
void sk_xfake_ask(sk_xfake *f, uint32_t selection, uint32_t target);
/* Once the answer is all there: true, with the bytes (malloc'd, NULL
 * when refused) and their type. */
bool sk_xfake_answer(sk_xfake *f, char **data, size_t *len, uint32_t *type);

/* The fake's keyboard, in GetKeyboardMapping's and GetModifierMapping's
 * terms. */
#define SK_XFAKE_MIN_KEYCODE 8
#define SK_XFAKE_PER 7
#define SK_XFAKE_PER_MOD 4
const uint32_t *sk_xfake_keysyms(int *count);
const uint8_t *sk_xfake_modifiers(void);

#endif
