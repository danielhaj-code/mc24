/*
 * app.c — „creierul” firmware-ului: leagă senzorul, LCD-ul și legătura serială cu ESP32.
 *
 * Apelat de main.c: app_init() o dată, apoi app_loop() la nesfârșit (super-buclă, fără RTOS).
 * Programare cooperativă pe baza HAL_GetTick() (ms de la pornire, incrementat de SysTick):
 *   - de câte ori se poate: golește comenzile primite (link_poll -> handle);
 *   - la READ_PERIOD_MS (1 s): o citire de senzor + mesajul L,<raw>,<pct>,<stabil> spre ESP32;
 *   - la TOUCH_PERIOD_MS (20 ms): touch-ul LCD-ului și butonul USER (antirebond);
 *   - la fiecare trecere: display_update(), care decide singur ce redesenează.
 * Comparațiile „HAL_GetTick() - t >= perioadă” pe unsigned rămân corecte și la depășirea
 * contorului de 32 biți (după ~49 de zile).
 *
 * Recepția UART se face în întrerupere (link.c) într-un buffer; aici, în bucla principală, se
 * consumă doar linii complete deja decodificate (protocol.c), deci starea `u` e scrisă exclusiv din
 * bucla principală și nu are nevoie de protecție la concurență.
 *
 * LED-uri: LD3 (verde) aprins = aplicația de pe telefon e activă (stare 2 din heartbeat H);
 *          LD4 (roșu) își schimbă starea la fiecare citire de senzor eșuată (clipește cât timp
 *          senzorul lipsește; o eroare fatală îl face să clipească repede, vezi Error_Handler).
 * Formatul mesajelor: PROTOCOL.md.
 */
#include "app.h"
#include "app_config.h"
#include "main.h"
#include "stm32f429i_discovery.h"   /* LED3 (PG13) / LED4 (PG14), butonul USER (PA0) */
#include "filter.h"
#include "protocol.h"
#include "sensor.h"
#include "link.h"
#include "display.h"
#include <stdio.h>

#define TOUCH_PERIOD_MS  20   /* ms; perioada de citire touch + buton (50 Hz, destul pentru deget) */

static ui_state_t u;                 /* tot ce afișează meniul (definit în display.h); scris doar aici,
                                      * citit de display_update/display_poll_touch */
/* Starea internă a planificatorului (nu se afișează). */
static struct {
    uint32_t last_read_ms, last_touch_ms;   /* HAL_GetTick() la ultima citire / ultimul poll touch */
    uint8_t btn_hist;                /* ultimele eșantioane ale butonului USER, câte unul la 20 ms;
                                      * bitul 0 = cel mai nou, 1 = apăsat */
    stability_t stab;                /* fereastra pentru detecția „nivel stabil” (filter.c) */
} s;

/* Starea aplicației de pe telefon, așa cum o raportează ESP32:
 *   0 = niciun client BLE, 1 = client conectat dar aplicația nu răspunde, 2 = aplicația e activă.
 * Memorată în u.app (afișată pe LCD, decide „trimis/doar local” la calibrare) și oglindită pe
 * LED3 verde, aprins cât timp aplicația e activă (doar starea 2). */
static void set_app(int state)
{
    u.app = state;
    if (state == 2) BSP_LED_On(LED3); else BSP_LED_Off(LED3);
}

/* ---- senzor simulat: o sticlă din care se bea din când în când ----
 * Pentru demonstrații fără senzor: activat din pagina de calibrare (buton tactil, UI_EV_TOGGLE_MOCK).
 * Modelul: nivelul (0..1000 = 0..100,0%) scade cu o „înghițitură” de 4..11% la intervale aleatoare
 * de 15..40 s; sub 15% sticla se „reumple” la 100%. Valoarea brută se obține prin calibrarea curentă,
 * deci restul lanțului (procent, stabilitate, mesaj L, aplicație) funcționează exact ca la senzorul real. */
static struct { int32_t level; /* 0..1000 */ uint32_t next_ms, seed; } sim;   /* next_ms = momentul
                                       * următoarei înghițituri; seed = starea generatorului */

/* Număr pseudo-aleator în [0, n). Generator congruențial liniar (constantele din „Numerical
 * Recipes”): seed = seed × 1664525 + 1013904223 mod 2^32. Biții de jos ai unui LCG au perioadă
 * scurtă, de aceea se folosesc biții 16..31. Suficient pentru o simulare, nu pentru criptografie. */
