/*********************************************************************************************************************
 * \file UART_Logging.c
 * \brief TC397 ASCLIN0 (P14.0 TX / P14.1 RX, 921600-8N1) logging, ported from tc387_1 ASCLIN4 version.
 *********************************************************************************************************************/

/*********************************************************************************************************************/
/*-----------------------------------------------------Includes------------------------------------------------------*/
/*********************************************************************************************************************/
#include "UART_Logging.h"
#include "IfxAsclin_Asc.h"
#include "IfxCpu_Irq.h"
#include <stdint.h>

/*********************************************************************************************************************/
/*------------------------------------------------------Macros-------------------------------------------------------*/
/*********************************************************************************************************************/
#define SERIAL_BAUDRATE         921600                                      /* Baud rate in bit/s  — 921600 high-speed */
#define SERIAL_TX_FIFO_LEVEL    8
#define SERIAL_RX_FIFO_LEVEL    1   /* Must be 1 for shell: trigger RX ISR per byte, else short commands (<8B) stay in HW FIFO */

#define SERIAL_PIN_RX           IfxAsclin0_RXA_P14_1_IN                      /* RX pin P14.1 */
#define SERIAL_PIN_TX           IfxAsclin0_TX_P14_0_OUT                     /* TX pin P14.0 */

/* RX higher than TX; both higher than STM tick to avoid overrun at 921600 */
#define INTPRIO_ASCLIN0_TX      31                                          /* Priority of the TX ISR                 */
#define INTPRIO_ASCLIN0_RX      32                                          /* Priority of the RX ISR (higher than TX) */

#define ASC_TX_BUFFER_SIZE      1024                                        /* TX SW FIFO — enlarged for burst printf */
#define ASC_RX_BUFFER_SIZE      1024                                        /* RX SW FIFO — enlarged for 921600 */

/*********************************************************************************************************************/
/*-------------------------------------------------Global variables--------------------------------------------------*/
/*********************************************************************************************************************/
IfxAsclin_Asc g_asc;                                                        /* Declaration of the ASC handle        */
uint8 g_ascTxBuffer[ASC_TX_BUFFER_SIZE + sizeof(Ifx_Fifo) + 8];             /* Declaration of the FIFO parameters   */
uint8 g_ascRxBuffer[ASC_RX_BUFFER_SIZE + sizeof(Ifx_Fifo) + 8];

/* Forward declaration from shell_port.c */
extern void Shell_RxPush(uint8_t ch);

/*********************************************************************************************************************/
/*---------------------------------------------Function Implementations----------------------------------------------*/
/*********************************************************************************************************************/
IFX_INTERRUPT(asclin0TxISR, 0, INTPRIO_ASCLIN0_TX);                     /* Adding the Interrupt Service Routine     */
IFX_INTERRUPT(asclin0RxISR, 0, INTPRIO_ASCLIN0_RX);

void asclin0TxISR(void)
{
    IfxAsclin_Asc_isrTransmit(&g_asc);
}

void asclin0RxISR(void)
{
    IfxAsclin_Asc_isrReceive(&g_asc);
    /* Bulk drain SW FIFO into shell ring — avoids per-byte driver lock overhead */
    sint32 avail = IfxAsclin_Asc_getReadCount(&g_asc);
    if (avail > 0) {
        uint8 bulk[64];
        while (avail > 0) {
            Ifx_SizeT chunk = (avail > (sint32)sizeof(bulk)) ? sizeof(bulk) : (Ifx_SizeT)avail;
            Ifx_SizeT cnt = chunk;
            if (IfxAsclin_Asc_read(&g_asc, bulk, &cnt, TIME_NULL) == IfxAsclin_Status_noError && cnt > 0) {
                for (Ifx_SizeT i = 0; i < cnt; ++i) {
                    Shell_RxPush(bulk[i]);
                }
                avail -= (sint32)cnt;
                if (cnt != chunk) break;
            } else {
                break;
            }
        }
    }
}

/* Optimized poll: only used as fallback if an RX interrupt was missed.
 * We disable interrupts around isrReceive to avoid re-entrancy with asclin0RxISR,
 * then bulk-drain to shell ring.
 */
