/* Inițializarea pinilor și ceasurilor pentru perifericele aplicației („MSP” = MCU Support Package).
 * LCD, SDRAM, SPI5 și I2C3 sunt inițializate de BSP-ul plăcii, nu aici.
 *
 * HAL separă configurarea perifericului (HAL_ADC_Init, HAL_UART_Init — chemate din main.c) de partea
 * dependentă de placă: ceasul perifericului, pinii (mod, funcție alternativă AF) și NVIC. Pentru asta
 * HAL_xxx_Init() cheamă automat callback-ul HAL_xxx_MspInit() de mai jos, cu handle-ul primit.
 * Funcțiile sunt definite „weak” (goale) în HAL; definițiile de aici le înlocuiesc la link.
 * Numerele AF sunt din tabelul „Alternate function mapping” din datasheet-ul STM32F429 (DS9405).
 */
#include "main.h"

/* Chemată de HAL_Init() o singură dată, la pornire. SYSCFG (remapare memorie, EXTI) și PWR
 * (necesar pentru Scale 1 și over-drive în SystemClock_Config) primesc ceas înainte de orice altceva. */
void HAL_MspInit(void)
{
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
}

/* Chemată de HAL_ADC_Init(). Pornește ceasul ADC1 (pe APB2) și al portului A și pune PA5 pe
 * modul analogic: fără buffer digital Schmitt și fără pull-up/down, care ar altera tensiunea
 * măsurată și ar consuma curent prin divizorul senzorului. Alte ADC-uri sunt ignorate. */
void HAL_ADC_MspInit(ADC_HandleTypeDef *h)
{
    if (h->Instance != ADC1) return;
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef g = { .Pin = GPIO_PIN_5, .Mode = GPIO_MODE_ANALOG, .Pull = GPIO_NOPULL };   /* PA5 = ADC1_IN5 */
    HAL_GPIO_Init(GPIOA, &g);
}

/* Chemată de HAL_UART_Init() pentru fiecare UART. Pini în AF push-pull cu pull-up: linia UART
 * în repaus e HIGH, iar pull-up-ul ține RX la 1 când nimic nu e conectat (altfel zgomotul ar
 * genera octeți falși și erori de încadrare). Viteză VERY_HIGH e mai mult decât trebuie la 115200,
 * dar nu strică.
 * NVIC: prioritate de preempțiune 5 (grupare 4 din HAL_Init: 0 = cea mai urgentă, 15 = cea mai
 * puțin urgentă). SysTick are 15 (TICK_INT_PRIORITY), deci un octet primit pe UART poate întrerupe
 * tick-ul, nu invers; recepția octet cu octet are nevoie de reacție rapidă (un octet la 87 µs).
 * Ambele UART-uri au IRQ activ, dar doar cel al legăturii are recepție pornită (link_init). */
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
