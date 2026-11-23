/*
 * crc_integrity —— 用 Crc 校验一段 Flash 数据：通过 / 失败自证
 *
 * 场景（AUTOSAR 里 Crc 的典型用途：Flash 数据完整性、E2E、NVM 块校验）：
 *  1) image  : 对正在运行的应用镜像 [0x80000000, etext) 算 CRC32/CRC16/CRC8，
 *              HW 与 SW 两条路径结果必须一致（自证 CRC 外设可用）；
 *  2) flash  : 通过 Mem 驱动把已知 pattern 写进保留扇区 0x800F0000，读回后算 CRC32，
 *              与离线算出的黄金值 0x489B7FD2 比对 -> PASS；
 *  3) tamper : 把同一扇区重写成"翻 1 bit"的数据，再算 CRC32 -> 必须与黄金值不同，
 *              报告 TAMPER DETECTED（证明 CRC 能发现单 bit 篡改）；
 *  4) blank  : 擦除后整扇区 0xFF 的 CRC32 应与黄金值 0xF154670A 一致。
 * 现象见 README.md。
 */
#include <string.h>
#include "bsp.h"
#include "Mem.h"
#include "Mem_PBcfg.h"
#include "Crc.h"
#include "hpm_mchtmr_drv.h"

#define TEST_ADDR        (0x800F0000UL)
#define TEST_SIZE        (4096UL)
#define PATTERN_CRC32    (0x489B7FD2UL)   /* pattern[i] = (i*13+0xA5)&0xFF 的 CRC32 黄金值 */
#define PATTERN_CRC16    (0x9AB8U)
#define CORRUPT_CRC32    (0x1CD5CE95UL)   /* pattern[100] ^= 0x01 后的 CRC32 */
#define BLANK_CRC32      (0xF154670AUL)   /* 4096 x 0xFF 的 CRC32 */

static uint8_t s_wbuf[TEST_SIZE] __attribute__((aligned(4)));
static uint8_t s_rbuf[TEST_SIZE] __attribute__((aligned(4)));
static uint32_t s_tmr_hz;

extern char etext[];

static void fill_pattern(uint32_t corrupt)
{
    for (uint32_t i = 0; i < TEST_SIZE; i++) {
        s_wbuf[i] = (uint8_t)((i * 13U + 0xA5U) & 0xFFU);
    }
    if (corrupt != 0U) {
        s_wbuf[100] ^= 0x01U;
    }
}

static boolean mem_wait(uint32_t timeout_ms)
{
    uint32_t t0 = mchtmr_get_count(HPM_MCHTMR);
    uint32_t limit = (uint32_t)(((uint64_t)timeout_ms * (uint64_t)s_tmr_hz) / 1000ULL);
    while (Mem_IsJobPending()) {
        Mem_MainFunction();
        if ((mchtmr_get_count(HPM_MCHTMR) - t0) > limit) {
            return FALSE;
        }
    }
    return TRUE;
}

/* 用 Mem 驱动把 s_wbuf 写进保留扇区并读回 s_rbuf */
static boolean flash_roundtrip(void)
{
    if (Mem_Erase(0U, TEST_ADDR, TEST_SIZE) != E_OK) { return FALSE; }
    if (!mem_wait(2000U) || (Mem_GetJobResult(0U) != MEM_JOB_OK)) { return FALSE; }
    if (Mem_Write(0U, TEST_ADDR, s_wbuf, TEST_SIZE) != E_OK) { return FALSE; }
    if (!mem_wait(3000U) || (Mem_GetJobResult(0U) != MEM_JOB_OK)) { return FALSE; }
    memset(s_rbuf, 0, sizeof(s_rbuf));
    if (Mem_Read(0U, TEST_ADDR, s_rbuf, TEST_SIZE) != E_OK) { return FALSE; }
    if (!mem_wait(2000U) || (Mem_GetJobResult(0U) != MEM_JOB_OK)) { return FALSE; }
    return TRUE;
}

