/* JSON as JSON.parse and JSON.stringify read and write it.
 *
 * sk_jr is a pull reader. It checks every byte as it goes and caps the
 * nesting depth, and it can skip a value without decoding it: a
 * transcript line can carry megabytes of base64 nobody reads. Invalid
 * UTF-8 inside a string reads as U+FFFD, as Node's decoder makes it
 * before JSON.parse sees it. Numbers go through strtod, which needs the C
 * locale: nothing in the C tree may call setlocale for LC_NUMERIC.
 *
 * sk_json_parse builds a tree in an arena on top of the reader. An
 * object's members are in the order a JavaScript object keeps them:
 * array-index keys ("0", "17") first, ascending, then the rest as they
 * came, and a repeated key holds its first place with its last value. So
 * walking a member list matches Object.keys, and writing the tree back
 * matches JSON.stringify(JSON.parse(text)).
 *
 * sk_jw writes what JSON.stringify(value, null, indent) writes, byte for
 * byte, number formatting included. The generated settings files are
 * named for the hash of that text (decision 0030). */
#ifndef SK_JSON_H
#define SK_JSON_H

#include "arena.h"
#include "str.h"

#define SK_JSON_MAX_DEPTH 256

typedef enum {
    SK_JSON_NONE, /* nothing there, or an error */
    SK_JSON_NULL,
    SK_JSON_BOOL,
    SK_JSON_NUMBER,
    SK_JSON_STRING,
    SK_JSON_ARRAY,
    SK_JSON_OBJECT,
} sk_json_type;

typedef struct {
    const char *s;
    size_t n, i;
    unsigned depth;
    bool first; /* the open container has had no member yet */
    unsigned char kind[SK_JSON_MAX_DEPTH];
    const char *error; /* the first error, or NULL */
    size_t error_at;
    sk_buf key, str; /* decoded text of the last key and string */
} sk_jr;

void sk_jr_init(sk_jr *r, const char *s, size_t n);
void sk_jr_free(sk_jr *r);
/* The type of the next value, without reading it. */
sk_json_type sk_jr_peek(sk_jr *r);
bool sk_jr_null(sk_jr *r);
bool sk_jr_bool(sk_jr *r, bool *out);
bool sk_jr_number(sk_jr *r, double *out);
/* The string, decoded. It points into the input when it needed no
 * decoding, else into the reader; it stays valid until the next string. */
bool sk_jr_string(sk_jr *r, sk_str *out);
/* Enters an object: then while (sk_jr_key(r, &k)) read or skip a value. */
bool sk_jr_object(sk_jr *r);
/* The next member's key, valid until the next key. False at the closing
 * brace, which it consumes, or on an error. */
bool sk_jr_key(sk_jr *r, sk_str *key);
/* Enters an array: then while (sk_jr_item(r)) read or skip a value. */
bool sk_jr_array(sk_jr *r);
bool sk_jr_item(sk_jr *r);
/* Reads past one value of any kind, checking it but decoding nothing. */
bool sk_jr_skip(sk_jr *r);
/* True when only whitespace is left and no error happened. */
bool sk_jr_done(sk_jr *r);
bool sk_jr_failed(const sk_jr *r);

typedef struct sk_json sk_json;
typedef struct sk_json_member sk_json_member;

struct sk_json {
    sk_json_type type;
    union {
        bool boolean;
        double number;
        sk_str string; /* NUL-terminated in the arena */
        struct {
            sk_json *items;
            size_t len;
        } array;
        struct {
            sk_json_member *members;
            size_t len;
        } object;
    };
};

struct sk_json_member {
    sk_str key;
    sk_json value;
};

typedef struct {
    const char *message;
    size_t offset;
} sk_json_error;

/* The whole of s as one value, in a, or NULL with *err set when err is
 * not NULL. */
sk_json *sk_json_parse(sk_arena *a, const char *s, size_t n, sk_json_error *err);
/* The member's value; NULL when v is not an object or lacks the key. */
const sk_json *sk_json_get(const sk_json *v, const char *key);
/* Follows keys: sk_json_dig(v, "cost", "total_cost_usd", NULL). */
const sk_json *sk_json_dig(const sk_json *v, ...);
const sk_json *sk_json_at(const sk_json *v, size_t i);
/* True and the value when v has that type; numbers must be finite, as
 * the JavaScript app's Number.isFinite checks required. */
bool sk_json_as_number(const sk_json *v, double *out);
bool sk_json_as_string(const sk_json *v, sk_str *out);
bool sk_json_as_bool(const sk_json *v, bool *out);

typedef struct {
    sk_buf *out;
    int indent; /* 0 writes compact JSON */
    unsigned depth;
    bool first, after_key;
} sk_jw;

void sk_jw_init(sk_jw *w, sk_buf *out, int indent);
void sk_jw_object(sk_jw *w);
void sk_jw_object_end(sk_jw *w);
void sk_jw_array(sk_jw *w);
void sk_jw_array_end(sk_jw *w);
void sk_jw_key(sk_jw *w, sk_str key);
void sk_jw_string(sk_jw *w, sk_str s);
/* A number as JavaScript prints it; NaN and the infinities as null. */
void sk_jw_number(sk_jw *w, double d);
void sk_jw_int(sk_jw *w, int64_t i);
void sk_jw_bool(sk_jw *w, bool b);
void sk_jw_null(sk_jw *w);
void sk_jw_value(sk_jw *w, const sk_json *v);
/* A value already written as JSON, such as a list a source stored. */
void sk_jw_raw(sk_jw *w, sk_str json);
/* Appends d formatted by ECMAScript's Number::toString. */
void sk_json_format_number(sk_buf *b, double d);

#endif
