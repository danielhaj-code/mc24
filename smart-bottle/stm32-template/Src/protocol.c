/*
 * protocol.c — parserul și formatarea mesajelor text (vezi protocol.h și PROTOCOL.md).
 *
 * Totul e C standard (strtol, snprintf, isspace), fără HAL: aceeași sursă rulează în testele de pe PC.
 * Fără stare globală: funcțiile pot fi apelate oricând din bucla principală; NU din ISR
 * (snprintf/strtol sunt prea lente și folosesc errno).
 */
#include "protocol.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

/* Citește un întreg zecimal; avansează *p. Întoarce 1 la succes.
 * strtol acceptă spații înainte și semn (+/-). Eșec dacă nu există nicio cifră (end == *p),
 * dacă valoarea depășește long (ERANGE) sau nu încape în int32 (pe ARM long e deja 32 de biți,
 * pe PC 64 — verificarea contează pentru testele de pe PC). La eșec *p rămâne neschimbat. */
static int parse_int(const char **p, int32_t *out)
{
    char *end;
    errno = 0;
    long v = strtol(*p, &end, 10);
    if (end == *p || errno == ERANGE || v < INT32_MIN || v > INT32_MAX) return 0;
    *out = (int32_t)v;
    *p = end;
    return 1;
}

/* 1 dacă de la p până la sfârșitul șirului sunt doar spații albe (inclusiv '\r').
 * Asta face parserul strict: "R,5" sau "S,1x" sunt respinse, nu interpretate parțial. */
static int at_end(const char *p)
{
    while (*p && isspace((unsigned char)*p)) p++;
    return *p == '\0';
}

/* Citește exact n întregi de forma ",x,y,z" până la sfârșitul liniei.
 * p indică imediat după litera tipului. Întoarce 1 doar dacă toate cele n câmpuri există și după
 * ultimul nu mai urmează nimic (în afară de spații). v[] e completat parțial la eșec. */
static int parse_fields(const char *p, int32_t *v, int n)
{
    for (int i = 0; i < n; i++) {
        if (*p++ != ',') return 0;
        if (!parse_int(&p, &v[i])) return 0;
    }
    return at_end(p);
}

/*
 * Interpretează o linie completă (vezi protocol.h). Algoritm:
 *  1. sare spațiile de la început; linie goală => CMD_NONE (nu e o eroare);
 *  2. prima literă alege tipul; de aici rezultatul implicit e CMD_INVALID;
 *  3. câmpurile se citesc strict (exact numărul cerut, apoi sfârșit de linie) și se verifică
 *     domeniul; doar dacă totul e corect se setează tipul real.
 * Domeniile sunt verificate aici, nu în app.c, ca restul firmware-ului să primească doar
 * valori folosibile direct (ex. index G sigur < HIST_POINTS, deci fără depășire de vector).
 * Literele sunt sensibile la majuscule ("r" e invalid).
 */
proto_cmd_t proto_parse(const char *line)
{
    proto_cmd_t cmd = { CMD_NONE, 0, 0, 0 };
    int32_t v[3] = { 0, 0, 0 };   /* câmpurile pentru tipurile citite cu parse_fields() */
    if (!line) return cmd;
    while (*line && isspace((unsigned char)*line)) line++;
    if (*line == '\0') return cmd;

    const char *p = line + 1;     /* imediat după litera tipului */
    cmd.type = CMD_INVALID;

    switch (line[0]) {
    case 'R':
        if (at_end(p)) cmd.type = CMD_READ_NOW;
        break;
    case 'C':   /* C,<gol>,<plin>: fără verificare de domeniu, valorile HX711 pot fi orice int32 */
        if (*p++ != ',') break;
        if (!parse_int(&p, &cmd.a)) break;
        if (*p++ != ',') break;
        if (!parse_int(&p, &cmd.b)) break;
        if (at_end(p)) cmd.type = CMD_CALIBRATE;
        break;
    case 'S':   /* S,<0|1> */
        if (*p++ != ',') break;
        if (!parse_int(&p, &cmd.a)) break;
        if (at_end(p) && (cmd.a == 0 || cmd.a == 1)) cmd.type = CMD_LINK_STATE;
        break;
    case 'M':   /* M,<ml>,<capacitate>: capacitatea e împărțitor => strict pozitivă */
        if (parse_fields(p, v, 2) && v[0] >= 0 && v[1] > 0) cmd.type = CMD_VOLUME;
        break;
    case 'T':   /* T,<total>,<obiectiv>,<nr>: obiectivul e împărțitor (procent zilnic) => > 0 */
        if (parse_fields(p, v, 3) && v[0] >= 0 && v[1] > 0 && v[2] >= 0) cmd.type = CMD_TODAY;
        break;
    case 'U':   /* U,<minute>: -1 = nicio înghițitură */
        if (parse_fields(p, v, 1) && v[0] >= -1) cmd.type = CMD_SINCE;
        break;
    case 'W':   /* W,<zi>,<ml>: zi 0..6 = index în vectorul celor 7 zile */
        if (parse_fields(p, v, 2) && v[0] >= 0 && v[0] <= 6 && v[1] >= 0) cmd.type = CMD_DAY;
        break;
    case 'H':   /* H,<uptime_s>,<aplicație>: 0 = deconectat, 1 = conectat, 2 = aplicație activă */
        if (parse_fields(p, v, 2) && v[0] >= 0 && v[1] >= 0 && v[1] <= 2) cmd.type = CMD_HEARTBEAT;
        break;
    case 'G':   /* G,<index>,<procent>: index < HIST_POINTS (index în vector), procent -1..100 */
        if (parse_fields(p, v, 2) && v[0] >= 0 && v[0] < HIST_POINTS && v[1] >= -1 && v[1] <= 100)
            cmd.type = CMD_HISTORY;
        break;
    default:
        break;
    }
    /* Tipurile citite cu parse_fields() au valorile în v[]; le copiem în a/b/c doar dacă
     * linia a fost validă (C și S scriu direct în cmd.a/cmd.b). */
    if (cmd.type == CMD_HEARTBEAT || cmd.type == CMD_VOLUME || cmd.type == CMD_TODAY || cmd.type == CMD_SINCE || cmd.type == CMD_DAY
        || cmd.type == CMD_HISTORY) {
        cmd.a = v[0]; cmd.b = v[1]; cmd.c = v[2];
    }
    return cmd;
}

