#include "serialio.h"

#include "IfxAsclin_Asc.h"
#include "IfxAsclin_PinMap.h"

static Ifx_ASCLIN *const g_serialioModule = &MODULE_ASCLIN0;
static const IfxAsclin_Tx_Out *const g_serialioTxPin = &IfxAsclin0_TX_F_P14_0_OUT;
static const IfxAsclin_Rx_In *const g_serialioRxPin = &IfxAsclin0_RXA_F_P14_1_IN;

static void serialioClearRxErrors(void)
{
    boolean flushRx = FALSE;

    if (IfxAsclin_getParityErrorFlagStatus(g_serialioModule))
    {
        IfxAsclin_clearParityErrorFlag(g_serialioModule);
        flushRx = TRUE;
    }

    if (IfxAsclin_getFrameErrorFlagStatus(g_serialioModule))
    {
        IfxAsclin_clearFrameErrorFlag(g_serialioModule);
        flushRx = TRUE;
    }

    if (IfxAsclin_getRxFifoOverflowFlagStatus(g_serialioModule))
    {
        IfxAsclin_clearRxFifoOverflowFlag(g_serialioModule);
        flushRx = TRUE;
    }

    if (IfxAsclin_getRxFifoUnderflowFlagStatus(g_serialioModule))
    {
        IfxAsclin_clearRxFifoUnderflowFlag(g_serialioModule);
        flushRx = TRUE;
    }

    if (flushRx)
    {
        IfxAsclin_flushRxFifo(g_serialioModule);
    }
}

void SERIALIO_Init(sint32 baudrate)
{
    IfxAsclin_Asc ascHandle;
    IfxAsclin_Asc_Config ascConfig;

    IfxAsclin_Asc_initModuleConfig(&ascConfig, g_serialioModule);

    ascConfig.baudrate.baudrate = (float32)baudrate;
    ascConfig.baudrate.oversampling = IfxAsclin_OversamplingFactor_16;
    ascConfig.bitTiming.medianFilter = IfxAsclin_SamplesPerBit_three;
    ascConfig.bitTiming.samplePointPosition = IfxAsclin_SamplePointPosition_8;

    {
        const IfxAsclin_Asc_Pins pins = {
            NULL_PTR, IfxPort_InputMode_pullUp,
            g_serialioRxPin, IfxPort_InputMode_pullUp,
            NULL_PTR, IfxPort_OutputMode_pushPull,
            g_serialioTxPin, IfxPort_OutputMode_pushPull,
            IfxPort_PadDriver_cmosAutomotiveSpeed1
        };

        ascConfig.pins = &pins;
        IfxAsclin_Asc_initModule(&ascHandle, &ascConfig);
    }

    IfxAsclin_flushRxFifo(g_serialioModule);
    IfxAsclin_flushTxFifo(g_serialioModule);
    serialioClearRxErrors();
    g_serialioModule->FLAGSSET.B.TFLS = 1;
}

boolean SERIALIO_TryReadByte(uint8 *byte)
{
    if (byte == NULL_PTR)
    {
        return FALSE;
    }

    serialioClearRxErrors();

    if (IfxAsclin_getRxFifoFillLevel(g_serialioModule) == 0U)
    {
        return FALSE;
    }

    (void)IfxAsclin_read8(g_serialioModule, byte, 1U);
    return TRUE;
}

boolean SERIALIO_WriteBuffer(const uint8 *data, Ifx_SizeT count)
{
    uint8 *current;

    if ((data == NULL_PTR) || (count == 0U))
    {
        return TRUE;
    }

    current = (uint8 *)data;

    while (count > 0U)
    {
        uint32 chunk = (count > 16U) ? 16U : (uint32)count;

        while (IfxAsclin_getTxFifoFillLevel(g_serialioModule) > 0U)
        {
            __nop();
        }

        (void)IfxAsclin_write8(g_serialioModule, current, chunk);
        current += chunk;
        count -= chunk;
    }

    return TRUE;
}

#ifdef __TASKING__
void _io_putc(char character)
{
    (void)SERIALIO_WriteBuffer((const uint8 *)&character, 1U);
}
#endif

#if defined(__HIGHTEC__) || defined(__GNUC__)
int write(int desc, void *buf, size_t len)
{
    (void)desc;
    (void)SERIALIO_WriteBuffer((const uint8 *)buf, (Ifx_SizeT)len);
    return (int)len;
}
#endif