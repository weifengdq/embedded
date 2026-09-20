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
/* card configuration kept from the last disk_initialize, so the card can be
   re-identified after changing structural host settings (HOST_CTRL2 mode bits). */
static IfxSdmmc_Sd_CardConfig s_cardCfg;
/* Capacity (sectors) cached at init: avoids re-issuing CMD9 (R2) on the data
   path. The 128GB SDXC + this SDMMC combo deterministically fails the first
   data WRITE after an R2 response (reads are unaffected and "prime" it), so
   runtime CMD9s are eliminated; capacity cannot change while powered. */
static DWORD s_cachedSectors = 0;
static boolean s_cachedValid = FALSE;

/* Forward declaration: defined next to disk_initialize_sdmmc(), used by
 * sdmmc_recover_datapath() above it. */
static IfxSdmmc_Status sdmmc_init_module_once(void);

/*-----------------------------------------------------------------------*/
/* ADMA2 descriptor table                                               */
/*-----------------------------------------------------------------------*/
/* The controller runs with HOST_CTRL2.HOST_VER4_ENABLE=1, where SDMASA is
 * re-purposed as the block-count register and the SDMA engine can no longer be
 * given a system address. ADMA2 is therefore the only usable DMA engine, and
 * it needs a descriptor table instead of a plain buffer address.
 *
 * One descriptor per 512-byte block, so a transfer of up to
 * IFXSDMMC_ADMA2_MAX_BLOCKS blocks can be described. The table must be
 * 4-byte aligned (8-byte recommended); the attribute is honoured by both
 * TASKING and GCC. */
#define IFXSDMMC_ADMA2_MAX_BLOCKS 256U   /* 128 KB per transfer */
IFX_ALIGN(8) static IfxSdmmc_Adma2Descriptor s_adma2Descr[IFXSDMMC_ADMA2_MAX_BLOCKS];

/** \brief Fill the ADMA2 descriptor table for a transfer of \p count 512-byte
 *  blocks located at \p data. Returns FALSE if \p count is too large. */
static boolean sdmmc_build_adma2_descr(uint32 *data, UINT count)
{
    UINT i;

    if ((count == 0U) || (count > IFXSDMMC_ADMA2_MAX_BLOCKS))
    {
        return FALSE;
    }

    for (i = 0U; i < count; i++)
    {
        s_adma2Descr[i].valid   = 1U;
        s_adma2Descr[i].end     = 0U;
        s_adma2Descr[i].intEn   = 0U;
        s_adma2Descr[i].act     = (uint32)IfxSdmmc_AdmaActionSymbol_tran;
        s_adma2Descr[i].lengthUpper = 0U;
        s_adma2Descr[i].length  = (uint32)IFXSDMMC_BLOCK_SIZE_DEFAULT;
        s_adma2Descr[i].address = (uint32)(uintptr_t)((uint8 *)data + (i * IFXSDMMC_BLOCK_SIZE_DEFAULT));
    }

    /* last line: terminate the table and raise the ADMA interrupt */
    s_adma2Descr[count - 1U].end   = 1U;
    s_adma2Descr[count - 1U].intEn = 1U;

    /* Make sure the descriptor table is visible to the ADMA engine before the
     * command is issued. The table lives in DSPR (uncached), but the CPU store
     * buffer still has to drain. */
    __asm__ volatile ("dsync" ::: "memory");

    return TRUE;
}

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

/** \brief Re-run CMD0/CMD8/ACMD41/CMD2/CMD3/CMD7/ACMD6/CMD6 with the current
 * host settings. Required after changing structural HOST_CTRL2 bits (e.g.
 * HOST_VER4_ENABLE) because this IP latches them during card identification.
 * Returns 0 on success. */
sint32 Sdmmc_ReInitCard(void)
{
    if (!g_Sdmmc_Inited)
    {
        return -1;
    }
    return (sint32)IfxSdmmc_Sd_initCard(&g_Sdmmc_Mmc.sd, &s_cardCfg);
}

