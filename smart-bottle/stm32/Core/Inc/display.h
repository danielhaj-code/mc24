#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

void display_init(void);
/* Redesenează doar dacă s-a schimbat ceva față de ultimul apel. */
void display_update(int32_t raw, int pct, int stable, int ble_connected, int sensor_ok);

#endif /* DISPLAY_H */
