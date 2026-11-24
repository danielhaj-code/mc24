/*
 * filter.h — prelucrarea numerică a citirilor HX711 (cântărirea sticlei).
 *
 * Trei piese independente, toate C pur (fără HAL, testabile pe PC):
 *  - filter_trimmed_mean(): reduce o rafală de eșantioane brute la o singură citire (apelat din sensor.c);
 *  - stability_*: spune când sticla stă nemișcată pe cântar (folosit de app.c înainte de a accepta
 *    o înghițitură sau o calibrare);
 *  - level_percent(): transformă citirea brută în procent de umplere pe baza calibrării gol/plin.
 */
#ifndef FILTER_H
#define FILTER_H

#include <stdint.h>

/* Medie trunchiată: sortează n eșantioane, aruncă `trim` valori de la fiecare
 * capăt și face media restului. Elimină spike-urile ca o mediană și netezește
 * ca o medie. Modifică vectorul (îl sortează). Dacă 2*trim >= n, întoarce mediana.
 * Exemplu, n = 7, trim = 2: {105, 98, 900, 101, 99, -50, 100} -> sortat
 * {-50, 98, 99, 100, 101, 105, 900} -> rămân {99, 100, 101} -> rezultat 100
 * (spike-urile 900 și -50 nu mai contează). n <= 0 => 0; trim < 0 e tratat ca 0.
 * Rezultatul e rotunjit la cel mai apropiat întreg (jumătățile departe de zero). */
int32_t filter_trimmed_mean(int32_t *s, int n, int trim);

/* Detecție de stabilitate pe ultimele `window` citiri (max - min <= threshold).
 * STAB_MAX_WINDOW = capacitatea statică a ferestrei (fără malloc); window cerut mai mare e limitat. */
#define STAB_MAX_WINDOW 16

/* Starea detectorului; câmpurile sunt private (se folosesc doar funcțiile de mai jos).
 * Folosit doar din bucla principală, nu din ISR. */
typedef struct {
    int32_t buf[STAB_MAX_WINDOW];   /* ring buffer cu ultimele citiri (doar primele `window` folosite) */
    int window;                     /* câte citiri se compară, 1..STAB_MAX_WINDOW */
    int count;                      /* câte citiri valide sunt în buf (satură la window) */
    int head;                       /* unde se scrie următoarea citire, 0..window-1 */
    int32_t threshold;              /* abaterea max admisă (max - min), în unități brute HX711 */
} stability_t;

/* Configurează fereastra (limitată la 1..STAB_MAX_WINDOW) și pragul, apoi golește istoricul. */
void stability_init(stability_t *st, int window, int32_t threshold);
/* Uită citirile anterioare (ex. după o calibrare); stabil abia după `window` citiri noi. */
void stability_reset(stability_t *st);
/* Adaugă o citire și întoarce 1 dacă fereastra e plină și stabilă.
 * Cost: O(window) comparații pe apel. */
int  stability_push(stability_t *st, int32_t value);

/* Procent 0..100 față de calibrare; merge și când full < empty (senzor inversat).
 * Formula: (raw - empty) * 100 / (full - empty), trunchiată spre zero, apoi limitată la 0..100.
 * Calcul pe 64 de biți: diferențele de int32 și produsul cu 100 pot depăși int32.
 * Senzor inversat (full < empty): numărătorul și numitorul sunt ambele negative => procent pozitiv.
 * empty == full (necalibrat) => 0, fără împărțire la zero. */
int level_percent(int32_t raw, int32_t empty, int32_t full);

#endif /* FILTER_H */
