/**
 * \file mmc_sdmmc.c
 * \brief FatFs low-level disk I/O backend via TC397 SDMMC0 (SD card, SDMA multi-block).
 *
 * Adapted from Infineon AURIX_code_examples
 *   code_examples/iLLD_TC397_3V3_ADS_SDCard_SDMMC_Read_Write/Libraries/FatFS/mmc_sdmmc.c
 * (Boost Software License 1.0, (c) Infineon Technologies AG).
 *
 * Adaptations for tc397_sdmmc + FatFs R0.16 (abbrv/fatfs master):
 *  - dropped `#pragma section text_cpuX/bss_cpuX` placement (default sections link fine);
 *  - R0.16 diskio signatures (LBA_t sector, UINT count);
 *  - pins fixed to board wiring: CLK P15.1 / CMD P15.3 / DAT0 P20.7 / DAT1 P20.8 /
 *    DAT2 P20.10 / DAT3 P20.11 (== official 3V3 AppKit mapping);
 *  - 4-bit bus, high-speed, SDMA; 25 MHz SD clock after init (as official example);
 *  - plain-C min instead of __minu intrinsic.
 */

#include "Ifx_Types.h"
#include "Sdmmc/Sd/IfxSdmmc_Sd.h"
#include "_PinMap/IfxSdmmc_PinMap.h"
#include "Cpu/Std/IfxCpu.h"
#include "ff.h"         /* integer types + config (must precede diskio.h) */
#include "diskio.h"     /* FatFs lower layer API (R0.16) */

/* Board wiring (SDMMC0, 4-bit SD) */
#define SDCARD_CLK_PIN      IfxSdmmc0_CLK_P15_1_OUT
#define SDCARD_CMD_PIN      IfxSdmmc0_CMD_P15_3_INOUT
#define SDCARD_DAT0_PIN     IfxSdmmc0_DAT0_P20_7_INOUT
#define SDCARD_DAT1_PIN     IfxSdmmc0_DAT1_P20_8_INOUT
#define SDCARD_DAT2_PIN     IfxSdmmc0_DAT2_P20_10_INOUT
#define SDCARD_DAT3_PIN     IfxSdmmc0_DAT3_P20_11_INOUT

/* SD R2 response (CSD) field helpers */
#define MMC_RSP_BITS_DECL static inline uint32 MMC_RSP_BITS(uint32 *src, int start, int len)
MMC_RSP_BITS_DECL
{
    uint32 mask = (len % 32 == 0) ? 0xffffffffU : 0xffffffffU >> (32 - (len % 32));
    uint32 word = (uint32)(start / 32);
    uint32 shift = (uint32)(start % 32);
    uint32 right = src[word] >> shift;
    uint32 left = (len + (int)shift <= 32) ? 0 : src[word + 1] << ((32 - shift) % 32);
    return (left | right) & mask;
}

#define SD_CSD_CSDVER(resp)             MMC_RSP_BITS((resp), 126, 2)
#define SD_CSD_CSDVER_1_0               0
#define SD_CSD_CSDVER_2_0               1
#define SD_CSD_SPEED(resp)              MMC_RSP_BITS((resp), 96, 8)
#define SD_CSD_SPEED_25_MHZ             0x32
#define SD_CSD_SPEED_50_MHZ             0x5a
#define SD_CSD_CCC(resp)                MMC_RSP_BITS((resp), 84, 12)
#define SD_CSD_READ_BL_LEN(resp)        MMC_RSP_BITS((resp), 80, 4)
#define SD_CSD_C_SIZE(resp)             MMC_RSP_BITS((resp), 62, 12)
#define SD_CSD_CAPACITY(resp)           ((SD_CSD_C_SIZE((resp))+1) << \
        (SD_CSD_C_SIZE_MULT((resp))+2))
#define SD_CSD_V2_C_SIZE(resp)          MMC_RSP_BITS((resp), 48, 22)
#define SD_CSD_V2_CAPACITY(resp)        ((SD_CSD_V2_C_SIZE((resp))+1) << 10)
#define SD_CSD_V2_BL_LEN                0x9     /* 512 */
#define SD_CSD_C_SIZE_MULT(resp)        MMC_RSP_BITS((resp), 47, 3)

