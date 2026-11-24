/*
 * app.c — bucla aplicației: o citire pe secundă, mesajele de la ESP32, LCD-ul și LED-ul verde.
 *
 * Mesaje (PROTOCOL.md din repo):
 *   trimise:  L,<brut>,<procent>,<stabil>   la fiecare secundă și la cererea „R”
 *             A,C / E,C                      calibrare acceptată / respinsă
 *   primite:  C,<gol>,<plin>   calibrarea, trimisă de aplicație la fiecare conectare
 *             R                cere o citire imediată
 *             M,<ml>,<capac.>  volumul calculat de aplicație, pentru LCD
 *             H,<timp>,<stare> heartbeat de la ESP32 la 2 s (stare 2 = aplicație activă)
 *             S,0              telefonul s-a deconectat
 *   Celelalte mesaje (G, T, U, W) sunt ignorate.
 */
#include "app.h"
#include "app_config.h"
#include "display.h"
#include "link.h"
#include "main.h"
#include "sensor.h"

static app_state_t st;
static uint32_t last_read_ms;

int app_connected(const app_state_t *s)
{
    return s->app_state == 2 && s->app_ms != 0 && HAL_GetTick() - s->app_ms < APP_TIMEOUT_MS;
}

static void do_reading(void)
{
    st.raw = sensor_read();
    st.stable = sensor_stable(st.raw);
    st.pct = sensor_percent(st.raw, st.cal_empty, st.cal_full);
    link_send_level(st.raw, st.pct, st.stable);
    last_read_ms = HAL_GetTick();
}

static void handle(const link_msg_t *m)
{
    switch (m->type) {
    case 'C':
        if (m->n != 2) break;
        if (m->v[0] == m->v[1]) {                    /* gol = plin: nu se poate calcula procentul */
            link_send("E,C\n");
        } else {
            st.cal_empty = m->v[0];
            st.cal_full = m->v[1];
            st.pct = sensor_percent(st.raw, st.cal_empty, st.cal_full);
            link_send("A,C\n");
        }
        break;
    case 'R':
        do_reading();
        break;
    case 'M':
        if (m->n == 2 && m->v[1] > 0) {
            st.ml = (int)m->v[0];
            st.capacity = (int)m->v[1];
            st.ml_ms = HAL_GetTick();
        }
        break;
    case 'H':
        if (m->n == 2) {
            st.app_state = (int)m->v[1];
            st.app_ms = HAL_GetTick();
        }
        break;
    case 'S':
        if (m->n == 1 && m->v[0] == 0) st.app_state = 0;
        break;
    default:
        break;                                       /* mesaj nefolosit de această variantă */
    }
}

void app_init(void)
{
    st.cal_empty = CAL_DEFAULT_EMPTY;
    st.cal_full = CAL_DEFAULT_FULL;
    display_init();
    link_init();
    do_reading();
    display_show(&st);
}

void app_loop(void)
{
    link_msg_t m;
    int changed = 0;

    while (link_poll(&m)) {                          /* toate mesajele sosite între timp */
        handle(&m);
        changed = 1;
    }
    if (HAL_GetTick() - last_read_ms >= READ_PERIOD_MS) {
        do_reading();
        changed = 1;
    }

    /* LED-ul verde LD3: aplicația web e conectată și activă */
    HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, app_connected(&st) ? GPIO_PIN_SET : GPIO_PIN_RESET);

    if (changed) display_show(&st);
}