static uint32_t sim_rand(uint32_t n)
{
    sim.seed = sim.seed * 1664525u + 1013904223u;
    return (sim.seed >> 16) % n;
}

/* O „citire” simulată, chemată din do_reading() în locul lui sensor_read() când u.mock e activ.
 * Avansează modelul dacă a venit momentul (comparația cu semn tolerează depășirea lui HAL_GetTick)
 * și întoarce cal_empty + span × nivel, plus zgomot -2..+2 ca detecția de stabilitate să aibă ce face. */
static int32_t sim_read(void)
{
    if ((int32_t)(HAL_GetTick() - sim.next_ms) >= 0) {
        if (sim.level < 150) sim.level = 1000;                     /* reumplere */
        else sim.level -= 40 + (int32_t)sim_rand(70);              /* înghițitură de 4..11% */
        if (sim.level < 0) sim.level = 0;
        sim.next_ms = HAL_GetTick() + 15000u + sim_rand(25000u);   /* următoarea peste 15..40 s */
    }
    int32_t span = u.cal_full - u.cal_empty;
    return u.cal_empty + span * sim.level / 1000 + (int32_t)sim_rand(5) - 2;  /* zgomot ±2 */
}

/* Comută între senzorul real și cel simulat (buton tactil din pagina de calibrare).
 * La pornirea simulării: sticla începe la 80%, prima înghițitură după 10 s, iar seed-ul se
 * amestecă cu timpul curent ca două sesiuni să nu fie identice. Fereastra de stabilitate se
 * golește în ambele sensuri, ca valorile vechi ale celeilalte surse să nu o influențeze. */
static void toggle_mock(void)
{
    u.mock = !u.mock;
    if (u.mock) {
        sim.level = 800;
        sim.seed ^= HAL_GetTick();
        sim.next_ms = HAL_GetTick() + 10000u;
    }
    stability_reset(&s.stab);
    display_notice(u.mock ? "senzor simulat pornit" : "senzor real", 1);
}

/* O citire completă + trimiterea ei. Chemată la fiecare READ_PERIOD_MS din app_loop, la cererea
 * telefonului (R) și o dată în app_init. Blocantă cât sensor_read (~5 ms analogic, ~1,5 s HX711).
 * Succes: actualizează u.raw/u.pct/u.stable și trimite L,<raw>,<pct>,<0|1>.
 * Eșec: marchează senzorul ca indisponibil (LCD arată eroarea), resetează stabilitatea, comută LD4
 * și NU trimite nimic — aplicația vede lipsa mesajelor L, nu o valoare falsă. */
static void do_reading(void)
{
    int32_t raw;
    char msg[PROTO_LINE_MAX];

    /* operatorul virgulă: în modul simulat setează raw și dă ok = 1 (simularea nu „eșuează”) */
    int ok = u.mock ? (raw = sim_read(), 1) : sensor_read(&raw);
    if (ok) {
        u.sensor_ok = 1;
        u.raw = raw;
        u.stable = stability_push(&s.stab, raw);
        u.pct = level_percent(raw, u.cal_empty, u.cal_full);
    } else {
        u.sensor_ok = 0;
        u.stable = 0;
        stability_reset(&s.stab);
        BSP_LED_Toggle(LED4);   /* LED roșu clipește la eroare de senzor (o schimbare pe citire, ~0,5 Hz) */
    }

    if (u.sensor_ok) {
        size_t n = proto_format_level(msg, sizeof msg, u.raw, u.pct, u.stable);
        if (n) link_send(msg, n);   /* ESP32 aruncă mesajul dacă nu e telefon conectat */
    }
}

/* Setează calibrarea (valori brute pentru 0% și 100%) și recalculează imediat procentul afișat
 * din ultima valoare brută, fără să aștepte citirea următoare. Apelantul garantează empty != full
 * (altfel level_percent ar împărți la zero). Nu salvează nimic: la reset revin valorile implicite
 * până când aplicația retrimite C,... */
static void set_calibration(int32_t empty, int32_t full)
{
    u.cal_empty = empty;
    u.cal_full = full;
    u.pct = level_percent(u.raw, u.cal_empty, u.cal_full);
}

