/**
 * \file shell_sd.c
 * \brief Letter-Shell SD card test commands (SDMMC0 + FatFs R0.16).
 *
 * One `sd` command with subcommands covering all common SD card tests:
 *  init/info/cd/ls/cat/stat/write/read/rm/mkdir/bench/mkfs/erase/raw/label/free.
 *
 * Wiring: CMD P15.3 / CLK P15.1 / DAT0 P20.7 / DAT1 P20.8 / DAT2 P20.10 /
 *         DAT3 P20.11 / CD P10.7. 128GB TF card -> exFAT (SDXC); FAT32 optional.
 */
#include "shell.h"
#include "IfxPort.h"
#include "IfxStm.h"
#include "ff.h"
#include "diskio.h"
#include "mmc_sdmmc.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern volatile uint32 g_TickCount_1ms;

/* Card-detect pin: P10.7 input pull-up (low = card inserted, typical switch to GND) */
#define SD_CD_MODULE    (&MODULE_P10)
#define SD_CD_PIN       7

/* 8 KB DMA-able transfer buffer (word aligned for SDMA uint32* casts) */
static uint32 s_ioBuf[2048];
/* f_mkfs work area: DWORD-aligned for SDMA uint32* access */
static DWORD s_mkfsWork[FF_MAX_SS / 4];

static FATFS s_fs;
static boolean s_mounted = FALSE;

/* ---------------- helpers ---------------- */

static const char *sd_fr_str(FRESULT fr)
{
    switch (fr)
    {
        case FR_OK: return "OK";
        case FR_DISK_ERR: return "DISK_ERR(hard error in low level disk I/O)";
        case FR_INT_ERR: return "INT_ERR(assertion failed)";
        case FR_NOT_READY: return "NOT_READY(physical drive cannot work)";
        case FR_NO_FILE: return "NO_FILE(not found)";
        case FR_NO_PATH: return "NO_PATH(path not found)";
        case FR_INVALID_NAME: return "INVALID_NAME(bad path format)";
        case FR_DENIED: return "DENIED(access denied/dir full)";
        case FR_EXIST: return "EXIST(already exists)";
        case FR_INVALID_OBJECT: return "INVALID_OBJECT(bad file/dir object)";
        case FR_WRITE_PROTECTED: return "WRITE_PROTECTED";
        case FR_INVALID_DRIVE: return "INVALID_DRIVE(bad logical drive)";
        case FR_NOT_ENABLED: return "NOT_ENABLED(no work area)";
        case FR_NO_FILESYSTEM: return "NO_FILESYSTEM(no valid FAT/exFAT volume)";
        case FR_MKFS_ABORTED: return "MKFS_ABORTED";
        case FR_TIMEOUT: return "TIMEOUT";
        case FR_LOCKED: return "LOCKED(file sharing policy)";
        case FR_NOT_ENOUGH_CORE: return "NOT_ENOUGH_CORE(LFN buffer)";
        case FR_TOO_MANY_OPEN_FILES: return "TOO_MANY_OPEN_FILES";
        case FR_INVALID_PARAMETER: return "INVALID_PARAMETER";
        default: return "???";
    }
}

static const char *sd_card_type_str(IfxSdmmc_SdCardType t)
{
    switch (t)
    {
        case IfxSdmmc_SdCardType_io: return "SDIO(io)";
        case IfxSdmmc_SdCardType_mem: return "SDmem";
        case IfxSdmmc_SdCardType_combo: return "COMBO";
        default: return "?";
    }
}

static const char *sd_card_cap_str(uint8 cap)
{
    /* IfxSdmmc_SdCardCapacity bit flags */
    if (cap & 0x8) /* blockAddressing */
    {
        return (cap & 0x4) ? "SDHC/SDXC(block)" : "block";
    }
    if (cap & 0x1)
    {
        return "SDSC(v2)";
    }
    if (cap & 0x2)
    {
        return "SDSC(v1x)";
    }
    return "?";
}

static const char *sd_fs_type_str(BYTE fs_type)
{
    switch (fs_type)
    {
        case FS_FAT12: return "FAT12";
        case FS_FAT16: return "FAT16";
        case FS_FAT32: return "FAT32";
        case FS_EXFAT: return "exFAT";
        default: return "?";
    }
}

static int sd_cd_level(void)
{
    return (int)IfxPort_getPinState(SD_CD_MODULE, SD_CD_PIN);
}

static void sd_print_capacity(Shell *sh)
{
    DWORD sectors = 0;
    int csdVer = -1;
    if (Sdmmc_GetCapacitySectors(&sectors, &csdVer) == 0)
    {
        uint64 mib = ((uint64)sectors * 512U) / (1024U * 1024U);
        shellPrint(sh, "Capacity: %lu sectors x 512B = %llu MiB (~%.1f GiB), CSD v%d\r\n",
                   (unsigned long)sectors, (unsigned long long)mib,
                   (double)mib / 1024.0, csdVer);
    }
    else
    {
        shellPrint(sh, "Capacity: CMD9/CSD read failed\r\n");
    }
}

static void sd_print_fs(Shell *sh)
{
    DWORD freeClusters = 0;
    FATFS *pfs = NULL;
    FRESULT fr = f_getfree("", &freeClusters, &pfs);
    if ((fr == FR_OK) && (pfs != NULL))
    {
#if FF_FS_EXFAT
        DWORD freeKB;
        if (pfs->fs_type == FS_EXFAT)
        {
            /* exFAT: csize is in sectors */
            freeKB = freeClusters * (pfs->csize / 2U);
        }
        else
#endif
        {
            freeKB = freeClusters * (pfs->csize / 2U);
        }
        shellPrint(sh, "FS: %s, cluster=%lu sectors, free=%lu clusters (~%lu MiB)\r\n",
                   sd_fs_type_str(pfs->fs_type), (unsigned long)pfs->csize,
                   (unsigned long)freeClusters, (unsigned long)(freeKB / 1024U));
    }
    else
    {
        shellPrint(sh, "FS free: f_getfree -> %s (%d)\r\n", sd_fr_str(fr), (int)fr);
    }
}

static void sd_print_info(Shell *sh)
{
    IfxSdmmc_Sd *h = Sdmmc_GetHandle();
    shellPrint(sh, "CD P10.7 : %d (%s)\r\n", sd_cd_level(),
               sd_cd_level() ? "HIGH(no card?)" : "LOW(card inserted, typical)");
    if (!Sdmmc_IsInited())
    {
        shellPrint(sh, "SDMMC: not initialized (run 'sd init')\r\n");
        return;
    }
    shellPrint(sh, "SDMMC: RCA=0x%04X state=0x%02X type=%s cap=%s(0x%02X)\r\n",
               (unsigned)h->cardInfo.rca, (unsigned)h->cardState,
               sd_card_type_str(h->cardType),
               sd_card_cap_str(h->cardCapacity), (unsigned)h->cardCapacity);
    shellPrint(sh, "Pins: CMD P15.3 / CLK P15.1 / DAT0-3 P20.7/P20.8/P20.10/P20.11, 4-bit HS SDMA 25MHz\r\n");
    sd_print_capacity(sh);
    if (s_mounted)
    {
        sd_print_fs(sh);
    }
    else
    {
        shellPrint(sh, "FS: not mounted (run 'sd init')\r\n");
    }
}

/* Fill buffer with deterministic pattern: word[i] = i ^ seed */
static void sd_fill_pattern(uint32 *buf, uint32 nWords, uint32 seed)
{
    uint32 i;
    for (i = 0; i < nWords; i++)
    {
        buf[i] = i ^ seed;
    }
}

static uint32 sd_now_ms(void)
{
    return g_TickCount_1ms;
}

/* ---------------- subcommands ---------------- */

static int sd_cmd_init(Shell *sh)
{
    DSTATUS ds;
    FRESULT fr;

    shellPrint(sh, "sd init: CD P10.7=%d ...\r\n", sd_cd_level());
    ds = disk_initialize(0);
    shellPrint(sh, "disk_initialize(0) -> 0x%02X%s\r\n", (unsigned)ds,
               (ds & STA_NOINIT) ? " (NOT READY - no card?)" : "");
    if (ds & STA_NOINIT)
    {
        return -1;
    }
    fr = f_mount(&s_fs, "", 1);
    shellPrint(sh, "f_mount -> %s (%d)\r\n", sd_fr_str(fr), (int)fr);
    if (fr != FR_OK)
    {
        s_mounted = FALSE;
        return -1;
    }
    s_mounted = TRUE;
    sd_print_info(sh);
    return 0;
}

static int sd_cmd_ls(Shell *sh, const char *path)
{
    DIR dir;
    FILINFO fno;
    FRESULT fr;
    int n = 0;

    if (!s_mounted)
    {
        shellPrint(sh, "not mounted (run 'sd init')\r\n");
        return -1;
    }
    fr = f_opendir(&dir, path);
    if (fr != FR_OK)
    {
        shellPrint(sh, "f_opendir('%s') -> %s (%d)\r\n", path, sd_fr_str(fr), (int)fr);
        return -1;
    }
    shellPrint(sh, "ls %s:\r\n", path);
    for (;;)
    {
        fr = f_readdir(&dir, &fno);
        if ((fr != FR_OK) || (fno.fname[0] == 0))
        {
            break;
        }
        shellPrint(sh, "  %c %10lu  %s\r\n",
                   (fno.fattrib & AM_DIR) ? 'd' : '-',
                   (unsigned long)fno.fsize, fno.fname);
        if (++n >= 64)
        {
            shellPrint(sh, "  ... (truncated at 64 entries)\r\n");
            break;
        }
    }
    f_closedir(&dir);
    shellPrint(sh, "%d entries\r\n", n);
    return (fr == FR_OK) ? 0 : -1;
}

static int sd_cmd_cat(Shell *sh, const char *path, uint32 maxBytes)
{
    FIL f;
    FRESULT fr;
    UINT br = 0;
    uint32 total = 0;

    if (!s_mounted)
    {
        shellPrint(sh, "not mounted (run 'sd init')\r\n");
        return -1;
    }
    fr = f_open(&f, path, FA_READ);
    if (fr != FR_OK)
    {
        shellPrint(sh, "f_open('%s') -> %s (%d)\r\n", path, sd_fr_str(fr), (int)fr);
        return -1;
    }
    shellPrint(sh, "cat %s (size %lu, show max %lu):\r\n",
               path, (unsigned long)f_size(&f), (unsigned long)maxBytes);
    while (total < maxBytes)
    {
        uint32 want = sizeof(s_ioBuf);
        if (want > maxBytes - total)
        {
            want = maxBytes - total;
        }
        fr = f_read(&f, s_ioBuf, want, &br);
        if ((fr != FR_OK) || (br == 0))
        {
            break;
        }
        {
            /* dump as text, non-printables as '.' */
            uint32 i;
            char *p = (char *)s_ioBuf;
            for (i = 0; i < br; i++)
            {
                char c = p[i];
                char o;
                if ((c == '\n') || (c == '\r'))
                {
                    o = c;
                }
                else if ((c < 0x20) || (c > 0x7e))
                {
                    o = '.';
                }
                else
                {
                    o = c;
                }
                shellPrint(sh, "%c", o);
            }
        }
        total += br;
        if (br < want)
        {
            break; /* EOF */
        }
    }
    f_close(&f);
    shellPrint(sh, "\r\n-- %lu bytes shown, fr=%s --\r\n", (unsigned long)total, sd_fr_str(fr));
    return (fr == FR_OK) ? 0 : -1;
}

