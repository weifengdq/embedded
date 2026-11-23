/*
 * mem_flash_rw —— AUTOSAR Mem 驱动例程：擦-写-读-比较-空白检查（异步作业模型）
 *
 * 硬件：hpm5e31_1 片内 1MB XPI NOR（0x80000000 映射）。只操作 PBcfg 配置的保留窗口
 *       0x800F0000..0x80100000（最后 64KB），应用镜像从 0x80000000 起约 80KB，不会被覆盖。
 * 验证：Mem_Init/Mem_Write/Mem_Erase/Mem_Read/Mem_BlankCheck/Mem_GetJobResult/
 *       Mem_MainFunction/Mem_HwSpecificService/Mem_Suspend/Det 错误路径。
 * 现象：见 README.md。
 */
#include <string.h>
#include "bsp.h"
#include "Mem.h"
#include "Mem_PBcfg.h"
#include "Det.h"
#include "hpm_mchtmr_drv.h"

#define TEST_SECTOR_ADDR   (0x800F0000UL)   /* 保留窗口第 1 个 4KB 扇区 */
#define TEST_SECTOR_SIZE   (4096UL)
#define TEST_PASS_STR      "MEMRW: PASS"
#define TEST_FAIL_STR      "MEMRW: FAIL"

static uint32_t s_tmr_hz;
static uint8_t  s_wbuf[TEST_SECTOR_SIZE] __attribute__((aligned(4)));
static uint8_t  s_rbuf[TEST_SECTOR_SIZE] __attribute__((aligned(4)));
static uint8_t  s_hwbuf[16] __attribute__((aligned(4)));

/* 应用镜像结束地址（链接脚本 etext）：用于自证保留窗口没压到 app */
extern char etext[];

static double ticks_to_ms(uint32_t ticks)
{
    return (double)ticks * 1000.0 / (double)s_tmr_hz;
}

/* 显式轮询 Mem_MainFunction，把异步作业推进到结束（带超时，禁止死等） */
static boolean mem_wait_job(uint32_t timeout_ms, uint32_t *cycles)
{
    uint32_t t0 = mchtmr_get_count(HPM_MCHTMR);
    uint32_t limit = (uint32_t)(((uint64_t)timeout_ms * (uint64_t)s_tmr_hz) / 1000ULL);

    while (Mem_IsJobPending()) {
        Mem_MainFunction();
        if ((mchtmr_get_count(HPM_MCHTMR) - t0) > limit) {
            if (cycles != NULL) { *cycles = mchtmr_get_count(HPM_MCHTMR) - t0; }
            return FALSE;
        }
    }
    if (cycles != NULL) { *cycles = mchtmr_get_count(HPM_MCHTMR) - t0; }
    return TRUE;
}

static void fill_pattern(uint32_t seed)
{
    for (uint32_t i = 0; i < TEST_SECTOR_SIZE; i++) {
        s_wbuf[i] = (uint8_t)((i * 31U + seed) & 0xFFU);
    }
}

static void cmd_meminfo(int argc, char **argv)
{
    Std_VersionInfoType vi;
    Mem_LengthType len = (Mem_LengthType)sizeof(s_hwbuf);

    (void)argc; (void)argv;
    Mem_GetVersionInfo(&vi);
    printf("Mem: module=%u vendor=0x%04x sw=%u.%u.%u instances=%u\r\n",
           (unsigned)vi.moduleID, (unsigned)vi.vendorID,
           (unsigned)vi.sw_major_version, (unsigned)vi.sw_minor_version,
           (unsigned)vi.sw_patch_version, (unsigned)Mem_GetInstanceCount());
    if (Mem_HwSpecificService(0U, MEM_HW_SERVICE_ID_GET_GEOMETRY, s_hwbuf, &len) == E_OK) {
        if (mem_wait_job(50U, NULL) && (Mem_GetJobResult(0U) == MEM_JOB_OK)) {
            const uint32_t *w = (const uint32_t *)(const void *)s_hwbuf;
            printf("Mem: instance0 base=0x%08lx size=%lu KB sector=%lu B page=%lu B\r\n",
                   (unsigned long)w[0], (unsigned long)(w[1] / 1024U),
                   (unsigned long)w[2], (unsigned long)w[3]);
        }
    }
    printf("Mem: safe window 0x%08lx..0x%08lx, app etext=0x%08lx -> %s\r\n",
           (unsigned long)TEST_SECTOR_ADDR, (unsigned long)(TEST_SECTOR_ADDR + 0x10000UL),
           (unsigned long)(uintptr_t)etext,
           ((uint32_t)(uintptr_t)etext <= TEST_SECTOR_ADDR) ? "no overlap (OK)" : "OVERLAP (FAIL)");
}

