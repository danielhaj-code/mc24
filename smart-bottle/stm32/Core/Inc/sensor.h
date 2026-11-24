#ifndef SENSOR_H
#define SENSOR_H

#include <stdint.h>

/*
 * sensor — achiziția nivelului de apă, într-o singură interfață pentru ambele variante hardware:
 *   SENSOR_USE_HX711=0: senzor rezistiv pe ADC1/PA5, alimentat doar în timpul citirii (PE4);
 *   SENSOR_USE_HX711=1: celulă de sarcină prin HX711 (DT = PE2, SCK = PE3, bit-bang).
 * Valoarea întoarsă e „brută” (unități ADC 0..4095 sau unități HX711 cu semn, 24 biți); conversia
 * în procent se face în app.c cu calibrarea gol/plin (level_percent din filter.h).
 * Pinii trebuie configurați înainte (MX_GPIO_Init/MX_ADC1_Init din main.c).
 */

/* Inițializează senzorul ales în app_config.h (SENSOR_USE_HX711). Apelată o dată din app_init(). */
void sensor_init(void);

/* Face o citire completă: SAMPLES_PER_READING eșantioane + medie trunchiată.
 * Întoarce 1 la succes și pune valoarea filtrată în *out; 0 dacă mai puțin de jumătate din
 * eșantioane au reușit (senzor lipsă/defect), caz în care *out nu e modificat.
 * Blocantă: ~5 ms (analogic) sau 15 × perioada HX711 (~1,5 s la 10 SPS, ~190 ms la 80 SPS);
 * fără senzor HX711 conectat, până la 15 × 200 ms = 3 s. Apelată din bucla principală (app.c). */
int sensor_read(int32_t *out);

#endif /* SENSOR_H */
