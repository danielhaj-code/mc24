/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * Smart Bottle pe șablonul de laborator Template_GPIO (STM32F429I-DISC1).
  *
  * Fișier generat de CubeMX din Template_GPIO.ioc; codul nostru stă doar între
  * marcajele USER CODE, ca să rămână la o regenerare.
  *
  * Pe lângă ce generează CubeMX (prototipul Error_Handler și etichetele pinilor),
  * exportă pentru modulele aplicației (sensor.c, link.c, app.c):
  *   - handle-urile HAL definite în main.c (hadc1, huart5, huart1);
  *   - LINK_HUART / LINK_INSTANCE: UART-ul pe care circulă protocolul (PROTOCOL.md);
  *   - dbg_log(): jurnal pe portul serial USB al ST-LINK.
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* Handle-uri HAL definite în main.c. Sunt valide după MX_xxx_Init(), adică începând cu app_init();
 * modulele le folosesc, nu le reinițializează. */
extern ADC_HandleTypeDef  hadc1;    /* senzorul analogic, PA5 = ADC1_IN5 (sensor.c) */
extern UART_HandleTypeDef huart5;   /* spre ESP32: PC12 TX, PD2 RX, 115200 8N1 */
extern UART_HandleTypeDef huart1;   /* port serial virtual ST-LINK (USB): PA9 TX, PA10 RX, 115200 8N1 */

/* UART-ul protocolului. Normal UART5, spre ESP32. Cu LINK_VCP=1 (în Keil: Options for Target ->
 * C/C++ -> Define) protocolul trece pe USART1, portul serial USB, ca să poată fi testat de pe PC
 * fără ESP32 (stm32/tools/vcp_test.py din repo). */
#if LINK_VCP
#define LINK_HUART      huart1
#define LINK_INSTANCE   USART1
#else
#define LINK_HUART      huart5
#define LINK_INSTANCE   UART5
#endif

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* Jurnal pe portul serial USB (USART1, 115200), stil printf, maxim 127 de caractere.
 * Blocant (până la 20 ms), deci doar din bucla principală, nu din întreruperi.
 * Cu LINK_VCP=1 nu face nimic: USART1 transportă atunci protocolul. */
void dbg_log(const char *fmt, ...);

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define HX711_DT_Pin GPIO_PIN_2
#define HX711_DT_GPIO_Port GPIOE
#define HX711_SCK_Pin GPIO_PIN_3
#define HX711_SCK_GPIO_Port GPIOE
#define SENS_PWR_Pin GPIO_PIN_4
#define SENS_PWR_GPIO_Port GPIOE
#define PH0_OSC_IN_Pin GPIO_PIN_0
#define PH0_OSC_IN_GPIO_Port GPIOH
#define PH1_OSC_OUT_Pin GPIO_PIN_1
#define PH1_OSC_OUT_GPIO_Port GPIOH
#define USER_Pin GPIO_PIN_0
#define USER_GPIO_Port GPIOA
#define SENSOR_ADC_Pin GPIO_PIN_5
#define SENSOR_ADC_GPIO_Port GPIOA
#define STLINK_RX_Pin GPIO_PIN_9
#define STLINK_RX_GPIO_Port GPIOA
#define STLINK_TX_Pin GPIO_PIN_10
#define STLINK_TX_GPIO_Port GPIOA
#define SWDIO_Pin GPIO_PIN_13
#define SWDIO_GPIO_Port GPIOA
#define SWCLK_Pin GPIO_PIN_14
#define SWCLK_GPIO_Port GPIOA
#define ESP32_TX_Pin GPIO_PIN_12
#define ESP32_TX_GPIO_Port GPIOC
#define ESP32_RX_Pin GPIO_PIN_2
#define ESP32_RX_GPIO_Port GPIOD
#define LD3_Pin GPIO_PIN_13
#define LD3_GPIO_Port GPIOG
#define LD4_Pin GPIO_PIN_14
#define LD4_GPIO_Port GPIOG

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
