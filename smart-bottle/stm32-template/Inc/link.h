/*
 * link.h — legătura serială cu ESP32 (UART5, 115200 8N1), mesaje text terminate cu '\n'.
 * Formatul mesajelor e descris în PROTOCOL.md din repo.
 */
#ifndef LINK_H
#define LINK_H

#include <stdint.h>

/* Un mesaj primit: litera de la început și până la 3 numere după virgule.
 * Exemplu: „C,800,3000” -> type 'C', n = 2, v[0] = 800, v[1] = 3000. */
typedef struct {
    char    type;
    int     n;               /* câte numere au fost citite */
    int32_t v[3];
} link_msg_t;

void link_init(void);                    /* pornește recepția pe întreruperi */
int  link_poll(link_msg_t *msg);         /* 1 dacă a completat un mesaj nou, 0 dacă nu */
void link_send(const char *text);        /* trimite text așa cum e (cu '\n' inclus) */
void link_send_level(int32_t raw, int pct, int stable);   /* „L,<raw>,<pct>,<stable>\n” */

#endif /* LINK_H */
