#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

/* Tot ce afișează meniul. Câmpurile has_* spun dacă aplicația a trimis deja datele. */
typedef struct {
    /* de la senzor (local) */
    int32_t raw;
    int pct, stable, sensor_ok;
    int32_t cal_empty, cal_full;
    /* legătura */
    int app;                    /* 0 = niciun client BLE, 1 = client conectat dar aplicația nu răspunde,
                                   2 = aplicație activă (după heartbeat-ul ESP32) */
    int mock;                   /* senzor simulat (buton în pagina de calibrare) */
    uint32_t last_app_ms;       /* ultimul mesaj M/T/U/W de la aplicație; 0 = niciodată */
    /* de la aplicație */
    int has_ml, ml, cap;
    int has_today, total, goal, sips;
    int since_min;              /* -1 = nicio înghițitură azi, -2 = necunoscut */
    uint32_t since_at_ms;       /* când a venit since_min */
    int has_week, week[7];      /* week[6] = azi */
    /* heartbeat ESP32 (H,<uptime_s>,<aplicație> la 2 s) */
    uint32_t esp_last_ms;       /* 0 = n-a venit niciodată */
    uint32_t esp_uptime;
    int esp_restarts;           /* de câte ori a scăzut uptime-ul = ESP32 a repornit */
} ui_state_t;

#define ESP_TIMEOUT_MS  5000    /* 2 heartbeat-uri pierdute + marjă */

typedef enum { UI_EV_NONE = 0, UI_EV_SET_EMPTY, UI_EV_SET_FULL, UI_EV_TOGGLE_MOCK } ui_event_t;

void display_init(void);
/* Redesenează ce s-a schimbat pe pagina curentă. Ieftin dacă nu s-a schimbat nimic. */
void display_update(const ui_state_t *st);
/* De apelat des (~20 ms): schimbă pagina la atingerea barei de jos; întoarce butoanele apăsate. */
ui_event_t display_poll_touch(const ui_state_t *st);
/* Mesaj scurt pe pagina de calibrare (ok = 1 verde, 0 roșu). */
void display_notice(const char *msg, int ok);

#endif /* DISPLAY_H */
