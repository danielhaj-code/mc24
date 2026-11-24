/*
 * main.h — legătura dintre main.c (inițializarea hardware) și restul modulelor.
 *
 * Exportă handle-urile HAL definite în main.c, alege UART-ul protocolului (LINK_HUART) după
 * definiția de compilare LINK_VCP și declară cele două utilitare globale: dbg_log() și
 * Error_Handler(). Include stm32f4xx_hal.h, deci cine include main.h are tot HAL-ul.
 * Contract: handle-urile sunt valide doar după ce main() a terminat MX_xxx_Init(), adică
 * începând cu app_init(); modulele nu le reinițializează.
 */
#ifndef MAIN_H
#define MAIN_H

#include "stm32f4xx_hal.h"

/* Handle-uri folosite de module (sensor.c, link.c). */
extern ADC_HandleTypeDef  hadc1;
extern UART_HandleTypeDef huart5;   /* spre ESP32: PC12 TX, PD2 RX */
extern UART_HandleTypeDef huart1;   /* port serial virtual ST-LINK (USB): PA9 TX, PA10 RX */

/* UART-ul pe care circulă protocolul (PROTOCOL.md).
 * Normal: UART5, spre ESP32. Cu `make LINK=vcp`: USART1, adică portul serial USB al plăcii,
 * ca să poți testa protocolul direct de pe PC, fără ESP32 (tools/vcp_test.py). */
#if LINK_VCP
#define LINK_HUART      huart1
#define LINK_INSTANCE   USART1
#else
#define LINK_HUART      huart5
#define LINK_INSTANCE   UART5
#endif

/* Jurnal pe portul serial USB (115200). În modul LINK=vcp e oprit, ca să nu se amestece cu protocolul.
 * Blocant (până la 20 ms), stil printf, maxim 127 caractere; doar din bucla principală, nu din ISR. */
void dbg_log(const char *fmt, ...);

/* Eroare fatală: oprește întreruperile și clipește LD4 (roșu) la nesfârșit. Nu se întoarce. */
void Error_Handler(void);

#endif /* MAIN_H */
