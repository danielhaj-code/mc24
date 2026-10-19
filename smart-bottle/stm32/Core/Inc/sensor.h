#ifndef SENSOR_H
#define SENSOR_H

#include <stdint.h>

/* Inițializează senzorul ales în app_config.h (SENSOR_USE_HX711). */
void sensor_init(void);

/* Face o citire completă: SAMPLES_PER_READING eșantioane + medie trunchiată.
 * Întoarce 1 la succes și pune valoarea filtrată în *out. */
int sensor_read(int32_t *out);

#endif /* SENSOR_H */
