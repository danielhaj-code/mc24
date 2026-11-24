/*
 * Smart Bottle — firmware STM32F429I-DISC1 (proiect Keil µVision în stm32-keil/ sau build cu make).
 *
 * Ce face main: ceasuri (180 MHz din HSE 8 MHz), periferice (ADC1, UART5, USART1, GPIO),
 * apoi predă controlul aplicației (app.c): citire senzor, LCD, protocol spre ESP32.
 *
 * Portul serial USB al plăcii (ST-LINK VCP, USART1) e folosit pentru jurnal: la pornire și
 * pentru fiecare linie trimisă/primită pe legătura cu ESP32. `make monitor` îl deschide
 * (în Keil: orice terminal serial pe portul COM „STMicroelectronics STLink Virtual COM Port”, 115200 8N1).
 *
 * Ordinea de pornire (contează):
 *   1. Reset_Handler (startup_stm32f429xx.s) -> SystemInit() (FPU, VTOR) -> main(); până aici
 *      procesorul merge pe HSI 16 MHz.
 *   2. HAL_Init(): Flash prefetch/cache, grupare priorități NVIC (4 biți preempțiune, 0 sub-prioritate),
 *      SysTick la 1 ms (încă pe HSI), apoi HAL_MspInit() din stm32f4xx_hal_msp.c.
 *   3. SystemClock_Config(): trece pe PLL 180 MHz; HAL_RCC_ClockConfig reconfigurează singur SysTick.
 *   4. GPIO/ADC/UART-uri: HAL_xxx_Init() cheamă la rândul lui HAL_xxx_MspInit() (pini, ceasuri, NVIC).
 *   5. app_init() (LCD, touch, senzor, recepție UART pe întreruperi), apoi app_loop() la nesfârșit.
 *
 * Fișierul e scris de mână (nu e generat de CubeMX): nu există blocuri „USER CODE”; handle-urile
 * hadc1/huart5/huart1 sunt definite aici și exportate prin main.h.
 *
 * Definiții de compilare (din Makefile sau din „Options for Target -> C/C++ -> Define” în Keil):
 *   SENSOR_USE_HX711=1  -> HX711 în loc de senzorul analogic (vezi app_config.h, sensor.c)
 *   LINK_VCP=1          -> protocolul circulă pe USART1 (USB) în loc de UART5; jurnalul e oprit
 *   FW_VERSION="..."    -> textul afișat în mesajul de boot (implicit "dev")
 */
#include "main.h"
#include "app.h"
#include "app_config.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Handle-uri HAL ale perifericelor. Definite aici, folosite și de sensor.c (hadc1) și link.c
 * (LINK_HUART = huart5 sau huart1). Conțin starea driverului HAL (inclusiv starea recepției pe
 * întreruperi), deci sunt scrise și din ISR-urile UART prin HAL_UART_IRQHandler. */
ADC_HandleTypeDef  hadc1;
UART_HandleTypeDef huart5;
UART_HandleTypeDef huart1;

static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_UART_Init(UART_HandleTypeDef *h, USART_TypeDef *inst, uint32_t baud);

/* Versiunea firmware-ului, de obicei dată din Makefile (git describe); doar pentru mesajul de boot. */
#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

/* Punctul de intrare după startup. Inițializează hardware-ul în ordinea descrisă în antet,
 * scrie mesajul de boot pe VCP și rulează bucla aplicației. Nu se întoarce niciodată.
 * Orice eșec de inițializare HAL ajunge în Error_Handler() (LD4 clipește). */
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

    /* Super-buclă fără sistem de operare: tot ce e periodic se programează în app_loop()
     * pe baza HAL_GetTick(); singurele întreruperi folosite sunt SysTick și UART-urile. */
    while (1) {
        app_loop();
    }
}

/* ===================== Jurnal pe VCP ===================== */
/* printf simplificat spre USART1 (portul serial USB al ST-LINK).
 * fmt/...: ca la printf; mesajul e trunchiat la 127 de caractere (bufferul are 128 cu terminatorul).
 * Blocant: HAL_UART_Transmit prin polling, maxim 20 ms (128 octeți la 115200 baud ≈ 11 ms), deci
 * se cheamă doar din bucla principală, niciodată din întreruperi.
 * Cu LINK_VCP=1 USART1 transportă protocolul, așa că funcția nu face nimic (altfel jurnalul s-ar
 * amesteca printre liniile protocolului și le-ar strica). */
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
    /* vsnprintf întoarce lungimea care AR FI rezultat; dacă depășește bufferul, trimitem doar cât
     * s-a scris efectiv (sizeof buf - 1, fără '\0'). Timeout 20 ms: dacă UART-ul e blocat, pierdem
     * mesajul în loc să oprim aplicația. */
    if (n > 0) HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)(n < (int)sizeof buf ? n : (int)sizeof buf - 1), 20);
