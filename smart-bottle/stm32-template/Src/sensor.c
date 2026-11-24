/*
 * sensor.c — senzorul analogic de nivel pe PA5 (ADC1, canalul 5).
 *
 * Senzorul e alimentat de pe PE4 doar cât durează măsurarea: un senzor rezistiv ținut permanent
 * sub tensiune în apă se corodează (electroliză).
 */
#include "sensor.h"
#include "app_config.h"
#include "main.h"

int32_t sensor_read(void)
{
    int32_t s[SAMPLES];

    HAL_GPIO_WritePin(SENS_PWR_GPIO_Port, SENS_PWR_Pin, GPIO_PIN_SET);   /* pornește senzorul */
    HAL_Delay(SENSOR_SETTLE_MS);

    for (int i = 0; i < SAMPLES; i++) {
        HAL_ADC_Start(&hadc1);
        HAL_ADC_PollForConversion(&hadc1, 10);         /* o conversie durează ~4 µs */
        s[i] = (int32_t)HAL_ADC_GetValue(&hadc1);
    }
    HAL_ADC_Stop(&hadc1);
    HAL_GPIO_WritePin(SENS_PWR_GPIO_Port, SENS_PWR_Pin, GPIO_PIN_RESET); /* oprește senzorul */

    /* sortare prin inserție (doar 16 valori), apoi media fără TRIM valori la fiecare capăt:
     * un vârf izolat de zgomot ajunge la capăt și e aruncat, restul se mediază */
    for (int i = 1; i < SAMPLES; i++) {
        int32_t v = s[i];
        int j = i - 1;
        while (j >= 0 && s[j] > v) { s[j + 1] = s[j]; j--; }
        s[j + 1] = v;
    }
    int32_t sum = 0;
    for (int i = TRIM; i < SAMPLES - TRIM; i++) sum += s[i];
    return sum / (SAMPLES - 2 * TRIM);
}

int sensor_stable(int32_t raw)
{
    static int32_t last[STABLE_WINDOW];   /* ultimele citiri, ca într-un buffer circular */
    static int count = 0, pos = 0;

    last[pos] = raw;
    pos = (pos + 1) % STABLE_WINDOW;
    if (count < STABLE_WINDOW) count++;
    if (count < STABLE_WINDOW) return 0;  /* încă nu avem destule citiri */

    int32_t mn = last[0], mx = last[0];
    for (int i = 1; i < STABLE_WINDOW; i++) {
        if (last[i] < mn) mn = last[i];
        if (last[i] > mx) mx = last[i];
    }
    return (mx - mn) <= STABLE_THRESHOLD;
}

int sensor_percent(int32_t raw, int32_t empty, int32_t full)
{
    if (full == empty) return 0;                      /* calibrare invalidă */
    int32_t pct = (raw - empty) * 100 / (full - empty);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return (int)pct;
}