/* write file with pattern; report speed */
static int sd_cmd_write(Shell *sh, const char *path, uint32 sizeKB, uint32 seed)
{
    FIL f;
    FRESULT fr;
    UINT bw = 0;
    uint32 remain, chunk;
    uint32 t0, ms;
    uint32 total = 0;

    if (!s_mounted)
    {
        shellPrint(sh, "not mounted (run 'sd init')\r\n");
        return -1;
    }
    fr = f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK)
    {
        shellPrint(sh, "f_open('%s',W) -> %s (%d)\r\n", path, sd_fr_str(fr), (int)fr);
        return -1;
    }
    remain = sizeKB * 1024U;
    t0 = sd_now_ms();
    while (remain > 0)
    {
        chunk = (remain > sizeof(s_ioBuf)) ? sizeof(s_ioBuf) : remain;
        /* pattern word[i] = (chunkOffsetWords + i) ^ seed (matches sd read verify) */
        {
            uint32 offW = total / 4U;
            uint32 i;
            for (i = 0; i < chunk / 4U; i++)
            {
                s_ioBuf[i] = (offW + i) ^ seed;
            }
        }
        fr = f_write(&f, s_ioBuf, chunk, &bw);
        if ((fr != FR_OK) || (bw != chunk))
        {
            break;
        }
        total += bw;
        remain -= bw;
        if ((total & 0xFFFFF) == 0)
        {
            shellPrint(sh, "  ... %lu KB\r\n", (unsigned long)(total / 1024U));
        }
    }
    f_close(&f);
    ms = sd_now_ms() - t0;
    if ((fr == FR_OK) && (remain == 0))
    {
        shellPrint(sh, "write %s %lu KB seed=0x%08lX OK in %lu ms -> %lu KB/s\r\n",
                   path, (unsigned long)(total / 1024U), (unsigned long)seed,
                   (unsigned long)ms,
                   (unsigned long)((ms == 0) ? 0 : (total / 1024U * 1000U / ms)));
        return 0;
    }
    shellPrint(sh, "write FAILED at %lu/%lu KB: %s (%d)\r\n",
               (unsigned long)(total / 1024U),
               (unsigned long)sizeKB, sd_fr_str(fr), (int)fr);
    return -1;
}

/* read file, verify pattern written by sd write with same seed */
static int sd_cmd_read(Shell *sh, const char *path, uint32 seed)
{
    FIL f;
    FRESULT fr;
    UINT br = 0;
    uint32 total = 0, badIdx = 0;
    uint32 t0, ms;
    int bad = 0;

    if (!s_mounted)
    {
        shellPrint(sh, "not mounted (run 'sd init')\r\n");
        return -1;
    }
    fr = f_open(&f, path, FA_READ);
    if (fr != FR_OK)
    {
        shellPrint(sh, "f_open('%s',R) -> %s (%d)\r\n", path, sd_fr_str(fr), (int)fr);
        return -1;
    }
    t0 = sd_now_ms();
    for (;;)
    {
        fr = f_read(&f, s_ioBuf, sizeof(s_ioBuf), &br);
        if ((fr != FR_OK) || (br == 0))
        {
            break;
        }
        {
            uint32 offW = total / 4U;
            uint32 i;
            for (i = 0; i < br / 4U; i++)
            {
                uint32 expect = (offW + i) ^ seed;
                if (((uint32 *)s_ioBuf)[i] != expect)
                {
                    badIdx = offW + i;
                    bad = 1;
                    break;
                }
            }
        }
        total += br;
        if (bad)
        {
            break;
        }
        if (br < sizeof(s_ioBuf))
        {
            break; /* EOF */
        }
    }
    f_close(&f);
    ms = sd_now_ms() - t0;
    if ((fr == FR_OK) && !bad)
    {
        shellPrint(sh, "read %s %lu KB seed=0x%08lX VERIFY-OK in %lu ms -> %lu KB/s\r\n",
                   path, (unsigned long)(total / 1024U), (unsigned long)seed,
                   (unsigned long)ms,
                   (unsigned long)((ms == 0) ? 0 : (total / 1024U * 1000U / ms)));
        return 0;
    }
    if (bad)
    {
        shellPrint(sh, "read %s VERIFY-FAIL at word %lu\r\n", path, (unsigned long)badIdx);
    }
    else
    {
        shellPrint(sh, "read FAILED: %s (%d)\r\n", sd_fr_str(fr), (int)fr);
    }
    return -1;
}

/* sequential bench: write + read + verify, default 4MB */
static int sd_cmd_bench(Shell *sh, uint32 sizeMB)
{
    const char *path = "0:/BENCH.BIN";
    const uint32 seed = 0xA5A55A5AU;
    uint32 sizeKB = sizeMB * 1024U;
    int rc;

    if ((sizeMB == 0) || (sizeMB > 512))
    {
        shellPrint(sh, "bench size 1..512 MB\r\n");
        return -1;
    }
    shellPrint(sh, "bench %lu MB (%s)...\r\n", (unsigned long)sizeMB, path);
    rc = sd_cmd_write(sh, path, sizeKB, seed);
    if (rc != 0)
    {
        return rc;
    }
    rc = sd_cmd_read(sh, path, seed);
    return rc;
}

static int sd_cmd_mkfs(Shell *sh, const char *fstype, uint32 au_shift)
{
    MKFS_PARM opt;
    FRESULT fr;

    if (!Sdmmc_IsInited())
    {
        shellPrint(sh, "SD not initialized (run 'sd init' first so CMD/RCA are up)\r\n");
        return -1;
    }
    memset(&opt, 0, sizeof(opt));
    if (strcmp(fstype, "exfat") == 0)
    {
#if FF_FS_EXFAT == 0
        shellPrint(sh, "exFAT not enabled in ffconf.h\r\n");
        return -1;
#else
        opt.fmt = FM_EXFAT;
#endif
    }
    else if (strcmp(fstype, "fat32") == 0)
    {
        opt.fmt = FM_FAT32;
    }
    else
    {
        shellPrint(sh, "usage: sd mkfs <exfat|fat32> [au_shift]\r\n");
        return -1;
    }
    if (au_shift > 24)
    {
        shellPrint(sh, "au_shift 0..24\r\n");
        return -1;
    }
    opt.au_size = (au_shift == 0) ? 0 : (1UL << au_shift);

    shellPrint(sh, "WARNING: f_mkfs(%s) destroys ALL data on the card. formatting...\r\n", fstype);
    {
        uint32 t0 = sd_now_ms();
        /* unmount first */
        f_mount(NULL, "", 0);
        s_mounted = FALSE;
        fr = f_mkfs("", &opt, s_mkfsWork, sizeof(s_mkfsWork));
        shellPrint(sh, "f_mkfs -> %s (%d) in %lu ms\r\n",
                   sd_fr_str(fr), (int)fr, (unsigned long)(sd_now_ms() - t0));
        if (fr != FR_OK)
        {
            return -1;
        }
        fr = f_mount(&s_fs, "", 1);
        shellPrint(sh, "remount -> %s (%d)\r\n", sd_fr_str(fr), (int)fr);
        if (fr != FR_OK)
        {
            return -1;
        }
        s_mounted = TRUE;
    }
    sd_print_fs(sh);
    return 0;
}

static int sd_cmd_raw(Shell *sh, const char *op, DWORD lba, UINT count)
{
    DRESULT dr;
    UINT i;

    if (!Sdmmc_IsInited())
    {
        shellPrint(sh, "SD not initialized (run 'sd init')\r\n");
        return -1;
    }
    if ((count == 0) || (count > 16))
    {
        shellPrint(sh, "count 1..16\r\n");
        return -1;
    }
    if (strcmp(op, "r") == 0)
    {
        dr = disk_read(0, (BYTE *)s_ioBuf, lba, count);
        shellPrint(sh, "disk_read(lba=%lu,n=%u) -> %d\r\n", (unsigned long)lba, count, (int)dr);
        if (dr != RES_OK)
        {
            return -1;
        }
        for (i = 0; i < 64 && i < count * 512U; i++)
        {
            if ((i % 16) == 0)
            {
                shellPrint(sh, "\r\n%08lX: ", (unsigned long)(lba * 512U + i));
            }
            shellPrint(sh, "%02X ", ((BYTE *)s_ioBuf)[i]);
        }
        shellPrint(sh, "\r\n(showing first 64 of %u bytes)\r\n", count * 512U);
        return 0;
    }
    if (strcmp(op, "w") == 0)
    {
        /* raw write: pattern fill (destructive!) */
        sd_fill_pattern(s_ioBuf, sizeof(s_ioBuf) / 4U, lba);
        dr = disk_write(0, (const BYTE *)s_ioBuf, lba, count);
        shellPrint(sh, "disk_write(lba=%lu,n=%u,pattern) -> %d\r\n",
                   (unsigned long)lba, count, (int)dr);
        return (dr == RES_OK) ? 0 : -1;
    }
    shellPrint(sh, "usage: sd raw <r|w> <lba> [count]\r\n");
    return -1;
}

static int sd_cmd_erase(Shell *sh, DWORD lba, UINT count)
{
    DRESULT dr;
    UINT n, chunk;
    uint32 total = 0;
    uint32 t0;

    if (!Sdmmc_IsInited())
    {
        shellPrint(sh, "SD not initialized (run 'sd init')\r\n");
        return -1;
    }
    if ((count == 0) || (count > 131072U))
    {
        shellPrint(sh, "count 1..131072 sectors (max 64MB per call)\r\n");
        return -1;
    }
    memset(s_ioBuf, 0, sizeof(s_ioBuf));
    shellPrint(sh, "erase: zero-fill lba=%lu n=%u ...\r\n", (unsigned long)lba, count);
    t0 = sd_now_ms();
    n = count;
    while (n > 0)
    {
        chunk = (n > 16U) ? 16U : n;
        dr = disk_write(0, (const BYTE *)s_ioBuf, lba + total, chunk);
        if (dr != RES_OK)
        {
            shellPrint(sh, "erase FAILED at +%lu: %d\r\n", (unsigned long)total, (int)dr);
            return -1;
        }
        total += chunk;
        n -= chunk;
    }
    shellPrint(sh, "erase OK %u sectors in %lu ms\r\n", count, (unsigned long)(sd_now_ms() - t0));
    return 0;
}

