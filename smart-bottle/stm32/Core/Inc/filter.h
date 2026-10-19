#ifndef FILTER_H
#define FILTER_H

#include <stdint.h>

/* Medie trunchiată: sortează n eșantioane, aruncă `trim` valori de la fiecare
 * capăt și face media restului. Elimină spike-urile ca o mediană și netezește
 * ca o medie. Modifică vectorul (îl sortează). Dacă 2*trim >= n, întoarce mediana. */
int32_t filter_trimmed_mean(int32_t *s, int n, int trim);

/* Detecție de stabilitate pe ultimele `window` citiri (max - min <= threshold). */
#define STAB_MAX_WINDOW 16

typedef struct {
    int32_t buf[STAB_MAX_WINDOW];
    int window;
    int count;
    int head;
    int32_t threshold;
} stability_t;

void stability_init(stability_t *st, int window, int32_t threshold);
void stability_reset(stability_t *st);
/* Adaugă o citire și întoarce 1 dacă fereastra e plină și stabilă. */
int  stability_push(stability_t *st, int32_t value);

/* Procent 0..100 față de calibrare; merge și când full < empty (senzor inversat). */
int level_percent(int32_t raw, int32_t empty, int32_t full);

#endif /* FILTER_H */
