/* The metric vocabulary skins bind to (decision 0011), as
 * app/metrics/names.js has it. docs/SKINS.md documents each name. */
#ifndef SK_NAMES_H
#define SK_NAMES_H

#include "../base/str.h"

typedef struct {
    const char *name, *meaning;
} sk_metric_name;

extern const sk_metric_name sk_metric_names[];
extern const size_t sk_metric_names_len;

/* A name of the vocabulary, or tools.<1 to 64 of A-Z a-z 0-9 _>. */
bool sk_metric_known(sk_str name);
/* checklist.items and limits.items hold lists, not numbers. */
bool sk_metric_is_list(sk_str name);
/* tools.<name> for tool name, each character outside A-Z a-z 0-9 _ as
 * _, cut to 64 characters, as toolKey does. Appends to out. */
void sk_metric_tool_key(sk_buf *out, sk_str tool);

#endif
