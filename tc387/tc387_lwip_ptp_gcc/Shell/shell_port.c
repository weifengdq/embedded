/**
 * @file    shell_port.c
 * @brief   Letter-shell on TC387 using the standard ASC RX FIFO path
 *
 * The ASCLIN4 driver is initialized by UART_Logging.c. RX interrupts only feed
 * the iLLD/software FIFO. The shell polls that FIFO in the main loop and passes
 * characters to letter-shell one by one.
 */

#include "shell_port.h"
#include "shell.h"
#include "UART_Logging.h"
#include "IfxAsclin_Asc.h"

#include <string.h>

extern IfxAsclin_Asc g_asc;

static Shell gShell;
static char  gShellBuf[512];

static short userShellWrite(char *data, unsigned short len)
{
    Ifx_SizeT count = (Ifx_SizeT)len;

    if ((data == NULL_PTR) || (len == 0U))
    {
        return 0;
    }

    IfxAsclin_Asc_write(&g_asc, data, &count, TIME_INFINITE);
    return (short)count;
}

static short userShellRead(char *data, unsigned short len)
{
    sint32 available;
    Ifx_SizeT count;

    if ((data == NULL_PTR) || (len == 0U))
    {
        return 0;
    }

    available = IfxAsclin_Asc_getReadCount(&g_asc);

    if (available <= 0)
    {
        return 0;
    }

    count = (Ifx_SizeT)(((sint32)len < available) ? (sint32)len : available);
    (void)IfxAsclin_Asc_read(&g_asc, data, &count, 0);
    return (short)count;
}

int __io_putchar(int ch)
{
    uint8_t byte = (uint8_t)ch;
    Ifx_SizeT count = 1U;

    (void)IfxAsclin_Asc_write(&g_asc, &byte, &count, TIME_INFINITE);
    return ch;
}

int Shell_Init(void)
{
    gShell.write = userShellWrite;
    gShell.read  = userShellRead;
    shellInit(&gShell, gShellBuf, sizeof(gShellBuf));
    return 0;
}

void Shell_Process(void)
{
    sint32 available;
    uint8_t ch;
    Ifx_SizeT count;

    do
    {
        available = IfxAsclin_Asc_getReadCount(&g_asc);

        if (available <= 0)
        {
            break;
        }

        count = 1U;
        (void)IfxAsclin_Asc_read(&g_asc, &ch, &count, 0);

        if (count == 1U)
        {
            shellHandler(&gShell, ch);
        }
    } while (available > 0);
}

void Shell_PrintBanner(void)
{
    const char *banner =
        "\r\n============================================\r\n"
        "  TC387  PTP/gPTP Shell  (letter 3.2.4)\r\n"
        "  921600 bps 8N1  |  help for commands\r\n"
        "============================================\r\n";
    Ifx_SizeT len = (Ifx_SizeT)strlen(banner);

    (void)IfxAsclin_Asc_write(&g_asc, banner, &len, TIME_INFINITE);
}
