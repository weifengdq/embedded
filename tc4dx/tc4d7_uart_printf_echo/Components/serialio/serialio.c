#include "serialio.h"

#include "IfxAsclin_Asc.h"
#include "IfxAsclin_PinMap.h"
#include "IfxAsclin_bf.h"

static IfxAsclin_Asc g_serialioAsc;
static Ifx_ASCLIN *const g_serialioModule = &MODULE_ASCLIN0;
static const IfxAsclin_Tx_Out *const g_serialioTxPin = &IfxAsclin0_TX_F_P14_0_OUT;
static const IfxAsclin_Rx_In *const g_serialioRxPin = &IfxAsclin0_RXA_F_P14_1_IN;

void SERIALIO_Init(sint32 baudrate)
{
    IfxAsclin_Asc_Config ascConfig;
    IfxAsclin_Asc_initModuleConfig(&ascConfig, g_serialioModule);

    ascConfig.baudrate.baudrate = (float32)baudrate;

    {
        const IfxAsclin_Asc_Pins pins = {
            NULL_PTR, IfxPort_InputMode_pullUp,
            g_serialioRxPin, IfxPort_InputMode_pullUp,
            NULL_PTR, IfxPort_OutputMode_pushPull,
            g_serialioTxPin, IfxPort_OutputMode_pushPull,
            IfxPort_PadDriver_cmosAutomotiveSpeed1
        };

        ascConfig.pins = &pins;
        IfxAsclin_Asc_initModule(&g_serialioAsc, &ascConfig);
    }

    g_serialioModule->FLAGSSET.B.TFLS = 1;
}

boolean SERIALIO_TryReadByte(uint8 *byte)
{
    if ((byte == NULL_PTR) || (IfxAsclin_getRxFifoFillLevel(g_serialioModule) == 0U))
    {
        return FALSE;
    }

    *byte = (uint8)IfxAsclin_readRxData(g_serialioModule);
    return TRUE;
}

#ifdef __TASKING__
void _io_putc(char character)
{
    while (IfxAsclin_getTxFifoFillLevelFlagStatus(g_serialioModule) != TRUE)
    {
    }

    IfxAsclin_clearTxFifoFillLevelFlag(g_serialioModule);
    IfxAsclin_writeTxData(g_serialioModule, character);
}
#endif

#if defined(__HIGHTEC__) || defined(__GNUC__)
int write(int desc, void *buf, size_t len)
{
    uint8 *current;
    size_t remaining;

    (void)desc;

    current = (uint8 *)buf;
    remaining = len;

    while (IfxAsclin_getTxFifoFillLevel(g_serialioModule) > 0U)
    {
    }

    while (remaining > 16U)
    {
        IfxAsclin_write8(g_serialioModule, current, 16U);
        current += 16;
        remaining -= 16U;

        while (IfxAsclin_getTxFifoFillLevel(g_serialioModule) > 0U)
        {
        }
    }

    IfxAsclin_write8(g_serialioModule, current, (uint32)remaining);
    return (int)len;
}
#endif