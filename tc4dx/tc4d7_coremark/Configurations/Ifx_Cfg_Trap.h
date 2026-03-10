#ifndef IFX_CFG_TRAP_H
#define IFX_CFG_TRAP_H

#include "Ifx_Cfg.h"

/* Write trap info directly to core-local DSPR — no function call, no LMU access.
   This avoids recursive bus-errors that occur when the hook itself accesses LMU. */
#define COREMARK_TRAP_HOOK(trapWatch) do {                      \
    DIAG_DSPR_TRAP_CLASS = (unsigned int)(trapWatch).tClass;    \
    DIAG_DSPR_TRAP_ID    = (unsigned int)(trapWatch).tId;       \
    DIAG_DSPR_TRAP_ADDR  = (unsigned int)(trapWatch).tAddr;     \
    DIAG_DSPR_TRAP_DEADD = (unsigned int)__mfcr(0x901C);       \
    DIAG_DSPR_TRAP_DATR  = (unsigned int)__mfcr(0x9018);       \
    DIAG_DSPR_TRAP_COUNT++;                                     \
} while(0)

#define IFX_CFG_CPU_TRAP_MME_HOOK(trapWatch) COREMARK_TRAP_HOOK(trapWatch)
#define IFX_CFG_CPU_TRAP_IPE_HOOK(trapWatch) COREMARK_TRAP_HOOK(trapWatch)
#define IFX_CFG_CPU_TRAP_IE_HOOK(trapWatch) COREMARK_TRAP_HOOK(trapWatch)
#define IFX_CFG_CPU_TRAP_CME_HOOK(trapWatch) COREMARK_TRAP_HOOK(trapWatch)
#define IFX_CFG_CPU_TRAP_BE_HOOK(trapWatch) COREMARK_TRAP_HOOK(trapWatch)
#define IFX_CFG_CPU_TRAP_ASSERT_HOOK(trapWatch) COREMARK_TRAP_HOOK(trapWatch)
#define IFX_CFG_CPU_TRAP_NMI_HOOK(trapWatch) COREMARK_TRAP_HOOK(trapWatch)
#define IFX_CFG_CPU_TRAP_SYSCALL_CPU0_HOOK(trapWatch) COREMARK_TRAP_HOOK(trapWatch)
#define IFX_CFG_CPU_TRAP_SYSCALL_CPU1_HOOK(trapWatch) COREMARK_TRAP_HOOK(trapWatch)
#define IFX_CFG_CPU_TRAP_SYSCALL_CPU2_HOOK(trapWatch) COREMARK_TRAP_HOOK(trapWatch)
#define IFX_CFG_CPU_TRAP_SYSCALL_CPU3_HOOK(trapWatch) COREMARK_TRAP_HOOK(trapWatch)
#define IFX_CFG_CPU_TRAP_SYSCALL_CPU4_HOOK(trapWatch) COREMARK_TRAP_HOOK(trapWatch)
#define IFX_CFG_CPU_TRAP_SYSCALL_CPU5_HOOK(trapWatch) COREMARK_TRAP_HOOK(trapWatch)
#define IFX_CFG_CPU_TRAP_DEBUG ((void)0)

#endif
