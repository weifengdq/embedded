/**
 * @file  stm32h7xx_it.c
 * @brief 中断入口 (最小实现, 不依赖 CubeMX 生成的 it.c)
 */
#include "board.h"

/* SysTick: HAL 1ms 时基 */
void SysTick_Handler(void)
{
    HAL_IncTick();
}

/* 调试串口接收 */
void USART1_IRQHandler(void)
{
    board_uart_rx_isr();
}

/* S2500 IRQ_N (PD5 -> EXTI5; H7 的 EXTI5..9 共用 EXTI9_5 向量) */
void EXTI9_5_IRQHandler(void)
{
    if ((EXTI->PR1 & EXTI_PR1_PR5) != 0u) {
        EXTI->PR1 = EXTI_PR1_PR5;       /* 写 1 清标志 */
        board_irq_count++;
    }
}

/* 故障处理: 停在原地, 便于调试器查看 */
void HardFault_Handler(void)
{
    __disable_irq();
    for (;;) { }
}

void MemManage_Handler(void)  __attribute__((alias("HardFault_Handler")));
void BusFault_Handler(void)   __attribute__((alias("HardFault_Handler")));
void UsageFault_Handler(void) __attribute__((alias("HardFault_Handler")));
void NMI_Handler(void)        __attribute__((alias("HardFault_Handler")));
