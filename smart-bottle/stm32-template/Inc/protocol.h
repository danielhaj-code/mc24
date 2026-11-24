/*
 * protocol.h — protocolul text pe linii dintre STM32 și ESP32 (vezi PROTOCOL.md).
 *
 * Fiecare mesaj e o linie ASCII: o literă (tipul), apoi câmpuri întregi zecimale separate prin ',',
 * terminată cu '\n' (un '\r' înainte e tolerat). Exemplu: "C,8423100,8612000\n".
 *
 * Cine folosește modulul:
 *  - link.c: line_asm_push() pe fiecare octet primit pe UART, apoi proto_parse() pe linia completă;
 *  - app.c: interpretează proto_cmd_t și trimite răspunsuri formatate cu proto_format_*().
 *
 * Decizii de design:
 *  - cod C pur (fără HAL, fără alocare dinamică) => se poate compila și testa pe PC;
 *  - parser strict: orice abatere (câmp lipsă, text în plus, valoare în afara domeniului) dă
 *    CMD_INVALID, nu o comandă „pe jumătate” — o linie coruptă pe UART nu poate modifica starea;
 *  - linii scurte (max PROTO_LINE_MAX - 1 caractere), ca bufferul să fie static și mic.
 */
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

/* Cod pur C (fără HAL) — testabil pe PC. Vezi PROTOCOL.md. */

/* Lungimea maximă a unei linii primite, inclusiv terminatorul '\0' => max 31 de caractere utile.
 * Cel mai lung mesaj real ("T,<total>,<obiectiv>,<nr>") încape larg. */
#define PROTO_LINE_MAX 32
#define HIST_POINTS    60   /* istoricul nivelului: un punct la 10 s, ultimele 10 min; index G valid 0..59 */

/*
 * Tipurile de mesaje primite de STM32. Toate vin pe UART de la ESP32; cele marcate „de la aplicație”
 * sunt trimise de telefon prin BLE și doar retransmise de ESP32. Formatul exact e în comentariu;
 * domeniile verificate de proto_parse() sunt cele de mai jos (altfel => CMD_INVALID).
 * Sensul opus (STM32 -> ESP32) are doar „L” și „K”, vezi proto_format_level() / proto_format_cal().
 */
typedef enum {
    CMD_NONE = 0,    /* nimic de făcut (linie goală / nicio linie completă) */
    CMD_CALIBRATE,   /* C,<gol>,<plin>  — valori brute HX711, orice int32 (pot fi negative);
                      * a = gol, b = plin. Nu se cere gol != plin (level_percent tratează span 0). */
    CMD_READ_NOW,    /* R  — cere o măsurătoare imediată (răspuns: L,...); fără câmpuri */
    CMD_LINK_STATE,  /* S,<0|1>  (de la ESP32) — 1 = telefon conectat prin BLE; a = stare */
    CMD_VOLUME,      /* M,<ml>,<capacitate_ml>          (de la aplicație); ml >= 0, capacitate > 0 */
    CMD_TODAY,       /* T,<total_ml>,<obiectiv_ml>,<nr> (de la aplicație); total >= 0, obiectiv > 0, nr >= 0 */
    CMD_SINCE,       /* U,<minute de la ultima înghițitură | -1>  (-1 = nicio înghițitură azi); >= -1 */
    CMD_DAY,         /* W,<0..6>,<ml>  (6 = azi); ml >= 0 */
    CMD_HEARTBEAT,   /* H,<uptime_s>,<aplicație 0|1|2>  (de la ESP32, la 2 s); uptime >= 0 */
    CMD_HISTORY,     /* G,<0..59>,<procent | -1>  (de la ESP32; 59 = cel mai recent); -1 = fără date */
    CMD_INVALID      /* linie nerecunoscută sau cu câmpuri invalide; apelantul o ignoră */
} proto_cmd_type_t;

/*
 * Comandă interpretată. Câmpurile a/b/c sunt valorile numerice în ordinea din linie:
 *   C: a = gol, b = plin            S: a = 0/1
 *   M: a = ml, b = capacitate       T: a = total, b = obiectiv, c = nr
 *   U: a = minute                   W: a = zi (0..6), b = ml
 *   H: a = uptime_s, b = aplicație  G: a = index (0..59), b = procent
 * Câmpurile nefolosite sunt 0. Pentru CMD_INVALID valorile nu au semnificație.
 */
typedef struct {
    proto_cmd_type_t type;
    int32_t a;   /* primul câmp (ex. gol / stare conexiune) */
    int32_t b;   /* al doilea câmp (ex. plin) */
    int32_t c;   /* al treilea câmp (doar T) */
} proto_cmd_t;

/* Interpretează o linie fără '\n' (se tolerează '\r' și spații la capete).
 * line == NULL sau doar spații => CMD_NONE; altfel tipul recunoscut sau CMD_INVALID.
 * Nu modifică linia; fără efecte secundare. */
proto_cmd_t proto_parse(const char *line);

/* Formatează "L,<raw>,<pct>,<stable>\n" (STM32 -> ESP32: citire brută, procent 0..100, stabil 0/1).
 * Întoarce lungimea (fără '\0') sau 0 dacă nu încape în `cap` octeți — atunci nu se trimite nimic.
 * Contract buffer: cap >= 1 + lungimea textului; ~40 de octeți acoperă orice valori int32. */
size_t proto_format_level(char *out, size_t cap, int32_t raw, int pct, int stable);

/* Formatează "K,<gol>,<plin>\n" (STM32 -> ESP32: calibrare setată din meniul LCD, ca aplicația
 * să o afle). Același contract de buffer și valoare întoarsă ca proto_format_level(). */
size_t proto_format_cal(char *out, size_t cap, int32_t empty, int32_t full);

/* Asamblare linii dintr-un flux de octeți (pentru UART).
 * buf: caracterele liniei curente; len: câte sunt în buf; overflow: 1 = linia curentă a depășit
 * bufferul și se aruncă până la următorul '\n'. Folosit doar din bucla principală (link_poll). */
typedef struct {
    char buf[PROTO_LINE_MAX];
    size_t len;
    int overflow;
} line_asm_t;

/* Golește asamblorul (fără linie în curs). Apelat din link_init(). */
void line_asm_init(line_asm_t *la);
/* Adaugă un octet; întoarce 1 când o linie completă e în la->buf (terminată cu '\0').
 * Liniile prea lungi (>= PROTO_LINE_MAX caractere) sunt aruncate întregi, nu trunchiate —
 * altfel o comandă tăiată ar putea părea validă. Liniile goale nu sunt raportate.
 * la->buf rămâne valid până la următorul apel line_asm_push(). */
int  line_asm_push(line_asm_t *la, char c);

#endif /* PROTOCOL_H */
