/* Debug: dump EVADC group queue + result register states (for AN3 NO-DATA analysis) */
#include "shell.h"
#include "Evadc/Std/IfxEvadc.h"
#include <stdio.h>

static void AdcDumpGroup(Shell *shell, Ifx_EVADC_G *g, const char *name)
{
    int i;
    shellPrint(shell, "%s QSR=0x%08lX Q0R=0x%08lX QMR0=0x%08lX CHCTR3=0x%08lX VFR=0x%08lX\r\n", name,
               (unsigned long)g->Q[0].QSR.U, (unsigned long)g->Q[0].Q0R.U,
               (unsigned long)g->Q[0].QMR.U,
               (unsigned long)g->CHCTR[3].U, (unsigned long)g->VFR.U);
    for (i = 0; i < 8; ++i)
    {
        shellPrint(shell, "  RES%-2d VF=%u RESULT=%4u (0x%08lX)\r\n", i,
                   (unsigned)g->RES[i].B.VF, (unsigned)g->RES[i].B.RESULT,
                   (unsigned long)g->RES[i].U);
    }
}

static int cmd_adcdbg(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    (void)argc; (void)argv;
    AdcDumpGroup(shell, &MODULE_EVADC.G[0], "G0");
    AdcDumpGroup(shell, &MODULE_EVADC.G[1], "G1");
    shellPrint(shell, "G2 QSR=0x%08lX G3 QSR=0x%08lX G8 QSR=0x%08lX\r\n",
               (unsigned long)MODULE_EVADC.G[2].Q[0].QSR.U,
               (unsigned long)MODULE_EVADC.G[3].Q[0].QSR.U,
               (unsigned long)MODULE_EVADC.G[8].Q[0].QSR.U);
    return 0;
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                 adcdbg, cmd_adcdbg, EVADC queue/result debug);
