/**
 * \file mmc_sdmmc.h
 * \brief SDMMC0 FatFs backend interface (see mmc_sdmmc.c).
 */
#ifndef MMC_SDMMC_H
#define MMC_SDMMC_H

#include "Ifx_Types.h"
#include "Sdmmc/Sd/IfxSdmmc_Sd.h"
#include "ff.h"     /* integer types (BYTE/WORD/DWORD/LBA_t/...) + config */
#include "diskio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* FatFs glue entry points (physical drive 0) */
DSTATUS disk_status_sdmmc(BYTE pdrv);
DSTATUS disk_initialize_sdmmc(BYTE pdrv);
DRESULT disk_read_sdmmc(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count);
DRESULT disk_write_sdmmc(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count);
DRESULT disk_ioctl_sdmmc(BYTE pdrv, BYTE cmd, void *buff);

/** \brief Must be called every 10 ms (implemented in diskio.c). */
void disk_timerproc(void);

/* Direct SD handle access for diagnostics */
IfxSdmmc_Sd *Sdmmc_GetHandle(void);
boolean Sdmmc_IsInited(void);

/** \brief Card capacity in 512B sectors via CMD9/CSD. Returns 0 on success. */
sint32 Sdmmc_GetCapacitySectors(DWORD *sectors, int *csdVer);

/* DEBUG read-trace accessors */

#ifdef __cplusplus
}
#endif

#endif /* MMC_SDMMC_H */



/* ff.c window trace */








void Sdmmc_LastErr(uint32 *op, uint32 *sec, uint32 *nistr, uint32 *eistr, uint32 *pstate);

sint32 Sdmmc_ReadR1(IfxSdmmc_CardStatus *out);

void sdmmc_cache_invalidate(const void *addr, uint32_t size);


void Sdmmc_StGet(int *a, int *b);
