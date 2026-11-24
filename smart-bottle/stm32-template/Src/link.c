/*
 * link.c — driverul legăturii UART (vezi link.h).
 *
 * Flux recepție: HAL_UART_Receive_IT(1 octet) -> întreruperea UART -> HAL_UART_RxCpltCallback()
 * -> link_on_rx_byte(): octetul intră în rx_ring și se cere imediat următorul octet.
 * Bucla principală (link_poll) scoate octeții, îi asamblează în linii și le interpretează.
 *
 * De ce 1 octet pe întrerupere: mesajele au lungime variabilă și sunt rare (câteva pe secundă),
 * deci costul unei întreruperi pe octet e neglijabil, iar codul rămâne simplu (fără DMA/IDLE).
 * Ring buffer-ul decuplează ISR-ul (rapid, fără parsare) de bucla principală (care poate
 * întârzia zeci de ms cu desenarea pe LCD fără să piardă octeți, până la 127 în așteptare).
 *
 * Acest fișier definește și callback-urile HAL_UART_RxCpltCallback/HAL_UART_ErrorCallback
 * (suprascriu variantele __weak din HAL) pentru tot firmware-ul.
 */
#include "link.h"
#include "main.h"
#include <string.h>

/* LINK_HUART / LINK_INSTANCE vin din main.h: UART5 (ESP32) sau USART1 (USB, cu make LINK=vcp). */

/* Dimensiunea ring buffer-ului. Trebuie să fie putere a lui 2: indicii se înfășoară cu
 * `& (RX_RING_SIZE - 1)` în loc de `%`. Un loc rămâne mereu liber (head == tail înseamnă gol),
 * deci încap max 127 de octeți necitiți; când e plin, octetul nou se aruncă. */
#define RX_RING_SIZE 128   /* putere a lui 2 */

/* Ring buffer fără lacăt, sigur fiindcă fiecare index are un singur scriitor:
 * head e scris doar în ISR, tail doar în bucla principală; accesele uint16_t sunt atomice pe
 * Cortex-M4. volatile: valorile se schimbă din alt context, compilatorul trebuie să le recitească. */
static volatile uint8_t  rx_ring[RX_RING_SIZE];
static volatile uint16_t rx_head;   /* scris doar în ISR */
static volatile uint16_t rx_tail;   /* scris doar în bucla principală */
static uint8_t rx_byte;   /* destinația recepției HAL de 1 octet; citit în ISR, la RxCplt */
static line_asm_t la;     /* linia în curs de asamblare; folosit doar în bucla principală */

/* Cere HAL-ului următorul octet pe întrerupere. Rezultatul (HAL_BUSY etc.) nu e verificat:
 * se apelează doar când recepția precedentă s-a terminat sau a fost oprită de o eroare. */
static void start_rx(void)
{
    HAL_UART_Receive_IT(&LINK_HUART, &rx_byte, 1);
}

/* Vezi link.h. Indicii se pun pe 0 înainte de prima recepție, deci fără concurență cu ISR-ul. */
void link_init(void)
{
    rx_head = rx_tail = 0;
    line_asm_init(&la);
    start_rx();
}

/* Context de întrerupere (din HAL_UART_RxCpltCallback). Scrie doar head; tail e doar citit.
 * Octetul e scris în ring ÎNAINTE de a publica noul head, ca bucla principală să nu citească
 * un loc încă nescris. */
void link_on_rx_byte(void)
{
    uint16_t next = (rx_head + 1) & (RX_RING_SIZE - 1);
    if (next != rx_tail) {          /* buffer plin => octetul se pierde */
        rx_ring[rx_head] = rx_byte;
        rx_head = next;
    }
    start_rx();
}

/* Vezi link.h. Scrie doar tail. Fiecare linie completă e afișată în jurnalul de depanare
 * (dbg_log; oprit în modul LINK=vcp) înainte de interpretare. */
proto_cmd_t link_poll(void)
{
    proto_cmd_t none = { CMD_NONE, 0, 0, 0 };
    while (rx_tail != rx_head) {
        char c = (char)rx_ring[rx_tail];
        rx_tail = (rx_tail + 1) & (RX_RING_SIZE - 1);
        if (line_asm_push(&la, c)) {
            dbg_log("[rx] %s\r\n", la.buf);
            proto_cmd_t cmd = proto_parse(la.buf);
            if (cmd.type != CMD_NONE) return cmd;   /* restul rămâne pentru apelul următor */
        }
    }
    return none;
}

/* Vezi link.h. HAL_UART_Transmit e polling: blochează bucla principală până pleacă ultimul octet
 * sau expiră timeout-ul de 50 ms (destul pentru ~570 de octeți la 115200 baud; mesajele au < 32).
 * Recepția pe întreruperi continuă în paralel, UART-ul fiind full-duplex. */
void link_send(const char *s, size_t len)
{
    HAL_UART_Transmit(&LINK_HUART, (const uint8_t *)s, (uint16_t)len, 50);
    dbg_log("[tx] %.*s", (int)len, s);
}

/* Vezi link.h. */
void link_send_str(const char *s)
{
    link_send(s, strlen(s));
}

/* ---- Callback-uri HAL (doar UART-ul legăturii primește pe întreruperi) ----
 * HAL apelează aceleași funcții pentru orice UART; LINK_INSTANCE filtrează UART-ul legăturii
 * (celălalt, USART1 pentru jurnal, transmite doar în polling). */

/* Context de întrerupere: s-a primit octetul cerut cu HAL_UART_Receive_IT. */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == LINK_INSTANCE) link_on_rx_byte();
}

/* Context de întrerupere: eroare de recepție (overrun = octet nou sosit înainte să fie citit
 * cel vechi, zgomot, framing). */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    /* overrun / zgomot: HAL oprește recepția, o repornim; fără repornire legătura ar rămâne
     * mută definitiv. Octetul pierdut strică cel mult o linie, pe care parserul o respinge. */
    if (huart->Instance == LINK_INSTANCE) {
        __HAL_UART_CLEAR_OREFLAG(huart);   /* șterge ORE (citire SR apoi DR), altfel eroarea revine */
        start_rx();
    }
}