/* ---------------- diagnostics: register dump + transfer experiments ---------------- */

/* second buffer for verify-after-write */
static uint32 s_chkBuf[128];

static const char *sd_pstate_extra(uint32 ps, char *buf, int n)
{
    /* decode PSTATE_REG per IfxSdmmc_regdef.h */
    (void)snprintf(buf, (size_t)n,
                   "CMD_INH=%u CMD_INH_DAT=%u DAT_ACT=%u WR_XFER=%u RD_XFER=%u BUFWR=%u BUFRD=%u\r\n"
                   "CARD_INS=%u STABLE=%u CD=%u WP=%u DAT3_0=%X CMD_LVL=%u CMD_ISSUE_ERR=%u",
                   (unsigned)(ps & 1U), (unsigned)((ps >> 1) & 1U), (unsigned)((ps >> 2) & 1U),
                   (unsigned)((ps >> 8) & 1U), (unsigned)((ps >> 9) & 1U),
                   (unsigned)((ps >> 10) & 1U), (unsigned)((ps >> 11) & 1U),
                   (unsigned)((ps >> 16) & 1U), (unsigned)((ps >> 17) & 1U),
                   (unsigned)((ps >> 18) & 1U), (unsigned)((ps >> 19) & 1U),
                   (unsigned)((ps >> 20) & 0xFU), (unsigned)((ps >> 24) & 1U),
                   (unsigned)((ps >> 27) & 1U));
    return buf;
}

static void sd_regdump(Shell *sh)
{
    Ifx_SDMMC *p = &MODULE_SDMMC0;
    uint32 c1 = p->CAPABILITIES1.U;
    uint32 c2 = p->CAPABILITIES2.U;
    char dec[220];

    shellPrint(sh, "ID=0x%08lX CLC=0x%08lX VERS=0x%04X MSHCID=0x%08lX MSHCTYP=0x%08lX\r\n",
               (unsigned long)p->ID.U, (unsigned long)p->CLC.U, (unsigned)p->HOST_CNTRL_VERS.U,
               (unsigned long)p->MSHC_VER_ID.U, (unsigned long)p->MSHC_VER_TYPE.U);
    shellPrint(sh, "CAP1=0x%08lX CAP2=0x%08lX MBIU=0x%08lX\r\n",
               (unsigned long)c1, (unsigned long)c2, (unsigned long)p->MBIU_CTRL.U);
    shellPrint(sh, "  BASE_CLK=%luMHz SDMA_SUP=%u ADMA2_SUP=%u HS_SUP=%u MAXBLK=%u TOUTCLK=%lu CLKMUL=%u\r\n",
               (unsigned long)((c1 >> 8) & 0xFFU), (unsigned)((c1 >> 22) & 1U),
               (unsigned)((c1 >> 19) & 1U), (unsigned)((c1 >> 21) & 1U),
               (unsigned)((c1 >> 16) & 3U), (unsigned long)(c1 & 0x3FU),
               (unsigned)((c2 >> 15) & 1U));
    shellPrint(sh, "HOST1=0x%02X(DMA_SEL=%u WIDTH=%u HS=%u EXT=%u)\r\n",
               (unsigned)p->HOST_CTRL1.U, (unsigned)((p->HOST_CTRL1.U >> 3) & 3U),
               (unsigned)((p->HOST_CTRL1.U >> 1) & 1U), (unsigned)((p->HOST_CTRL1.U >> 2) & 1U),
               (unsigned)((p->HOST_CTRL1.U >> 5) & 1U));
    shellPrint(sh, "HOST2=0x%04X(V4=%u ADDR64=%u PRESET=%u UHSMODE=%u DRVSTR=%u SMPCLK=%u)\r\n",
               (unsigned)p->HOST_CTRL2.U, (unsigned)((p->HOST_CTRL2.U >> 12) & 1U),
               (unsigned)((p->HOST_CTRL2.U >> 13) & 1U), (unsigned)((p->HOST_CTRL2.U >> 15) & 1U),
               (unsigned)(p->HOST_CTRL2.U & 7U), (unsigned)((p->HOST_CTRL2.U >> 4) & 3U),
               (unsigned)((p->HOST_CTRL2.U >> 7) & 1U));
    shellPrint(sh, "CLKCTL=0x%04X PWR=0x%02X TOUT=0x%02X BGAP=0x%02X PRESET_HS=0x%04X\r\n",
               (unsigned)p->CLK_CTRL.U, (unsigned)p->PWR_CTRL.U, (unsigned)p->TOUT_CTRL.U,
               (unsigned)p->BGAP_CTRL.U, (unsigned)p->PRESET_HS.U);
    shellPrint(sh, "BLKSIZE=0x%04X BLKCNT=0x%04X XFER=0x%04X SDMASA=0x%08lX ADMASA=0x%08lX\r\n",
               (unsigned)p->BLOCKSIZE.U, (unsigned)p->BLOCKCOUNT.U, (unsigned)p->XFER_MODE.U,
               (unsigned long)p->SDMASA.U, (unsigned long)p->ADMA_SA_LOW.U);
    shellPrint(sh, "NISTR=0x%04X EISTR=0x%04X NISTR_EN=0x%04X EISTR_EN=0x%04X\r\n",
               (unsigned)p->NORMAL_INT_STAT.U, (unsigned)p->ERROR_INT_STAT.U,
               (unsigned)p->NORMAL_INT_STAT_EN.U, (unsigned)p->ERROR_INT_STAT_EN.U);
    shellPrint(sh, "AUTOCMD=0x%04X ADMAERR=0x%02X PSTATE=0x%08lX\r\n",
               (unsigned)p->AUTO_CMD_STAT.U, (unsigned)p->ADMA_ERR_STAT.U,
               (unsigned long)p->PSTATE_REG.U);
    shellPrint(sh, "%s\r\n", sd_pstate_extra(p->PSTATE_REG.U, dec, (int)sizeof(dec)));
    shellPrint(sh, "buf s_ioBuf=0x%08lX s_chkBuf=0x%08lX\r\n",
               (unsigned long)(uintptr_t)s_ioBuf, (unsigned long)(uintptr_t)s_chkBuf);
}

/* strict single-block PIO write: gate each word with PSTATE.BUF_WR_ENABLE */
static int sd_pio_write(Shell *sh, DWORD lba, const uint32 *data)
{
    Ifx_SDMMC        *p = &MODULE_SDMMC0;
    IfxSdmmc_Response rsp;
    IfxSdmmc_Status   st;
    uint32            i;

    if (p->PSTATE_REG.B.CMD_INHIBIT_DAT)
    {
        shellPrint(sh, "  pre: CMD_INHIBIT_DAT=1 -> SW_RST_DAT\r\n");
        p->SW_RST.B.SW_RST_DAT = 1U;
        while (p->SW_RST.B.SW_RST_DAT) { }
    }
    p->NORMAL_INT_STAT.U = 0xFFFFU;
    p->ERROR_INT_STAT.U  = 0xFFFFU;

    p->BLOCKSIZE.B.XFER_BLOCK_SIZE = 512U;
    p->BLOCKSIZE.B.SDMA_BUF_BDARY  = 0U;
    p->BLOCKCOUNT.B.BLOCK_CNT      = 1U;
    p->XFER_MODE.U                 = 0U;   /* clears DMA_ENABLE / MULTI_BLK_SEL / ... */
    p->XFER_MODE.B.DATA_XFER_DIR   = 0U;   /* host -> card */

    shellPrint(sh, "  pre  PSTATE=0x%08lX XFER=0x%04X BLKSIZE=0x%04X\r\n",
               (unsigned long)p->PSTATE_REG.U, (unsigned)p->XFER_MODE.U, (unsigned)p->BLOCKSIZE.U);

    st = IfxSdmmc_sendCommand(p, IfxSdmmc_Command_writeBlock, (uint32)lba,
                              IfxSdmmc_ResponseType_r1, &rsp);
    if (st != IfxSdmmc_Status_success)
    {
        shellPrint(sh, "  CMD24 st=%d PSTATE=0x%08lX EISTR=0x%04X NISTR=0x%04X\r\n",
                   (int)st, (unsigned long)p->PSTATE_REG.U,
                   (unsigned)p->ERROR_INT_STAT.U, (unsigned)p->NORMAL_INT_STAT.U);
        return -1;
    }
    shellPrint(sh, "  CMD24 ok R1=0x%08lX PSTATE=0x%08lX BUFWR=%u\r\n",
               (unsigned long)rsp.cardStatus.U, (unsigned long)p->PSTATE_REG.U,
               (unsigned)p->PSTATE_REG.B.BUF_WR_ENABLE);

    {
        uint32 datAnd = 0xFFFFFFFFUL, datOr = 0U, datChg = 0U, datLast = 0xFFFFFFFFUL;
        for (i = 0; i < 128U; i++)
        {
            uint32 to = 4000000UL;
            while ((p->PSTATE_REG.B.BUF_WR_ENABLE == 0U) && (to > 0U)) { to--; }
            if (to == 0U)
            {
                shellPrint(sh, "  word %lu: BUF_WR_ENABLE timeout PSTATE=0x%08lX EISTR=0x%04X NISTR=0x%04X\r\n",
                           (unsigned long)i, (unsigned long)p->PSTATE_REG.U,
                           (unsigned)p->ERROR_INT_STAT.U, (unsigned)p->NORMAL_INT_STAT.U);
                return -2;
            }
            p->BUF_DATA.U = data[i];
            {
                uint32 d = (p->PSTATE_REG.U >> 20) & 0xFU;
                if (d != datLast) { datChg++; datLast = d; }
                datAnd &= d; datOr |= d;
            }
        }
        /* keep sampling while the FIFO shifts out onto the DAT lines */
        {
            uint32 n;
            for (n = 0; n < 300000UL; n++)
            {
                uint32 d = (p->PSTATE_REG.U >> 20) & 0xFU;
                if (d != datLast) { datChg++; datLast = d; }
                datAnd &= d; datOr |= d;
            }
        }
        shellPrint(sh, "  pushed: DAT and=0x%lX or=0x%lX chg=%lu (and==or==0xF => never driven)\r\n",
                   (unsigned long)datAnd, (unsigned long)datOr, (unsigned long)datChg);
        shellPrint(sh, "  post PSTATE=0x%08lX NISTR=0x%04X EISTR=0x%04X\r\n",
                   (unsigned long)p->PSTATE_REG.U,
                   (unsigned)p->NORMAL_INT_STAT.U, (unsigned)p->ERROR_INT_STAT.U);
    }

    {
        uint32 to = 40000000UL;
        while ((p->NORMAL_INT_STAT.B.XFER_COMPLETE == 0U) && (to > 0U)) { to--; }
        shellPrint(sh, "  wait XFER_COMPLETE: to=%lu NISTR=0x%04X EISTR=0x%04X PSTATE=0x%08lX\r\n",
                   (unsigned long)to, (unsigned)p->NORMAL_INT_STAT.U,
                   (unsigned)p->ERROR_INT_STAT.U, (unsigned long)p->PSTATE_REG.U);
        if (to == 0U) { return -3; }
        p->NORMAL_INT_STAT.B.XFER_COMPLETE = 1U;
    }
    return 0;
}

