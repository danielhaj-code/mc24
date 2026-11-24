/*
 * sensor.h — citirea senzorului analogic de nivel (PA5) și prelucrarea valorii.
 */
#ifndef SENSOR_H
#define SENSOR_H

#include <stdint.h>

/* Alimentează senzorul, face SAMPLES conversii ADC, întoarce media fără extreme (0..4095).
 * Durează ~6 ms (SENSOR_SETTLE_MS + conversiile). */
int32_t sensor_read(void);

/* Adaugă citirea la ultimele STABLE_WINDOW și întoarce 1 dacă toate sunt apropiate. */
int sensor_stable(int32_t raw);

/* Procentul 0..100 dintre „gol” și „plin”; merge și când gol > plin (senzor inversat). */
int sensor_percent(int32_t raw, int32_t empty, int32_t full);

#endif /* SENSOR_H */