/* Execută o comandă primită pe legătură (deja validată de protocol.c: câmpuri numerice, indici în
 * interval). Chemată din app_loop, în bucla principală. Răspunsurile A/E merg înapoi prin ESP32
 * spre telefon. Câmpurile a/b/c ale comenzii sunt numerele din linie, în ordine. */
static void handle(const proto_cmd_t *c)
{
    switch (c->type) {
    case CMD_CALIBRATE:     /* C,<gol>,<plin>: calibrare de la aplicație (la conectare sau din setări) */
        if (c->a == c->b) {             /* gol = plin ar da împărțire la zero: refuzată cu E,C */
            link_send_str("E,C\n");
        } else {
            set_calibration(c->a, c->b);
            link_send_str("A,C\n");     /* confirmare */
        }
        break;
    case CMD_READ_NOW:      /* R: citire imediată; reprogramează și citirea periodică de la acest moment */
        do_reading();
        s.last_read_ms = HAL_GetTick();
        break;
    case CMD_LINK_STATE:    /* S,<0|1>: de la ESP32, la conectarea/deconectarea unui client BLE */
        /* S spune doar că există o conexiune BLE; heartbeat-ul H spune dacă aplicația e vie */
        /* S,1 păstrează starea 2 dacă heartbeat-ul a spus deja că aplicația e activă, altfel 1;
         * S,0 => 0 (nimeni conectat) */
        set_app(c->a ? (u.app ? u.app : 1) : 0);
        break;
    /* statistici calculate de aplicație, doar pentru afișare; last_app_ms = ultima dată la care
     * aplicația a trimis date (LCD-ul poate arăta cât de vechi sunt) */
    case CMD_VOLUME:        /* M,<ml>,<capacitate_ml>: volum curent în sticlă */
        u.has_ml = 1; u.ml = c->a; u.cap = c->b;
        u.last_app_ms = HAL_GetTick();
        break;
    case CMD_TODAY:         /* T,<total_ml>,<obiectiv_ml>,<nr_înghițituri>: totalul de azi */
        u.has_today = 1; u.total = c->a; u.goal = c->b; u.sips = c->c;
        u.last_app_ms = HAL_GetTick();
        break;
    case CMD_SINCE:         /* U,<minute>: de la ultima înghițitură (-1 = niciuna azi) */
        u.since_min = c->a;
        u.since_at_ms = HAL_GetTick();  /* momentul sosirii; momentan nu e citit de display.c */
        u.last_app_ms = HAL_GetTick();
        break;
    case CMD_DAY:           /* W,<0..6>,<ml>: consumul pe zilele săptămânii, 6 = azi (index verificat) */
        u.week[c->a] = c->b;
        if (c->a == 0) u.has_week = 1;    /* aplicația trimite W,0..6 la conectare */
        u.last_app_ms = HAL_GetTick();
        break;
    case CMD_HISTORY:       /* G,<0..59>,<procent|-1>: istoric nivel de la ESP32 pentru grafic; -1 = lipsă */
        u.hist[c->a] = (int8_t)c->b;
        u.has_hist = 1;
        break;
    case CMD_HEARTBEAT:     /* H,<uptime_s>,<0|1|2>: de la ESP32, la fiecare 2 s */
        /* uptime mai mic decât data trecută = ESP32 a repornit */
        if (u.esp_last_ms && (uint32_t)c->a < u.esp_uptime) u.esp_restarts++;
        u.esp_uptime = (uint32_t)c->a;
        u.esp_last_ms = HAL_GetTick();
        set_app(c->b);                    /* resincronizează și după un S,x pierdut (ex. reset) */
        break;
    default:
        break;   /* CMD_INVALID: ignorat (linie coruptă sau necunoscută) */
    }
}

/* Butoanele de calibrare din meniu: aplicăm local și trimitem aplicației, care o salvează
 * (ea rămâne sursa de adevăr și o retrimite cu C,... la fiecare conectare).
 * full = 0: „gol” = valoarea brută curentă (u.raw), „plin” rămâne; full = 1: invers.
 * Trimite K,<gol>,<plin> (PROTOCOL.md); aplicația îl salvează și răspunde cu C,... Mesajul pleacă
 * mereu — dacă nu e niciun telefon conectat, ESP32 îl aruncă, de aceea nota de pe ecran spune
 * „trimis” doar când aplicația e activă (u.app == 2) și „doar local” altfel.
 * Chemată din app_loop la evenimentele tactile UI_EV_SET_EMPTY / UI_EV_SET_FULL. */