/* strict single-block PIO read */
static int sd_pio_read(Shell *sh, DWORD lba, uint32 *data)
{
    Ifx_SDMMC        *p = &MODULE_SDMMC0;
    IfxSdmmc_Response rsp;
    IfxSdmmc_Status   st;
    uint32            i;

    if (p->PSTATE_REG.B.CMD_INHIBIT_DAT)
    {
        shellPrint(sh, "  pre: CMD_INHIBIT_DAT=1 -> SW_RST_DAT\r\n");
        p->SW_RST.B.SW_RST_DAT = 1U;
        while (p->SW_RST.B.SW_RST_DAT) { }
    }
    p->NORMAL_INT_STAT.U = 0xFFFFU;
    p->ERROR_INT_STAT.U  = 0xFFFFU;

    p->BLOCKSIZE.B.XFER_BLOCK_SIZE = 512U;
    p->BLOCKSIZE.B.SDMA_BUF_BDARY  = 0U;
    p->BLOCKCOUNT.B.BLOCK_CNT      = 1U;
    p->XFER_MODE.U                 = 0U;
    p->XFER_MODE.B.DATA_XFER_DIR   = 1U;   /* card -> host */

    st = IfxSdmmc_sendCommand(p, IfxSdmmc_Command_readSingleBlock, (uint32)lba,
                              IfxSdmmc_ResponseType_r1, &rsp);
    if (st != IfxSdmmc_Status_success)
    {
        shellPrint(sh, "  CMD17 st=%d PSTATE=0x%08lX EISTR=0x%04X\r\n",
                   (int)st, (unsigned long)p->PSTATE_REG.U, (unsigned)p->ERROR_INT_STAT.U);
        return -1;
    }
    shellPrint(sh, "  CMD17 ok R1=0x%08lX PSTATE=0x%08lX\r\n",
               (unsigned long)rsp.cardStatus.U, (unsigned long)p->PSTATE_REG.U);

    for (i = 0; i < 128U; i++)
    {
        uint32 to = 4000000UL;
        while ((p->PSTATE_REG.B.BUF_RD_ENABLE == 0U) && (to > 0U)) { to--; }
        if (to == 0U)
        {
            shellPrint(sh, "  word %lu: BUF_RD_ENABLE timeout PSTATE=0x%08lX EISTR=0x%04X\r\n",
                       (unsigned long)i, (unsigned long)p->PSTATE_REG.U,
                       (unsigned)p->ERROR_INT_STAT.U);
            return -2;
        }
        data[i] = p->BUF_DATA.U;
    }
    {
        uint32 to = 40000000UL;
        while ((p->NORMAL_INT_STAT.B.XFER_COMPLETE == 0U) && (to > 0U)) { to--; }
        shellPrint(sh, "  XFER_COMPLETE to=%lu NISTR=0x%04X EISTR=0x%04X\r\n",
                   (unsigned long)to, (unsigned)p->NORMAL_INT_STAT.U,
                   (unsigned)p->ERROR_INT_STAT.U);
        if (to == 0U) { return -3; }
        p->NORMAL_INT_STAT.B.XFER_COMPLETE = 1U;
    }
    return 0;
}

/* Probe: issue CMD24 with NO data, then read CMD13 to see the card's state.
 *   state 6 (RCV) => the card accepted CMD24 and is waiting for the data block
 *                    (=> host->card data path broken)
 *   state 4 (TRAN) => CMD24 was never really executed (stale response) */
static int sd_cmd_c24probe(Shell *sh, DWORD lba)
{
    Ifx_SDMMC         *p = &MODULE_SDMMC0;
    IfxSdmmc_Response  rsp;
    IfxSdmmc_CardStatus cs;
    IfxSdmmc_Status    st;
    uint32             rst1 = 0;

    if (!Sdmmc_IsInited())
    {
        shellPrint(sh, "SD not initialized (run 'sd init')\r\n");
        return -1;
    }
    if (Sdmmc_ReadR1(&cs) == 0) { rst1 = cs.U; }
    shellPrint(sh, "before : R1=0x%08lX state=%lu\r\n",
               (unsigned long)rst1, (unsigned long)((rst1 >> 9) & 0xFU));

    p->NORMAL_INT_STAT.U = 0xFFFFU;
    p->ERROR_INT_STAT.U  = 0xFFFFU;
    p->BLOCKSIZE.B.XFER_BLOCK_SIZE = 512U;
    p->BLOCKCOUNT.B.BLOCK_CNT      = 1U;
    p->XFER_MODE.U                 = 0U;
    p->XFER_MODE.B.DATA_XFER_DIR   = 0U;

    st = IfxSdmmc_sendCommand(p, IfxSdmmc_Command_writeBlock, (uint32)lba,
                              IfxSdmmc_ResponseType_r1, &rsp);
    shellPrint(sh, "cmd24 : st=%d RESP01=0x%08lX (ILLEGAL_CMD=%u) PSTATE=0x%08lX\r\n",
               (int)st, (unsigned long)rsp.resp01,
               (unsigned)((rsp.resp01 >> 22) & 1U), (unsigned long)p->PSTATE_REG.U);
    shellPrint(sh, "quirk : XFER=0x%04X BLOCKSIZE=0x%04X BLKCNT=0x%04X NISTR=0x%04X EISTR=0x%04X\r\n",
               (unsigned)p->XFER_MODE.U, (unsigned)p->BLOCKSIZE.U,
               (unsigned)p->BLOCKCOUNT.U, (unsigned)p->NORMAL_INT_STAT.U,
               (unsigned)p->ERROR_INT_STAT.U);

    if (Sdmmc_ReadR1(&cs) == 0)
    {
        uint32 r = cs.U;
        shellPrint(sh, "after : R1=0x%08lX state=%lu ready=%u ILLEGAL=%u ERRR=%u\r\n",
                   (unsigned long)r, (unsigned long)((r >> 9) & 0xFU),
                   (unsigned)((r >> 8) & 1U), (unsigned)((r >> 22) & 1U),
                   (unsigned)((r >> 19) & 1U));
    }
    else
    {
        shellPrint(sh, "after : CMD13 failed (card busy/RCV)\r\n");
    }
    return 0;
}

/* Experiment: PIO single-block write with configurable host flags.
 *   bce=1 -> BLOCK_COUNT_ENABLE=1 + BLOCKCOUNT=1
 *   mbs=1 -> MULTI_BLK_SEL=1
 *   v4    -> HOST_VER4_ENABLE (0/1)
 *   width -> HOST_CTRL1.DAT_XFER_WIDTH (1 or 4) */
static int sd_exp_write(Shell *sh, DWORD lba, uint32 bce, uint32 mbs, uint32 v4, uint32 width)
{
    Ifx_SDMMC         *p = &MODULE_SDMMC0;
    IfxSdmmc_Response  rsp;
    IfxSdmmc_Status    st;
    uint32             i;
    uint32             datAnd = 0xFFFFFFFFUL, datOr = 0U, datChg = 0U, datLast = 0xFFFFFFFFUL;

    if (!Sdmmc_IsInited())
    {
        shellPrint(sh, "SD not initialized (run 'sd init')\r\n");
        return -1;
    }
    shellPrint(sh, "exp w lba=%lu bce=%lu mbs=%lu v4=%lu width=%lu\r\n",
               (unsigned long)lba, (unsigned long)bce, (unsigned long)mbs,
               (unsigned long)v4, (unsigned long)width);

    p->HOST_CTRL2.B.HOST_VER4_ENABLE = (v4 != 0U) ? 1U : 0U;
    p->HOST_CTRL1.B.DAT_XFER_WIDTH   = (width == 1U) ? 1U : 0U;
    p->HOST_CTRL2.B.PRESET_VAL_ENABLE = 0U;

    if (p->PSTATE_REG.B.CMD_INHIBIT_DAT)
    {
        p->SW_RST.B.SW_RST_DAT = 1U;
        while (p->SW_RST.B.SW_RST_DAT) { }
    }
    p->NORMAL_INT_STAT.U = 0xFFFFU;
    p->ERROR_INT_STAT.U  = 0xFFFFU;

    p->BLOCKSIZE.B.XFER_BLOCK_SIZE = 512U;
    p->BLOCKSIZE.B.SDMA_BUF_BDARY  = 0U;
    p->BLOCKCOUNT.B.BLOCK_CNT      = 1U;
    p->XFER_MODE.U                 = 0U;
    p->XFER_MODE.B.DATA_XFER_DIR   = 0U;
    if (bce != 0U) { p->XFER_MODE.B.BLOCK_COUNT_ENABLE = 1U; }
    if (mbs != 0U) { p->XFER_MODE.B.MULTI_BLK_SEL      = 1U; }

    sd_fill_pattern(s_ioBuf, sizeof(s_ioBuf) / 4U, lba);

    shellPrint(sh, "  pre PSTATE=0x%08lX XFER=0x%04X HOST1=0x%02X HOST2=0x%04X\r\n",
               (unsigned long)p->PSTATE_REG.U, (unsigned)p->XFER_MODE.U,
               (unsigned)p->HOST_CTRL1.U, (unsigned)p->HOST_CTRL2.U);

    st = IfxSdmmc_sendCommand(p, IfxSdmmc_Command_writeBlock, (uint32)lba,
                              IfxSdmmc_ResponseType_r1, &rsp);
    shellPrint(sh, "  cmd24 st=%d R1=0x%08lX PSTATE=0x%08lX\r\n",
               (int)st, (unsigned long)rsp.cardStatus.U, (unsigned long)p->PSTATE_REG.U);
    if (st != IfxSdmmc_Status_success) { return -1; }

    for (i = 0; i < 128U; i++)
    {
        uint32 to = 4000000UL;
        while ((p->PSTATE_REG.B.BUF_WR_ENABLE == 0U) && (to > 0U)) { to--; }
        if (to == 0U)
        {
            shellPrint(sh, "  word %lu BUF_WR_ENABLE timeout PSTATE=0x%08lX EISTR=0x%04X\r\n",
                       (unsigned long)i, (unsigned long)p->PSTATE_REG.U,
                       (unsigned)p->ERROR_INT_STAT.U);
            return -2;
        }
        p->BUF_DATA.U = s_ioBuf[i];
        {
            uint32 d = (p->PSTATE_REG.U >> 20) & 0xFU;
            if (d != datLast) { datChg++; datLast = d; }
            datAnd &= d; datOr |= d;
        }
    }
    {
        uint32 n;
        for (n = 0; n < 300000UL; n++)
        {
            uint32 d = (p->PSTATE_REG.U >> 20) & 0xFU;
            if (d != datLast) { datChg++; datLast = d; }
            datAnd &= d; datOr |= d;
        }
    }
    shellPrint(sh, "  DAT and=0x%lX or=0x%lX chg=%lu | PSTATE=0x%08lX NISTR=0x%04X EISTR=0x%04X\r\n",
               (unsigned long)datAnd, (unsigned long)datOr, (unsigned long)datChg,
               (unsigned long)p->PSTATE_REG.U, (unsigned)p->NORMAL_INT_STAT.U,
               (unsigned)p->ERROR_INT_STAT.U);
    {
        uint32 to = 20000000UL;
        while ((p->NORMAL_INT_STAT.B.XFER_COMPLETE == 0U) && (to > 0U)) { to--; }
        shellPrint(sh, "  xfc to=%lu NISTR=0x%04X EISTR=0x%04X PSTATE=0x%08lX\r\n",
                   (unsigned long)to, (unsigned)p->NORMAL_INT_STAT.U,
                   (unsigned)p->ERROR_INT_STAT.U, (unsigned long)p->PSTATE_REG.U);
        p->NORMAL_INT_STAT.B.XFER_COMPLETE = 1U;
        return (to == 0U) ? -3 : 0;
    }
}

