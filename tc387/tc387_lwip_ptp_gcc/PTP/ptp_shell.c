#include "ptp_shell.h"

#include "flexptp_app.h"
#include "UART_Logging.h"
#include "Ifx_Console.h"
#include "Ifx_Shell.h"
#include "IfxStdIf_DPipe.h"

#include <string.h>

#define PTP_SHELL_MAX_ARGS 16

typedef struct
{
    IfxStdIf_DPipe io;
    Ifx_Shell shell;
    boolean initialized;
} PtpShell_Context;

static PtpShell_Context g_ptpShell;

static boolean PtpShell_execute(const char *command, pchar args, int (*handler)(int argc, char *argv[]))
{
    char buffer[IFX_CFG_SHELL_CMD_LINE_SIZE];
    char *argv[PTP_SHELL_MAX_ARGS];
    int argc = 0;
    char *cursor;

    argv[argc++] = (char *)command;

    if (args != NULL_PTR)
    {
        cursor = args;
        while ((*cursor != '\0') && (argc < PTP_SHELL_MAX_ARGS))
        {
            while ((*cursor == ' ') || (*cursor == '\t'))
            {
                cursor++;
            }

            if (*cursor == '\0')
            {
                break;
            }

            argv[argc++] = &buffer[(size_t)(cursor - args)];

            while ((*cursor != '\0') && (*cursor != ' ') && (*cursor != '\t'))
            {
                cursor++;
            }

            if (*cursor == '\0')
            {
                break;
            }

            cursor++;
        }
    }

    if (args != NULL_PTR)
    {
        (void)strncpy(buffer, args, sizeof(buffer) - 1U);
        buffer[sizeof(buffer) - 1U] = '\0';

        cursor = buffer;
        while (*cursor != '\0')
        {
            if ((*cursor == ' ') || (*cursor == '\t'))
            {
                *cursor = '\0';
            }
            cursor++;
        }
    }

    return (handler(argc, argv) >= 0) ? TRUE : TRUE;
}

static boolean PtpShell_onPtp(pchar args, void *data, IfxStdIf_DPipe *io)
{
    IFX_UNUSED_PARAMETER(data);
    IFX_UNUSED_PARAMETER(io);
    return PtpShell_execute("ptp", args, FlexPTP_ShellPtp);
}

static boolean PtpShell_onTime(pchar args, void *data, IfxStdIf_DPipe *io)
{
    IFX_UNUSED_PARAMETER(data);
    IFX_UNUSED_PARAMETER(io);
    return PtpShell_execute("time", args, FlexPTP_ShellTime);
}

static Ifx_Shell_Command g_ptpShellCommands[] =
{
    {"help", SHELL_HELP_DESCRIPTION_TEXT, &g_ptpShell.shell, &Ifx_Shell_showHelp},
    {"ptp",
     "     : PTP/gPTP/PPS control" ENDL
     "/s ptp [on|gptp|off|status|help|-h]",
     NULL_PTR,
     &PtpShell_onPtp},
    {"time",
     "    : show PTP time" ENDL
     "/s time [ns|utc|local|+H|-H|offset N|-h]",
     NULL_PTR,
     &PtpShell_onTime},
    IFX_SHELL_COMMAND_LIST_END
};

boolean PtpShell_Init(void)
{
    Ifx_Shell_Config shellConfig;

    if (g_ptpShell.initialized)
    {
        return TRUE;
    }

    if (!initUARTConsole(&g_ptpShell.io))
    {
        return FALSE;
    }

    Ifx_Console_init(&g_ptpShell.io);
    Ifx_Shell_initConfig(&shellConfig);
    shellConfig.standardIo = &g_ptpShell.io;
    shellConfig.echo = TRUE;
    shellConfig.showPrompt = TRUE;
    shellConfig.sendResultCode = FALSE;
    shellConfig.commandList[0] = g_ptpShellCommands;

    g_ptpShellCommands[0].data = &g_ptpShell.shell;

    if (!Ifx_Shell_init(&g_ptpShell.shell, &shellConfig))
    {
        return FALSE;
    }

    g_ptpShell.initialized = TRUE;
    return TRUE;
}

void PtpShell_Process(void)
{
    if (g_ptpShell.initialized)
    {
        Ifx_Shell_process(&g_ptpShell.shell);
    }
}
