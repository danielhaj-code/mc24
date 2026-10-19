#include "filter.h"

static void insertion_sort(int32_t *a, int n)
{
    for (int i = 1; i < n; i++) {
        int32_t v = a[i];
        int j = i - 1;
        while (j >= 0 && a[j] > v) {
            a[j + 1] = a[j];
            j--;
        }
        a[j + 1] = v;
    }
}

int32_t filter_trimmed_mean(int32_t *s, int n, int trim)
{
    if (n <= 0) return 0;
    insertion_sort(s, n);
    if (trim < 0) trim = 0;
    if (2 * trim >= n) return s[n / 2];

    int64_t sum = 0;
    int used = n - 2 * trim;
    for (int i = trim; i < n - trim; i++) sum += s[i];
    /* rotunjire corectă și pentru valori negative (HX711) */
    return (int32_t)(sum >= 0 ? (sum + used / 2) / used : (sum - used / 2) / used);
}

void stability_init(stability_t *st, int window, int32_t threshold)
{
    if (window < 1) window = 1;
    if (window > STAB_MAX_WINDOW) window = STAB_MAX_WINDOW;
    st->window = window;
    st->threshold = threshold;
    stability_reset(st);
}

void stability_reset(stability_t *st)
{
    st->count = 0;
    st->head = 0;
}

int stability_push(stability_t *st, int32_t value)
{
    st->buf[st->head] = value;
    st->head = (st->head + 1) % st->window;
    if (st->count < st->window) st->count++;
    if (st->count < st->window) return 0;

    int32_t mn = st->buf[0], mx = st->buf[0];
    for (int i = 1; i < st->window; i++) {
        if (st->buf[i] < mn) mn = st->buf[i];
        if (st->buf[i] > mx) mx = st->buf[i];
    }
    return (mx - mn) <= st->threshold;
}

int level_percent(int32_t raw, int32_t empty, int32_t full)
{
    int64_t span = (int64_t)full - empty;
    if (span == 0) return 0;
    int64_t pct = ((int64_t)raw - empty) * 100 / span;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return (int)pct;
}