typedef struct
{
    int csd_ver;                /*!< CSD structure format */
    int capacity;               /*!< total number of sectors */
    int sector_size;            /*!< sector size in bytes */
    int read_block_len;         /*!< block length for reads */
    int card_command_class;     /*!< Card Command Class for SD */
    int tr_speed;               /*!< Max transfer speed */
} sdmmc_csd_t;

typedef struct
{
    IfxSdmmc_Sd sd;
} App_Sdmmc_Mmc;

App_Sdmmc_Mmc g_Sdmmc_Mmc;
static boolean g_Sdmmc_Inited = FALSE;
/* Capacity (sectors) cached at init: avoids re-issuing CMD9 (R2) on the data
   path. The 128GB SDXC + this SDMMC combo deterministically fails the first
   data WRITE after an R2 response (reads are unaffected and "prime" it), so
   runtime CMD9s are eliminated; capacity cannot change while powered. */
static DWORD s_cachedSectors = 0;
static boolean s_cachedValid = FALSE;

static sint32 sdmmc_decode_csd(IfxSdmmc_Response *response, sdmmc_csd_t *out_csd)
{
    out_csd->csd_ver = (int)SD_CSD_CSDVER((uint32 *)response);
    switch (out_csd->csd_ver)
    {
        case SD_CSD_CSDVER_2_0:
            out_csd->capacity = (int)SD_CSD_V2_CAPACITY((uint32 *)response);
            out_csd->read_block_len = SD_CSD_V2_BL_LEN;
            break;
        case SD_CSD_CSDVER_1_0:
            out_csd->capacity = (int)SD_CSD_CAPACITY((uint32 *)response);
            out_csd->read_block_len = (int)SD_CSD_READ_BL_LEN((uint32 *)response);
            break;
        default:
            return -1;
    }
    out_csd->card_command_class = (int)SD_CSD_CCC((uint32 *)response);
    {
        int read_bl_size = 1 << out_csd->read_block_len;
        out_csd->sector_size = (read_bl_size < 512) ? read_bl_size : 512;
        if (out_csd->sector_size < read_bl_size)
        {
            out_csd->capacity *= read_bl_size / out_csd->sector_size;
        }
    }
    {
        int speed = (int)SD_CSD_SPEED((uint32 *)response);
        out_csd->tr_speed = (speed == SD_CSD_SPEED_50_MHZ) ? 50000000 : 25000000;
    }
    return 0;
}

static sint32 sdmmc_send_cmd_send_status(IfxSdmmc_Sd *sd, IfxSdmmc_CardStatus *out_status)
{
    IfxSdmmc_Status   status = IfxSdmmc_Status_success;
    IfxSdmmc_Response response;
    uint32            argument = 0;

    (void)sd;
    argument |= (uint32)(g_Sdmmc_Mmc.sd.cardInfo.rca << 16);
    status = IfxSdmmc_sendCommand(g_Sdmmc_Mmc.sd.sdmmcSFR, IfxSdmmc_Command_sendStatus,
                                  argument, IfxSdmmc_ResponseType_r1, &response);

    if ((status == IfxSdmmc_Status_success) && (out_status != NULL))
    {
        *out_status = response.cardStatus;
    }

    return (sint32)status;
}

static sint32 sdmmc_send_cmd_send_csd(IfxSdmmc_Sd *sd, sdmmc_csd_t *csd)
{
    IfxSdmmc_Status   status = IfxSdmmc_Status_success;
    IfxSdmmc_Response response;
    uint32            argument = 0;

    (void)sd;
    argument |= (uint32)(g_Sdmmc_Mmc.sd.cardInfo.rca << 16);
    status = IfxSdmmc_sendCommand(g_Sdmmc_Mmc.sd.sdmmcSFR, IfxSdmmc_Command_sendCSD,
                                  argument, IfxSdmmc_ResponseType_r2, &response);

    if ((status == IfxSdmmc_Status_success) && (csd != NULL))
    {
        sdmmc_decode_csd(&response, csd);
    }

    return (sint32)status;
}

