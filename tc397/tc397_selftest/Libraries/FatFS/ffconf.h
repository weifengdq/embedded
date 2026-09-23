/*---------------------------------------------------------------------------/
/  FatFs R0.16 configuration for tc397_sdmmc (TC397XX + SDMMC0 + 128GB TF)
/  Based on ref/fatfs R0.16 default ffconf.h (FFCONF_DEF 80386).
/  Changes vs default:
/---------------------------------------------------------------------------*/

#define FFCONF_DEF	80386	/* Revision ID - must match ff.h FF_DEFINED */

/*---------------------------------------------------------------------------/
/ Function Configurations
/---------------------------------------------------------------------------*/

#define FF_FS_READONLY	0
#define FF_FS_MINIMIZE	0
#define FF_USE_FIND		0
#define FF_USE_MKFS		1	/* sd mkfs needs f_mkfs() */
#define FF_USE_FASTSEEK	0
#define FF_USE_EXPAND	0
#define FF_USE_CHMOD	0
#define FF_USE_LABEL	1	/* sd label get/set */
#define FF_USE_FORWARD	0
#define FF_USE_STRFUNC	0
#define FF_PRINT_LLI	0
#define FF_PRINT_FLOAT	0
#define FF_STRF_ENCODE	0

/*---------------------------------------------------------------------------/
/ Locale and Namespace Configurations
/---------------------------------------------------------------------------*/

#define FF_CODE_PAGE	437	/* U.S. English; keeps OEM table small, fine for test */

#define FF_USE_LFN		1	/* LFN with static BSS buffer (NOT thread-safe, OK bare-metal) */
#define FF_MAX_LFN		255
#define FF_LFN_UNICODE	0	/* ANSI/OEM API (TCHAR = char) */
#define FF_LFN_BUF		255
#define FF_SFN_BUF		12
#define FF_FS_RPATH		0
#define FF_PATH_DEPTH	10

/*---------------------------------------------------------------------------/
/ Drive/Volume Configurations
/---------------------------------------------------------------------------*/

#define FF_VOLUMES		1
#define FF_STR_VOLUME_ID	0
#define FF_VOLUME_STRS		"RAM","NAND","CF","SD","SD2","USB","USB2","USB3"
#define FF_MULTI_PARTITION	0
#define FF_MIN_SS		512
#define FF_MAX_SS		512
#define FF_LBA64		1	/* 64-bit LBA (needs exFAT); harmless for 128GB, future-proof */
#define FF_MIN_GPT		0x10000000
#define FF_USE_TRIM		0

/*---------------------------------------------------------------------------/
/ System Configurations
/---------------------------------------------------------------------------*/

#define FF_FS_TINY		0
#define FF_FS_EXFAT		1	/* exFAT for SDXC 128GB (needs LFN >= 1) */
#define FF_FS_NORTC		1	/* no RTC on board: fixed timestamp below */
#define FF_NORTC_MON	9
#define FF_NORTC_MDAY	15
#define FF_NORTC_YEAR	2026
#define FF_FS_CRTIME	0
#define FF_FS_NOFSINFO	0
#define FF_FS_LOCK		0
#define FF_FS_REENTRANT	0
#define FF_FS_TIMEOUT	1000

/*--- End of configuration options ---*/