static void calibrate_from_touch(int full)
{
    char msg[PROTO_LINE_MAX], note[40];   /* note: textul notificării de pe LCD */
    int32_t empty = full ? u.cal_empty : u.raw;
    int32_t fullv = full ? u.raw : u.cal_full;
    if (empty == fullv) {
        display_notice("gol = plin, respins", 0);
        return;
    }
    set_calibration(empty, fullv);
    size_t n = proto_format_cal(msg, sizeof msg, empty, fullv);
    if (n) link_send(msg, n);
    /* „·” e UTF-8 (2 octeți); fontul LCD-ului trebuie să-l conțină. %ld + cast: int32_t e long pe ARM */
    snprintf(note, sizeof note, "%s = %ld%s", full ? "plin" : "gol", (long)u.raw,
             u.app == 2 ? " · trimis" : " · doar local");
    display_notice(note, 1);
}

/* Inițializarea aplicației (o dată, din main după perifericele de bază). Starea pornește cu
 * calibrarea implicită și „necunoscut” pentru datele aplicației (since_min = -2, has_* = 0 din
 * inițializarea statică cu zero). Apoi LED-uri, buton, LCD + touch (BSP), senzor, legătură
 * (pornește recepția UART pe întreruperi), o primă citire ca ecranul să nu fie gol, și primul desen. */
void app_init(void)
{
    u.cal_empty = CAL_DEFAULT_EMPTY;
    u.cal_full = CAL_DEFAULT_FULL;
    u.sensor_ok = 1;          /* optimist până la prima citire */
    u.since_min = -2;         /* -2 = încă nu știm (aplicația n-a trimis U) */
    stability_init(&s.stab, STABLE_WINDOW, STABLE_THRESHOLD);

    BSP_LED_Init(LED3);
    BSP_LED_Init(LED4);
    /* citit prin polling în app_loop (fără EXTI) */
    BSP_PB_Init(BUTTON_KEY, BUTTON_MODE_GPIO);   /* B1 albastru (PA0), activ pe 1 (pull-down pe placă) */
    display_init();
    sensor_init();
    link_init();

    do_reading();
    s.last_read_ms = HAL_GetTick();
    display_update(&u);
}

/* O trecere prin bucla principală (vezi programarea din antetul fișierului). Nu are întârzieri
 * proprii: se întoarce imediat dacă nu e nimic de făcut, iar main o recheamă. */
void app_loop(void)
{
    proto_cmd_t c;
    /* toate liniile primite de la ultima trecere, până la CMD_NONE (buffer gol) */
    while ((c = link_poll()).type != CMD_NONE)
        handle(&c);

    if (HAL_GetTick() - s.last_read_ms >= READ_PERIOD_MS) {
        s.last_read_ms = HAL_GetTick();
        do_reading();
    }

    if (HAL_GetTick() - s.last_touch_ms >= TOUCH_PERIOD_MS) {
        s.last_touch_ms = HAL_GetTick();
        /* display_poll_touch citește controllerul tactil (I2C3) și întoarce evenimentul butonului
         * atins, dacă e unul; schimbarea de tab o tratează singur display.c */
        switch (display_poll_touch(&u)) {
        case UI_EV_SET_EMPTY: calibrate_from_touch(0); break;
        case UI_EV_SET_FULL:  calibrate_from_touch(1); break;
        case UI_EV_TOGGLE_MOCK: toggle_mock(); break;
        default: break;
        }
        /* antirebond: o apăsare = 2 eșantioane apăsat (40 ms) după 2 eliberat.
         * btn_hist e un registru de deplasare: la fiecare 20 ms intră starea curentă în bitul 0.
         * Ultimii 4 biți = 0b0011 (cei mai vechi 2 eliberat, cei mai noi 2 apăsat) apar o singură dată
         * pe apăsare: ținut apăsat dă 0b0111, apoi 0b1111, deci pagina nu avansează repetat, iar un
         * ricoșeu de contact mai scurt de 20 ms nu produce 2 eșantioane consecutive. */
        s.btn_hist = (uint8_t)((s.btn_hist << 1) | (BSP_PB_GetState(BUTTON_KEY) != 0));
        if ((s.btn_hist & 0x0F) == 0x03) display_next_page();
    }

    display_update(&u);
}