/** \brief Direct access to the SD handle (for shell `sd info` etc.) */
IfxSdmmc_Sd *Sdmmc_GetHandle(void)
{
    return &g_Sdmmc_Mmc.sd;
}

boolean Sdmmc_IsInited(void)
{
    return g_Sdmmc_Inited;
}

/** \brief Capacity in 512B sectors via CMD9 (CSD). -1 on error. */
sint32 Sdmmc_GetCapacitySectors(DWORD *sectors, int *csdVer)
{
    sdmmc_csd_t csd;
    if (!g_Sdmmc_Inited || (sectors == NULL))
    {
        return -1;
    }
    if (sdmmc_send_cmd_send_csd(&g_Sdmmc_Mmc.sd, &csd) != (sint32)IfxSdmmc_Status_success)
    {
        return -1;
    }
    *sectors = (DWORD)csd.capacity;
    if (csdVer != NULL)
    {
        *csdVer = csd.csd_ver;
    }
    return 0;
}

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

static int s_stS1 = 0, s_stS2 = 0;
static uint32 s_lastNistr = 0, s_lastEistr = 0, s_lastPstate = 0;
static uint32 s_lastOp = 0, s_lastSector = 0; /* 1=read,2=write,3=status-cmd */
void Sdmmc_StGet(int *a, int *b) { *a = s_stS1; *b = s_stS2; }

/** \brief Clear sticky normal/error interrupt flags (write-1-to-clear).
 * The iLLD SDMMC driver does not clear error flags on failure paths; a stale
 * Auto-CMD/command-timeout flag (EISTR) otherwise wedges the controller
 * (PSTATE.CMD_ISSUE_ERR) and fails all later commands. */
/** \brief DCache maintenance for SDMA buffers (TriCore CACHEA).
 * clean = write-back dirty lines (before SDMA reads RAM for writes);
 * invalidate = drop stale lines (after SDMA wrote RAM for reads). */
static void sdmmc_cache_clean(const void *addr, uint32_t size)
{
    uintptr_t a = (uintptr_t)addr & ~31UL;
    uintptr_t end = ((uintptr_t)addr + size + 31UL) & ~31UL;
    for (; a < end; a += 32)
    {
        __asm__ volatile ("cachea.wi [%0]0" :: "a" (a) : "memory");
    }
    __asm__ volatile ("dsync" ::: "memory");
}

void sdmmc_cache_invalidate(const void *addr, uint32_t size)
{
    uintptr_t a = (uintptr_t)addr & ~31UL;
    uintptr_t end = ((uintptr_t)addr + size + 31UL) & ~31UL;
    for (; a < end; a += 32)
    {
        __asm__ volatile ("cachea.i [%0]0" :: "a" (a) : "memory");
    }
    __asm__ volatile ("dsync" ::: "memory");
}

static void sdmmc_clear_sticky(Ifx_SDMMC *sdmmcSFR)
{
    IfxSdmmc_clearErrorInterruptAll(sdmmcSFR, 0xFFFFU);
    IfxSdmmc_clearNormalInterruptAll(sdmmcSFR, 0xFFFFU);
}

DSTATUS disk_status_sdmmc(BYTE pdrv)
{
    DSTATUS card_status;

    if (pdrv)
    {
        return STA_NOINIT;
    }
    if (!g_Sdmmc_Inited)
    {
        return STA_NOINIT;
    }

    card_status = STA_NOINIT | STA_PROTECT;
    sdmmc_clear_sticky(g_Sdmmc_Mmc.sd.sdmmcSFR);
    /* Some cards (e.g. 128GB SDXC) transiently NAK the first CMD13 after an
       idle gap following an R2 command (CMD9). Retry a few times. */
    {
        int tries;
        s_stS1 = (int)IfxSdmmc_Status_failure;
        for (tries = 0; tries < 3; tries++)
        {
            volatile uint32_t spin;
            s_stS1 = (int)sdmmc_send_cmd_send_status(&(g_Sdmmc_Mmc.sd), NULL);
            if (s_stS1 == (sint32)IfxSdmmc_Status_success)
            {
                break;
            }
            for (spin = 0; spin < 50000UL; spin++) { }
        }
    }
    if (s_stS1 == (sint32)IfxSdmmc_Status_success)
    {
        IfxSdmmc_CardLockStatus lock_status;
        card_status &= (DSTATUS)~STA_NOINIT;
        s_stS2 = (int)IfxSdmmc_Sd_getLockStatus(&(g_Sdmmc_Mmc.sd), &lock_status);
        if (s_stS2 == (int)IfxSdmmc_Status_success)
        {
            if (lock_status != IfxSdmmc_CardLockStatus_locked)
            {
                card_status &= (DSTATUS)~STA_PROTECT;
            }
        }
    }

    return card_status;
}