/* 1) 应用镜像 CRC：只读区，安全；验证 HW/SW 一致 */
static void cmd_image(int argc, char **argv)
{
    (void)argc; (void)argv;
    uint32_t len = (uint32_t)((uint32_t)(uintptr_t)etext - 0x80000000UL);
    const uint8_t *img = (const uint8_t *)(uintptr_t)0x80000000UL;
    boolean hwOn[3];

    printf("--- image: CRC over app image 0x80000000..0x%08lx (%lu B) ---\r\n",
           (unsigned long)(uintptr_t)etext, (unsigned long)len);

    Crc_SetHwEnabled(FALSE);
    uint32_t sw32 = Crc_CalculateCRC32(img, len, 0U, TRUE);
    uint16_t sw16 = Crc_CalculateCRC16(img, len, 0U, TRUE);
    uint8_t  sw8  = Crc_CalculateCRC8(img, len, 0U, TRUE);
    Crc_SetHwEnabled(TRUE);
    uint32_t hw32 = Crc_CalculateCRC32(img, len, 0U, TRUE);
    uint16_t hw16 = Crc_CalculateCRC16(img, len, 0U, TRUE);
    uint8_t  hw8  = Crc_CalculateCRC8(img, len, 0U, TRUE);

    hwOn[0] = Crc_IsHwActive(CRC_ALGO_CRC32);
    hwOn[1] = Crc_IsHwActive(CRC_ALGO_CRC16);
    hwOn[2] = Crc_IsHwActive(CRC_ALGO_CRC8);
    printf("image crc32 sw=0x%08lx hw=0x%08lx %s\r\n", (unsigned long)sw32, (unsigned long)hw32,
           (sw32 == hw32) ? "MATCH" : "MISMATCH");
    printf("image crc16 sw=0x%04x hw=0x%04x %s\r\n", (unsigned)sw16, (unsigned)hw16,
           (sw16 == hw16) ? "MATCH" : "MISMATCH");
    printf("image crc8  sw=0x%02x hw=0x%02x %s\r\n", (unsigned)sw8, (unsigned)hw8,
           (sw8 == hw8) ? "MATCH" : "MISMATCH");
    printf("image hw_active: crc32=%d crc16=%d crc8=%d\r\n", hwOn[0], hwOn[1], hwOn[2]);
}

/* 2) Flash 数据 CRC：通过路径 */
static void cmd_flash(int argc, char **argv)
{
    boolean ok;
    uint32_t crc32;
    uint16_t crc16;
    (void)argc; (void)argv;
    printf("--- flash: write pattern to 0x%08lx, read back, CRC ---\r\n", (unsigned long)TEST_ADDR);
    fill_pattern(0U);
    ok = flash_roundtrip();
    if (!ok) {
        printf("flash roundtrip FAILED (Mem job result=%u)\r\n", (unsigned)Mem_GetJobResult(0U));
        printf("crcint flash: FAIL\r\n");
        return;
    }
    crc32 = Crc_CalculateCRC32(s_rbuf, TEST_SIZE, 0U, TRUE);
    crc16 = Crc_CalculateCRC16(s_rbuf, TEST_SIZE, 0U, TRUE);
    printf("flash crc32=0x%08lx golden=0x%08lx %s\r\n", (unsigned long)crc32,
           (unsigned long)PATTERN_CRC32, (crc32 == PATTERN_CRC32) ? "MATCH" : "MISMATCH");
    printf("flash crc16=0x%04x golden=0x%04x %s\r\n", (unsigned)crc16, (unsigned)PATTERN_CRC16,
           (crc16 == PATTERN_CRC16) ? "MATCH" : "MISMATCH");
    printf("crcint flash: %s\r\n", ((crc32 == PATTERN_CRC32) && (crc16 == PATTERN_CRC16)) ? "PASS" : "FAIL");
}

