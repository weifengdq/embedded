#include "app_extensions.h"

void App_Extensions_Init(void)
{
#if ENABLE_LWIP
    extern void MX_LWIP_Init(void);
    MX_LWIP_Init();
#endif

#if ENABLE_FREERTOS
    extern void MX_FREERTOS_Init(void);
    extern void osKernelStart(void);
    MX_FREERTOS_Init();
    osKernelStart();
#endif
}