/*-----------------------------------------------------------------------*/
/* Initialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize_sdmmc(BYTE pdrv)
{
    DSTATUS stat = STA_NOINIT | STA_PROTECT;
    IfxSdmmc_Sd_Config config;
    IfxSdmmc_Sd_Pins pins;

    if (pdrv)
    {
        return STA_NOINIT;
    }

    /* fill the config structure with default values */
    IfxSdmmc_Sd_initModuleConfig(&config, &MODULE_SDMMC0);

    pins.clk       = &SDCARD_CLK_PIN;
    pins.cmd       = &SDCARD_CMD_PIN;
    pins.dat0      = &SDCARD_DAT0_PIN;
    pins.dat1      = &SDCARD_DAT1_PIN;
    pins.dat2      = &SDCARD_DAT2_PIN;
    pins.dat3      = &SDCARD_DAT3_PIN;
    pins.inputMode = IfxPort_InputMode_pullUp;
    pins.pinDriver = IfxPort_PadDriver_cmosAutomotiveSpeed1;
    config.pins = &pins;

    /* 4-bit bus, high-speed mode, SDMA multi-block */
    config.cardConfig.dataWidth = IfxSdmmc_SdDataTransferWidth_4Bit;
    config.cardConfig.speedMode = IfxSdmmc_SdSpeedMode_high;

    config.useDma = TRUE;
    config.dmaConfig.dmaType = IfxSdmmc_DmaType_sdma;

    config.interruptConfig.commandCompleteInterruptEnable  = TRUE;
    config.interruptConfig.transferCompleteInterruptEnable = TRUE;
    config.interruptConfig.commandTimeoutInterruptEnable   = FALSE;
    config.interruptConfig.dataTimeoutInterruptEnable      = FALSE;

    if (IfxSdmmc_Sd_initModule(&(g_Sdmmc_Mmc.sd), &config) == IfxSdmmc_Status_success)
    {
        stat &= (DSTATUS)~STA_NOINIT;
        g_Sdmmc_Inited = TRUE;
    }
    else
    {
        g_Sdmmc_Inited = FALSE;
        return stat;
    }

    if ((g_Sdmmc_Mmc.sd.cardState & IfxSdmmc_CardState_locked) == 0)
    {
        stat &= (DSTATUS)~STA_PROTECT;
    }

    IfxSdmmc_configureClock(g_Sdmmc_Mmc.sd.sdmmcSFR, 25000000);

    /* Force the software clock divider to take effect.
     *
     * iLLD's default hostConfig.usePresetValues=TRUE makes
     * IfxSdmmc_Sd_configureSpeedAndBusWidth() set HOST_CTRL2.PRESET_VAL_ENABLE=1,
     * after which the controller ignores CLK_CTRL.FREQ_SEL. Measured on TC397:
     * FREQ_SEL stayed 0 (CLKCTL=0x000F) => SDCLK ran at the base clock
     * (SPB=100MHz), i.e. 4x above the 25MHz SD limit. Reads happened to work,
     * writes failed with EISTR.CMD_TOUT_ERR and the card was left in RCV state.
     *
     * Fix: clear PRESET_VAL_ENABLE, then program the divider explicitly using
     * the SDHCI sequence (stop SDCLK -> change divider -> wait stable -> start).
     * Verified: CLKCTL becomes 0x010F (FREQ_SEL=1 => 100MHz/(2*2)=25MHz). */
    {
        Ifx_SDMMC *p = g_Sdmmc_Mmc.sd.sdmmcSFR;
        uint32 spb = (uint32)IfxScuCcu_getSpbFrequency();
        uint32 div = (spb != 0U) ? (spb / (2U * 25000000U)) : 2U;
        uint16 setVal;
        if (div == 0U) { div = 1U; }
        setVal = (uint16)(div - 1U);

        p->HOST_CTRL2.B.PRESET_VAL_ENABLE = 0;
        p->CLK_CTRL.B.SD_CLK_EN = 0;
        p->CLK_CTRL.B.FREQ_SEL       = (uint32)(setVal & 0xFFU);
        p->CLK_CTRL.B.UPPER_FREQ_SEL = (uint32)((setVal >> 8) & 0x3U);
        {
            uint32 spin;
            for (spin = 0; spin < 100000UL; spin++) { }
        }
        p->CLK_CTRL.B.SD_CLK_EN = 1;
    }

    /* refresh cached capacity once per (re-)init; later callers use cache */
    s_cachedValid = FALSE;
    {
        DWORD sec = 0;
        int ver = 0;
        if (Sdmmc_GetCapacitySectors(&sec, &ver) == 0 && sec != 0)
        {
            s_cachedSectors = sec;
            s_cachedValid = TRUE;
        }
    }

    return stat;
}

