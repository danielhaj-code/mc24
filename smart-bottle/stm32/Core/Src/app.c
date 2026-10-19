#include "app.h"
#include "app_config.h"
#include "main.h"
#include "stm32f429i_discovery.h"   /* LED3 (PG13) / LED4 (PG14) */
#include "filter.h"
#include "protocol.h"
#include "sensor.h"
#include "link.h"
#include "display.h"

static struct {
    int32_t cal_empty, cal_full;
    int32_t raw;
    int pct;
    int stable;
    int sensor_ok;
    int ble_connected;
    uint32_t last_read_ms;
    stability_t stab;
} s;

static void do_reading(void)
{
    int32_t raw;
    char msg[PROTO_LINE_MAX];

    if (sensor_read(&raw)) {
        s.sensor_ok = 1;
        s.raw = raw;
        s.stable = stability_push(&s.stab, raw);
        s.pct = level_percent(raw, s.cal_empty, s.cal_full);
    } else {
        s.sensor_ok = 0;
        s.stable = 0;
        stability_reset(&s.stab);
        BSP_LED_Toggle(LED4);   /* LED roșu clipește la eroare de senzor */
    }

    display_update(s.raw, s.pct, s.stable, s.ble_connected, s.sensor_ok);

    if (s.sensor_ok) {
        size_t n = proto_format_level(msg, sizeof msg, s.raw, s.pct, s.stable);
        if (n) link_send(msg, n);   /* ESP32 aruncă mesajul dacă nu e telefon conectat */
    }
}

static void handle(const proto_cmd_t *c)
{
    switch (c->type) {
    case CMD_CALIBRATE:
        if (c->a == c->b) {
            link_send_str("E,C\n");
        } else {
            s.cal_empty = c->a;
            s.cal_full = c->b;
            s.pct = level_percent(s.raw, s.cal_empty, s.cal_full);
            link_send_str("A,C\n");
            display_update(s.raw, s.pct, s.stable, s.ble_connected, s.sensor_ok);
        }
        break;
    case CMD_READ_NOW:
        do_reading();
        s.last_read_ms = HAL_GetTick();
        break;
    case CMD_LINK_STATE:
        s.ble_connected = c->a;
        if (s.ble_connected) BSP_LED_On(LED3); else BSP_LED_Off(LED3);
        display_update(s.raw, s.pct, s.stable, s.ble_connected, s.sensor_ok);
        break;
    default:
        break;   /* CMD_INVALID: ignorat */
    }
}

void app_init(void)
{
    s.cal_empty = CAL_DEFAULT_EMPTY;
    s.cal_full = CAL_DEFAULT_FULL;
    s.sensor_ok = 1;
    stability_init(&s.stab, STABLE_WINDOW, STABLE_THRESHOLD);

    BSP_LED_Init(LED3);
    BSP_LED_Init(LED4);
    display_init();
    sensor_init();
    link_init();

    do_reading();
    s.last_read_ms = HAL_GetTick();
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
}
