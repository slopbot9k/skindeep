/* The usage limits as one list, limits.items (decision 0011), as
 * app/metrics/limits.js builds it: every limit the provider sends now,
 * account-wide first, then each model's. An item is { key, label,
 * window, model, pct, resets_at }: window a short span such as 5h, 7d or
 * 30d (null for a spend limit), model the provider's name for the model
 * or null, pct the percent used, resets_at Unix seconds or null. A limit
 * with no reading is no item. */
#ifndef SK_LIMITS_H
#define SK_LIMITS_H

#include "bus.h"

/* minutes as a short span: 300 is "5h", 10080 "7d", 90 "90m". False,
 * writing nothing, when minutes is not a positive number. */
bool sk_limit_span(double minutes, sk_buf *out);

typedef struct {
    sk_buf json;
    size_t n;
} sk_limits;

/* One item; none when pct is not a finite number. window, model and
 * label may be NULL or empty; resets_at may be NAN for none. The label,
 * when not given, is the model and the window, such as "Spark 5h", else
 * the key. */
void sk_limits_add(sk_limits *l, const char *key, const char *window, const char *model,
                   const char *label, double pct, double resets_at);
/* limits.items into src: the list, or null with no item. */
void sk_limits_set(sk_limits *l, sk_source *src);
void sk_limits_free(sk_limits *l);

#endif