/* Control experiment: PIO single-block read with the same DAT sampling as the write,
 * to prove the sampling method itself can observe toggling DAT lines. */
static int sd_exp_read(Shell *sh, DWORD lba)
{
    Ifx_SDMMC         *p = &MODULE_SDMMC0;
    IfxSdmmc_Response  rsp;
    IfxSdmmc_Status    st;
    uint32             i;
    uint32             datAnd = 0xFFFFFFFFUL, datOr = 0U, datChg = 0U, datLast = 0xFFFFFFFFUL;

    if (!Sdmmc_IsInited())
    {
        shellPrint(sh, "SD not initialized (run 'sd init')\r\n");
        return -1;
    }
    if (p->PSTATE_REG.B.CMD_INHIBIT_DAT)
    {
        p->SW_RST.B.SW_RST_DAT = 1U;
        while (p->SW_RST.B.SW_RST_DAT) { }
    }
    p->NORMAL_INT_STAT.U = 0xFFFFU;
    p->ERROR_INT_STAT.U  = 0xFFFFU;
    p->BLOCKSIZE.B.XFER_BLOCK_SIZE = 512U;
    p->BLOCKSIZE.B.SDMA_BUF_BDARY  = 0U;
    p->BLOCKCOUNT.B.BLOCK_CNT      = 1U;
    p->XFER_MODE.U                 = 0U;
    p->XFER_MODE.B.DATA_XFER_DIR   = 1U;

    st = IfxSdmmc_sendCommand(p, IfxSdmmc_Command_readSingleBlock, (uint32)lba,
                              IfxSdmmc_ResponseType_r1, &rsp);
    shellPrint(sh, "exp r lba=%lu: cmd17 st=%d R1=0x%08lX PSTATE=0x%08lX\r\n",
               (unsigned long)lba, (int)st, (unsigned long)rsp.cardStatus.U,
               (unsigned long)p->PSTATE_REG.U);
    if (st != IfxSdmmc_Status_success) { return -1; }

    for (i = 0; i < 128U; i++)
    {
        uint32 to = 8000000UL;
        while ((p->PSTATE_REG.B.BUF_RD_ENABLE == 0U) && (to > 0U))
        {
            uint32 d = (p->PSTATE_REG.U >> 20) & 0xFU;
            if (d != datLast) { datChg++; datLast = d; }
            datAnd &= d; datOr |= d;
            to--;
        }
        if (to == 0U)
        {
            shellPrint(sh, "  word %lu BUF_RD_ENABLE timeout PSTATE=0x%08lX EISTR=0x%04X\r\n",
                       (unsigned long)i, (unsigned long)p->PSTATE_REG.U,
                       (unsigned)p->ERROR_INT_STAT.U);
            shellPrint(sh, "  DAT and=0x%lX or=0x%lX chg=%lu\r\n",
                       (unsigned long)datAnd, (unsigned long)datOr, (unsigned long)datChg);
            return -2;
        }
        s_chkBuf[i & 127U] = p->BUF_DATA.U;
        {
            uint32 d = (p->PSTATE_REG.U >> 20) & 0xFU;
            if (d != datLast) { datChg++; datLast = d; }
            datAnd &= d; datOr |= d;
        }
    }
    shellPrint(sh, "  DAT and=0x%lX or=0x%lX chg=%lu | PSTATE=0x%08lX NISTR=0x%04X EISTR=0x%04X\r\n",
               (unsigned long)datAnd, (unsigned long)datOr, (unsigned long)datChg,
               (unsigned long)p->PSTATE_REG.U, (unsigned)p->NORMAL_INT_STAT.U,
               (unsigned)p->ERROR_INT_STAT.U);
    return 0;
}

/* Port-level inspection of the SDMMC pads (read-only).
 * Compares the SDMMC CMD pad (P15.3, known to drive commands fine) with the
 * DAT pads (P20.x) to spot a pad-configuration difference. */
static int sd_cmd_portinfo(Shell *sh)
{
    Ifx_SDMMC *p   = &MODULE_SDMMC0;
    Ifx_P     *p15 = &MODULE_P15;
    Ifx_P     *p20 = &MODULE_P20;

    shellPrint(sh, "P15 IOCR0=0x%08lX PDR0=0x%08lX IN=0x%08lX\r\n",
               (unsigned long)p15->IOCR0.U, (unsigned long)p15->PDR0.U,
               (unsigned long)p15->IN.U);
    shellPrint(sh, "P20 IOCR4=0x%08lX IOCR8=0x%08lX PDR0=0x%08lX PDR1=0x%08lX IN=0x%08lX\r\n",
               (unsigned long)p20->IOCR4.U, (unsigned long)p20->IOCR8.U,
               (unsigned long)p20->PDR0.U, (unsigned long)p20->PDR1.U,
               (unsigned long)p20->IN.U);
    /* per-pin 8-bit PC fields: IOCR<n> holds 4 pins, lowest byte = first pin */
    shellPrint(sh, "P15.1(CLK)=0x%02lX P15.3(CMD)=0x%02lX\r\n",
               (unsigned long)((p15->IOCR0.U >> 8) & 0xFFU),
               (unsigned long)((p15->IOCR0.U >> 24) & 0xFFU));
    shellPrint(sh, "P20.7(DAT0)=0x%02lX P20.8(DAT1)=0x%02lX P20.10(DAT2)=0x%02lX P20.11(DAT3)=0x%02lX\r\n",
               (unsigned long)((p20->IOCR4.U >> 24) & 0xFFU),
               (unsigned long)((p20->IOCR8.U) & 0xFFU),
               (unsigned long)((p20->IOCR8.U >> 16) & 0xFFU),
               (unsigned long)((p20->IOCR8.U >> 24) & 0xFFU));

    shellPrint(sh, "PSTATE.DAT3_0=0x%lX (read-only probe)\r\n",
               (unsigned long)((p->PSTATE_REG.U >> 20) & 0xFU));
    return 0;
}

/* Experiment: pre-fill the 512-byte FIFO BEFORE issuing CMD24.
 * Some host controllers start the write data phase only when the TX FIFO is
 * already populated at command time. */
static int sd_exp_prewrite(Shell *sh, DWORD lba)
{
    Ifx_SDMMC         *p = &MODULE_SDMMC0;
    IfxSdmmc_Response  rsp;
    IfxSdmmc_Status    st;
    uint32             i;
    uint32             datAnd = 0xFFFFFFFFUL, datOr = 0U, datChg = 0U, datLast = 0xFFFFFFFFUL;

    if (!Sdmmc_IsInited())
    {
        shellPrint(sh, "SD not initialized (run 'sd init')\r\n");
        return -1;
    }
    sd_fill_pattern(s_ioBuf, sizeof(s_ioBuf) / 4U, lba);

    if (p->PSTATE_REG.B.CMD_INHIBIT_DAT)
    {
        p->SW_RST.B.SW_RST_DAT = 1U;
        while (p->SW_RST.B.SW_RST_DAT) { }
    }
    p->NORMAL_INT_STAT.U = 0xFFFFU;
    p->ERROR_INT_STAT.U  = 0xFFFFU;
    p->BLOCKSIZE.B.XFER_BLOCK_SIZE = 512U;
    p->BLOCKSIZE.B.SDMA_BUF_BDARY  = 0U;
    p->BLOCKCOUNT.B.BLOCK_CNT      = 1U;
    p->XFER_MODE.U                 = 0U;
    p->XFER_MODE.B.DATA_XFER_DIR   = 0U;

    /* pre-load 128 words while idle */
    for (i = 0; i < 128U; i++)
    {
        uint32 to = 4000000UL;
        while ((p->PSTATE_REG.B.BUF_WR_ENABLE == 0U) && (to > 0U)) { to--; }
        if (to == 0U)
        {
            shellPrint(sh, "  preload word %lu: BUF_WR_ENABLE never set (PSTATE=0x%08lX)\r\n",
                       (unsigned long)i, (unsigned long)p->PSTATE_REG.U);
            return -1;
        }
        p->BUF_DATA.U = s_ioBuf[i];
    }
    shellPrint(sh, "  preloaded 128 words, PSTATE=0x%08lX (now CMD24)\r\n",
               (unsigned long)p->PSTATE_REG.U);

    st = IfxSdmmc_sendCommand(p, IfxSdmmc_Command_writeBlock, (uint32)lba,
                              IfxSdmmc_ResponseType_r1, &rsp);
    shellPrint(sh, "  cmd24 st=%d R1=0x%08lX PSTATE=0x%08lX\r\n",
               (int)st, (unsigned long)rsp.cardStatus.U, (unsigned long)p->PSTATE_REG.U);

    {
        uint32 n;
        for (n = 0; n < 400000UL; n++)
        {
            uint32 d = (p->PSTATE_REG.U >> 20) & 0xFU;
            if (d != datLast) { datChg++; datLast = d; }
            datAnd &= d; datOr |= d;
        }
    }
    shellPrint(sh, "  DAT and=0x%lX or=0x%lX chg=%lu | PSTATE=0x%08lX NISTR=0x%04X EISTR=0x%04X\r\n",
               (unsigned long)datAnd, (unsigned long)datOr, (unsigned long)datChg,
               (unsigned long)p->PSTATE_REG.U, (unsigned)p->NORMAL_INT_STAT.U,
               (unsigned)p->ERROR_INT_STAT.U);
    return 0;
}