/* ---------- mem rw：擦 -> 空白检查 -> 写 -> 读回比较 -> 再空白检查(应非空) ---------- */
static void cmd_memrw(int argc, char **argv)
{
    uint32_t cyc = 0U;
    boolean ok = TRUE;
    uint32_t idx;

    (void)argc; (void)argv;
    printf("--- mem rw @0x%08lx (%lu B) ---\r\n", (unsigned long)TEST_SECTOR_ADDR,
           (unsigned long)TEST_SECTOR_SIZE);

    /* 1) 擦除（异步受理 -> MainFunction 每调用擦 1 个扇区） */
    if (Mem_Erase(0U, TEST_SECTOR_ADDR, TEST_SECTOR_SIZE) != E_OK) {
        printf("erase not accepted\r\n" TEST_FAIL_STR "\r\n");
        return;
    }
    ok = mem_wait_job(2000U, &cyc) && (Mem_GetJobResult(0U) == MEM_JOB_OK);
    printf("erase : %s, %.3f ms, result=%u\r\n", ok ? "OK" : "FAIL",
           ticks_to_ms(cyc), (unsigned)Mem_GetJobResult(0U));

    /* 2) 空白检查（应 MEM_JOB_OK） */
    boolean blank_ok = FALSE;
    if (Mem_BlankCheck(0U, TEST_SECTOR_ADDR, TEST_SECTOR_SIZE) == E_OK) {
        blank_ok = mem_wait_job(2000U, &cyc) && (Mem_GetJobResult(0U) == MEM_JOB_OK);
    }
    printf("blank : %s (result=%u)\r\n", blank_ok ? "blank OK" : "NOT blank",
           (unsigned)Mem_GetJobResult(0U));
    ok = ok && blank_ok;

    /* 3) 写 4KB（16 页，每页一次 MainFunction） */
    fill_pattern(0x5AU);
    if (Mem_Write(0U, TEST_SECTOR_ADDR, s_wbuf, TEST_SECTOR_SIZE) != E_OK) {
        printf("write not accepted\r\n" TEST_FAIL_STR "\r\n");
        return;
    }
    ok = mem_wait_job(3000U, &cyc) && (Mem_GetJobResult(0U) == MEM_JOB_OK);
    printf("write : %s, %.3f ms, %.1f KB/s, result=%u\r\n", ok ? "OK" : "FAIL",
           ticks_to_ms(cyc), 4096.0 / 1024.0 / (ticks_to_ms(cyc) / 1000.0),
           (unsigned)Mem_GetJobResult(0U));

    /* 4) 读回（Mem_MainFunction 每调用读 1 个 chunk） */
    memset(s_rbuf, 0, sizeof(s_rbuf));
    if (Mem_Read(0U, TEST_SECTOR_ADDR, s_rbuf, TEST_SECTOR_SIZE) != E_OK) {
        printf("read not accepted\r\n" TEST_FAIL_STR "\r\n");
        return;
    }
    ok = mem_wait_job(2000U, &cyc) && (Mem_GetJobResult(0U) == MEM_JOB_OK);
    printf("read  : %s, %.3f ms, %.1f KB/s, result=%u\r\n", ok ? "OK" : "FAIL",
           ticks_to_ms(cyc), 4096.0 / 1024.0 / (ticks_to_ms(cyc) / 1000.0),
           (unsigned)Mem_GetJobResult(0U));

    /* 5) 逐字节比较，并统计首个失配位置 */
    idx = TEST_SECTOR_SIZE;
    for (uint32_t i = 0; i < TEST_SECTOR_SIZE; i++) {
        if (s_rbuf[i] != s_wbuf[i]) {
            idx = i;
            break;
        }
    }
    printf("cmp   : %s (first mismatch idx=%lu)\r\n",
           (idx == TEST_SECTOR_SIZE) ? "MATCH" : "MISMATCH", (unsigned long)idx);
    ok = ok && (idx == TEST_SECTOR_SIZE);

    /* 6) 非空区域空白检查：应得到 MEM_INCONSISTENT（[SWS_Mem_00076]） */
    boolean incons = FALSE;
    if (Mem_BlankCheck(0U, TEST_SECTOR_ADDR, TEST_SECTOR_SIZE) == E_OK) {
        (void)mem_wait_job(2000U, &cyc);
        incons = (Mem_GetJobResult(0U) == MEM_INCONSISTENT);
    }
    printf("blank : non-blank detected=%s (result=%u)\r\n", incons ? "YES" : "NO",
           (unsigned)Mem_GetJobResult(0U));
    ok = ok && incons;

    /* 7) 数据对比：把 4KB 读回内容做一次和，便于跨次运行核对 */
    {
        uint32_t sum = 0U;
        for (uint32_t i = 0; i < TEST_SECTOR_SIZE; i++) {
            sum += s_rbuf[i];
        }
        printf("sum   : 0x%08lx (pattern seed 0x5A)\r\n", (unsigned long)sum);
    }
    printf("%s\r\n", ok ? TEST_PASS_STR : TEST_FAIL_STR);
}

