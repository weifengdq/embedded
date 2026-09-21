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
#include "UART_Logging.h"   /* g_asc / IfxAsclin_Asc_write for the transfer trace */
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
    int    csd_ver;             /*!< CSD structure format */
    DWORD  capacity;            /*!< total number of sectors */
    int    sector_size;         /*!< sector size in bytes */
    int    read_block_len;      /*!< block length for reads */
    int    card_command_class;  /*!< Card Command Class for SD */
    int    tr_speed;            /*!< Max transfer speed */
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
/* Where the accepted capacity came from: 0 = CMD9/CSD, 1 = read probe. */
static int s_capSource = -1;
/* CSD version of the cached capacity (-1 when it came from the probe). */
static int s_cachedCsdVer = -1;

/* Forward declaration: defined next to disk_initialize_sdmmc(), used by
 * sdmmc_recover_datapath() above it. */
static IfxSdmmc_Status sdmmc_init_module_once(void);

/*-----------------------------------------------------------------------*/
/* Transfer trace (shell `sd dbg on`)                                    */
/*-----------------------------------------------------------------------*/
/* Minimal, libc-free progress trace of the data path. It exists because a
 * failing/blocked transfer otherwise shows up as a silent hang of the whole
 * shell, with no way to tell which of the retry / recovery stages is stuck.
 * Off by default; enable with `sd dbg on`. */
boolean g_SdDbgTrace = FALSE;

static void sdmmc_trace(const char *tag, uint32 v1, uint32 v2)
{
    static const char hx[] = "0123456789ABCDEF";
    char     buf[40];
    Ifx_SizeT n = 0U;
    int i;

    if (!g_SdDbgTrace)
    {
        return;
    }
    while ((tag[n] != '\0') && (n < 24U))
    {
        buf[n] = tag[n];
        n++;
    }
    buf[n++] = ':';
    for (i = 0; i < 2; i++)
    {
        uint32 v = (i == 0) ? v1 : v2;
        int    k;
        for (k = 28; k >= 0; k -= 4)
        {
            buf[n++] = hx[(v >> k) & 0xFU];
        }
        buf[n++] = (i == 0) ? ' ' : '\r';
        if (i == 1)
        {
            buf[n++] = '\n';
        }
    }
    IfxAsclin_Asc_write(&g_asc, (uint8_t *)buf, &n, TIME_INFINITE);
}

/* Capacity helpers (defined after sdmmc_transfer(), forward declared for
 * Sdmmc_GetCapacitySectors()/disk_ioctl() above them). */
static boolean sdmmc_csd_plausible(const sdmmc_csd_t *csd);
sint32 Sdmmc_ProbeCapacitySectors(DWORD *sectors, UINT *outProbes);

/* Data-path priming (defined after sdmmc_adma2_transfer_once(), called at the
 * end of sdmmc_init_module_once()) - see sdmmc_prime_datapath(). */
static IfxSdmmc_Status sdmmc_adma2_transfer_once(BYTE *buff, LBA_t sector, UINT count, boolean isRead);
static void sdmmc_prime_datapath(void);

/*-----------------------------------------------------------------------*/
/* ADMA2 descriptor table (chained pages)                                */
/*-----------------------------------------------------------------------*/
/* The controller runs with HOST_CTRL2.HOST_VER4_ENABLE=1, where SDMASA is
 * re-purposed as the block-count register and the SDMA engine can no longer be
 * given a system address. ADMA2 is therefore the only usable DMA engine, and
 * it needs a descriptor table instead of a plain buffer address.
 *
 * Layout (2026-09-21, round 7): the table is split into pages of
 * IFXSDMMC_ADMA2_PAGE_DESCR descriptors. One descriptor covers one 512-byte
 * block. When a transfer needs more descriptors than the current page has
 * left, the last slot of the page becomes a *link* descriptor
 * (act = IfxSdmmc_AdmaActionSymbol_link) that points at the first entry of the
 * next page. That is what removes the old hard ceiling of one page
 * (previously 256 blocks = 128 KB, anything larger returned RES_PARERR):
 *   table capacity  = IFXSDMMC_ADMA2_PAGES * IFXSDMMC_ADMA2_PAGE_DESCR
 *   block capacity  = table capacity - (pages - 1)  [one link per page hop]
 * and disk_read()/disk_write() additionally chunk a call that is still too
 * large into several commands, so no request size is rejected any more.
 *
 * The table must be 8-byte aligned (attribute honoured by TASKING and GCC). */
