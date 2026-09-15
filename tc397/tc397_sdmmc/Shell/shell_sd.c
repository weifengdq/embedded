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