/* 3) 篡改自证：重写翻 1 bit 的数据，CRC 必须变化 */
static void cmd_tamper(int argc, char **argv)
{
    uint32_t crc32;
    (void)argc; (void)argv;
    printf("--- tamper: rewrite sector with 1 bit flipped (byte 100) ---\r\n");
    fill_pattern(1U);
    if (!flash_roundtrip()) {
        printf("crcint tamper: FAIL (flash roundtrip)\r\n");
        return;
    }
    crc32 = Crc_CalculateCRC32(s_rbuf, TEST_SIZE, 0U, TRUE);
    printf("tamper crc32=0x%08lx golden=0x%08lx expected_corrupt=0x%08lx\r\n",
           (unsigned long)crc32, (unsigned long)PATTERN_CRC32, (unsigned long)CORRUPT_CRC32);
    if (crc32 == PATTERN_CRC32) {
        printf("crcint tamper: FAIL (CRC did not change -> no detection)\r\n");
    } else {
        printf("crcint tamper: TAMPER DETECTED (crc32 != golden)\r\n");
    }
}

/* 4) 擦除态校验：整扇区 0xFF 的 CRC 与黄金值比对 */
static void cmd_blank(int argc, char **argv)
{
    uint32_t crc32;
    (void)argc; (void)argv;
    printf("--- blank: erase sector, CRC over 4096 x 0xFF ---\r\n");
    fill_pattern(0U);
    if (Mem_Erase(0U, TEST_ADDR, TEST_SIZE) != E_OK) {
        printf("crcint blank: FAIL (erase not accepted)\r\n");
        return;
    }
    if (!mem_wait(2000U) || (Mem_GetJobResult(0U) != MEM_JOB_OK)) {
        printf("crcint blank: FAIL (erase job)\r\n");
        return;
    }
    memset(s_rbuf, 0, sizeof(s_rbuf));
    if (Mem_Read(0U, TEST_ADDR, s_rbuf, TEST_SIZE) != E_OK) {
        printf("crcint blank: FAIL (read not accepted)\r\n");
        return;
    }
    if (!mem_wait(2000U) || (Mem_GetJobResult(0U) != MEM_JOB_OK)) {
        printf("crcint blank: FAIL (read job)\r\n");
        return;
    }
    crc32 = Crc_CalculateCRC32(s_rbuf, TEST_SIZE, 0U, TRUE);
    printf("blank crc32=0x%08lx golden=0x%08lx %s\r\n", (unsigned long)crc32,
           (unsigned long)BLANK_CRC32, (crc32 == BLANK_CRC32) ? "MATCH" : "MISMATCH");
    printf("crcint blank: %s\r\n", (crc32 == BLANK_CRC32) ? "PASS" : "FAIL");
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 crcimage, cmd_image, "CRC over running app image (hw/sw)");
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 crcflash, cmd_flash, "write pattern to reserved flash, CRC -> PASS");
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 crctamper, cmd_tamper, "flip 1 bit in flash, CRC must change");
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 crcblank, cmd_blank, "CRC over erased sector (0xFF)");

int main(void)
{
    Std_VersionInfoType vi;

    bsp_init();
    s_tmr_hz = clock_get_frequency(clock_mchtmr0);

    printf("\r\n=== crc_integrity: Crc + Mem on internal XPI NOR ===\r\n");
    Crc_Init();
    Mem_Init(&Mem_Config);
    Crc_GetVersionInfo(&vi);
    printf("Crc %u.%u.%u, Mem instance count %u, hw crc32 path=%s\r\n",
           (unsigned)vi.sw_major_version, (unsigned)vi.sw_minor_version, (unsigned)vi.sw_patch_version,
           (unsigned)Mem_GetInstanceCount(),
           Crc_IsHwActive(CRC_ALGO_CRC32) ? "HW" : "SW");
    printf("golden values: pattern crc32=0x%08lx crc16=0x%04x, blank crc32=0x%08lx\r\n",
           (unsigned long)PATTERN_CRC32, (unsigned)PATTERN_CRC16, (unsigned long)BLANK_CRC32);

    board_delay_ms(1500);   /* 等串口抓取启动，避免开头输出被截断 */
    cmd_image(0, NULL);
    cmd_flash(0, NULL);
    cmd_tamper(0, NULL);
    cmd_blank(0, NULL);
    printf("shell: crcimage | crcflash | crctamper | crcblank\r\n");

    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
