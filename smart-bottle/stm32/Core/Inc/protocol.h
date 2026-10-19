#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

/* Cod pur C (fără HAL) — testabil pe PC. Vezi PROTOCOL.md. */

#define PROTO_LINE_MAX 32

typedef enum {
    CMD_NONE = 0,
    CMD_CALIBRATE,   /* C,<gol>,<plin> */
    CMD_READ_NOW,    /* R */
    CMD_LINK_STATE,  /* S,<0|1>  (de la ESP32) */
    CMD_INVALID
} proto_cmd_type_t;

typedef struct {
    proto_cmd_type_t type;
    int32_t a;   /* gol / stare conexiune */
    int32_t b;   /* plin */
} proto_cmd_t;

/* Interpretează o linie fără '\n' (se tolerează '\r' și spații la capete). */
proto_cmd_t proto_parse(const char *line);

/* Formatează "L,<raw>,<pct>,<stable>\n". Întoarce lungimea sau 0 dacă nu încape. */
size_t proto_format_level(char *out, size_t cap, int32_t raw, int pct, int stable);

/* Asamblare linii dintr-un flux de octeți (pentru UART). */
typedef struct {
    char buf[PROTO_LINE_MAX];
    size_t len;
    int overflow;
} line_asm_t;

void line_asm_init(line_asm_t *la);
/* Adaugă un octet; întoarce 1 când o linie completă e în la->buf (terminată cu '\0').
 * Liniile prea lungi sunt aruncate întregi. */
int  line_asm_push(line_asm_t *la, char c);

#endif /* PROTOCOL_H */