#define IFXSDMMC_ADMA2_PAGE_DESCR   64U   /* descriptors per page (512 B) */
#define IFXSDMMC_ADMA2_PAGES        8U    /* chained pages */
#define IFXSDMMC_ADMA2_MAX_DESCR    (IFXSDMMC_ADMA2_PAGE_DESCR * IFXSDMMC_ADMA2_PAGES)
#define IFXSDMMC_ADMA2_MAX_BLOCKS   (IFXSDMMC_ADMA2_MAX_DESCR - (IFXSDMMC_ADMA2_PAGES - 1U))
IFX_ALIGN(8) static IfxSdmmc_Adma2Descriptor s_adma2Descr[IFXSDMMC_ADMA2_MAX_DESCR];

/* Runtime cap on the blocks per single ADMA2 command (<= table capacity).
 * Exposed through Sdmmc_Get/SetAdma2MaxBlocks() so the chunking path can be
 * exercised on the bench with a small buffer.
 *
 * The chained table can describe IFXSDMMC_ADMA2_MAX_BLOCKS (505) blocks in one
 * command, but a single command must also finish inside the iLLD's *fixed*
 * polling timeout (IFXSDMMC_TIMEOUT_1E5): measured on this board the timeout
 * corresponds to ~15 ms of data phase, so a 320-block write (>16 ms) fails with
 * IfxSdmmc_Status_dataError while 256 blocks (13.9 ms) still passes - and the
 * margin shrinks as the card gets slower (internal garbage collection).
 * ADMA_ERR_STAT stays 0 and EISTR stays 0 in that case: it is a pure timeout,
 * not a descriptor/DMA error.
 *
 * The defaults below therefore keep roughly a 2x margin, and sdmmc_transfer()
 * splits anything larger into several commands so that no request size is
 * rejected any more (the old fixed 256-block table returned RES_PARERR).
 * `sd lim <blocks>` overrides both (0 restores the defaults). */
#define SDMMC_READ_MAX_BLOCKS   256U   /* 128 KB, measured ~5.8 ms */
#define SDMMC_WRITE_MAX_BLOCKS  128U   /*  64 KB, measured  8.0 ms */
static UINT s_maxReadBlocks  = SDMMC_READ_MAX_BLOCKS;
static UINT s_maxWriteBlocks = SDMMC_WRITE_MAX_BLOCKS;
/* Statistics of the last transfer (for `sd big` / diagnostics). */
static UINT s_lastDescrCount = 0U;
static UINT s_lastLinkCount  = 0U;
static UINT s_lastChunkCount = 0U;

/** \brief Fill the chained ADMA2 descriptor table for \p count 512-byte blocks
 *  starting at \p data. Returns FALSE if \p count cannot be described. */
