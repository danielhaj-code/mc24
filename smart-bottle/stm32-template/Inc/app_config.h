/*
 * app_config.h — toate constantele reglabile ale firmware-ului, într-un singur loc.
 *
 * Inclus de main.c, app.c, sensor.c, display.c. Valorile marcate cu #ifndef pot fi suprascrise
 * din linia de compilare (Makefile: `make SENSOR=hx711`; Keil: target-ul „...HX711” are
 * SENSOR_USE_HX711=1 la Define). Restul se schimbă editând fișierul și recompilând totul.
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* ---- Alegere senzor ----
 * 0 = senzor analogic rezistiv (ADC1, cu alimentare comutată pe GPIO)
 * 1 = celulă de sarcină cu HX711 (bit-bang pe 2 GPIO)
 */
#ifndef SENSOR_USE_HX711
#define SENSOR_USE_HX711        0   /* sau: make SENSOR=hx711 */
#endif

/* ---- Pini (STM32F429I-DISC1) ----
 * Aleși dintre pinii nefolosiți de LCD/SDRAM/giroscop/USB. Verifică în UM1670
 * și în CubeMX înainte de cablare. Macro-urile de mai jos sunt folosite de sensor.c, dar
 * configurarea pinilor NU e doar aici: MX_GPIO_Init() din main.c scrie direct PE2/PE3/PE4, iar
 * pinii ADC (PA5) și UART (PC12/PD2, PA9/PA10) sunt în stm32f4xx_hal_msp.c. Dacă schimbi un pin,
 * schimbă-l în toate aceste locuri.
 *   ADC senzor       : PA5  (ADC1_IN5)
 *   Alimentare senzor: PE4  (GPIO output push-pull, default LOW)
 *   HX711 DT / SCK   : PE2 (input pull-up) / PE3 (output, default LOW)
 *   UART spre ESP32  : UART5 TX = PC12, RX = PD2, 115200 8N1
 */
#define SENS_PWR_PORT           GPIOE
#define SENS_PWR_PIN            GPIO_PIN_4
#define HX711_DT_PORT           GPIOE
#define HX711_DT_PIN            GPIO_PIN_2
#define HX711_SCK_PORT          GPIOE
#define HX711_SCK_PIN           GPIO_PIN_3

/* ---- Achiziție ----
 * O „citire” = SAMPLES_PER_READING eșantioane, din care se aruncă TRIM_COUNT cele mai mici și
 * TRIM_COUNT cele mai mari, iar restul (15 - 2×3 = 9) se mediază. Elimină vârfurile de zgomot fără
 * să întârzie răspunsul ca un filtru lung. Citirea e invalidă dacă reușesc sub 15/2 = 7 eșantioane. */
#define SAMPLES_PER_READING     15    /* eșantioane per citire (dimensionează și un tablou pe stivă) */
#define TRIM_COUNT              3     /* valori extreme eliminate la fiecare capăt (medie trunchiată); < 15/2 */
#define SENSOR_SETTLE_MS        5     /* ms; stabilizare după alimentarea senzorului analogic (HAL_Delay) */
#define READ_PERIOD_MS          1000  /* ms; perioada dintre citiri automate = rata mesajelor L spre ESP32 */
#define HX711_TIMEOUT_MS        200   /* ms; la 10 SPS un eșantion durează ~100 ms; RATE=VCC => 80 SPS.
                                       * Dacă DT nu coboară în acest timp, eșantionul e ratat (modul lipsă). */

/* ---- Detecție stabilitate ----
 * Nivelul e „stabil” (câmpul stable din mesajul L) când ultimele STABLE_WINDOW citiri au
 * max - min <= STABLE_THRESHOLD; cu 1 citire/s înseamnă ~5 s fără mișcare. Pragul depinde de
 * senzor fiindcă unitățile brute diferă (ADC 12 biți vs. HX711 24 biți). */
#define STABLE_WINDOW           5     /* citiri consecutive analizate */
#if SENSOR_USE_HX711
#define STABLE_THRESHOLD        2000  /* toleranță max-min (unități HX711) */
#else
#define STABLE_THRESHOLD        15    /* toleranță max-min (unități ADC) */
#endif

/* ---- Calibrare implicită (suprascrisă de aplicație cu "C,gol,plin") ----
 * Valori brute (aceleași unități ca senzorul) corespunzătoare lui 0% și 100%. Se folosesc doar
 * până la prima conectare a aplicației sau până la calibrarea din meniul tactil; nu se salvează
 * în Flash (sursa de adevăr e aplicația). 3000 ≈ 2,4 V pe ADC; pentru HX711 trebuie calibrat. */
#define CAL_DEFAULT_EMPTY       0
#define CAL_DEFAULT_FULL        3000

/* ---- Orientare LCD: 1 = rotit 180° (placa ținută cu USB-ul în jos) ---- */
#ifndef LCD_ROTATE_180
#define LCD_ROTATE_180          1
#endif

/* ---- Screensaver: ecran întunecat după atâta timp fără atingere (0 = dezactivat) ---- */
#define SCREENSAVER_MS          120000   /* ms = 2 minute */

/* ---- Praguri culoare LCD (aceleași ca în aplicație; procente din nivelul calibrat) ---- */
#define LEVEL_OK_PCT            50    /* >= 50%  albastru (optim) */
#define LEVEL_MID_PCT           20    /* >= 20%  galben (mediu), altfel roșu (scăzut) */

#endif /* APP_CONFIG_H */
