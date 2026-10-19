#ifndef LINK_H
#define LINK_H

#include <stdint.h>
#include <stddef.h>
#include "protocol.h"

/* Legătura UART cu ESP32: recepție pe întreruperi într-un ring buffer,
 * interpretare în bucla principală. */
void link_init(void);

/* Golește octeții primiți; întoarce prima comandă completă găsită (sau CMD_NONE). */
proto_cmd_t link_poll(void);

void link_send(const char *s, size_t len);
void link_send_str(const char *s);

/* De apelat din HAL_UART_RxCpltCallback. */
void link_on_rx_byte(void);

#endif /* LINK_H */
