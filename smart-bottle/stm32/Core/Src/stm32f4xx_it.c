/* Întreruperi: SysTick pentru HAL_Delay/HAL_GetTick și UART-urile. Erorile grave aprind LD4. */
#include "main.h"

void NMI_Handler(void)        { while (1) { } }
void HardFault_Handler(void)  { Error_Handler(); }
void MemManage_Handler(void)  { Error_Handler(); }
void BusFault_Handler(void)   { Error_Handler(); }
void UsageFault_Handler(void) { Error_Handler(); }
void SVC_Handler(void)        { }
void DebugMon_Handler(void)   { }
void PendSV_Handler(void)     { }

void SysTick_Handler(void)
{
    HAL_IncTick();
}

void UART5_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart5);
}

void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);
}