/* Formatează mesajul L (vezi protocol.h). snprintf întoarce lungimea care AR fi fost scrisă:
 * dacă >= cap, textul a fost trunchiat => întoarcem 0 ca apelantul să nu trimită o linie ciuntită.
 * %ld + cast la long: int32_t e long pe ARM, dar int pe PC — cast-ul face formatul portabil. */
size_t proto_format_level(char *out, size_t cap, int32_t raw, int pct, int stable)
{
    int n = snprintf(out, cap, "L,%ld,%d,%d\n", (long)raw, pct, stable ? 1 : 0);
    if (n < 0 || (size_t)n >= cap) return 0;
    return (size_t)n;
}

/* Formatează mesajul K; aceleași reguli ca proto_format_level(). */
size_t proto_format_cal(char *out, size_t cap, int32_t empty, int32_t full)
{
    int n = snprintf(out, cap, "K,%ld,%ld\n", (long)empty, (long)full);
    if (n < 0 || (size_t)n >= cap) return 0;
    return (size_t)n;
}

/* Pornește asamblorul fără linie în curs; buf[0] = '\0' ca buf să fie mereu un șir valid. */
void line_asm_init(line_asm_t *la)
{
    la->len = 0;
    la->overflow = 0;
    la->buf[0] = '\0';
}

/*
 * Asamblorul de linii, apelat pentru fiecare octet primit:
 *  - caracter normal: se adaugă în buf; dacă buf ar ajunge la PROTO_LINE_MAX - 1 caractere
 *    (ultimul loc e rezervat pentru '\0'), linia e marcată overflow și golită — restul ei până
 *    la '\n' se ignoră, deci se aruncă toată linia, nu doar coada;
 *  - '\n': închide linia. Un '\r' final (terminator Windows "\r\n") e șters. Linia e raportată
 *    (întoarce 1) doar dacă nu a fost overflow și nu e goală; oricum, starea revine la „linie nouă”.
 * Caz particular: o linie formată doar din "\r" e raportată ca șir gol, iar proto_parse() o
 * transformă în CMD_NONE, deci e inofensivă.
 */
int line_asm_push(line_asm_t *la, char c)
{
    if (c == '\n') {
        int complete = !la->overflow && la->len > 0;   /* decis ÎNAINTE de a tăia '\r' */
        la->buf[la->len] = '\0';
        if (la->len > 0 && la->buf[la->len - 1] == '\r') la->buf[--la->len] = '\0';
        if (!complete) la->len = 0;   /* linie prea lungă sau goală: aruncată */
        la->overflow = 0;
        if (complete) {
            la->len = 0;               /* bufferul rămâne valid până la următorul push */
            return 1;
        }
        return 0;
    }
    if (la->overflow) return 0;
    if (la->len >= PROTO_LINE_MAX - 1) {
        la->overflow = 1;
        la->len = 0;
        return 0;
    }
    la->buf[la->len++] = c;
    return 0;
}
