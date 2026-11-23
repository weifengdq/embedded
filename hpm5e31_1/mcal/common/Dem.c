/*
 * Dem.c —— Dem 上报计数桩
 */
#include "Dem.h"

static uint32 s_dem_count;

#if (MCAL_DEM_ENABLE == STD_ON)
void Dem_ReportErrorStatus(uint16 EventId, uint8 EventStatus)
{
    (void)EventId;
    (void)EventStatus;
    s_dem_count++;
}
#endif

uint32 Dem_GetReportCount(void)
{
    return s_dem_count;
}
