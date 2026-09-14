/**********************************************************************************************************************
 * \file ConfigurationIsr.h
 * \copyright Copyright (C) Infineon Technologies AG 2019
 *********************************************************************************************************************/

#ifndef CONFIGURATIONISR_H
#define CONFIGURATIONISR_H

/*********************************************************************************************************************/
/*------------------------------------------------------Macros-------------------------------------------------------*/
/*********************************************************************************************************************/
#define ISR_PRIORITY_OS_TICK        10                          /* STM 1ms tick — lowered so UART can preempt it   */
#define ISR_PRIORITY_ASCLIN0_TX     31                          /* ASCLIN0 TX — high-speed, 921600 needs low latency */
#define ISR_PRIORITY_ASCLIN0_RX     32                          /* ASCLIN0 RX — highest among UART, above TX & tick   */

#endif
