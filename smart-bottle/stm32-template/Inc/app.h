/*
 * app.h — starea aplicației și cele două funcții chemate din main.c.
 */
#ifndef APP_H
#define APP_H

#include <stdint.h>

/* Tot ce știe placa și ce afișează LCD-ul. */
typedef struct {
    int32_t  raw;            /* ultima citire a senzorului (0..4095) */
    int      pct;            /* nivelul în procente, după calibrare */
    int      stable;         /* 1 = ultimele citiri sunt stabile */
    int32_t  cal_empty;      /* valoarea brută cu recipientul gol (din „C”) */
    int32_t  cal_full;       /* valoarea brută cu recipientul plin */
    int      ml;             /* volumul calculat de aplicație (din „M”) */
    int      capacity;       /* capacitatea recipientului, mL (din „M”) */
    uint32_t ml_ms;          /* când a venit ultimul „M” (HAL_GetTick); 0 = niciodată */
    int      app_state;      /* din „H”: 0 = niciun telefon, 1 = conectat dar tace, 2 = aplicație activă */
    uint32_t app_ms;         /* când a venit ultimul „H”; 0 = niciodată */
} app_state_t;

void app_init(void);         /* o dată, după inițializarea perifericelor */
void app_loop(void);         /* la nesfârșit, din while(1) */

/* 1 dacă aplicația web e activă (heartbeat recent de la ESP32). */
int app_connected(const app_state_t *st);

#endif /* APP_H */