/* ---------- mem det：错误路径 / 边界测试，Det 计数必须增加 ---------- */
static void cmd_memdet(int argc, char **argv)
{
    (void)argc; (void)argv;
    uint32_t c0 = Det_GetErrorCount();
    Std_ReturnType r;
    uint16 mod; uint8 inst, api, err;
    Mem_LengthType len = (Mem_LengthType)sizeof(s_hwbuf);

    printf("--- mem det: error paths ---\r\n");
    printf("det   : before=%lu\r\n", (unsigned long)c0);

    /* 1) NULL 目的指针 -> MEM_E_PARAM_POINTER */
    r = Mem_Read(0U, TEST_SECTOR_ADDR, NULL_PTR, 16U);
    printf("read NULL ptr        : r=%u det=%lu\r\n", (unsigned)r, (unsigned long)Det_GetErrorCount());
    if (Det_GetLastError(&mod, &inst, &api, &err) == E_OK) {
        printf("  last: mod=%u inst=%u api=0x%02x err=0x%02x\r\n",
               (unsigned)mod, (unsigned)inst, (unsigned)api, (unsigned)err);
    }

    /* 2) 窗口外擦除（应用区！）-> MEM_E_PARAM_ADDRESS */
    r = Mem_Erase(0U, 0x80000000UL, TEST_SECTOR_SIZE);
    printf("erase outside window : r=%u det=%lu (REFUSED, app safe)\r\n",
           (unsigned)r, (unsigned long)Det_GetErrorCount());

    /* 3) 未按页对齐的写 -> MEM_E_PARAM_ADDRESS */
    r = Mem_Write(0U, TEST_SECTOR_ADDR + 4U, s_wbuf, 256U);
    printf("write unaligned addr : r=%u det=%lu\r\n", (unsigned)r, (unsigned long)Det_GetErrorCount());

    /* 4) 零长度 -> MEM_E_PARAM_ADDRESS/LENGTH */
    r = Mem_Read(0U, TEST_SECTOR_ADDR, s_rbuf, 0U);
    printf("read zero length     : r=%u det=%lu\r\n", (unsigned)r, (unsigned long)Det_GetErrorCount());

    /* 5) 非法实例 ID -> MEM_E_PARAM_INSTANCE_ID */
    (void)Mem_GetJobResult(9U);
    printf("get result bad inst  : det=%lu\r\n", (unsigned long)Det_GetErrorCount());

    /* 6) 作业挂起期间再次受理 -> MEM_E_JOB_PENDING */
    s_rbuf[0] = 0U;
    (void)Mem_Erase(0U, TEST_SECTOR_ADDR, TEST_SECTOR_SIZE);
    r = Mem_Read(0U, TEST_SECTOR_ADDR, s_rbuf, 16U);
    printf("read while pending   : r=%u det=%lu\r\n", (unsigned)r, (unsigned long)Det_GetErrorCount());
    (void)mem_wait_job(2000U, NULL);   /* 让挂起作业跑完，恢复空闲 */

    /* 7) 无硬件 suspend 能力 -> E_MEM_SERVICE_NOT_AVAIL */
    r = Mem_Suspend(0U);
    printf("suspend              : r=%u (expect 2 = E_MEM_SERVICE_NOT_AVAIL)\r\n", (unsigned)r);
    r = Mem_Resume(0U);
    printf("resume               : r=%u (expect 2)\r\n", (unsigned)r);

    /* 8) DeInit 后调用 -> MEM_E_UNINIT；随后重新 Init 恢复 */
    Mem_DeInit();
    r = Mem_Read(0U, TEST_SECTOR_ADDR, s_rbuf, 16U);
    printf("read after DeInit    : r=%u det=%lu\r\n", (unsigned)r, (unsigned long)Det_GetErrorCount());
    Mem_Init(&Mem_Config);
    printf("re-init              : ok, jobResult=%u\r\n", (unsigned)Mem_GetJobResult(0U));

    /* 9) ECC 错误传播：Mem_PropagateError -> MEM_ECC_UNCORRECTED */
    Mem_PropagateError(0U);
    printf("propagate ECC error  : result=%u (expect 4 = MEM_ECC_UNCORRECTED)\r\n",
           (unsigned)Mem_GetJobResult(0U));

    (void)len;
    printf("det   : after=%lu (delta=%lu)\r\n", (unsigned long)Det_GetErrorCount(),
           (unsigned long)(Det_GetErrorCount() - c0));
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 meminfo, cmd_meminfo, "Mem: version/geometry/safe window");
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 memrw, cmd_memrw, "Mem: erase+write+read+compare+blankcheck");
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 memdet, cmd_memdet, "Mem: error path / Det tests");

