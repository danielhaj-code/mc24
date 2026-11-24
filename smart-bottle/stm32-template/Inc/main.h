/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  ******************************************************************************
  * Smart Bottle pe șablonul Template_GPIO. Generat de CubeMX din Template_GPIO.ioc; codul nostru
  * stă doar între marcajele USER CODE. Pe lângă etichetele pinilor (SENS_PWR_Pin, LD3_Pin, ...),
  * exportă pentru modulele aplicației handle-urile perifericelor și funcția de jurnal.
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

/* Definite în main.c, folosite de sensor.c (hadc1) și link.c (huart5). */
extern ADC_HandleTypeDef  hadc1;    /* senzorul, PA5 = ADC1_IN5 */
extern UART_HandleTypeDef huart5;   /* spre ESP32: PC12 TX, PD2 RX, 115200 8N1 */
extern UART_HandleTypeDef huart1;   /* portul serial USB al ST-LINK (jurnal), 115200 8N1 */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* Jurnal pe portul serial USB, ca printf (maxim 127 de caractere). */
void dbg_log(const char *fmt, ...);

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
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