/* Experiment: SDMA multi-block transfer with DAT sampling */
static int sd_exp_dma(Shell *sh, DWORD lba, uint32 count, uint32 dirRead)
{
    IfxSdmmc_Sd     *h = Sdmmc_GetHandle();
    Ifx_SDMMC       *p = &MODULE_SDMMC0;
    IfxSdmmc_Status  st;
    uint32           n;
    uint32           datAnd = 0xFFFFFFFFUL, datOr = 0U, datChg = 0U, datLast = 0xFFFFFFFFUL;

    if (!Sdmmc_IsInited())
    {
        shellPrint(sh, "SD not initialized (run 'sd init')\r\n");
        return -1;
    }
    if ((count == 0U) || (count > 8U)) { count = 2U; }
    sd_fill_pattern(s_ioBuf, sizeof(s_ioBuf) / 4U, lba);
    sdmmc_cache_invalidate(s_ioBuf, (uint32_t)count * 512U);

    shellPrint(sh, "exp dma %s lba=%lu n=%lu\r\n", dirRead ? "r" : "w",
               (unsigned long)lba, (unsigned long)count);

    if (dirRead)
    {
        st = IfxSdmmc_Sd_readMultiBlock(h, (uint32)lba, s_ioBuf, count,
                                        IfxSdmmc_BlockBoundarySize_512K);
    }
    else
    {
        /* start the transfer then sample DAT while it runs */
        st = IfxSdmmc_Sd_writeMultiBlock(h, (uint32)lba, s_ioBuf, count,
                                         IfxSdmmc_BlockBoundarySize_512K);
    }
    /* sample whatever the lines are doing */
    for (n = 0; n < 400000UL; n++)
    {
        uint32 d = (p->PSTATE_REG.U >> 20) & 0xFU;
        if (d != datLast) { datChg++; datLast = d; }
        datAnd &= d; datOr |= d;
    }
    shellPrint(sh, "  st=%d DAT and=0x%lX or=0x%lX chg=%lu\r\n",
               (int)st, (unsigned long)datAnd, (unsigned long)datOr, (unsigned long)datChg);
    shellPrint(sh, "  PSTATE=0x%08lX NISTR=0x%04X EISTR=0x%04X XFER=0x%04X BLKSIZE=0x%04X\r\n",
               (unsigned long)p->PSTATE_REG.U, (unsigned)p->NORMAL_INT_STAT.U,
               (unsigned)p->ERROR_INT_STAT.U, (unsigned)p->XFER_MODE.U,
               (unsigned)p->BLOCKSIZE.U);
    return (st == IfxSdmmc_Status_success) ? 0 : -1;
}

static int sd_cmd_tx(Shell *sh, const char *mode, const char *dir, DWORD lba, UINT count)
{
    int rc = -1;
    if (!Sdmmc_IsInited())
    {
        shellPrint(sh, "SD not initialized (run 'sd init')\r\n");
        return -1;
    }
    if ((count == 0U) || (count > 8U))
    {
        shellPrint(sh, "count 1..8\r\n");
        return -1;
    }
    sd_fill_pattern(s_ioBuf, sizeof(s_ioBuf) / 4U, lba);

    if (strcmp(mode, "pio") == 0)
    {
        UINT i;
        shellPrint(sh, "tx pio %s lba=%lu n=%u\r\n", dir, (unsigned long)lba, count);
        rc = 0;
        for (i = 0; i < count; i++)
        {
            int r;
            if (strcmp(dir, "w") == 0)
            {
                shellPrint(sh, " blk %u:\r\n", i);
                r = sd_pio_write(sh, lba + i, &s_ioBuf[i * 128U]);
            }
            else
            {
                shellPrint(sh, " blk %u:\r\n", i);
                r = sd_pio_read(sh, lba + i, &s_ioBuf[i * 128U]);
            }
            if (r != 0) { shellPrint(sh, " -> block %u FAIL rc=%d\r\n", i, r); rc = -1; break; }
        }
        if (rc == 0) { shellPrint(sh, " -> pio %s OK\r\n", dir); }
        return rc;
    }
    if (strcmp(mode, "dma") == 0)
    {
        IfxSdmmc_Sd *h = Sdmmc_GetHandle();
        IfxSdmmc_Status st;
        shellPrint(sh, "tx dma %s lba=%lu n=%u\r\n", dir, (unsigned long)lba, count);
        sdmmc_cache_invalidate(s_ioBuf, (uint32_t)count * 512U);
        if (strcmp(dir, "w") == 0)
        {
            st = IfxSdmmc_Sd_writeMultiBlock(h, (uint32)lba, s_ioBuf, (uint32)count,
                                             IfxSdmmc_BlockBoundarySize_512K);
        }
        else
        {
            st = IfxSdmmc_Sd_readMultiBlock(h, (uint32)lba, s_ioBuf, (uint32)count,
                                            IfxSdmmc_BlockBoundarySize_512K);
        }
        shellPrint(sh, " -> dma st=%d PSTATE=0x%08lX NISTR=0x%04X EISTR=0x%04X XFER=0x%04X\r\n",
                   (int)st, (unsigned long)h->sdmmcSFR->PSTATE_REG.U,
                   (unsigned)h->sdmmcSFR->NORMAL_INT_STAT.U,
                   (unsigned)h->sdmmcSFR->ERROR_INT_STAT.U,
                   (unsigned)h->sdmmcSFR->XFER_MODE.U);
        return (st == IfxSdmmc_Status_success) ? 0 : -1;
    }
    shellPrint(sh, "usage: sd tx <pio|dma> <r|w> <lba> [count 1..8]\r\n");
    return -1;
}

/* ---------------- main sd command ---------------- */

static void sd_usage(Shell *sh)
{
    shellPrint(sh, "Usage: sd <subcmd> [args]  (SDMMC0 P15.1/15.3/P20.7/8/10/11 CD P10.7)\r\n");
    shellPrint(sh, "  sd init                 - init SD + mount (shows CID/CSD/capacity/FS)\r\n");
    shellPrint(sh, "  sd info                 - card + FS info (no re-init)\r\n");
    shellPrint(sh, "  sd cd                   - card-detect P10.7 level\r\n");
    shellPrint(sh, "  sd ls [path]            - list dir (default 0:/)\r\n");
    shellPrint(sh, "  sd cat <file> [maxB]    - dump text file (default 2048B)\r\n");
    shellPrint(sh, "  sd stat <file>          - file size/attrib/time\r\n");
    shellPrint(sh, "  sd write <f> <KB> [seed]- create pattern file, report KB/s\r\n");
    shellPrint(sh, "  sd read <f> [seed]      - read+verify pattern file, report KB/s\r\n");
    shellPrint(sh, "  sd bench [MB]           - seq write+read+verify (default 4MB)\r\n");
    shellPrint(sh, "  sd mkfs <exfat|fat32> [au_shift] - FORMAT (destroys data!)\r\n");
    shellPrint(sh, "  sd erase <lba> <count>  - zero-fill sectors (raw, destructive)\r\n");
    shellPrint(sh, "  sd raw <r|w> <lba> [n]  - raw sector read(hex)/write(pattern)\r\n");
    shellPrint(sh, "  sd rm <path>            - delete file/dir\r\n");
    shellPrint(sh, "  sd mkdir <path>         - make dir\r\n");
    shellPrint(sh, "  sd label [new]          - get/set volume label\r\n");
    shellPrint(sh, "  sd free                 - free space\r\n");
    shellPrint(sh, "  sd st                   - link status + card R1 state\r\n");
    shellPrint(sh, "  sd regs                 - host regs + last error\r\n");
    shellPrint(sh, "  sd recover              - CMD12 abort + host reset + re-init\r\n");
    shellPrint(sh, "  sd diag                 - FULL register dump + decoded PSTATE\r\n");
    shellPrint(sh, "  sd tx <pio|dma> <r|w> <lba> [n] - transfer experiment (traced)\r\n");
    shellPrint(sh, "  sd chk <lba>            - verify sector == sd_fill_pattern(seed=lba)\r\n");
    shellPrint(sh, "  sd host [v4|dsel|preset|mbui|print] [val] - poke host regs\r\n");
    shellPrint(sh, "  sd clk [kHz]            - get/set SDCLK (e.g. 'sd clk 400')\r\n");
    shellPrint(sh, "128GB TF: use 'sd mkfs exfat'. 'sd bench' default file 0:/BENCH.BIN.\r\n");
}

