/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * Smart Bottle pe șablonul de laborator Template_GPIO (STM32F429I-DISC1).
  *
  * Fișier în formatul generat de CubeMX din Template_GPIO.ioc. Inițializările perifericelor
  * (ceas, GPIO, USART1, ADC1, UART5) corespund configurației din .ioc; codul aplicației stă
  * doar între marcajele USER CODE, deci rămâne la o regenerare din CubeMX.
  *
  * Ordinea de pornire:
  *   1. Reset_Handler (MDK-ARM/startup_stm32f429xx.s) -> SystemInit() -> __main -> main();
  *      până aici procesorul merge pe HSI 16 MHz.
  *   2. HAL_Init(): Flash prefetch/cache, grupare priorități NVIC 4, SysTick la 1 ms,
  *      apoi HAL_MspInit() din stm32f4xx_hal_msp.c.
  *   3. SystemClock_Config(): PLL din HSE 8 MHz -> 180 MHz (vezi calculul la funcție).
  *   4. MX_xxx_Init(): HAL_xxx_Init() cheamă HAL_xxx_MspInit() (pini, ceasuri, NVIC).
  *   5. USER CODE 2: mesajul de boot și app_init() (LCD, touch, senzor, recepție UART);
  *      USER CODE 3: app_loop() la nesfârșit (app.c).
  *
  * Definiții de compilare (Keil: Options for Target -> C/C++ -> Define):
  *   SENSOR_USE_HX711=1  -> celula de sarcină cu HX711 în loc de senzorul analogic
  *   LINK_VCP=1          -> protocolul pe USART1 (USB) în loc de UART5; jurnalul se oprește
  *   FW_VERSION="..."    -> textul din mesajul de boot (implicit "dev")
  *
  * Copyright (c) 2020 STMicroelectronics (structura generată). Licență BSD 3-Clause.
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app.h"
#include "app_config.h"
#include <stdarg.h>
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* Versiunea firmware-ului, doar pentru mesajul de boot. */
#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

