#ifndef PTP_SHELL_H
#define PTP_SHELL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "Ifx_Types.h"

boolean PtpShell_Init(void);
void    PtpShell_Process(void);

#ifdef __cplusplus
}
#endif

#endif