/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read_sdmmc(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv)
    {
        return RES_PARERR;
    }
    if (disk_status_sdmmc(pdrv) & STA_NOINIT)
    {
        return RES_NOTRDY;
    }
    if (!count)
    {
        return RES_PARERR;
    }
    sdmmc_clear_sticky(g_Sdmmc_Mmc.sd.sdmmcSFR);
    /* WI (clean+invalidate) before SDMA: flushes dirty lines safely (incl.
       neighbors sharing cache lines), then CPU fetches fresh DMA data.
       NOTE: never use bare invalidate (cachea.i) here - it would discard
       dirty FATFS metadata sharing the lines. */
    sdmmc_cache_clean(buff, (uint32_t)count * 512U);
    {
        /* Retry transient transfer errors (same class of card flakiness as CMD13) */
        int tries;
        IfxSdmmc_Status st = IfxSdmmc_Status_failure;
        for (tries = 0; tries < 3; tries++)
        {
            /* Single-block PIO read.
             *
             * The SDMA multi-block path (IfxSdmmc_Sd_readMultiBlock) never raises
             * NORMAL_INT_STAT.transferComplete on this controller: it times out
             * with IfxSdmmc_Status_dataError, NISTR=0, EISTR=0 and
             * PSTATE=0x03070202 (DAT_INHIBIT set, DAT lines stuck low), which
             * makes f_mount fail with FR_DISK_ERR. The single-block PIO path
             * (IfxSdmmc_Sd_singleBlockTransfer) works reliably, so use it for
             * every sector and loop over the requested count. */
            UINT i;
            st = IfxSdmmc_Status_success;
            for (i = 0; i < count; i++)
            {
                st = IfxSdmmc_Sd_singleBlockTransfer(&(g_Sdmmc_Mmc.sd),
                                                     IfxSdmmc_Command_readSingleBlock,
                                                     (uint32)(sector + i),
                                                     IFXSDMMC_BLOCK_SIZE_DEFAULT,
                                                     (uint32 *)(buff + (i * 512U)),
                                                     IfxSdmmc_TransferDirection_read);
                if (st != IfxSdmmc_Status_success)
                {
                    break;
                }
            }
            if (st == IfxSdmmc_Status_success)
            {
                break;
            }
        }
        if (st != IfxSdmmc_Status_success)
        {
            Ifx_SDMMC *sp = g_Sdmmc_Mmc.sd.sdmmcSFR;
            s_lastNistr = (uint32)sp->NORMAL_INT_STAT.U;
            s_lastEistr = (uint32)sp->ERROR_INT_STAT.U;
            s_lastPstate = (uint32)sp->PSTATE_REG.U;
            s_lastOp = 1; s_lastSector = (uint32)sector;
            return RES_ERROR;
        }
    }

    return RES_OK;
}

