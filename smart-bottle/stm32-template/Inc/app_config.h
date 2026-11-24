/*
 * app_config.h — toate setările aplicației, într-un singur loc.
 *
 * Pinii (PA5 = senzor, PE4 = alimentarea senzorului, PC12/PD2 = UART5 spre ESP32) sunt în
 * Template_GPIO.ioc și în main.h (etichetele generate de CubeMX: SENS_PWR_Pin, LD3_Pin etc.).
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* ---- Citirea senzorului ---- */
#define READ_PERIOD_MS      1000   /* o citire pe secundă */
#define SAMPLES             16     /* conversii ADC pe citire */
#define TRIM                4      /* valori aruncate la fiecare capăt (cele mai mici și cele mai mari) */
#define SENSOR_SETTLE_MS    5      /* așteptare după alimentarea senzorului, înainte de măsurare */

/* ---- Stabilitate: ultimele STABLE_WINDOW citiri diferă cu cel mult STABLE_THRESHOLD ---- */
#define STABLE_WINDOW       5
#define STABLE_THRESHOLD    15     /* unități ADC (0..4095) */

/* ---- Calibrarea până o trimite aplicația cu „C,gol,plin” ---- */
#define CAL_DEFAULT_EMPTY   0
#define CAL_DEFAULT_FULL    3000

/* ---- Legătura cu aplicația ---- */
#define APP_TIMEOUT_MS      5000   /* ESP32 trimite H la 2 s; fără H atâta timp = aplicație deconectată */
#define ML_TIMEOUT_MS       10000  /* aplicația retrimite M la 5 s; un M mai vechi nu se mai afișează */

/* ---- LCD ---- */
#define LCD_ROTATE_180      1      /* 1 = imagine rotită, pentru placa ținută cu USB-ul în jos */

#endif /* APP_CONFIG_H */