#endif
}

/* ===================== Ceasuri: 180 MHz ===================== */
/* HSE 8 MHz (cristal X3 pe DISC1) -> PLL: /8 *360 /2 = 180 MHz. APB1 45 MHz, APB2 90 MHz.
 * Aceleași valori ca în șablonul ST pentru placă; LTDC-ul are ceasul lui (PLLSAI), setat de BSP.
 *
 * Calculul PLL principal:
 *   intrare VCO  = HSE / PLLM = 8 MHz / 8  = 1 MHz     (permis 1..2 MHz)
 *   ieșire VCO   = 1 MHz * PLLN = 1 * 360   = 360 MHz  (permis 100..432 MHz)
 *   SYSCLK       = VCO / PLLP   = 360 / 2   = 180 MHz  (maximul lui STM32F429)
 *   PLL48CK      = VCO / PLLQ   = 360 / 7   ≈ 51,4 MHz (ar trebui 48 MHz pentru USB OTG/SDIO,
 *                  dar nu le folosim, așa că valoarea nu contează)
 * Magistrale: HCLK (AHB) = 180 MHz; APB1 = 180/4 = 45 MHz (max 45); APB2 = 180/2 = 90 MHz (max 90).
 * Timerele de pe APB1/APB2 primesc dublul (90/180 MHz), fiindcă prescalerul APB e diferit de 1.
 *
 * Peste 168 MHz regulatorul intern trebuie pus în Scale 1 ȘI în modul over-drive; fără over-drive
 * nucleul nu e garantat stabil la 180 MHz. Over-drive se activează după pornirea PLL și înainte de
 * comutarea SYSCLK pe PLL (secvența din manualul de referință RM0090).
 * Flash-ul nu ține pasul cu 180 MHz: la 2,7..3,6 V și 150 < HCLK <= 180 MHz trebuie 5 stări de
 * așteptare (FLASH_LATENCY_5); ART accelerator + prefetch (activate în HAL_Init) ascund mare parte.
 * Apelată o singură dată, din main(), imediat după HAL_Init(). Eșec (ex. cristal lipsă) -> Error_Handler. */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);   /* Scale 1 = tensiune maximă nucleu */

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 8;
    osc.PLL.PLLN = 360;
    osc.PLL.PLLP = RCC_PLLP_DIV2;
    osc.PLL.PLLQ = 7;                     /* ieșire 48 MHz nefolosită (vezi calculul de mai sus) */
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) Error_Handler();

    if (HAL_PWREx_EnableOverDrive() != HAL_OK) Error_Handler();   /* necesar peste 168 MHz */

    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;
    /* Latența Flash se setează aici: HAL o crește ÎNAINTE de a mări frecvența. HAL_RCC_ClockConfig
     * actualizează și SystemCoreClock și reprogramează SysTick la 1 ms pentru noul ceas. */
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK) Error_Handler();
}

/* ===================== GPIO ===================== */
/* Pinii senzorului (aceiași ca în app_config.h: SENS_PWR_*, HX711_*). Atenție: aici sunt scriși
 * direct ca GPIOE / PIN_2/3/4, deci o schimbare de pin în app_config.h trebuie făcută și aici.
 * Pinii UART/ADC sunt configurați în stm32f4xx_hal_msp.c; LED-urile, butonul, LCD-ul și touch-ul
 * de BSP (din app_init / display_init). Ambii pini de ieșire sunt configurați indiferent de
 * senzorul ales, ca să nu rămână flotanți. */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOE_CLK_ENABLE();

    /* PE4: alimentarea senzorului analogic; PE3: SCK pentru HX711. Ambele pornesc LOW.
     * Nivelul se scrie ÎNAINTE de a face pinii ieșiri, ca să nu apară un impuls HIGH scurt
     * (SCK HIGH > 60 µs ar pune HX711 în power-down; alimentarea ar porni senzorul degeaba). */
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_4 | GPIO_PIN_3, GPIO_PIN_RESET);
    g.Pin = GPIO_PIN_4 | GPIO_PIN_3;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;        /* fronturi lente = mai puțin zgomot; HX711 merge la ~500 kHz */
    HAL_GPIO_Init(GPIOE, &g);

    /* PE2: DT de la HX711 (intrare cu pull-up; nefolosit cu senzorul analogic).
     * Pull-up-ul face ca un modul deconectat să arate „ocupat” (DT HIGH), iar sensor.c iese pe
     * timeout în loc să citească zerouri false. */
    g.Pin = GPIO_PIN_2;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOE, &g);
}

