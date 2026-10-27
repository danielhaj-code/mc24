/* Inițializarea pinilor și ceasurilor pentru perifericele aplicației.
 * LCD, SDRAM, SPI5 și I2C3 sunt inițializate de BSP-ul plăcii, nu aici. */
#include "main.h"

void HAL_MspInit(void)
{
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
}

void HAL_ADC_MspInit(ADC_HandleTypeDef *h)
{
    if (h->Instance != ADC1) return;
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef g = { .Pin = GPIO_PIN_5, .Mode = GPIO_MODE_ANALOG, .Pull = GPIO_NOPULL };   /* PA5 = ADC1_IN5 */
    HAL_GPIO_Init(GPIOA, &g);
}

void HAL_UART_MspInit(UART_HandleTypeDef *h)
{
    GPIO_InitTypeDef g = { .Mode = GPIO_MODE_AF_PP, .Pull = GPIO_PULLUP, .Speed = GPIO_SPEED_FREQ_VERY_HIGH };

    if (h->Instance == UART5) {
        /* spre ESP32: PC12 = TX, PD2 = RX (AF8) */
        __HAL_RCC_UART5_CLK_ENABLE();
        __HAL_RCC_GPIOC_CLK_ENABLE();
        __HAL_RCC_GPIOD_CLK_ENABLE();
        g.Alternate = GPIO_AF8_UART5;
        g.Pin = GPIO_PIN_12; HAL_GPIO_Init(GPIOC, &g);
        g.Pin = GPIO_PIN_2;  HAL_GPIO_Init(GPIOD, &g);
        HAL_NVIC_SetPriority(UART5_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(UART5_IRQn);
    } else if (h->Instance == USART1) {
        /* port serial virtual ST-LINK: PA9 = TX, PA10 = RX (AF7) */
        __HAL_RCC_USART1_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        g.Alternate = GPIO_AF7_USART1;
        g.Pin = GPIO_PIN_9 | GPIO_PIN_10;
        HAL_GPIO_Init(GPIOA, &g);
        HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(USART1_IRQn);
    }
}
