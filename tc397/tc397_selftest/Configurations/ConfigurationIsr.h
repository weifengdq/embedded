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
#define ISR_PRIORITY_GETH_TX        100                         /* Define the Ethernet transmit interrupt priority  */
#define ISR_PRIORITY_GETH_RX        101                         /* Define the Ethernet receive interrupt priority   */
#define ISR_PRIORITY_QSPI2_TX       50                          /* QSPI2 TLF35584 TX (Infineon SPI_TLF example)      */
#define ISR_PRIORITY_QSPI2_RX       51                          /* QSPI2 TLF35584 RX                                 */
#define ISR_PRIORITY_QSPI2_ER       52                          /* QSPI2 TLF35584 error                              */
#define ISR_PRIORITY_QSPI4_TX       60                          /* QSPI4 TX (LAN8651 TC6 over SPI)                   */
#define ISR_PRIORITY_QSPI4_RX       61                          /* QSPI4 RX                                          */
#define ISR_PRIORITY_QSPI4_ER       62                          /* QSPI4 ER                                          */

#endif