static int cmd_sd(int argc, char *argv[])
{
    Shell *sh = shellGetCurrent();

    if (argc < 2)
    {
        sd_usage(sh);
        return 0;
    }
    if (strcmp(argv[1], "init") == 0)
    {
        return sd_cmd_init(sh);
    }
    if (strcmp(argv[1], "info") == 0)
    {
        sd_print_info(sh);
        return 0;
    }
    if (strcmp(argv[1], "cd") == 0)
    {
        shellPrint(sh, "CD P10.7=%d (%s)\r\n", sd_cd_level(),
                   sd_cd_level() ? "HIGH(no card?)" : "LOW(inserted, typical)");
        return 0;
    }
    if (strcmp(argv[1], "ls") == 0)
    {
        return sd_cmd_ls(sh, (argc >= 3) ? argv[2] : "0:/");
    }
    if (strcmp(argv[1], "cat") == 0)
    {
        uint32 maxB = 2048;
        if (argc < 3)
        {
            shellPrint(sh, "usage: sd cat <file> [maxBytes]\r\n");
            return -1;
        }
        if (argc >= 4)
        {
            maxB = (uint32)strtoul(argv[3], NULL, 0);
            if (maxB == 0)
            {
                maxB = 2048;
            }
        }
        return sd_cmd_cat(sh, argv[2], maxB);
    }
    if (strcmp(argv[1], "stat") == 0)
    {
        FILINFO fi;
        FRESULT fr;
        if (argc < 3)
        {
            shellPrint(sh, "usage: sd stat <file>\r\n");
            return -1;
        }
        fr = f_stat(argv[2], &fi);
        if (fr != FR_OK)
        {
            shellPrint(sh, "f_stat -> %s (%d)\r\n", sd_fr_str(fr), (int)fr);
            return -1;
        }
        shellPrint(sh, "%s: %lu bytes attr=0x%02X date=%u/%u/%u time=%u:%u:%u\r\n",
                   argv[2], (unsigned long)fi.fsize, (unsigned)fi.fattrib,
                   (unsigned)((fi.fdate >> 9) & 0x7f) + 1980,
                   (unsigned)((fi.fdate >> 5) & 0x0f), (unsigned)(fi.fdate & 0x1f),
                   (unsigned)((fi.ftime >> 11) & 0x1f), (unsigned)((fi.ftime >> 5) & 0x3f),
                   (unsigned)((fi.ftime & 0x1f) * 2));
        return 0;
    }
    if (strcmp(argv[1], "write") == 0)
    {
        uint32 kb, seed = 0xA5A55A5AU;
        if (argc < 4)
        {
            shellPrint(sh, "usage: sd write <file> <sizeKB> [seed]\r\n");
            return -1;
        }
        kb = (uint32)strtoul(argv[3], NULL, 0);
        if ((kb == 0) || (kb > 1024U * 512U))
        {
            shellPrint(sh, "sizeKB 1..524288\r\n");
            return -1;
        }
        if (argc >= 5)
        {
            seed = (uint32)strtoul(argv[4], NULL, 0);
        }
        return sd_cmd_write(sh, argv[2], kb, seed);
    }
    if (strcmp(argv[1], "read") == 0)
    {
        uint32 seed = 0xA5A55A5AU;
        if (argc < 3)
        {
            shellPrint(sh, "usage: sd read <file> [seed]\r\n");
            return -1;
        }
        if (argc >= 4)
        {
            seed = (uint32)strtoul(argv[3], NULL, 0);
        }
        return sd_cmd_read(sh, argv[2], seed);
    }
    if (strcmp(argv[1], "bench") == 0)
    {
        uint32 mb = 4;
        if (argc >= 3)
        {
            mb = (uint32)strtoul(argv[2], NULL, 0);
        }
        return sd_cmd_bench(sh, mb);
    }
    if (strcmp(argv[1], "mkfs") == 0)
    {
        uint32 au = 0;
        if (argc < 3)
        {
            shellPrint(sh, "usage: sd mkfs <exfat|fat32> [au_shift]\r\n");
            return -1;
        }
        if (argc >= 4)
        {
            au = (uint32)strtoul(argv[3], NULL, 0);
        }
        return sd_cmd_mkfs(sh, argv[2], au);
    }
    if (strcmp(argv[1], "erase") == 0)
    {
        DWORD lba;
        UINT count;
        if (argc < 4)
        {
            shellPrint(sh, "usage: sd erase <lba> <count>\r\n");
            return -1;
        }
        lba = (DWORD)strtoul(argv[2], NULL, 0);
        count = (UINT)strtoul(argv[3], NULL, 0);
        return sd_cmd_erase(sh, lba, count);
    }
    if (strcmp(argv[1], "raw") == 0)
    {
        DWORD lba;
        UINT count = 1;
        if (argc < 4)
        {
            shellPrint(sh, "usage: sd raw <r|w> <lba> [count 1..16]\r\n");
            return -1;
        }
        lba = (DWORD)strtoul(argv[3], NULL, 0);
        if (argc >= 5)
        {
            count = (UINT)strtoul(argv[4], NULL, 0);
        }
        return sd_cmd_raw(sh, argv[2], lba, count);
    }
    if (strcmp(argv[1], "rm") == 0)
    {
        FRESULT fr;
        if (argc < 3)
        {
            shellPrint(sh, "usage: sd rm <path>\r\n");
            return -1;
        }
        if (!s_mounted)
        {
            shellPrint(sh, "not mounted (run 'sd init')\r\n");
            return -1;
        }
        fr = f_unlink(argv[2]);
        shellPrint(sh, "f_unlink('%s') -> %s (%d)\r\n", argv[2], sd_fr_str(fr), (int)fr);
        return (fr == FR_OK) ? 0 : -1;
    }
    if (strcmp(argv[1], "mkdir") == 0)
    {
        FRESULT fr;
        if (argc < 3)
        {
            shellPrint(sh, "usage: sd mkdir <path>\r\n");
            return -1;
        }
        if (!s_mounted)
        {
            shellPrint(sh, "not mounted (run 'sd init')\r\n");
            return -1;
        }
        fr = f_mkdir(argv[2]);
        shellPrint(sh, "f_mkdir('%s') -> %s (%d)\r\n", argv[2], sd_fr_str(fr), (int)fr);
        return (fr == FR_OK) ? 0 : -1;
    }
    if (strcmp(argv[1], "label") == 0)
    {
        FRESULT fr;
#if FF_USE_LABEL
        char lbl[32];
        if (argc >= 3)
        {
            fr = f_setlabel(argv[2]);
            shellPrint(sh, "f_setlabel -> %s (%d)\r\n", sd_fr_str(fr), (int)fr);
            return (fr == FR_OK) ? 0 : -1;
        }
        lbl[0] = 0;
        fr = f_getlabel("", lbl, NULL);
        shellPrint(sh, "label='%s' -> %s (%d)\r\n", lbl, sd_fr_str(fr), (int)fr);
        return (fr == FR_OK) ? 0 : -1;
#else
        shellPrint(sh, "label support disabled (FF_USE_LABEL=0)\r\n");
        (void)fr;
        return -1;
#endif
    }
    if (strcmp(argv[1], "free") == 0)
    {
        if (!s_mounted)
        {
            shellPrint(sh, "not mounted (run 'sd init')\r\n");
            return -1;
        }
        sd_print_capacity(sh);
        sd_print_fs(sh);
        return 0;
    }
    if (strcmp(argv[1], "st") == 0)
    {
        /* link status + card R1 state decode */
        int a = 0, b = 0;
        DSTATUS ds = disk_status(0);
        IfxSdmmc_CardStatus cst;
        Sdmmc_StGet(&a, &b);
        shellPrint(sh, "disk_status=0x%02X send_status=%d lock=%d\r\n", (unsigned)ds, a, b);
        if (Sdmmc_ReadR1(&cst) == 0)
        {
            uint32_t r = cst.U;
            shellPrint(sh, "R1=0x%08lX state=%lu (%s) ready=%u\r\n", (unsigned long)r,
                       (unsigned long)((r >> 9) & 0xF),
                       (((r >> 9) & 0xF) == 4) ? "TRAN" : "NOT-TRAN",
                       (unsigned)((r >> 8) & 1));
        }
        return 0;
    }
    if (strcmp(argv[1], "regs") == 0)
    {
        /* host controller + last-error snapshot (diagnostics) */
        IfxSdmmc_Sd *h = Sdmmc_GetHandle();
        Ifx_SDMMC *sp;
        uint32 op = 0, sec = 0, ni = 0, ei = 0, ps = 0;
        sp = h->sdmmcSFR;
        if (!Sdmmc_IsInited() || !sp)
        {
            /* pre-init: raw fixed-address read (no handle needed) */
            volatile uint32_t *pN = (volatile uint32_t *)0xF02B0030UL;
            volatile uint32_t *pE = (volatile uint32_t *)0xF02B0032UL;
            volatile uint32_t *pP = (volatile uint32_t *)0xF02B0024UL;
            shellPrint(sh, "pre-init: NISTR=0x%04X EISTR=0x%04X PSTATE=0x%08lX\r\n",
                       (unsigned)(*pN & 0xFFFFU), (unsigned)(*pE & 0xFFFFU), (unsigned long)*pP);
            return 0;
        }
        Sdmmc_LastErr(&op, &sec, &ni, &ei, &ps);
        shellPrint(sh, "PSTATE=0x%08lX NISTR=0x%04X EISTR=0x%04X\r\n",
                   (unsigned long)sp->PSTATE_REG.U,
                   (unsigned)sp->NORMAL_INT_STAT.U, (unsigned)sp->ERROR_INT_STAT.U);
        shellPrint(sh, "CLKCTL=0x%04X HOST1=0x%02X XFER=0x%04X BLKCNT=%u\r\n",
                   (unsigned)sp->CLK_CTRL.U, (unsigned)sp->HOST_CTRL1.U,
                   (unsigned)sp->XFER_MODE.U, (unsigned)sp->BLOCKCOUNT.U);
        shellPrint(sh, "AUTOCMD=0x%04X lastErr: op=%lu sector=%lu EISTR=0x%04X\r\n",
                   (unsigned)sp->AUTO_CMD_STAT.U,
                   (unsigned long)op, (unsigned long)sec, (unsigned)ei);
        return 0;
    }
    if (strcmp(argv[1], "recover") == 0)
    {
        /* unwedge: CMD12 abort, host DAT/CMD reset, full re-init */
        IfxSdmmc_Sd *h = Sdmmc_GetHandle();
        IfxSdmmc_Response rsp;
        DSTATUS ds;
        shellPrint(sh, "CMD12 abort...\r\n");
        (void)IfxSdmmc_sendCommand(h->sdmmcSFR, IfxSdmmc_Command_stopTransmission,
                                   0, IfxSdmmc_ResponseType_r1b, &rsp);
        h->sdmmcSFR->SW_RST.B.SW_RST_DAT = 1;
        h->sdmmcSFR->SW_RST.B.SW_RST_CMD = 1;
        {
            volatile uint32_t spin;
            uint32_t to = 1000000UL;
            while ((h->sdmmcSFR->SW_RST.B.SW_RST_DAT || h->sdmmcSFR->SW_RST.B.SW_RST_CMD) && to--) { }
            for (spin = 0; spin < 300000UL; spin++) { }
        }
        ds = disk_initialize(0);
        shellPrint(sh, "re-init ds=0x%02X\r\n", (unsigned)ds);
        return (ds & STA_NOINIT) ? -1 : 0;
    }
    if (strcmp(argv[1], "diag") == 0)
    {
        sd_regdump(sh);
        return 0;
    }
    if (strcmp(argv[1], "clk") == 0)
    {
        Ifx_SDMMC *p = &MODULE_SDMMC0;
        uint32 khz = (argc >= 3) ? (uint32)strtoul(argv[2], NULL, 0) : 0U;
        if (khz == 0U)
        {
            shellPrint(sh, "CLKCTL=0x%04X FREQ_SEL=%u UPPER=%u SDCLKEN=%u PLLEN=%u PRESETEN=%u\r\n",
                       (unsigned)p->CLK_CTRL.U, (unsigned)((p->CLK_CTRL.U >> 8) & 0xFFU),
                       (unsigned)((p->CLK_CTRL.U >> 6) & 3U),
                       (unsigned)((p->CLK_CTRL.U >> 2) & 1U), (unsigned)((p->CLK_CTRL.U >> 3) & 1U),
                       (unsigned)((p->HOST_CTRL2.U >> 15) & 1U));
            return 0;
        }
        {
            uint32 baseHz = ((p->CAPABILITIES1.U >> 8) & 0xFFU) * 1000000U;
            uint32 div, setVal;
            if (baseHz == 0U) { baseHz = 100000000U; }
            div = baseHz / (2U * khz * 1000U);
            if (div == 0U) { div = 1U; }
            setVal = div - 1U;
            p->HOST_CTRL2.B.PRESET_VAL_ENABLE = 0U;
            p->CLK_CTRL.B.SD_CLK_EN = 0U;
            p->CLK_CTRL.B.FREQ_SEL       = setVal & 0xFFU;
            p->CLK_CTRL.B.UPPER_FREQ_SEL = (setVal >> 8) & 3U;
            {
                volatile uint32 spin;
                for (spin = 0; spin < 30000UL; spin++) { }
            }
            p->CLK_CTRL.B.SD_CLK_EN = 1U;
            shellPrint(sh, "SDCLK ~%lu kHz: div=%lu base=%luHz CLKCTL=0x%04X\r\n",
                       (unsigned long)khz, (unsigned long)div, (unsigned long)baseHz,
                       (unsigned)p->CLK_CTRL.U);
        }
        return 0;
    }
    if (strcmp(argv[1], "c24") == 0)
    {
        DWORD lba = (argc >= 3) ? (DWORD)strtoul(argv[2], NULL, 0) : 0UL;
        return sd_cmd_c24probe(sh, lba);
    }
    if (strcmp(argv[1], "exp") == 0)
    {
        DWORD lba = 45000000UL;
        uint32 bce = 0, mbs = 0, v4 = 1, width = 4;
        if ((argc >= 3) && (strcmp(argv[2], "r") == 0))
        {
            if (argc >= 4) { lba = (DWORD)strtoul(argv[3], NULL, 0); }
            return sd_exp_read(sh, lba);
        }
        if ((argc >= 3) && (strcmp(argv[2], "w") == 0))
        {
            if (argc >= 4) { lba   = (DWORD)strtoul(argv[3], NULL, 0); }
            if (argc >= 5) { bce   = (uint32)strtoul(argv[4], NULL, 0); }
            if (argc >= 6) { mbs   = (uint32)strtoul(argv[5], NULL, 0); }
            if (argc >= 7) { v4    = (uint32)strtoul(argv[6], NULL, 0); }
            if (argc >= 8) { width = (uint32)strtoul(argv[7], NULL, 0); }
            return sd_exp_write(sh, lba, bce, mbs, v4, width);
        }
        if ((argc >= 3) && (strcmp(argv[2], "pre") == 0))
        {
            if (argc >= 4) { lba = (DWORD)strtoul(argv[3], NULL, 0); }
            return sd_exp_prewrite(sh, lba);
        }
        if ((argc >= 3) && (strcmp(argv[2], "dma") == 0))
        {
            uint32 n = 2, rd = 0;
            if (argc >= 4) { lba = (DWORD)strtoul(argv[3], NULL, 0); }
            if (argc >= 5) { n   = (uint32)strtoul(argv[4], NULL, 0); }
            if (argc >= 6) { rd  = ((argv[5][0] == 'r') || (argv[5][0] == 'R')) ? 1U : 0U; }
            return sd_exp_dma(sh, lba, n, rd);
        }
        shellPrint(sh, "usage: sd exp r|w|pre|dma ...\r\n");
        return -1;
    }
    if (strcmp(argv[1], "portinfo") == 0)
    {
        return sd_cmd_portinfo(sh);
    }
    if (strcmp(argv[1], "mpeek") == 0)
    {
        uint32 addr = (argc >= 3) ? (uint32)strtoul(argv[2], NULL, 16) : 0xF003B400UL;
        uint32 n    = (argc >= 4) ? (uint32)strtoul(argv[3], NULL, 0) : 8U;
        uint32 i;
        for (i = 0; i < n; i++)
        {
            uint32 v = *(volatile uint32 *)(uintptr_t)(addr + i * 4U);
            shellPrint(sh, "%08lX=%08lX ", (unsigned long)(addr + i * 4U), (unsigned long)v);
            if ((i & 1U) == 1U) { shellPrint(sh, "\r\n"); }
        }
        shellPrint(sh, "\r\n");
        return 0;
    }
    if (strcmp(argv[1], "poke") == 0)
    {
        /* raw SDMMC register poke: sd poke <byteOffsetHex> <valueHex> [8|16|32] */
        uint32 off, val, w = 32;
        volatile uint8  *p8  = (volatile uint8  *)(0xF02B0000UL);
        if (argc < 4)
        {
            shellPrint(sh, "usage: sd poke <byteOffsetHex> <valueHex> [8|16|32]\r\n");
            return -1;
        }
        off = (uint32)strtoul(argv[2], NULL, 16);
        val = (uint32)strtoul(argv[3], NULL, 16);
        if (argc >= 5) { w = (uint32)strtoul(argv[4], NULL, 0); }
        if (w == 8U)
        {
            p8[off] = (uint8)val;
            shellPrint(sh, "[%03lX] <- 0x%02lX (rd 0x%02lX)\r\n", (unsigned long)off,
                       (unsigned long)(val & 0xFFU), (unsigned long)p8[off]);
        }
        else if (w == 16U)
        {
            volatile uint16 *p16 = (volatile uint16 *)(0xF02B0000UL);
            p16[off / 2U] = (uint16)val;
            shellPrint(sh, "[%03lX] <- 0x%04lX (rd 0x%04lX)\r\n", (unsigned long)off,
                       (unsigned long)(val & 0xFFFFU), (unsigned long)p16[off / 2U]);
        }
        else
        {
            volatile uint32 *p32 = (volatile uint32 *)(0xF02B0000UL);
            p32[off / 4U] = val;
            shellPrint(sh, "[%03lX] <- 0x%08lX (rd 0x%08lX)\r\n", (unsigned long)off,
                       (unsigned long)val, (unsigned long)p32[off / 4U]);
        }
        return 0;
    }
    if (strcmp(argv[1], "peek") == 0)
    {
        uint32 off = (argc >= 3) ? (uint32)strtoul(argv[2], NULL, 16) : 0U;
        uint32 n   = (argc >= 4) ? (uint32)strtoul(argv[3], NULL, 0) : 16U;
        uint32 i;
        volatile uint32 *p32 = (volatile uint32 *)(0xF02B0000UL);
        for (i = 0; i < n; i++)
        {
            shellPrint(sh, "%03lX=%08lX ", (unsigned long)(off + i * 4U),
                       (unsigned long)p32[(off / 4U) + i]);
            if ((i & 1U) == 1U) { shellPrint(sh, "\r\n"); }
        }
        shellPrint(sh, "\r\n");
        return 0;
    }
    if (strcmp(argv[1], "tx") == 0)
    {
        DWORD lba;
        UINT count = 1;
        if (argc < 5)
        {
            shellPrint(sh, "usage: sd tx <pio|dma> <r|w> <lba> [count 1..8]\r\n");
            return -1;
        }
        lba = (DWORD)strtoul(argv[4], NULL, 0);
        if (argc >= 6)
        {
            count = (UINT)strtoul(argv[5], NULL, 0);
        }
        return sd_cmd_tx(sh, argv[2], argv[3], lba, count);
    }
    if (strcmp(argv[1], "chk") == 0)
    {
        DWORD lba;
        UINT i;
        int bad = -1;
        if (argc < 3)
        {
            shellPrint(sh, "usage: sd chk <lba>\r\n");
            return -1;
        }
        lba = (DWORD)strtoul(argv[2], NULL, 0);
        memset(s_chkBuf, 0, sizeof(s_chkBuf));
        if (sd_pio_read(sh, lba, s_chkBuf) != 0)
        {
            shellPrint(sh, "chk: read failed\r\n");
            return -1;
        }
        for (i = 0; i < 128U; i++)
        {
            uint32 exp = (uint32)i ^ (uint32)lba;
            if (s_chkBuf[i] != exp) { bad = (int)i; break; }
        }
        if (bad < 0)
        {
            shellPrint(sh, "chk lba=%lu: MATCH (128 words)\r\n", (unsigned long)lba);
            return 0;
        }
        shellPrint(sh, "chk lba=%lu: MISMATCH word %d got=0x%08lX exp=0x%08lX\r\n",
                   (unsigned long)lba, bad, (unsigned long)s_chkBuf[bad],
                   (unsigned long)((uint32)bad ^ (uint32)lba));
        return -1;
    }
    if (strcmp(argv[1], "host") == 0)
    {
        Ifx_SDMMC *p = &MODULE_SDMMC0;
        if ((argc < 3) || (strcmp(argv[2], "print") == 0))
        {
            shellPrint(sh, "HOST1=0x%02X HOST2=0x%04X CLKCTL=0x%04X MBIU=0x%08lX CAP1=0x%08lX\r\n",
                       (unsigned)p->HOST_CTRL1.U, (unsigned)p->HOST_CTRL2.U,
                       (unsigned)p->CLK_CTRL.U, (unsigned long)p->MBIU_CTRL.U,
                       (unsigned long)p->CAPABILITIES1.U);
            return 0;
        }
        if (strcmp(argv[2], "v4") == 0)
        {
            uint32 v = (argc >= 4) ? (uint32)strtoul(argv[3], NULL, 0) : 0U;
            p->HOST_CTRL2.B.HOST_VER4_ENABLE = (v != 0U) ? 1U : 0U;
            shellPrint(sh, "HOST_VER4_ENABLE -> %u (HOST2=0x%04X)\r\n",
                       (unsigned)p->HOST_CTRL2.B.HOST_VER4_ENABLE, (unsigned)p->HOST_CTRL2.U);
            return 0;
        }
        if (strcmp(argv[2], "preset") == 0)
        {
            uint32 v = (argc >= 4) ? (uint32)strtoul(argv[3], NULL, 0) : 0U;
            p->HOST_CTRL2.B.PRESET_VAL_ENABLE = (v != 0U) ? 1U : 0U;
            shellPrint(sh, "PRESET_VAL_ENABLE -> %u (HOST2=0x%04X CLKCTL=0x%04X)\r\n",
                       (unsigned)p->HOST_CTRL2.B.PRESET_VAL_ENABLE,
                       (unsigned)p->HOST_CTRL2.U, (unsigned)p->CLK_CTRL.U);
            return 0;
        }
        if (strcmp(argv[2], "dsel") == 0)
        {
            uint32 v = (argc >= 4) ? (uint32)strtoul(argv[3], NULL, 0) : 0U;
            p->HOST_CTRL1.B.DMA_SEL = v & 3U;
            shellPrint(sh, "DMA_SEL -> %u (HOST1=0x%02X)\r\n",
                       (unsigned)p->HOST_CTRL1.B.DMA_SEL, (unsigned)p->HOST_CTRL1.U);
            return 0;
        }
        if (strcmp(argv[2], "mbui") == 0)
        {
            uint32 v = (argc >= 4) ? (uint32)strtoul(argv[3], NULL, 0) : 0U;
            p->MBIU_CTRL.U = v;
            shellPrint(sh, "MBIU_CTRL -> 0x%08lX\r\n", (unsigned long)p->MBIU_CTRL.U);
            return 0;
        }
        shellPrint(sh, "usage: sd host [v4|dsel|preset|mbui|print] [val]\r\n");
        return -1;
    }
    shellPrint(sh, "unknown sd subcmd '%s'\r\n", argv[1]);
    sd_usage(sh);
    return -1;
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                 sd, cmd_sd, SD card tests (SDMMC0+FatFs, try 'sd' for usage));

/** \brief Configure CD pin P10.7 as input pull-up. Called once at boot. */
void SdShell_InitPins(void)
{
    IfxPort_setPinMode(SD_CD_MODULE, SD_CD_PIN, IfxPort_Mode_inputPullUp);
}
