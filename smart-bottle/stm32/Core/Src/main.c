/*
 * Smart Bottle — firmware STM32F429I-DISC1 (build cu make, fără CubeIDE).
 *
 * Ce face main: ceasuri (180 MHz din HSE 8 MHz), periferice (ADC1, UART5, USART1, GPIO),
 * apoi predă controlul aplicației (app.c): citire senzor, LCD, protocol spre ESP32.
 *
 * Portul serial USB al plăcii (ST-LINK VCP, USART1) e folosit pentru jurnal: la pornire și
 * pentru fiecare linie trimisă/primită pe legătura cu ESP32. `make monitor` îl deschide.
 */
#include "main.h"
#include "app.h"
#include "app_config.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

ADC_HandleTypeDef  hadc1;
UART_HandleTypeDef huart5;
UART_HandleTypeDef huart1;

static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_UART_Init(UART_HandleTypeDef *h, USART_TypeDef *inst, uint32_t baud);

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_ADC1_Init();
    MX_UART_Init(&huart5, UART5, 115200);
    MX_UART_Init(&huart1, USART1, 115200);

    dbg_log("\r\n[boot] Smart Bottle %s, %lu MHz, senzor %s, legatura pe %s\r\n",
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

    app_init();
    dbg_log("[boot] app pornita\r\n");

    while (1) {
        app_loop();
    }
}

/* ===================== Jurnal pe VCP ===================== */
void dbg_log(const char *fmt, ...)
{
#if LINK_VCP
    (void)fmt;
#else
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n > 0) HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)(n < (int)sizeof buf ? n : (int)sizeof buf - 1), 20);
#endif
}

/* ===================== Ceasuri: 180 MHz ===================== */
/* HSE 8 MHz (cristal X3 pe DISC1) -> PLL: /8 *360 /2 = 180 MHz. APB1 45 MHz, APB2 90 MHz.
 * Aceleași valori ca în șablonul ST pentru placă; LTDC-ul are ceasul lui, setat de BSP. */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 8;
    osc.PLL.PLLN = 360;
    osc.PLL.PLLP = RCC_PLLP_DIV2;
    osc.PLL.PLLQ = 7;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) Error_Handler();

    if (HAL_PWREx_EnableOverDrive() != HAL_OK) Error_Handler();   /* necesar peste 168 MHz */

    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK) Error_Handler();
}

/* ===================== GPIO ===================== */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOE_CLK_ENABLE();

    /* PE4: alimentarea senzorului analogic; PE3: SCK pentru HX711. Ambele pornesc LOW. */
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_4 | GPIO_PIN_3, GPIO_PIN_RESET);
    g.Pin = GPIO_PIN_4 | GPIO_PIN_3;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOE, &g);

    /* PE2: DT de la HX711 (intrare cu pull-up; nefolosit cu senzorul analogic). */
    g.Pin = GPIO_PIN_2;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOE, &g);
}

/* ===================== ADC1, canal 5 (PA5) ===================== */
static void MX_ADC1_Init(void)
{
    ADC_ChannelConfTypeDef ch = {0};

    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;     /* 90 / 4 = 22,5 MHz */
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
    if (HAL_ADC_Init(&hadc1) != HAL_OK) Error_Handler();

    ch.Channel = ADC_CHANNEL_5;
    ch.Rank = 1;
    ch.SamplingTime = ADC_SAMPLETIME_84CYCLES;   /* senzorul are impedanță mare */
    if (HAL_ADC_ConfigChannel(&hadc1, &ch) != HAL_OK) Error_Handler();
}

/* ===================== UART-uri: 8N1 ===================== */
static void MX_UART_Init(UART_HandleTypeDef *h, USART_TypeDef *inst, uint32_t baud)
{
    h->Instance = inst;
    h->Init.BaudRate = baud;
    h->Init.WordLength = UART_WORDLENGTH_8B;
    h->Init.StopBits = UART_STOPBITS_1;
    h->Init.Parity = UART_PARITY_NONE;
    h->Init.Mode = UART_MODE_TX_RX;
    h->Init.HwFlowCtl = UART_HWCONTROL_NONE;
    h->Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(h) != HAL_OK) Error_Handler();
}

/* ===================== Erori ===================== */
/* LED-ul roșu (LD4, PG14) clipește repede la nesfârșit. */
void Error_Handler(void)
{
    __disable_irq();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    GPIO_InitTypeDef g = { .Pin = GPIO_PIN_14, .Mode = GPIO_MODE_OUTPUT_PP, .Pull = GPIO_NOPULL, .Speed = GPIO_SPEED_FREQ_LOW };
    HAL_GPIO_Init(GPIOG, &g);
    while (1) {
        HAL_GPIO_TogglePin(GPIOG, GPIO_PIN_14);
        for (volatile uint32_t i = 0; i < 2000000; i++) { }
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file; (void)line;
    Error_Handler();
}
#endif
