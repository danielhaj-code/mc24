#ifndef APP_H
#define APP_H

/*
 * app — logica aplicației: programează citirile senzorului, procesează comenzile de la ESP32/telefon
 * (protocol.h, PROTOCOL.md), actualizează LCD-ul (display.h) și tratează touch-ul și butonul USER.
 *
 * Contract: main.c (scris de mână, nu generat de CubeMX) le apelează direct, după ce a inițializat
 * ceasurile, GPIO, ADC1 și UART-urile:
 *   app_init();              o dată, înainte de bucla principală
 *   while (1) app_loop();    la nesfârșit
 * app_init inițializează LED-uri, buton, LCD, senzor și legătura și face prima citire.
 * app_loop nu blochează mult: cel mai lung pas e o citire de senzor (~5 ms analogic, până la
 * ~1,5 s cu HX711 la 10 SPS) plus redesenarea LCD-ului.
 */
void app_init(void);
void app_loop(void);

#endif /* APP_H */