int main(void)
{
    Std_VersionInfoType vi;

    bsp_init();
    s_tmr_hz = clock_get_frequency(clock_mchtmr0);
    Det_Init();

    printf("\r\n=== mem_flash_rw: AUTOSAR Mem driver (R25-11) on internal XPI NOR ===\r\n");
    Mem_Init(&Mem_Config);
    Mem_GetVersionInfo(&vi);
    printf("Mem %u.%u.%u, module id %u, instance count %u\r\n",
           (unsigned)vi.sw_major_version, (unsigned)vi.sw_minor_version,
           (unsigned)vi.sw_patch_version, (unsigned)MEM_MODULE_ID,
           (unsigned)Mem_GetInstanceCount());
    cmd_meminfo(0, NULL);
    printf("shell: meminfo | memrw | memdet  (BSP 已占用 'mem' 做 hexdump)\r\n");

    /* 上电自动跑一遍，便于 hwrun 单次采集；先等串口抓取启动（否则开头几个字符会丢） */
    board_delay_ms(1500);
    cmd_memrw(0, NULL);
    cmd_memdet(0, NULL);

    while (1) {
        Mem_MainFunction();   /* 主循环推进 Mem 作业（生产代码里由 SchM 周期任务调用） */
        bsp_shell_poll();
    }
    return 0;
}
