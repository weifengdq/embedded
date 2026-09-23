/**
 * \file diskio.c
 * \brief FatFs R0.16 glue: single physical drive 0 -> SDMMC0 backend (mmc_sdmmc.c).
 *
 * Card-detect switch is NOT used here (_USE_CD = 0, as in the Infineon example);
 * P10.7 state is reported separately by the shell `sd cd` command.
 * disk_timerproc() must be called every 10 ms (done from the STM 1 ms ISR).
 */

#include "ff.h"         /* Obtains integer types */
#include "diskio.h"     /* Declarations of disk functions */
#include "mmc_sdmmc.h"

/* 100Hz decrement timers for FatFs (stopped at zero) */
volatile WORD Timer1, Timer2;

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/
DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != 0)
    {
        return STA_NOINIT;
    }
    return disk_status_sdmmc(pdrv);
}

/*-----------------------------------------------------------------------*/
/* Initialize a Drive                                                    */
/*-----------------------------------------------------------------------*/
DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != 0)
    {
        return STA_NOINIT;
    }
    return disk_initialize_sdmmc(pdrv);
}

/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/
DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0)
    {
        return RES_PARERR;
    }
    return disk_read_sdmmc(pdrv, buff, sector, count);
}

/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/
#if FF_FS_READONLY == 0
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0)
    {
        return RES_PARERR;
    }
    return disk_write_sdmmc(pdrv, buff, sector, count);
}
#endif

/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != 0)
    {
        return RES_PARERR;
    }
    return disk_ioctl_sdmmc(pdrv, cmd, buff);
}

/*-----------------------------------------------------------------------*/
/* Device timer function - call every 10 ms from STM ISR                 */
/*-----------------------------------------------------------------------*/
void disk_timerproc(void)
{
    WORD n;

    n = Timer1;
    if (n)
    {
        Timer1 = (WORD)(n - 1);
    }
    n = Timer2;
    if (n)
    {
        Timer2 = (WORD)(n - 1);
    }
}
