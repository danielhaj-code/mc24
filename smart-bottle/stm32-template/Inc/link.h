/*
 * link.h — legătura serială cu ESP32 (sau cu PC-ul, în varianta LINK_VCP).
 *
 * Recepția e pe întreruperi, câte un octet, într-un ring buffer; interpretarea liniilor
 * (protocol.h) se face în bucla principală prin link_poll(). Transmisia e blocantă.
 * UART-ul folosit e ales în main.h: LINK_VCP = 0 -> UART5 (spre ESP32),
 * LINK_VCP = 1 (make LINK=vcp) -> USART1 = portul serial virtual ST-LINK (teste de pe PC).
 * Cine apelează: main.c/app.c (link_init, link_poll, link_send*); HAL (callback-urile din link.c).
 */
#ifndef LINK_H
#define LINK_H

#include <stdint.h>
#include <stddef.h>
#include "protocol.h"

/* Legătura UART cu ESP32: recepție pe întreruperi într-un ring buffer,
 * interpretare în bucla principală.
 * link_init(): golește ring buffer-ul și asamblorul de linii și pornește prima recepție de 1 octet.
 * De apelat o dată, după inițializarea UART-ului și a NVIC (în main.c). */
void link_init(void);

/* Golește octeții primiți; întoarce prima comandă completă găsită (sau CMD_NONE).
 * Se oprește la prima comandă ca apelantul s-o trateze înainte de a citi următoarea; octeții
 * rămași stau în ring buffer, deci se apelează în buclă până la CMD_NONE (vezi app.c).
 * Liniile goale nu se întorc; CMD_INVALID da (apelantul îl ignoră). Doar din bucla principală. */
proto_cmd_t link_poll(void);

/* Trimite len octeți (blocant, timeout HAL 50 ms; la eroare/timeout datele se pierd fără semnal).
 * La 115200 baud, 1 octet ~87 µs, deci un mesaj L (~20 octeți) ține ~2 ms. Doar din bucla principală. */
void link_send(const char *s, size_t len);
/* Ca link_send(), pentru un șir terminat cu '\0'. */
void link_send_str(const char *s);

/* De apelat din HAL_UART_RxCpltCallback (context de întrerupere): pune octetul primit în ring
 * buffer și repornește recepția. Apelat deja de callback-ul definit în link.c. */
void link_on_rx_byte(void);

#endif /* LINK_H */