/** \brief Switch Host Version 4.00 mode and re-identify the card. */
sint32 Sdmmc_SetHostVer4(boolean enable)
{
    if (!g_Sdmmc_Inited)
    {
        return -1;
    }
    g_Sdmmc_Mmc.sd.sdmmcSFR->HOST_CTRL2.B.PRESET_VAL_ENABLE = 0;
    g_Sdmmc_Mmc.sd.sdmmcSFR->HOST_CTRL2.B.HOST_VER4_ENABLE   = (enable == TRUE) ? 1U : 0U;
    return Sdmmc_ReInitCard();
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

/** \brief Recover the controller after a failed data transfer.
 *
 * A failed ADMA2 transfer leaves the controller in a state where every
 * subsequent transfer also fails (observed on the very first data transfer
 * after a reset: EISTR = DATA_CRC_ERR | DATA_END_BIT_ERR | AUTO_CMD_ERR,
 * AUTOCMD = 0x0003). The shell's `sd recover` command fixes it, and it does
 * exactly this: CMD12 abort, DAT+CMD software reset, then a full
 * IfxSdmmc_Sd_initModule() re-initialisation.
 *
 * A plain SW_RST_DAT is NOT enough - the host configuration (clock, bus width,
 * DMA type) is lost by the reset, so the module has to be initialised again.
 *
 * Returns TRUE if the controller was successfully re-initialised. */
static boolean sdmmc_recover_datapath(void)
{
    Ifx_SDMMC *p = g_Sdmmc_Mmc.sd.sdmmcSFR;
    uint32     spin;

    /* 1. try to abort whatever the card is doing (CMD12) */
    (void)IfxSdmmc_sendCommand(p, IfxSdmmc_Command_stopTransmission,
                               (uint32)(g_Sdmmc_Mmc.sd.cardInfo.rca << 16),
                               IfxSdmmc_ResponseType_r1b, NULL_PTR);

    /* 2. reset the data and command paths */
    p->SW_RST.B.SW_RST_DAT = 1U;
    p->SW_RST.B.SW_RST_CMD = 1U;
    for (spin = 0U; (spin < 1000000UL) &&
                    (p->SW_RST.B.SW_RST_DAT || p->SW_RST.B.SW_RST_CMD); spin++) { }
    for (spin = 0U; spin < 300000UL; spin++) { }
    sdmmc_clear_sticky(p);

    /* 3. full module re-initialisation (restores clock/bus width/DMA type and
     *    re-identifies the card) */
    return (sdmmc_init_module_once() == IfxSdmmc_Status_success) ? TRUE : FALSE;
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
    /* NOTE (2026-09-20, round 6): sdmmc_clear_sticky() used to be called here.
     * The official Infineon example does NOT touch the interrupt status
     * registers in disk_status(), and disk_write() calls disk_status() first,
     * so the extra 0xFFFF writes were landing right before every write.
     * Removed to match the known-good official sequence. */
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

/** \brief Run one full IfxSdmmc_Sd_initModule() with the board's SD settings.
 *
 * Shared by disk_initialize_sdmmc() and sdmmc_recover_datapath() so both use
 * exactly the same configuration (4-bit, high-speed, ADMA2, 25 MHz). */
static IfxSdmmc_Status sdmmc_init_module_once(void)
{
    IfxSdmmc_Sd_Config config;
    IfxSdmmc_Sd_Pins   pins;

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

    /* 4-bit bus, high-speed mode */
    config.cardConfig.dataWidth = IfxSdmmc_SdDataTransferWidth_4Bit;
    config.cardConfig.speedMode = IfxSdmmc_SdSpeedMode_high;
    s_cardCfg = config.cardConfig;

    config.useDma = TRUE;
    /* ROOT CAUSE FIX (2026-09-20, round 6): use ADMA2, not SDMA.
     *
     * With HOST_CTRL2.HOST_VER4_ENABLE=1 (which iLLD always sets in
     * IfxSdmmc_Sd_initHostController) the SDHCI v4 register map re-purposes
     * SDMASA as the 32-bit block-count register, so the SDMA engine can no
     * longer be programmed with a system address. Programming SDMA anyway
     * leaves the controller with no valid DMA target: the data phase never
     * completes, NORMAL_INT_STAT.transferComplete never sets, Auto CMD12 times
     * out (EISTR.CMD_TOUT_ERR) and the card is left in RCV/TRAN with DAT lines
     * never driven. That is exactly the "host never drives DAT" symptom.
     *
     * ADMA2 is the DMA engine that is valid in v4 mode and it works on this
     * controller: measured 5696 KB/s write and 13837 KB/s read (8 MB, Tasking
     * Debug, 25 MHz, 4-bit high-speed). */
    config.dmaConfig.dmaType = IfxSdmmc_DmaType_adma2;

    config.interruptConfig.commandCompleteInterruptEnable  = TRUE;
    config.interruptConfig.transferCompleteInterruptEnable = TRUE;
    config.interruptConfig.commandTimeoutInterruptEnable   = FALSE;
    config.interruptConfig.dataTimeoutInterruptEnable      = FALSE;

    return IfxSdmmc_Sd_initModule(&(g_Sdmmc_Mmc.sd), &config);
}

DSTATUS disk_initialize_sdmmc(BYTE pdrv)
{
    DSTATUS stat = STA_NOINIT | STA_PROTECT;

    if (pdrv)
    {
        return STA_NOINIT;
    }

    if (sdmmc_init_module_once() != IfxSdmmc_Status_success)
    {
        g_Sdmmc_Inited = FALSE;
        return stat;
    }

    stat &= (DSTATUS)~STA_NOINIT;
    g_Sdmmc_Inited = TRUE;

    if ((g_Sdmmc_Mmc.sd.cardState & IfxSdmmc_CardState_locked) == 0)
    {
        stat &= (DSTATUS)~STA_PROTECT;
    }

    IfxSdmmc_configureClock(g_Sdmmc_Mmc.sd.sdmmcSFR, 25000000);

    /* NOTE (2026-09-20, round 6): the previous "force software clock divider"
     * workaround that cleared HOST_CTRL2.PRESET_VAL_ENABLE and reprogrammed
     * CLK_CTRL.FREQ_SEL by hand has been REMOVED.
     *
     * Rationale: the official Infineon example
     * iLLD_TC397_3V3_ADS_SDCard_SDMMC_Read_Write does exactly the single
     * IfxSdmmc_configureClock() call above and its f_write() succeeds on this
     * very board. Clearing PRESET_VAL_ENABLE after the card was already
     * identified leaves the controller in a state where the write data path
     * never drives DAT0-3 (observed: PSTATE.DAT_3_0 stuck at 0xF, no
     * WR_XFER_ACTIVE, no XFER_COMPLETE, card left in RCV).
     *
     * If the SDCLK divider ever needs to be forced again, do it BEFORE
     * IfxSdmmc_Sd_initModule() (i.e. before card identification), not after. */

    /* NOTE (2026-09-20, round 6): the Sdmmc_GetCapacitySectors() call that used
     * to run here issues CMD9 (SEND_CSD, an R2/136-bit response) at the end of
     * every disk_initialize(). The official Infineon example never issues CMD9
     * during init - it only does so from disk_ioctl(GET_SECTOR_COUNT).
     *
     * An R2 response right before the first data write is exactly the pattern
     * that was already documented as breaking writes on this SDMMC/card combo
     * ("the first data WRITE after an R2 response fails; reads are unaffected
     * and prime it"). Removed so the init sequence matches the known-good
     * official one. Capacity is still available on demand via
     * Sdmmc_GetCapacitySectors() / disk_ioctl(GET_SECTOR_COUNT). */
    s_cachedValid = FALSE;

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
    /* WI (clean+invalidate) before DMA: flushes dirty lines safely (incl.
       neighbors sharing cache lines), then CPU fetches fresh DMA data.
       NOTE: never use bare invalidate (cachea.i) here - it would discard
       dirty FATFS metadata sharing the lines. */
    sdmmc_cache_clean(buff, (uint32_t)count * 512U);
    {
        /* ADMA2 multi-block read (see the ADMA2 note in disk_initialize_sdmmc).
         * The descriptor table replaces the plain buffer address that SDMA
         * would have taken.
         *
         * The very first data transfer after a reset can fail once with the
         * controller wedged in RD_XFER_ACTIVE (ADMAERR=0, no error flags).
         * Recover the data path and retry; reads are idempotent. */
        IfxSdmmc_Status st = IfxSdmmc_Status_failure;
        int tries;
        for (tries = 0; tries < 3; tries++)
        {
            if (!sdmmc_build_adma2_descr((uint32 *)(uintptr_t)buff, count))
            {
                return RES_PARERR;
            }
            st = IfxSdmmc_Sd_multiBlockAdma2Transfer(&(g_Sdmmc_Mmc.sd),
                                                     IfxSdmmc_Command_readMultipleBLock,
                                                     (uint32)sector,
                                                     IFXSDMMC_BLOCK_SIZE_DEFAULT,
                                                     (uint32 *)s_adma2Descr,
                                                     IfxSdmmc_TransferDirection_read,
                                                     (uint32)count);
            if (st == IfxSdmmc_Status_success)
            {
                break;
            }
            {
                Ifx_SDMMC *sp = g_Sdmmc_Mmc.sd.sdmmcSFR;
                s_lastNistr = (uint32)sp->NORMAL_INT_STAT.U;
                s_lastEistr = (uint32)sp->ERROR_INT_STAT.U;
                s_lastPstate = (uint32)sp->PSTATE_REG.U;
                s_lastOp = 1; s_lastSector = (uint32)sector;
            }
            if (!sdmmc_recover_datapath())
            {
                break;
            }
        }
        if (st != IfxSdmmc_Status_success)
        {
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
    /* NOTE (2026-09-20, round 6): the disk_status_sdmmc() call that used to be
     * here issues CMD13 (+ CMD13/CMD16 via getLockStatus) right before every
     * write. The official Infineon example does call disk_status() too, so this
     * is kept - but the STA_NOINIT check is done on the cached flag instead of
     * re-probing the card, to keep the pre-write command sequence minimal. */
    if (!g_Sdmmc_Inited)
    {
        return RES_NOTRDY;
    }
    if (!count)
    {
        return RES_PARERR;
    }

    {
        /* ADMA2 multi-block write (see the ADMA2 note in disk_initialize_sdmmc).
         * The descriptor table replaces the plain buffer address that SDMA
         * would have taken.
         *
         * The very first data transfer after a reset can fail once with the
         * controller wedged in RD_XFER_ACTIVE (ADMAERR=0, no error flags).
         * Recover the data path and retry; writing the same data twice is
         * idempotent. */
        IfxSdmmc_Status wst = IfxSdmmc_Status_failure;
        int tries;
        for (tries = 0; tries < 3; tries++)
        {
            if (!sdmmc_build_adma2_descr((uint32 *)(uintptr_t)buff, count))
            {
                return RES_PARERR;
            }
            wst = IfxSdmmc_Sd_multiBlockAdma2Transfer(&(g_Sdmmc_Mmc.sd),
                                                      IfxSdmmc_Command_writeMultipleBlock,
                                                      (uint32)sector,
                                                      IFXSDMMC_BLOCK_SIZE_DEFAULT,
                                                      (uint32 *)s_adma2Descr,
                                                      IfxSdmmc_TransferDirection_write,
                                                      (uint32)count);
            if (wst == IfxSdmmc_Status_success)
            {
                break;
            }
            {
                Ifx_SDMMC *sp = g_Sdmmc_Mmc.sd.sdmmcSFR;
                s_lastNistr = (uint32)sp->NORMAL_INT_STAT.U;
                s_lastEistr = (uint32)sp->ERROR_INT_STAT.U;
                s_lastPstate = (uint32)sp->PSTATE_REG.U;
                s_lastOp = 2; s_lastSector = (uint32)sector;
            }
            if (!sdmmc_recover_datapath())
            {
                break;
            }
        }
        if (wst != IfxSdmmc_Status_success)
        {
            return RES_ERROR;
        }
        return RES_OK;
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
