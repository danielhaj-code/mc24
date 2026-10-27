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
 * și în CubeMX înainte de cablare; dacă schimbi un pin, schimbă-l doar aici.
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

/* ---- Achiziție ---- */
#define SAMPLES_PER_READING     15    /* eșantioane per citire */
#define TRIM_COUNT              3     /* valori extreme eliminate la fiecare capăt (medie trunchiată) */
#define SENSOR_SETTLE_MS        5     /* stabilizare după alimentarea senzorului analogic */
#define READ_PERIOD_MS          1000  /* perioada dintre citiri */
#define HX711_TIMEOUT_MS        200   /* la 10 SPS un eșantion durează ~100 ms; RATE=VCC => 80 SPS */

/* ---- Detecție stabilitate ---- */
#define STABLE_WINDOW           5     /* citiri consecutive analizate */
#if SENSOR_USE_HX711
#define STABLE_THRESHOLD        2000  /* toleranță max-min (unități HX711) */
#else
#define STABLE_THRESHOLD        15    /* toleranță max-min (unități ADC) */
#endif

/* ---- Calibrare implicită (suprascrisă de aplicație cu "C,gol,plin") ---- */
#define CAL_DEFAULT_EMPTY       0
#define CAL_DEFAULT_FULL        3000

/* ---- Praguri culoare LCD (aceleași ca în aplicație) ---- */
#define LEVEL_OK_PCT            50    /* >= 50%  albastru (optim) */
#define LEVEL_MID_PCT           20    /* >= 20%  galben (mediu), altfel roșu (scăzut) */

#endif /* APP_CONFIG_H */
