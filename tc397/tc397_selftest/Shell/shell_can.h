#ifndef SHELL_CAN_H
#define SHELL_CAN_H

#include "Ifx_Types.h"

/* Non-zero when `canlive on` was issued. */
uint8 shell_can_live_enabled(void);
/* Pop and print all pending RX rings (called from main loop). */
void shell_can_live_dump(void);

#endif /* SHELL_CAN_H */