static boolean sdmmc_build_adma2_descr(uint32 *data, UINT count)
{
    UINT   left = count;
    UINT   idx  = 0U;
    UINT   links = 0U;
    uint8 *p    = (uint8 *)data;

    if ((count == 0U) || (count > IFXSDMMC_ADMA2_MAX_BLOCKS))
    {
        return FALSE;
    }

    while (left > 0U)
    {
        UINT slotsInPage = IFXSDMMC_ADMA2_PAGE_DESCR - (idx % IFXSDMMC_ADMA2_PAGE_DESCR);
        boolean needLink = (left > slotsInPage);
        UINT nData       = needLink ? (slotsInPage - 1U) : left;
        UINT i;

        for (i = 0U; i < nData; i++)
        {
            IfxSdmmc_Adma2Descriptor *d = &s_adma2Descr[idx++];

            d->valid       = 1U;
            d->end         = 0U;
            d->intEn       = 0U;
            d->act         = (uint32)IfxSdmmc_AdmaActionSymbol_tran;
            d->lengthUpper = 0U;
            d->length      = (uint32)IFXSDMMC_BLOCK_SIZE_DEFAULT;
            d->address     = (uint32)(uintptr_t)p;
            p += IFXSDMMC_BLOCK_SIZE_DEFAULT;
        }
        left -= nData;

        if (needLink)
        {
            /* last slot of this page -> jump to the first slot of the next page
               (idx is now the page's last slot, so idx + 1 is the next page base) */
            IfxSdmmc_Adma2Descriptor *d = &s_adma2Descr[idx];

            d->valid       = 1U;
            d->end         = 0U;
            d->intEn       = 0U;
            d->act         = (uint32)IfxSdmmc_AdmaActionSymbol_link;
            d->lengthUpper = 0U;
            d->length      = 0U;
            d->address     = (uint32)(uintptr_t)&s_adma2Descr[idx + 1U];
            idx++;
            links++;
        }
    }

    /* last line: terminate the table and raise the ADMA interrupt */
    s_adma2Descr[idx - 1U].end   = 1U;
    s_adma2Descr[idx - 1U].intEn = 1U;

    s_lastDescrCount = idx;
    s_lastLinkCount  = links;

    /* Make sure the descriptor table is visible to the ADMA engine before the
     * command is issued. The table lives in DSPR (uncached), but the CPU store
     * buffer still has to drain. */
    __asm__ volatile ("dsync" ::: "memory");

    return TRUE;
}

