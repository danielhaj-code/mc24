#include "link.h"
#include "main.h"
#include <string.h>

/* LINK_HUART / LINK_INSTANCE vin din main.h: UART5 (ESP32) sau USART1 (USB, cu make LINK=vcp). */

#define RX_RING_SIZE 128   /* putere a lui 2 */

static volatile uint8_t  rx_ring[RX_RING_SIZE];
static volatile uint16_t rx_head;   /* scris doar în ISR */
static volatile uint16_t rx_tail;   /* scris doar în bucla principală */
static uint8_t rx_byte;
static line_asm_t la;

static void start_rx(void)
{
    HAL_UART_Receive_IT(&LINK_HUART, &rx_byte, 1);
}

void link_init(void)
{
    rx_head = rx_tail = 0;
    line_asm_init(&la);
    start_rx();
}

void link_on_rx_byte(void)
{
    uint16_t next = (rx_head + 1) & (RX_RING_SIZE - 1);
    if (next != rx_tail) {          /* buffer plin => octetul se pierde */
        rx_ring[rx_head] = rx_byte;
        rx_head = next;
    }
    start_rx();
}

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

void link_send(const char *s, size_t len)
{
    HAL_UART_Transmit(&LINK_HUART, (const uint8_t *)s, (uint16_t)len, 50);
    dbg_log("[tx] %.*s", (int)len, s);
}

void link_send_str(const char *s)
{
    link_send(s, strlen(s));
}

/* ---- Callback-uri HAL (doar UART-ul legăturii primește pe întreruperi) ---- */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == LINK_INSTANCE) link_on_rx_byte();
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    /* overrun / zgomot: HAL oprește recepția, o repornim */
    if (huart->Instance == LINK_INSTANCE) {
        __HAL_UART_CLEAR_OREFLAG(huart);
        start_rx();
    }
}
