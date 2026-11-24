/*
 * link.c — UART5 spre ESP32: primire pe întreruperi, mesaje text pe linii.
 *
 * Primirea: HAL anunță fiecare octet în HAL_UART_RxCpltCallback (din întrerupere). Octetul intră
 * într-un buffer circular; bucla principală îl scoate în link_poll() și compune liniile. Așa
 * întreruperea rămâne foarte scurtă și nu se pierd octeți cât timp bucla desenează pe LCD.
 */
#include "link.h"
#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RX_SIZE   128               /* buffer circular; putere a lui 2 */
#define LINE_MAX  32                /* cea mai lungă linie acceptată */

static volatile uint8_t  rx_buf[RX_SIZE];
static volatile uint16_t rx_head;   /* scris doar de întrerupere */
static volatile uint16_t rx_tail;   /* scris doar de bucla principală */
static uint8_t rx_byte;             /* aici pune HAL octetul primit */

static char line[LINE_MAX];
static int  line_len;

void link_init(void)
{
    HAL_UART_Receive_IT(&huart5, &rx_byte, 1);       /* așteaptă primul octet */
}

/* Chemat de HAL din întrerupere, după fiecare octet. */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != UART5) return;
    uint16_t next = (rx_head + 1) & (RX_SIZE - 1);
    if (next != rx_tail) {                           /* buffer plin = octetul se pierde */
        rx_buf[rx_head] = rx_byte;
        rx_head = next;
    }
    HAL_UART_Receive_IT(&huart5, &rx_byte, 1);       /* așteaptă octetul următor */
}

/* Eroare de recepție (ex. octet pierdut): HAL oprește recepția, o pornim din nou. */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != UART5) return;
    __HAL_UART_CLEAR_OREFLAG(huart);
    HAL_UART_Receive_IT(&huart5, &rx_byte, 1);
}

/* Desface „X,1,2,3” în litera X și numere. */
static void parse(const char *text, link_msg_t *msg)
{
    msg->type = text[0];
    msg->n = 0;
    const char *p = text + 1;
    while (*p == ',' && msg->n < 3) {
        msg->v[msg->n++] = (int32_t)strtol(p + 1, (char **)&p, 10);
    }
}

int link_poll(link_msg_t *msg)
{
    while (rx_tail != rx_head) {
        char c = (char)rx_buf[rx_tail];
        rx_tail = (rx_tail + 1) & (RX_SIZE - 1);

        if (c == '\r') continue;
        if (c != '\n') {
            if (line_len < LINE_MAX - 1) line[line_len++] = c;   /* prea lung: restul se ignoră */
            continue;
        }
        if (line_len == 0) continue;                 /* linie goală */
        line[line_len] = '\0';
        line_len = 0;
        dbg_log("[rx] %s\r\n", line);
        parse(line, msg);
        return 1;
    }
    return 0;
}

void link_send(const char *text)
{
    HAL_UART_Transmit(&huart5, (const uint8_t *)text, (uint16_t)strlen(text), 50);
    dbg_log("[tx] %s", text);
}

void link_send_level(int32_t raw, int pct, int stable)
{
    char buf[32];
    snprintf(buf, sizeof buf, "L,%ld,%d,%d\n", (long)raw, pct, stable ? 1 : 0);
    link_send(buf);
}
