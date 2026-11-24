/* display.h — interfața meniului de pe LCD-ul plăcii STM32F429I-Discovery (240 x 320, tactil).
 *
 * Rol în sistem: app.c ține un singur ui_state_t (variabila `u`), îl actualizează din senzor
 * și din mesajele venite pe legătura serială (ESP32 / aplicație) și îl dă la fiecare trecere
 * prin app_loop() lui display_update(). Modulul display NU citește nimic singur în afară de
 * panoul tactil și de HAL_GetTick(); tot ce afișează vine din ui_state_t.
 *
 * Contract de utilizare (toate din bucla principală, nimic din ISR):
 *   - display_init() o dată, din app_init(), înainte de orice alt apel;
 *   - display_update(&u) la fiecare trecere prin app_loop() (redesenează doar ce s-a schimbat);
 *   - display_poll_touch(&u) la ~TOUCH_PERIOD_MS; app.c tratează evenimentul întors;
 *   - display_next_page() la apăsarea butonului USER (după antirebond, în app.c);
 *   - display_notice() după o acțiune de calibrare / comutare a senzorului simulat.
 * Desenul se face prin ui_gfx (framebuffer în SDRAM), vezi ui_gfx.h. */
#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include "protocol.h"   /* HIST_POINTS */

/* Tot ce afișează meniul. Câmpurile has_* spun dacă aplicația a trimis deja datele.
 * Scris doar de app.c (bucla principală); display.c îl citește și ține o copie (`shown`) cu ce a
 * desenat ultima dată, ca să știe ce s-a schimbat. Toate timpii *_ms sunt valori HAL_GetTick(). */
typedef struct {
    /* de la senzor (local) — actualizate la fiecare citire (READ_PERIOD_MS) */
    int32_t raw;                /* citirea brută (ADC 0..4095 sau HX711 cu semn), unități de convertor */
    int pct, stable, sensor_ok; /* pct: nivel 0..100 din raw și calibrare; stable: 1 = citiri stabile;
                                   sensor_ok: 0 = citirea a eșuat (atunci stable = 0) */
    int32_t cal_empty, cal_full; /* calibrarea: valorile raw pentru sticlă goală / plină */
    /* legătura */
    int app;                    /* 0 = niciun client BLE, 1 = client conectat dar aplicația nu răspunde,
                                   2 = aplicație activă (după heartbeat-ul ESP32) */
    int mock;                   /* senzor simulat (buton în pagina de calibrare) */
    uint32_t last_app_ms;       /* ultimul mesaj M/T/U/W de la aplicație; 0 = niciodată */
    /* de la aplicație (prin ESP32); valide doar dacă has_* = 1 */
    int has_ml, ml, cap;        /* M: ml = apa din sticlă, cap = capacitatea sticlei, ambele în mL */
    int has_today, total, goal, sips;   /* T: băut azi (mL), obiectivul zilnic (mL), nr. de înghițituri */
    /* U și W: primite și păstrate, dar meniul actual nu le afișează (sips, since_*, week nu apar
       pe nicio pagină; contează doar la state_changed, deci produc o redesenare la sosire) */
    int since_min;              /* minute de la ultima înghițitură; -1 = nicio înghițitură azi, -2 = necunoscut */
    uint32_t since_at_ms;       /* când a venit since_min */
    int has_week, week[7];      /* W: mL băuți pe zi, ultimele 7 zile; week[6] = azi */
    /* istoricul nivelului, ținut de ESP32 (G,<i>,<procent>) */
    int has_hist;
    int8_t hist[HIST_POINTS];   /* procent 0..100, -1 = fără citiri în cele 10 s; [59] = acum */
    /* heartbeat ESP32 (H,<uptime_s>,<aplicație> la 2 s) */
    uint32_t esp_last_ms;       /* 0 = n-a venit niciodată */
    uint32_t esp_uptime;        /* secunde de la pornirea ESP32, din ultimul heartbeat */
    int esp_restarts;           /* de câte ori a scăzut uptime-ul = ESP32 a repornit */
} ui_state_t;

/* ESP32-ul e considerat „viu” cât timp ultimul heartbeat e mai nou de atât (ms). */
#define ESP_TIMEOUT_MS  5000    /* 2 heartbeat-uri pierdute + marjă */

/* Ce a apăsat utilizatorul pe ecran; tratat de app_loop():
 * SET_EMPTY / SET_FULL = calibrare cu citirea curentă, TOGGLE_MOCK = comută senzorul simulat. */
typedef enum { UI_EV_NONE = 0, UI_EV_SET_EMPTY, UI_EV_SET_FULL, UI_EV_TOGGLE_MOCK } ui_event_t;

/* Pornește LCD-ul (BSP + LTDC + SDRAM), rotirea 180° dacă e cerută, ecranul tactil; șterge ecranul. */
void display_init(void);
/* Redesenează ce s-a schimbat pe pagina curentă. Ieftin dacă nu s-a schimbat nimic. */
void display_update(const ui_state_t *st);
/* De apelat des (~20 ms): schimbă pagina la atingerea barei de jos; întoarce butoanele apăsate. */
ui_event_t display_poll_touch(const ui_state_t *st);
/* Butonul albastru USER: trece la pagina următoare (după ultima revine la prima). */
void display_next_page(void);
/* Mesaj scurt pe pagina de calibrare (ok = 1 albastru/accent, 0 roz/eroare), vizibil NOTICE_MS (4 s).
 * Forțează redesenarea completă a paginii curente la următorul display_update(). */
void display_notice(const char *msg, int ok);

#endif /* DISPLAY_H */