/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

DRESULT disk_write_sdmmc(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv)
    {
        return RES_PARERR;
    }
    if (disk_status_sdmmc(pdrv) & STA_NOINIT)
    {
        return RES_NOTRDY;
    }
    if (!count)
    {
        return RES_PARERR;
    }

    {
        DRESULT rr;
        sdmmc_clear_sticky(g_Sdmmc_Mmc.sd.sdmmcSFR);
        sdmmc_cache_clean(buff, (uint32_t)count * 512U);
        {
            /* Retry transient transfer errors (writes of identical data are idempotent) */
            int tries;
            rr = RES_ERROR;
            for (tries = 0; tries < 3; tries++)
            {
                {
                    IfxSdmmc_Status wst = IfxSdmmc_Sd_writeMultiBlock(&(g_Sdmmc_Mmc.sd), (uint32)sector, (uint32 *)buff,
                                                 (uint32)count, IfxSdmmc_BlockBoundarySize_512K);
                    if (wst == IfxSdmmc_Status_success) { rr = RES_OK; break; }
                    {
                        Ifx_SDMMC *sp = g_Sdmmc_Mmc.sd.sdmmcSFR;
                        s_lastNistr = (uint32)sp->NORMAL_INT_STAT.U;
                        s_lastEistr = (uint32)sp->ERROR_INT_STAT.U;
                        s_lastPstate = (uint32)sp->PSTATE_REG.U;
                        s_lastOp = 2; s_lastSector = (uint32)sector;
                    }
                    rr = RES_ERROR;
                }
            }
        }
        return rr;
    }
}

/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl_sdmmc(BYTE pdrv, BYTE cmd, void *buff)
{
    sdmmc_csd_t csd;
    DRESULT res;

    if (pdrv)
    {
        return RES_PARERR;
    }
    if (disk_status_sdmmc(pdrv) & STA_NOINIT)
    {
        return RES_NOTRDY;    /* Check if card is in the socket */
    }

    res = RES_ERROR;
    switch (cmd)
    {
        case CTRL_SYNC:        /* Make sure that no pending write process */
            res = RES_OK;
            break;

        case GET_SECTOR_COUNT: /* Get number of sectors on the disk (DWORD) */
            if (s_cachedValid)
            {
                /* NOTE: caller may pass a 64-bit LBA_t (FF_LBA64); clear it fully */
                *(LBA_t *)buff = (LBA_t)s_cachedSectors;
                res = RES_OK;
            }
            else if (sdmmc_send_cmd_send_csd(&(g_Sdmmc_Mmc.sd), &csd) == (sint32)IfxSdmmc_Status_success)
            {
                *(LBA_t *)buff = (LBA_t)(DWORD)csd.capacity;
                s_cachedSectors = (DWORD)csd.capacity;
                s_cachedValid = TRUE;
                res = RES_OK;
            }
            break;

        case GET_BLOCK_SIZE:   /* Get erase block size in unit of sector (DWORD) */
            /* NOT supported: the CSD-based query (CMD9) back-to-back with other
               commands is timing-sensitive on some cards and can fail, which
               needlessly aborts f_mkfs(). FatFs then uses default alignment
               (sz_blk=1), which is safe. Capacity is still available via
               GET_SECTOR_COUNT and Sdmmc_GetCapacitySectors(). */
            res = RES_PARERR;
            break;

        default:
            res = RES_PARERR;
    }

    return res;
}

void Sdmmc_LastErr(uint32 *op, uint32 *sec, uint32 *nistr, uint32 *eistr, uint32 *pstate)
{
    *op = s_lastOp; *sec = s_lastSector;
    *nistr = s_lastNistr; *eistr = s_lastEistr; *pstate = s_lastPstate;
}

sint32 Sdmmc_ReadR1(IfxSdmmc_CardStatus *out)
{
    return sdmmc_send_cmd_send_status(&g_Sdmmc_Mmc.sd, out);
}