static sint32 sdmmc_decode_csd(IfxSdmmc_Response *response, sdmmc_csd_t *out_csd)
{
    /* ---------------------------------------------------------------------
     * FIX (2026-09-21, round 7): the word array passed to MMC_RSP_BITS() must
     * start at resp01, NOT at the IfxSdmmc_Response structure.
     *
     * MMC_RSP_BITS(src, start, len) numbers the 128-bit R2 response from LSB:
     *   src[0] = bits[ 31: 0] = RESP01, src[1] = bits[ 63: 32] = RESP23,
     *   src[2] = bits[ 95: 64] = RESP45, src[3] = bits[127: 96] = RESP67.
     * IfxSdmmc_Response is { cardStatus; resp01; resp23; resp45; resp67; }, so
     * passing the structure itself shifted every field by 32 bits:
     *   CSD_STRUCTURE (bits 127:126) was read out of RESP45  -> always 0 (v1)
     *   C_SIZE        (bits  73: 62) was read out of RESP23/RESP01 -> garbage
     * hence the bogus "Capacity: 1024 sectors, CSD v1" for a 32 GB SDHC card.
     * With the correct base pointer the caller now sees the real capacity
     * (SDHC/SDXC: (C_SIZE+1) << 10 sectors = 62,499,840 for this 32 GB card).
     * ------------------------------------------------------------------- */
    uint32 *r = &response->resp01;

    out_csd->csd_ver = (int)SD_CSD_CSDVER(r);
    switch (out_csd->csd_ver)
    {
        case SD_CSD_CSDVER_2_0:
            out_csd->capacity = (DWORD)SD_CSD_V2_CAPACITY(r);
            out_csd->read_block_len = SD_CSD_V2_BL_LEN;
            break;
        case SD_CSD_CSDVER_1_0:
            out_csd->capacity = (DWORD)SD_CSD_CAPACITY(r);
            out_csd->read_block_len = (int)SD_CSD_READ_BL_LEN(r);
            break;
        default:
            return -1;
    }
    out_csd->card_command_class = (int)SD_CSD_CCC(r);
    {
        int read_bl_size = 1 << out_csd->read_block_len;
        out_csd->sector_size = (read_bl_size < 512) ? read_bl_size : 512;
        if (out_csd->sector_size < read_bl_size)
        {
            out_csd->capacity *= (DWORD)(read_bl_size / out_csd->sector_size);
        }
    }
    {
        int speed = (int)SD_CSD_SPEED(r);
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

/** \brief Capacity in 512B sectors. CSD first (when this IP delivers it),
 *  read-probe fallback otherwise - see Sdmmc_ProbeCapacitySectors().
 *  Returns 0 on success, -1 on error. */
sint32 Sdmmc_GetCapacitySectors(DWORD *sectors, int *csdVer)
{
    sdmmc_csd_t csd;
    if (!g_Sdmmc_Inited || (sectors == NULL))
    {
        return -1;
    }
    if (s_cachedValid)
    {
        *sectors = s_cachedSectors;
        if (csdVer != NULL)
        {
            *csdVer = s_cachedCsdVer;
        }
        return 0;
    }
    if ((sdmmc_send_cmd_send_csd(&g_Sdmmc_Mmc.sd, &csd) == (sint32)IfxSdmmc_Status_success)
        && sdmmc_csd_plausible(&csd))
    {
        s_cachedSectors = (DWORD)csd.capacity;
        s_cachedCsdVer  = csd.csd_ver;
        s_cachedValid   = TRUE;
        s_capSource     = 0;
        *sectors = s_cachedSectors;
        if (csdVer != NULL)
        {
            *csdVer = s_cachedCsdVer;
        }
        return 0;
    }
    /* Probe (also caches the result, so this costs ~1.5 s only once). */
    if (Sdmmc_ProbeCapacitySectors(sectors, NULL) != 0)
    {
        return -1;
    }
    s_cachedCsdVer = -1;    /* not from the CSD: probed */
    if (csdVer != NULL)
    {
        *csdVer = -1;
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
    Ifx_SDMMC        *p = g_Sdmmc_Mmc.sd.sdmmcSFR;
    IfxSdmmc_Response rsp;          /* MUST be a real buffer - see note below */
    uint32            spin;

    sdmmc_trace("REC-enter", (uint32)p->NORMAL_INT_STAT.U, (uint32)p->ERROR_INT_STAT.U);

    /* 1. try to abort whatever the card is doing (CMD12)
     *
     * BUG FIX (2026-09-21, round 7): this call used to pass NULL_PTR for the
     * response. IfxSdmmc_sendCommand() -> IfxSdmmc_readResponse() dereferences
     * it unconditionally for every command that is not CMD0/CMD1
     * (`response->resp01 = 0;`), so a successful CMD12 wrote to address 0 and
     * trapped (class 4 data access error): the whole MCU went silent, which
     * looked like `sd erase` / `sd mkfs` / `sd big` "hanging" with no output.
     * It only showed up when CMD12 actually completed; while the card is still
     * busy resetting its data lines CMD12 times out inside sendCommand(), the
     * response is never touched and the recovery continued normally - which is
     * why the read probe survived and the shell's own `sd recover` command
     * (which passes a real buffer) always worked. */
    (void)IfxSdmmc_sendCommand(p, IfxSdmmc_Command_stopTransmission,
                               (uint32)(g_Sdmmc_Mmc.sd.cardInfo.rca << 16),
                               IfxSdmmc_ResponseType_r1b, &rsp);

    /* 2. reset the data and command paths */
    p->SW_RST.B.SW_RST_DAT = 1U;
    p->SW_RST.B.SW_RST_CMD = 1U;
    for (spin = 0U; (spin < 1000000UL) &&
                    (p->SW_RST.B.SW_RST_DAT || p->SW_RST.B.SW_RST_CMD); spin++) { }
    for (spin = 0U; spin < 300000UL; spin++) { }
    sdmmc_clear_sticky(p);

    /* 3. full module re-initialisation (restores clock/bus width/DMA type and
     *    re-identifies the card) */
    sdmmc_trace("REC-reinit", spin, 0U);
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

    {
        IfxSdmmc_Status st = IfxSdmmc_Sd_initModule(&(g_Sdmmc_Mmc.sd), &config);

        /* Prime the data path with one read: without it the first WRITE after
           an initialisation always fails (see sdmmc_prime_datapath()). */
        if (st == IfxSdmmc_Status_success)
        {
            sdmmc_prime_datapath();
        }
        return st;
    }
}

/*-----------------------------------------------------------------------*/
/* Data-path priming                                                      */
/*-----------------------------------------------------------------------*/
/* FIX (2026-09-21, round 7): the first DATA transfer of a freshly
 * (re-)initialised host is only usable if it is a READ. A write issued as the
 * first data transfer fails deterministically: CMD25 returns success, but the
 * data phase never completes, IfxSdmmc_Sd_multiBlockAdma2Transfer() reports
 * IfxSdmmc_Status_dataError and every retry fails as well - because each retry
 * runs sdmmc_recover_datapath(), which re-initialises the host again and thus
 * recreates the very same condition. That is what made `sd mkfs` (whose first
 * transfer is a write), `sd erase` and a `sd big w` issued right after
 * `sd init` fail, while `sd big r` always worked. It also hid behind "reads
 * prime it", observed earlier with CMD9/R2.
 *
 * Fix: issue a throw-away 1-block read immediately after every successful
 * module initialisation, so the data path is always primed before any write.
 * Read-only, does not depend on the card content (LBA 0). */
IFX_ALIGN(8) static uint32 s_primeBuf[IFXSDMMC_BLOCK_SIZE_DEFAULT / 4U];

static void sdmmc_prime_datapath(void)
{
    IfxSdmmc_Status st = sdmmc_adma2_transfer_once((BYTE *)s_primeBuf, (LBA_t)0, 1U, TRUE);
    int tries;

    for (tries = 0; (st != IfxSdmmc_Status_success) && (tries < 2); tries++)
    {
        st = sdmmc_adma2_transfer_once((BYTE *)s_primeBuf, (LBA_t)0, 1U, TRUE);
    }
    sdmmc_trace("PRIME", (uint32)st, 0U);
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

/** \brief One ADMA2 multi-block command: build the (chained) descriptor table
 *  and run it. Returns the raw iLLD status. */
static IfxSdmmc_Status sdmmc_adma2_transfer_once(BYTE *buff, LBA_t sector, UINT count, boolean isRead)
{
    if (!sdmmc_build_adma2_descr((uint32 *)(uintptr_t)buff, count))
    {
        return IfxSdmmc_Status_failure;
    }
    return IfxSdmmc_Sd_multiBlockAdma2Transfer(&(g_Sdmmc_Mmc.sd),
                                               isRead ? IfxSdmmc_Command_readMultipleBLock
                                                      : IfxSdmmc_Command_writeMultipleBlock,
                                               (uint32)sector,
                                               IFXSDMMC_BLOCK_SIZE_DEFAULT,
                                               (uint32 *)s_adma2Descr,
                                               isRead ? IfxSdmmc_TransferDirection_read
                                                      : IfxSdmmc_TransferDirection_write,
                                               (uint32)count);
}

/** \brief Data transfer of \p count blocks with chunking + failure recovery.
 *
 *  A single ADMA2 command can describe at most IFXSDMMC_ADMA2_MAX_BLOCKS
 *  blocks (and, for testing, whatever Sdmmc_SetAdma2MaxBlocks() reduced that
 *  to). Larger requests are split into several multi-block commands here, so
 *  the disk layer accepts any size instead of returning RES_PARERR.
 *
 *  Each command is retried up to 3 times: the very first data transfer after a
 *  reset can fail once with the controller wedged (see
 *  sdmmc_recover_datapath()). Reads are idempotent, writing the same data
 *  twice is idempotent as well. */
static DRESULT sdmmc_transfer(BYTE *buff, LBA_t sector, UINT count, boolean isRead)
{
    s_lastChunkCount = 0U;

    while (count > 0U)
    {
        UINT limit = isRead ? s_maxReadBlocks : s_maxWriteBlocks;
        UINT n     = count;
        IfxSdmmc_Status st = IfxSdmmc_Status_failure;
        int tries;

        if ((limit == 0U) || (limit > IFXSDMMC_ADMA2_MAX_BLOCKS))
        {
            limit = IFXSDMMC_ADMA2_MAX_BLOCKS;
        }
        if (n > limit)
        {
            n = limit;
        }

        for (tries = 0; tries < 3; tries++)
        {
            Ifx_SDMMC *sp = g_Sdmmc_Mmc.sd.sdmmcSFR;

            sp->ADMA_ERR_STAT.U = 0U;   /* so a reported value belongs to this try */
            sdmmc_trace("XFER-try", ((uint32)n << 16) | (sector & 0xFFFFUL), (uint32)isRead);
            st = sdmmc_adma2_transfer_once(buff, sector, n, isRead);
            /* ADMA_ERR_STAT: bits[1:0] error state (0=stop, 1=FDS, 3=TFR),
               bits[3:2] error type (1=descriptor, 2=page/length mismatch) */
            sdmmc_trace("XFER-st", (uint32)st, (uint32)sp->ADMA_ERR_STAT.U);
            if (st == IfxSdmmc_Status_success)
            {
                break;
            }
            s_lastNistr  = (uint32)sp->NORMAL_INT_STAT.U;
            s_lastEistr  = (uint32)sp->ERROR_INT_STAT.U;
            s_lastPstate = (uint32)sp->PSTATE_REG.U;
            s_lastOp     = isRead ? 1U : 2U;
            s_lastSector = (uint32)sector;

            if (!sdmmc_recover_datapath())
            {
                break;
            }
        }
        if (st != IfxSdmmc_Status_success)
        {
            return RES_ERROR;
        }

        s_lastChunkCount++;
        buff   += ((UINT)512U * n);
        sector += n;
        count  -= n;
    }

    return RES_OK;
}

/*-----------------------------------------------------------------------*/
/* Capacity: CSD first, read-probe fallback                              */
/*-----------------------------------------------------------------------*/
/* This SDMMC IP never latches a 136-bit R2 response. Verified with the
 * `sd csd` shell command (see README §11.6): CMD9 completes with status
 * success and CMDREG shows index 9 with RESP_TYPE_SELECT = 01, yet
 * RESP01..RESP67 keep the value of the previous 48-bit R1 response.
 * 48-bit responses (CMD3 -> RCA, CMD13 -> card status) are captured
 * normally, and the result is identical with HOST_CTRL2.HOST_VER4_ENABLE
 * forced to 0 for the duration of the command, so the CSD simply cannot be
 * obtained on this silicon and a CSD-based capacity is impossible.
 *
 * The capacity is therefore measured from the card itself: for a block
 * addressed card (SDHC/SDXC) CMD17 is rejected once the LBA reaches the
 * capacity, so an exponential + binary search over the LBA space yields the
 * exact sector count with read-only traffic. Measured on this board/card:
 * reads up to 61000000 succeed, 61500000 and above fail, the controller
 * recovers after every failed probe and no card data is touched.
 *
 * The CSD path is still tried first and used whenever it yields a value that
 * is consistent with the identified card, so a future card/board combination
 * that does latch R2 keeps the exact standards-based answer. */
IFX_ALIGN(8) static uint32 s_probeBuf[IFXSDMMC_BLOCK_SIZE_DEFAULT / 4U];
static UINT s_lastProbeCount = 0U;

/** \brief Is a CSD-based capacity plausible for the identified card? */
static boolean sdmmc_csd_plausible(const sdmmc_csd_t *csd)
{
    boolean blockAddr = (boolean)((g_Sdmmc_Mmc.sd.cardCapacity & 0x08U) != 0U);

    /* SDHC/SDXC must report CSD v2.0 (and byte addressed cards must not). */
    if (blockAddr != (boolean)(csd->csd_ver == SD_CSD_CSDVER_2_0))
    {
        return FALSE;
    }
    if (csd->capacity < 128U)
    {
        return FALSE;
    }
    return TRUE;
}

/** \brief One read-only LBA probe: TRUE if the card answered with data. */
static boolean sdmmc_lba_ok(DWORD lba)
{
    IfxSdmmc_Status st = sdmmc_adma2_transfer_once((BYTE *)s_probeBuf, (LBA_t)lba, 1U, TRUE);

    if (st != IfxSdmmc_Status_success)
    {
        /* A rejected (out-of-range) read leaves a sticky error behind; give the
           controller one recovery + retry so that a merely transient failure is
           not mistaken for the end of the card. */
        if (!sdmmc_recover_datapath())
        {
            return FALSE;
        }
        st = sdmmc_adma2_transfer_once((BYTE *)s_probeBuf, (LBA_t)lba, 1U, TRUE);
    }
    return (boolean)(st == IfxSdmmc_Status_success);
}

/** \brief Sector count by read probing (see the comment above). 0 = ok.
 *  \param outProbes optional: number of CMD17 attempts used. */
sint32 Sdmmc_ProbeCapacitySectors(DWORD *sectors, UINT *outProbes)
{
    DWORD lo = 0UL;
    DWORD hi = 0UL;
    DWORD step;
    UINT  probes = 0U;

    if (!g_Sdmmc_Inited || (sectors == NULL))
    {
        return -1;
    }

    /* Exponential search: 512 MiB steps outward until a read fails. */
    step = 1024UL * 1024UL;                      /* 512 MiB = 1048576 sectors */
    while ((step != 0UL) && (hi == 0UL))
    {
        probes++;
        if (sdmmc_lba_ok(step))
        {
            lo   = step;
            step = (step > (0x7FFFFFFFUL / 2UL)) ? 0UL : (step * 2UL);
            if (step == 0UL)
            {
                hi = 0xFFFFFFFFUL;               /* 2T sectors: unreachable */
            }
        }
        else
        {
            hi = step;
        }
    }

    /* Binary search for the first unreadable LBA (== capacity in sectors). */
    while ((hi - lo) > 1UL)
    {
        DWORD mid = lo + ((hi - lo) / 2UL);
        probes++;
        if (sdmmc_lba_ok(mid))
        {
            lo = mid;
        }
        else
        {
            hi = mid;
        }
    }

    *sectors         = hi;
    s_cachedSectors  = hi;
    s_cachedValid    = TRUE;
    s_lastProbeCount = probes;
    s_capSource      = 1;
    if (outProbes != NULL)
    {
        *outProbes = probes;
    }
    return 0;
}

/** \brief 0 = capacity came from CMD9/CSD, 1 = from the read probe. */
int Sdmmc_GetCapacitySource(void)
{
    return s_capSource;
}

/** \brief CMD17 attempts used by the last capacity probe. */
UINT Sdmmc_GetLastProbeCount(void)
{
    return s_lastProbeCount;
}

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

    return sdmmc_transfer(buff, sector, count, TRUE);
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

    return sdmmc_transfer((BYTE *)(uintptr_t)buff, sector, count, FALSE);
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
            else if ((sdmmc_send_cmd_send_csd(&(g_Sdmmc_Mmc.sd), &csd) == (sint32)IfxSdmmc_Status_success)
                     && sdmmc_csd_plausible(&csd))
            {
                *(LBA_t *)buff = (LBA_t)(DWORD)csd.capacity;
                s_cachedSectors = (DWORD)csd.capacity;
                s_cachedValid = TRUE;
                s_capSource = 0;
                res = RES_OK;
            }
            /* This IP does not latch the 136-bit R2 response, so the CSD is
               normally unusable here (see Sdmmc_ProbeCapacitySectors()). */
            else if (Sdmmc_ProbeCapacitySectors(&s_cachedSectors, NULL) == 0)
            {
                *(LBA_t *)buff = (LBA_t)s_cachedSectors;
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

/*-----------------------------------------------------------------------*/
/* Diagnostics helpers (shell `sd big` / `sd lim` / `sd clk`)             */
/*-----------------------------------------------------------------------*/

/** \brief Blocks described by one ADMA2 command at most (chained table). */
UINT Sdmmc_GetAdma2TableBlocks(void)
{
    return IFXSDMMC_ADMA2_MAX_BLOCKS;
}

/** \brief Runtime cap on the blocks per read command (0 / out of range
 *  restored the per-direction default). */
void Sdmmc_SetAdma2MaxBlocks(UINT blocks)
{
    if ((blocks == 0U) || (blocks > IFXSDMMC_ADMA2_MAX_BLOCKS))
    {
        s_maxReadBlocks  = SDMMC_READ_MAX_BLOCKS;
        s_maxWriteBlocks = SDMMC_WRITE_MAX_BLOCKS;
    }
    else
    {
        s_maxReadBlocks  = blocks;
        s_maxWriteBlocks = blocks;
    }
}

UINT Sdmmc_GetAdma2MaxBlocks(void)
{
    return s_maxReadBlocks;
}

/** \brief Runtime cap on the blocks per single WRITE command. */
UINT Sdmmc_GetAdma2MaxWriteBlocks(void)
{
    return s_maxWriteBlocks;
}

/** \brief Address of the chained descriptor table (alignment diagnostics). */
uint32 Sdmmc_GetAdma2DescrAddr(void)
{
    return (uint32)(uintptr_t)&s_adma2Descr[0];
}

/** \brief Descriptor/hop/chunk statistics of the last executed transfer. */
void Sdmmc_GetLastTransferInfo(UINT *descr, UINT *links, UINT *chunks)
{
    if (descr  != NULL) { *descr  = s_lastDescrCount; }
    if (links  != NULL) { *links  = s_lastLinkCount;  }
    if (chunks != NULL) { *chunks = s_lastChunkCount; }
}

/** \brief Current SD clock in Hz.
 *
 *  Both the software divider and the hardware preset values end up in
 *  CLK_CTRL.FREQ_SEL, and this IP divides as
 *      SDCLK = base / (2 * (FREQ_SEL_10bit + 1))
 *  - which is exactly what IfxSdmmc_configureClock() programs, and what the
 *  IP's own hardwired presets encode (read out of the presets on this board,
 *  base clock = 100 MHz):
 *      PRESET_INIT FREQ_SEL_VAL=127 -> 390.6 kHz (card identification)
 *      PRESET_DS   FREQ_SEL_VAL=1   ->  25.0 MHz (default speed limit)
 *      PRESET_HS   FREQ_SEL_VAL=0   ->  50.0 MHz (high speed limit)
 *  In high-speed preset mode (the default here) the clock is therefore
 *  50 MHz, not the 25 MHz that an IfxSdmmc_configureClock(25 MHz) call would
 *  suggest: with HOST_CTRL2.PRESET_VAL_ENABLE = 1 the controller uses the
 *  preset divider and ignores the value written by software. */
uint32 Sdmmc_GetSdClockHz(void)
{
    Ifx_SDMMC *p = &MODULE_SDMMC0;
    uint32 baseHz = (uint32)p->CAPABILITIES1.B.BASE_CLK_FREQ * 1000000U;
    uint32 div;

    if (baseHz == 0U)
    {
        baseHz = 100000000U;    /* SPB = 100 MHz on this board */
    }
    div = ((uint32)p->CLK_CTRL.B.UPPER_FREQ_SEL << 8) | (uint32)p->CLK_CTRL.B.FREQ_SEL;
    return baseHz / (2U * (div + 1U));
}
