#ifndef MAIN_H
#define MAIN_H

#include "lin.h"

extern lin_parameter g_lin_node;
extern volatile uint32_t wait_timeout;
extern void systick_1ms_callback(void);

#endif /* MAIN_H */