UART_HandleTypeDef huart5;
UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_ADC1_Init(void);
static void MX_UART5_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_ADC1_Init();
  MX_UART5_Init();
  /* USER CODE BEGIN 2 */

  /* Mesajul de boot pe portul serial USB (115200 8N1): confirmă ceasul și varianta compilată. */
  dbg_log("\r\n[boot] Smart Bottle %s (Template_GPIO), %lu MHz, senzor %s, legatura pe %s\r\n",
          FW_VERSION, (unsigned long)(SystemCoreClock / 1000000U),
#if SENSOR_USE_HX711
          "HX711",
#else
          "analogic PA5",
#endif
#if LINK_VCP
          "USB (VCP)"
#else
          "UART5 (ESP32)"
#endif
  );

  app_init();                       /* LCD, touch, senzor, recepția UART pe întreruperi */
  dbg_log("[boot] app pornita\r\n");

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* Super-buclă fără sistem de operare: citirea la 1 s, touch-ul la 20 ms, mesajele primite
     * și redesenarea LCD-ului se programează toate în app_loop() după HAL_GetTick(). */
    app_loop();
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /* USER CODE BEGIN SystemClock_Config 0 */
  /* HSE 8 MHz (cristalul X3 al plăcii) -> PLL:
   *   intrare VCO = 8 MHz / PLLM 8  = 1 MHz     (permis 1..2 MHz)
   *   ieșire VCO  = 1 MHz * PLLN 360 = 360 MHz  (permis 100..432 MHz)
   *   SYSCLK      = 360 / PLLP 2    = 180 MHz  (maximul lui STM32F429)
   *   PLL48CK     = 360 / PLLQ 7    ≈ 51,4 MHz (doar pentru USB/SDIO, nefolosite)
   * AHB 180 MHz, APB1 = /4 = 45 MHz (UART5), APB2 = /2 = 90 MHz (USART1, ADC1).
   * Peste 168 MHz trebuie Scale 1 și over-drive; Flash-ul are nevoie de 5 stări de așteptare.
   * LTDC-ul are ceasul lui (PLLSAI), setat de BSP_LCD_Init. */
  /* USER CODE END SystemClock_Config 0 */

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 360;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Activate the Over-Drive mode
  */
  if (HAL_PWREx_EnableOverDrive() != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */
  /* O conversie la cerere (start software, polling), 12 biți: 0..4095 pentru 0..3,3 V.
   * Ceas ADC = APB2 90 MHz / 4 = 22,5 MHz (maxim 36 MHz). Folosit de sensor.c (varianta analogică). */
  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_5;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_84CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */
  /* 84 de cicluri de eșantionare (≈3,7 µs): senzorul are impedanță mare, iar condensatorul de
   * eșantionare al ADC-ului are nevoie de timp să se încarce prin el. */
  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief UART5 Initialization Function
  * @param None
  * @retval None
  */
static void MX_UART5_Init(void)
{

  /* USER CODE BEGIN UART5_Init 0 */
  /* Legătura cu ESP32 (PROTOCOL.md): 115200 8N1, fără control de flux. Recepția octet cu octet
   * pe întreruperi o pornește link_init() (link.c), doar pe UART-ul legăturii. */
  /* USER CODE END UART5_Init 0 */

  /* USER CODE BEGIN UART5_Init 1 */

  /* USER CODE END UART5_Init 1 */
  huart5.Instance = UART5;
  huart5.Init.BaudRate = 115200;
  huart5.Init.WordLength = UART_WORDLENGTH_8B;
  huart5.Init.StopBits = UART_STOPBITS_1;
  huart5.Init.Parity = UART_PARITY_NONE;
  huart5.Init.Mode = UART_MODE_TX_RX;
  huart5.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart5.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart5) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN UART5_Init 2 */

  /* USER CODE END UART5_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */
  /* Portul serial virtual al ST-LINK (COM-ul „STMicroelectronics STLink Virtual COM Port”):
   * jurnalul dbg_log() sau, cu LINK_VCP=1, protocolul. 115200 8N1 (șablonul avea 9600). */
  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */
  /* Pinii senzorului (aceiași ca în app_config.h: SENS_PWR_*, HX711_*):
   *   PE4 SENS_PWR  - alimentarea senzorului analogic, pornită doar în timpul citirii
   *   PE3 HX711_SCK - ceasul HX711 (HIGH > 60 µs = power-down, deci pornește LOW)
   *   PE2 HX711_DT  - date HX711, cu pull-up: modul lipsă = „ocupat” -> timeout, nu zerouri false
   * LED-urile LD3/LD4 și butonul USER sunt reinițializate și de BSP (app_init). */
  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOE, HX711_SCK_Pin|SENS_PWR_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOG, LD3_Pin|LD4_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : HX711_DT_Pin */
  GPIO_InitStruct.Pin = HX711_DT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(HX711_DT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : HX711_SCK_Pin SENS_PWR_Pin */
  GPIO_InitStruct.Pin = HX711_SCK_Pin|SENS_PWR_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pin : USER_Pin */
  GPIO_InitStruct.Pin = USER_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(USER_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : LD3_Pin LD4_Pin */
  GPIO_InitStruct.Pin = LD3_Pin|LD4_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* printf simplificat spre USART1 (portul serial USB al ST-LINK). Mesajul e trunchiat la 127 de
 * caractere; transmisie blocantă cu timeout 20 ms (128 octeți la 115200 ≈ 11 ms): dacă UART-ul
 * e blocat, mesajul se pierde în loc să oprească aplicația. Doar din bucla principală. */
void dbg_log(const char *fmt, ...)
{
#if LINK_VCP
  (void)fmt;                        /* USART1 transportă protocolul: jurnalul l-ar strica */
#else
  char buf[128];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  /* vsnprintf întoarce lungimea care AR FI rezultat; trimitem doar cât încape în buffer. */
  if (n > 0) HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)(n < (int)sizeof buf ? n : (int)sizeof buf - 1), 20);
#endif
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* Eroare fatală (init HAL eșuat, HardFault etc.): oprește întreruperile și clipește LD4 (roșu)
   * la nesfârșit. PG14 e reconfigurat aici, fiindcă eroarea poate apărea înainte de MX_GPIO_Init.
   * Frecvența clipirii depinde de ceasul la care a apărut eroarea (doar orientativă). */
  __disable_irq();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  GPIO_InitTypeDef g = { .Pin = LD4_Pin, .Mode = GPIO_MODE_OUTPUT_PP, .Pull = GPIO_NOPULL, .Speed = GPIO_SPEED_FREQ_LOW };
  HAL_GPIO_Init(LD4_GPIO_Port, &g);
  while (1)
  {
    HAL_GPIO_TogglePin(LD4_GPIO_Port, LD4_Pin);
    for (volatile uint32_t i = 0; i < 2000000; i++) { }
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* Parametru invalid dat unei funcții HAL (doar cu USE_FULL_ASSERT). */
  (void)file; (void)line;
  Error_Handler();
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