/* ===================== ADC1, canal 5 (PA5) ===================== */
/* O singură conversie la cerere (start software, polling), 12 biți (0..4095 pentru 0..3,3 V).
 * Folosit doar de sensor.c în varianta analogică, dar inițializat mereu (cost neglijabil).
 * Pinul PA5 e trecut pe modul analogic în HAL_ADC_MspInit(). */
static void MX_ADC1_Init(void)
{
    ADC_ChannelConfTypeDef ch = {0};

    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;     /* 90 / 4 = 22,5 MHz (max ADC: 36 MHz) */
    hadc1.Init.Resolution = ADC_RESOLUTION_12B;
    hadc1.Init.ScanConvMode = DISABLE;                        /* un singur canal */
    hadc1.Init.ContinuousConvMode = DISABLE;                  /* o conversie per HAL_ADC_Start */
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = 1;
    hadc1.Init.DMAContinuousRequests = DISABLE;
    hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;            /* EOC după fiecare conversie (pentru polling) */
    if (HAL_ADC_Init(&hadc1) != HAL_OK) Error_Handler();

    ch.Channel = ADC_CHANNEL_5;
    ch.Rank = 1;
    /* senzorul are impedanță mare: condensatorul de eșantionare al ADC-ului are nevoie de timp să
     * se încarce prin ea. 84 cicluri / 22,5 MHz ≈ 3,7 µs; conversia totală = 84 + 12 = 96 cicluri
     * ≈ 4,3 µs, deci 15 eșantioane durează ~65 µs — neglijabil față de perioada de 1 s. */
    ch.SamplingTime = ADC_SAMPLETIME_84CYCLES;
    if (HAL_ADC_ConfigChannel(&hadc1, &ch) != HAL_OK) Error_Handler();
}

/* ===================== UART-uri: 8N1 ===================== */
/* Configurare comună pentru UART5 (ESP32) și USART1 (VCP): 8 biți date, fără paritate, 1 stop,
 * fără RTS/CTS, oversampling 16 (toleranță mai bună la eroarea de baud decât 8).
 * Baud: UART5 e pe APB1 (45 MHz), USART1 pe APB2 (90 MHz); HAL calculează BRR din PCLK, eroarea la
 * 115200 e sub 0,1%. h = handle de completat, inst = periferic, baud = viteza (ambele 115200).
 * Pinii, ceasul perifericului și NVIC-ul se fac în HAL_UART_MspInit(); recepția pe întreruperi o
 * pornește link_init() doar pe UART-ul legăturii. */
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
/* Capcana finală pentru erori fatale: init HAL eșuat, HardFault/MemManage/BusFault/UsageFault,
 * assert_failed. LED-ul roșu (LD4, PG14) clipește repede la nesfârșit.
 * Dezactivează întreruperile (nu mai rulează nimic altceva) și reconfigurează singur PG14, fiindcă
 * eroarea poate apărea înainte ca BSP_LED_Init să fi fost apelat. Bucla de întârziere e goală și
 * „volatile” ca să nu fie eliminată de compilator; frecvența clipirii depinde de ceasul la care a
 * apărut eroarea (HSI 16 MHz înainte de SystemClock_Config, 180 MHz după), deci e doar orientativă.
 * Nu se întoarce; ieșirea se face doar prin reset. */
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

/* Apelată de macro-ul assert_param din HAL doar dacă se compilează cu USE_FULL_ASSERT
 * (parametru invalid dat unei funcții HAL). file/line sunt ignorate: nu avem unde le afișa sigur. */
#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file; (void)line;
    Error_Handler();
}
#endif
