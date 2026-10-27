#include "app.h"
#include "app_config.h"
#include "main.h"
#include "stm32f429i_discovery.h"   /* LED3 (PG13) / LED4 (PG14) */
#include "filter.h"
#include "protocol.h"
#include "sensor.h"
#include "link.h"
#include "display.h"
#include <stdio.h>

#define TOUCH_PERIOD_MS  20

static ui_state_t u;                 /* tot ce afișează meniul */
static struct {
    uint32_t last_read_ms, last_touch_ms;
    stability_t stab;
} s;

/* LED3 verde aprins cât timp aplicația e activă */
static void set_app(int state)
{
    u.app = state;
    if (state == 2) BSP_LED_On(LED3); else BSP_LED_Off(LED3);
}

/* ---- senzor simulat: o sticlă din care se bea din când în când ---- */
static struct { int32_t level; /* 0..1000 */ uint32_t next_ms, seed; } sim;

static uint32_t sim_rand(uint32_t n)
{
    sim.seed = sim.seed * 1664525u + 1013904223u;
    return (sim.seed >> 16) % n;
}

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

static void do_reading(void)
{
    int32_t raw;
    char msg[PROTO_LINE_MAX];

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
        BSP_LED_Toggle(LED4);   /* LED roșu clipește la eroare de senzor */
    }

    if (u.sensor_ok) {
        size_t n = proto_format_level(msg, sizeof msg, u.raw, u.pct, u.stable);
        if (n) link_send(msg, n);   /* ESP32 aruncă mesajul dacă nu e telefon conectat */
    }
}

static void set_calibration(int32_t empty, int32_t full)
{
    u.cal_empty = empty;
    u.cal_full = full;
    u.pct = level_percent(u.raw, u.cal_empty, u.cal_full);
}

static void handle(const proto_cmd_t *c)
{
    switch (c->type) {
    case CMD_CALIBRATE:
        if (c->a == c->b) {
            link_send_str("E,C\n");
        } else {
            set_calibration(c->a, c->b);
            link_send_str("A,C\n");
        }
        break;
    case CMD_READ_NOW:
        do_reading();
        s.last_read_ms = HAL_GetTick();
        break;
    case CMD_LINK_STATE:
        /* S spune doar că există o conexiune BLE; heartbeat-ul H spune dacă aplicația e vie */
        set_app(c->a ? (u.app ? u.app : 1) : 0);
        break;
    /* statistici calculate de aplicație, doar pentru afișare */
    case CMD_VOLUME:
        u.has_ml = 1; u.ml = c->a; u.cap = c->b;
        u.last_app_ms = HAL_GetTick();
        break;
    case CMD_TODAY:
        u.has_today = 1; u.total = c->a; u.goal = c->b; u.sips = c->c;
        u.last_app_ms = HAL_GetTick();
        break;
    case CMD_SINCE:
        u.since_min = c->a;
        u.since_at_ms = HAL_GetTick();
        u.last_app_ms = HAL_GetTick();
        break;
    case CMD_DAY:
        u.week[c->a] = c->b;
        if (c->a == 0) u.has_week = 1;    /* aplicația trimite W,0..6 la conectare */
        u.last_app_ms = HAL_GetTick();
        break;
    case CMD_HEARTBEAT:
        /* uptime mai mic decât data trecută = ESP32 a repornit */
        if (u.esp_last_ms && (uint32_t)c->a < u.esp_uptime) u.esp_restarts++;
        u.esp_uptime = (uint32_t)c->a;
        u.esp_last_ms = HAL_GetTick();
        set_app(c->b);                    /* resincronizează și după un S,x pierdut (ex. reset) */
        break;
    default:
        break;   /* CMD_INVALID: ignorat */
    }
}

/* Butoanele de calibrare din meniu: aplicăm local și trimitem aplicației, care o salvează
 * (ea rămâne sursa de adevăr și o retrimite cu C,... la fiecare conectare). */
static void calibrate_from_touch(int full)
{
    char msg[PROTO_LINE_MAX], note[40];
    int32_t empty = full ? u.cal_empty : u.raw;
    int32_t fullv = full ? u.raw : u.cal_full;
    if (empty == fullv) {
        display_notice("gol = plin, respins", 0);
        return;
    }
    set_calibration(empty, fullv);
    size_t n = proto_format_cal(msg, sizeof msg, empty, fullv);
    if (n) link_send(msg, n);
    snprintf(note, sizeof note, "%s = %ld%s", full ? "plin" : "gol", (long)u.raw,
             u.app == 2 ? " · trimis" : " · doar local");
    display_notice(note, 1);
}

void app_init(void)
{
    u.cal_empty = CAL_DEFAULT_EMPTY;
    u.cal_full = CAL_DEFAULT_FULL;
    u.sensor_ok = 1;
    u.since_min = -2;
    stability_init(&s.stab, STABLE_WINDOW, STABLE_THRESHOLD);

    BSP_LED_Init(LED3);
    BSP_LED_Init(LED4);
    display_init();
    sensor_init();
    link_init();

    do_reading();
    s.last_read_ms = HAL_GetTick();
    display_update(&u);
}

void app_loop(void)
{
    proto_cmd_t c;
    while ((c = link_poll()).type != CMD_NONE)
        handle(&c);

    if (HAL_GetTick() - s.last_read_ms >= READ_PERIOD_MS) {
        s.last_read_ms = HAL_GetTick();
        do_reading();
    }

    if (HAL_GetTick() - s.last_touch_ms >= TOUCH_PERIOD_MS) {
        s.last_touch_ms = HAL_GetTick();
        switch (display_poll_touch(&u)) {
        case UI_EV_SET_EMPTY: calibrate_from_touch(0); break;
        case UI_EV_SET_FULL:  calibrate_from_touch(1); break;
        case UI_EV_TOGGLE_MOCK: toggle_mock(); break;
        default: break;
        }
    }

    display_update(&u);
}
