/* Întreruperi: SysTick pentru HAL_Delay/HAL_GetTick și UART-urile. Erorile grave aprind LD4.
 *
 * Numele funcțiilor sunt fixe: sunt simbolurile din tabela de vectori din startup_stm32f429xx.s
 * (acolo definite „weak” ca bucle infinite). O greșeală de nume nu dă eroare de compilare, doar
 * face ca întreruperea să cadă în handler-ul implicit, deci atenție la scriere.
 * Lanțul pentru recepție: octet pe RX -> UARTx_IRQHandler -> HAL_UART_IRQHandler -> la octetul
 * complet HAL cheamă HAL_UART_RxCpltCallback (la erori: HAL_UART_ErrorCallback), ambele în link.c,
 * care pun octetul în buffer și repornesc recepția. Totul rulează în context de întrerupere.
 */
#include "main.h"

/* Excepțiile nucleului Cortex-M4. NMI (ex. eroare HSE cu CSS activ — nu-l activăm) blochează
 * procesorul; erorile de memorie/bus/instrucțiune ajung în Error_Handler (LD4 clipește) ca să se
 * vadă pe placă. SVC/PendSV/DebugMon sunt folosite doar de un RTOS sau de debugger — aici nimic. */
void NMI_Handler(void)        { while (1) { } }
void HardFault_Handler(void)  { Error_Handler(); }
void MemManage_Handler(void)  { Error_Handler(); }
void BusFault_Handler(void)   { Error_Handler(); }
void UsageFault_Handler(void) { Error_Handler(); }
void SVC_Handler(void)        { }
void DebugMon_Handler(void)   { }
void PendSV_Handler(void)     { }

/* La fiecare 1 ms (configurat de HAL_Init/HAL_RCC_ClockConfig): incrementează uwTick, contorul
 * citit de HAL_GetTick() și HAL_Delay(). Toată programarea din app_loop() se bazează pe el. */
void SysTick_Handler(void)
{
    HAL_IncTick();
}

/* UART5 (ESP32): delegă la HAL, care duce la callback-urile din link.c (doar dacă LINK_VCP=0). */
void UART5_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart5);
}

/* USART1 (VCP): la fel; generează recepție doar cu LINK_VCP=1, altfel e folosit numai la transmisie
 * blocantă de dbg_log(), care nu folosește întreruperi. */
void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);
}
