#include "display.h"
#include "app_config.h"
#include "stm32f429i_discovery_lcd.h"   /* BSP din STM32CubeF4: Drivers/BSP/STM32F429I-Discovery */
#include <stdio.h>

/* Ecran 240 x 320 (portret). */
#define BOTTLE_X   70
#define BOTTLE_Y   70
#define BOTTLE_W   100
#define BOTTLE_H   170
#define NECK_W     40
#define NECK_H     16

static struct { int32_t raw; int pct, stable, ble, ok, valid; } last;

void display_init(void)
{
    BSP_LCD_Init();
    BSP_LCD_LayerDefaultInit(LCD_BACKGROUND_LAYER, LCD_FRAME_BUFFER);
    BSP_LCD_SelectLayer(LCD_BACKGROUND_LAYER);
    BSP_LCD_DisplayOn();
    BSP_LCD_Clear(LCD_COLOR_WHITE);

    BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
    BSP_LCD_SetTextColor(LCD_COLOR_DARKBLUE);
    BSP_LCD_SetFont(&Font20);
    BSP_LCD_DisplayStringAt(0, 12, (uint8_t *)"Smart Bottle", CENTER_MODE);

    /* conturul sticlei: gât + corp */
    BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
    BSP_LCD_DrawRect(BOTTLE_X + (BOTTLE_W - NECK_W) / 2, BOTTLE_Y - NECK_H, NECK_W, NECK_H);
    BSP_LCD_DrawRect(BOTTLE_X - 1, BOTTLE_Y - 1, BOTTLE_W + 2, BOTTLE_H + 2);

    last.valid = 0;
}

static uint32_t level_color(int pct)
{
    if (pct >= LEVEL_OK_PCT)  return LCD_COLOR_BLUE;
    if (pct >= LEVEL_MID_PCT) return LCD_COLOR_YELLOW;
    return LCD_COLOR_RED;
}

void display_update(int32_t raw, int pct, int stable, int ble_connected, int sensor_ok)
{
    if (last.valid && last.raw == raw && last.pct == pct && last.stable == stable &&
        last.ble == ble_connected && last.ok == sensor_ok)
        return;

    char line[24];

    /* lichid: partea goală albă, partea plină colorată (fără clear complet => fără flicker) */
    int fill = (BOTTLE_H * pct) / 100;
    BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
    BSP_LCD_FillRect(BOTTLE_X, BOTTLE_Y, BOTTLE_W, BOTTLE_H - fill);
    if (fill > 0) {
        BSP_LCD_SetTextColor(level_color(pct));
        BSP_LCD_FillRect(BOTTLE_X, BOTTLE_Y + BOTTLE_H - fill, BOTTLE_W, fill);
    }

    /* procent mare */
    BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
    BSP_LCD_SetTextColor(sensor_ok ? LCD_COLOR_BLACK : LCD_COLOR_RED);
    BSP_LCD_SetFont(&Font24);
    if (sensor_ok) snprintf(line, sizeof line, "  %3d %%  ", pct);
    else           snprintf(line, sizeof line, " EROARE  ");
    BSP_LCD_DisplayStringAt(0, 252, (uint8_t *)line, CENTER_MODE);

    /* valoare brută + stabilitate */
    BSP_LCD_SetFont(&Font12);
    BSP_LCD_SetTextColor(LCD_COLOR_DARKGRAY);
    snprintf(line, sizeof line, " raw %8ld %s ", (long)raw, stable ? "stabil " : "instabil");
    BSP_LCD_DisplayStringAt(0, 282, (uint8_t *)line, CENTER_MODE);

    /* stare BLE */
    BSP_LCD_SetTextColor(ble_connected ? LCD_COLOR_DARKGREEN : LCD_COLOR_GRAY);
    BSP_LCD_DisplayStringAt(0, 300, (uint8_t *)(ble_connected ? "  BLE: conectat   "
                                                              : " BLE: neconectat  "), CENTER_MODE);

    last.raw = raw; last.pct = pct; last.stable = stable;
    last.ble = ble_connected; last.ok = sensor_ok; last.valid = 1;
}
