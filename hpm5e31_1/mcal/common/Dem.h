/*
 * Dem.h —— Diagnostic Event Manager 桩（模块 ID 54）
 *
 * MCAL 驱动对"生产错误"（如 CAN BusOff、SPI 溢出）可上报 Dem 事件；
 * 完整 Dem 属于 BSW，本仓库只提供计数桩，默认由 MCAL_DEM_ENABLE 关闭。
 */
#ifndef DEM_H
#define DEM_H

#include "Std_Types.h"
#include "Mcal_Cfg.h"
#include "Mcal_ModuleId.h"

#define DEM_EVENT_STATUS_PASSED    (0x00u)
#define DEM_EVENT_STATUS_FAILED    (0x01u)
#define DEM_EVENT_STATUS_PREPASSED (0x02u)
#define DEM_EVENT_STATUS_PREFAILED (0x03u)

#if (MCAL_DEM_ENABLE == STD_ON)
void Dem_ReportErrorStatus(uint16 EventId, uint8 EventStatus);
#else
#define Dem_ReportErrorStatus(event, status) ((void)0)
#endif

/* 本仓库扩展：查询桩计数（用于上板验证"错误路径确实被走到"） */
uint32 Dem_GetReportCount(void);

#endif /* DEM_H */
