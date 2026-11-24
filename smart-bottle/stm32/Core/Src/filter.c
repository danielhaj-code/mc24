/*
 * filter.c — implementarea funcțiilor din filter.h (medie trunchiată, stabilitate, procent).
 * Fără stare globală și fără HAL; apelate doar din bucla principală (sensor.c, app.c).
 */
#include "filter.h"

/* Sortare crescătoare pe loc. Insertion sort e suficient: n e mic (câteva zeci de eșantioane),
 * nu cere memorie suplimentară și e rapid pe date aproape sortate (citirile sunt apropiate). */
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

/* Vezi filter.h pentru algoritm și exemplu. */
int32_t filter_trimmed_mean(int32_t *s, int n, int trim)
{
    if (n <= 0) return 0;
    insertion_sort(s, n);
    if (trim < 0) trim = 0;
    if (2 * trim >= n) return s[n / 2];   /* n-ar rămâne nimic de mediat: mediana (superioară la n par) */

    int64_t sum = 0;   /* 64 de biți: n valori de ~24 de biți pot depăși int32 */
    int used = n - 2 * trim;
    for (int i = trim; i < n - trim; i++) sum += s[i];
    /* rotunjire corectă și pentru valori negative (HX711): împărțirea în C trunchiază spre zero,
     * deci adăugăm jumătate din împărțitor pentru sum >= 0 și o scădem pentru sum < 0.
     * Ex. used = 4: 10/4 = 2.5 -> (10+2)/4 = 3;  -10/4 = -2.5 -> (-10-2)/4 = -3 (simetric).
     * Fără ramura negativă, (-10+2)/4 = -2 => eroare sistematică spre zero. */
    return (int32_t)(sum >= 0 ? (sum + used / 2) / used : (sum - used / 2) / used);
}

/* Vezi filter.h. window e limitat la 1..STAB_MAX_WINDOW ca indexul în buf să fie mereu valid. */
void stability_init(stability_t *st, int window, int32_t threshold)
{
    if (window < 1) window = 1;
    if (window > STAB_MAX_WINDOW) window = STAB_MAX_WINDOW;
    st->window = window;
    st->threshold = threshold;
    stability_reset(st);
}

/* Golește fereastra; buf nu se șterge — count = 0 garantează că valorile vechi nu sunt comparate. */
void stability_reset(stability_t *st)
{
    st->count = 0;
    st->head = 0;
}

/*
 * Ring buffer: head avansează circular (modulo window), suprascriind cea mai veche citire.
 * Până se umple fereastra (count < window) întoarce 0 — nu declarăm „stabil” pe prea puține date.
 * Apoi compară min/max pe toată fereastra (ordinea nu contează, deci nu trebuie parcurs de la head).
 * threshold mic => stabil doar când sticla stă complet nemișcată; mare => tolerează vibrații.
 */
int stability_push(stability_t *st, int32_t value)
{
    st->buf[st->head] = value;
    st->head = (st->head + 1) % st->window;
    if (st->count < st->window) st->count++;
    if (st->count < st->window) return 0;

    int32_t mn = st->buf[0], mx = st->buf[0];   /* fereastra e plină, deci buf[0..window-1] sunt valide */
    for (int i = 1; i < st->window; i++) {
        if (st->buf[i] < mn) mn = st->buf[i];
        if (st->buf[i] > mx) mx = st->buf[i];
    }
    return (mx - mn) <= st->threshold;
}

/* Vezi filter.h. Ex. empty = 1000, full = 2000, raw = 1500 -> 50;
 * senzor inversat empty = 2000, full = 1000, raw = 1250 -> (-750 * 100) / -1000 = 75. */
int level_percent(int32_t raw, int32_t empty, int32_t full)
{
    int64_t span = (int64_t)full - empty;   /* cast înainte de scădere: altfel overflow pe int32 */
    if (span == 0) return 0;
    int64_t pct = ((int64_t)raw - empty) * 100 / span;   /* poate ieși < 0 sau > 100 (sub gol / peste plin) */
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return (int)pct;
}
