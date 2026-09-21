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

/** \brief Card capacity in 512B sectors. Uses CMD9/CSD when this IP delivers
 *  it, otherwise the read probe. Returns 0 on success. `csdVer` is the CSD
 *  version, or -1 when the value came from the probe. */
sint32 Sdmmc_GetCapacitySectors(DWORD *sectors, int *csdVer);

/** \brief Sector count by read-only LBA probing (binary search over CMD17).
 *  Needed because this SDMMC IP does not latch 136-bit R2 responses, so the
 *  CSD cannot be read (see `sd csd` / README §11.6). Returns 0 on success;
 *  `outProbes` (optional) receives the number of CMD17 attempts used. */
sint32 Sdmmc_ProbeCapacitySectors(DWORD *sectors, UINT *outProbes);

/** \brief 0 = capacity came from CMD9/CSD, 1 = from the read probe, -1 = none. */
int    Sdmmc_GetCapacitySource(void);

/** \brief CMD17 attempts used by the last capacity probe. */
UINT   Sdmmc_GetLastProbeCount(void);

/** \brief Re-run card identification with the current host settings. 0 = ok. */
sint32 Sdmmc_ReInitCard(void);

/** \brief Set HOST_CTRL2.HOST_VER4_ENABLE and re-identify the card. 0 = ok. */
sint32 Sdmmc_SetHostVer4(boolean enable);

/* ADMA2 transfer diagnostics (chained descriptor table) */
UINT   Sdmmc_GetAdma2TableBlocks(void);            /**< max blocks per ADMA2 command */
uint32 Sdmmc_GetAdma2DescrAddr(void);              /**< descriptor table address */
void   Sdmmc_SetAdma2MaxBlocks(UINT blocks);       /**< 0 = restore per-direction defaults */
UINT   Sdmmc_GetAdma2MaxBlocks(void);              /**< current max blocks per READ command */
UINT   Sdmmc_GetAdma2MaxWriteBlocks(void);         /**< current max blocks per WRITE command */
void   Sdmmc_GetLastTransferInfo(UINT *descr, UINT *links, UINT *chunks);

/** \brief Current SD clock in Hz, derived from CLK_CTRL (+ CAP1 base clock). */
uint32 Sdmmc_GetSdClockHz(void);

/* DEBUG read-trace accessors */

/** \brief Transfer trace switch (shell `sd dbg on`). Prints the retry /
 *  recovery stages of the data path to the UART; off by default. */
extern boolean g_SdDbgTrace;

#ifdef __cplusplus
}
#endif

#endif /* MMC_SDMMC_H */



/* ff.c window trace */








void Sdmmc_LastErr(uint32 *op, uint32 *sec, uint32 *nistr, uint32 *eistr, uint32 *pstate);

sint32 Sdmmc_ReadR1(IfxSdmmc_CardStatus *out);

void sdmmc_cache_invalidate(const void *addr, uint32_t size);


void Sdmmc_StGet(int *a, int *b);