void UART_Poll(void)
{
    /* Fast path: if SW FIFO already has data, just drain it */
    sint32 swAvail = IfxAsclin_Asc_getReadCount(&g_asc);
    if (swAvail > 0) {
        uint8 bulk[64];
        while (swAvail > 0) {
            Ifx_SizeT chunk = (swAvail > (sint32)sizeof(bulk)) ? sizeof(bulk) : (Ifx_SizeT)swAvail;
            Ifx_SizeT cnt = chunk;
            if (IfxAsclin_Asc_read(&g_asc, bulk, &cnt, TIME_NULL) == IfxAsclin_Status_noError && cnt > 0) {
                for (Ifx_SizeT i = 0; i < cnt; ++i) Shell_RxPush(bulk[i]);
                swAvail -= (sint32)cnt;
                if (cnt != chunk) break;
            } else break;
            swAvail = IfxAsclin_Asc_getReadCount(&g_asc);
            /* only one bulk iteration per poll to keep main loop responsive */
            break;
        }
        return;
    }
    /* HW FIFO has data but SW FIFO empty => interrupt missed, recover */
    if (MODULE_ASCLIN0.FLAGS.B.RFL) {
        boolean ie = IfxCpu_disableInterrupts();
        IfxAsclin_Asc_isrReceive(&g_asc);
        IfxCpu_restoreInterrupts(ie);
        swAvail = IfxAsclin_Asc_getReadCount(&g_asc);
        if (swAvail > 0) {
            uint8 bulk[64];
            Ifx_SizeT cnt = (swAvail > (sint32)sizeof(bulk)) ? sizeof(bulk) : (Ifx_SizeT)swAvail;
            Ifx_SizeT c = cnt;
            if (IfxAsclin_Asc_read(&g_asc, bulk, &c, TIME_NULL) == IfxAsclin_Status_noError && c > 0) {
                for (Ifx_SizeT i = 0; i < c; ++i) Shell_RxPush(bulk[i]);
            }
        }
    }
}

void initUART(void)
{
    IfxAsclin_Asc_Config ascConfig;
    IfxAsclin_Asc_initModuleConfig(&ascConfig, &MODULE_ASCLIN0);
    ascConfig.baudrate.baudrate = SERIAL_BAUDRATE;
    /* High-speed tuning for 921600: oversampling 16, median filter 3, sample point 12 */
    ascConfig.baudrate.oversampling = IfxAsclin_OversamplingFactor_16;
    ascConfig.bitTiming.medianFilter = IfxAsclin_SamplesPerBit_three;
    ascConfig.bitTiming.samplePointPosition = IfxAsclin_SamplePointPosition_12;
    ascConfig.baudrate.prescaler = 1;
    ascConfig.interrupt.txPriority = INTPRIO_ASCLIN0_TX;
    ascConfig.interrupt.rxPriority = INTPRIO_ASCLIN0_RX;
    ascConfig.interrupt.typeOfService = IfxCpu_Irq_getTos(IfxCpu_getCoreIndex());
    ascConfig.interrupt.erPriority = 0; /* no error ISR needed */
    ascConfig.txBuffer = &g_ascTxBuffer;
    ascConfig.txBufferSize = ASC_TX_BUFFER_SIZE;
    ascConfig.rxBuffer = &g_ascRxBuffer;
    ascConfig.rxBufferSize = ASC_RX_BUFFER_SIZE;
    /* FIFO interrupt levels: fire earlier (lower) for TX, moderate for RX to reduce ISR count */
    ascConfig.fifo.txFifoInterruptLevel = SERIAL_TX_FIFO_LEVEL;
    ascConfig.fifo.rxFifoInterruptLevel = SERIAL_RX_FIFO_LEVEL;
    const IfxAsclin_Asc_Pins pins =
    {
        NULL_PTR,         IfxPort_InputMode_pullUp,
        &SERIAL_PIN_RX,   IfxPort_InputMode_pullUp,
        NULL_PTR,         IfxPort_OutputMode_pushPull,
        &SERIAL_PIN_TX,   IfxPort_OutputMode_pushPull,
        IfxPort_PadDriver_cmosAutomotiveSpeed4
    };
    ascConfig.pins = &pins;
    IfxAsclin_Asc_initModule(&g_asc, &ascConfig);
    /* Ensure FIFOs enabled */
    MODULE_ASCLIN0.RXFIFOCON.B.ENI = 1;
    MODULE_ASCLIN0.TXFIFOCON.B.ENO = 1;
}

void sendUARTMessage(char * msg, Ifx_SizeT count)
{
    IfxAsclin_Asc_write(&g_asc, msg, &count, TIME_INFINITE);            /* Transfer of data                         */
}

/* Provide _write for printf retarget */
int _write(int file, char *ptr, int len)
{
    (void)file;
    if (ptr == NULL || len <= 0) return 0;
    Ifx_SizeT cnt = (Ifx_SizeT)len;
    IfxAsclin_Asc_write(&g_asc, (uint8_t*)ptr, &cnt, TIME_INFINITE);
    return (int)cnt;
}

int _read(int file, char *ptr, int len)
{
    (void)file;
    int i = 0;
    for (i = 0; i < len; ++i) {
        uint8 ch;
        Ifx_SizeT cnt = 1;
        /* Non-blocking try: if FIFO has data, read one char */
        if (IfxAsclin_Asc_getReadCount(&g_asc) == 0) break;
        if (IfxAsclin_Asc_read(&g_asc, &ch, &cnt, TIME_NULL) != IfxAsclin_Status_noError || cnt == 0) break;
        ptr[i] = (char)ch;
    }
    return i;
}
