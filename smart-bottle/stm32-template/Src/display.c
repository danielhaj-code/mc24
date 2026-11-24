/*
 * display.c — ecranul LCD (240 x 320) cu funcțiile din BSP-ul plăcii (stm32f429i_discovery_lcd.c).
 *
 *   ┌──────────────────────┐
 *   │     Smart Bottle     │  bară albastră
 *   │ ┌──┐  Nivel          │
 *   │ │  │  64 %           │
 *   │ │▓▓│  Volum          │
 *   │ │▓▓│  450 mL         │
 *   │ │▓▓│  brut 2310      │
 *   │ └──┘  stabil         │
 *   │ Aplicatie: conectata │
 *   └──────────────────────┘
 *
 * Fonturile BSP au doar caractere ASCII, de aceea textele sunt fără diacritice. Textele care se
 * schimbă sunt completate cu spații până la lățime fixă, ca să acopere textul vechi.
 */
#include "display.h"
#include "app_config.h"
#include "main.h"
#include "stm32f429i_discovery_lcd.h"
#include "ili9341.h"
#include <stdio.h>

/* Sticla desenată în stânga */
#define BOT_X  20
#define BOT_Y  60
#define BOT_W  70
#define BOT_H  210
#define COL_X  110                   /* coloana cu valori */

static void text(uint16_t x, uint16_t y, sFONT *font, uint32_t color, const char *s)
{
    BSP_LCD_SetFont(font);
    BSP_LCD_SetTextColor(color);
    BSP_LCD_DisplayStringAt(x, y, (uint8_t *)s, LEFT_MODE);
}

void display_init(void)
{
    BSP_LCD_Init();                  /* SDRAM (memoria imaginii), LTDC, controlerul ILI9341 */
#if LCD_ROTATE_180
    /* Rotire cu 180°: în comanda „Display Function Control” se inversează sensul liniilor și
     * coloanelor (al doilea octet 0xC7 în loc de 0xA7, cât scrie BSP-ul). */
    ili9341_WriteReg(LCD_DFC);
    ili9341_WriteData(0x0A);
    ili9341_WriteData(0xC7);
    ili9341_WriteData(0x27);
    ili9341_WriteData(0x04);
#endif
    BSP_LCD_LayerDefaultInit(LCD_BACKGROUND_LAYER, LCD_FRAME_BUFFER);
    BSP_LCD_SelectLayer(LCD_BACKGROUND_LAYER);
    BSP_LCD_DisplayOn();
    BSP_LCD_Clear(LCD_COLOR_WHITE);

    /* partea fixă: titlul, conturul sticlei, etichetele */
    BSP_LCD_SetTextColor(LCD_COLOR_DARKBLUE);
    BSP_LCD_FillRect(0, 0, 240, 40);
    BSP_LCD_SetBackColor(LCD_COLOR_DARKBLUE);
    BSP_LCD_SetFont(&Font24);
    BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
    BSP_LCD_DisplayStringAt(0, 8, (uint8_t *)"Smart Bottle", CENTER_MODE);

    BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
    BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
    BSP_LCD_DrawRect(BOT_X - 2, BOT_Y - 2, BOT_W + 3, BOT_H + 3);
    text(COL_X, 62, &Font16, LCD_COLOR_GRAY, "Nivel");
    text(COL_X, 122, &Font16, LCD_COLOR_GRAY, "Volum");
}

void display_show(const app_state_t *st)
{
    char buf[24];

    /* sticla: partea de jos albastră, proporțional cu nivelul */
    int fill = BOT_H * st->pct / 100;
    BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
    BSP_LCD_FillRect(BOT_X, BOT_Y, BOT_W, BOT_H - fill);
    if (fill > 0) {
        BSP_LCD_SetTextColor(LCD_COLOR_BLUE);
        BSP_LCD_FillRect(BOT_X, BOT_Y + BOT_H - fill, BOT_W, fill);
    }

    /* lățimi fixe (Font24 = 17 px/caracter, Font16 = 11 px): textul nou acoperă exact textul vechi
     * și încape în cei 130 px din dreapta sticlei */
    snprintf(buf, sizeof buf, "%3d %%", st->pct);                       /* 5 caractere */
    text(COL_X, 82, &Font24, LCD_COLOR_BLACK, buf);

    /* mL vin de la aplicație; dacă n-au mai venit de mult, nu le mai arătăm */
    if (st->ml_ms != 0 && HAL_GetTick() - st->ml_ms < ML_TIMEOUT_MS)
        snprintf(buf, sizeof buf, "%4d mL", st->ml);                    /* 7 caractere */
    else
        snprintf(buf, sizeof buf, "  -- mL");
    text(COL_X, 142, &Font24, LCD_COLOR_BLACK, buf);

    snprintf(buf, sizeof buf, "brut %4ld", (long)st->raw);              /* 9 caractere */
    text(COL_X, 190, &Font16, LCD_COLOR_DARKGRAY, buf);

    text(COL_X, 215, &Font16, st->stable ? LCD_COLOR_DARKGREEN : LCD_COLOR_ORANGE,
         st->stable ? "stabil  " : "se misca");

    /* 21 de caractere = 231 px, cât încape pe lățimea ecranului */
    if (app_connected(st)) text(5, 290, &Font16, LCD_COLOR_DARKGREEN, "Aplicatie conectata  ");
    else                   text(5, 290, &Font16, LCD_COLOR_RED,       "Aplicatie neconectata");
}
