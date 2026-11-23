/*
 * canopen_sdo_seg - CANopen 分段 SDO（segmented transfer）吞吐实测：1 KB vs 4 KB
 *
 * 硬件: hpm5e31_1，CAN0(PD00/PD01) <-> CAN1(PD05/PD04) 同一段总线。
 *   - CAN0: CANopen 从站节点 1，500 kbit/s；厂商区 0x2001 = 1024 B domain，
 *           0x2002 = 4096 B domain（各 1 个 VAR 对象，dataLength>4 时 CiA 301 自动走分段传输）；
 *           0x2000 = 128 x UNSIGNED64 = 1024 B 数组（对照：数组对象需要逐 subindex 访问）。
 *   - CAN1: components/app_canopen 最小主站，实现 SDO client 的分段 upload/download，
 *           用 mcycle 时基测量总耗时，按 7 B/段估算段数与帧数。
 *
 * 命令: run [iterations] / stat / sdo
 * 上电自动 run 5：1 KB / 4 KB x upload / download，各 5 轮取最佳值。
 */
#include <stdio.h>
#include <string.h>

#include "bsp.h"
#include "canopen_node.h"
#include "co_master.h"
#include "CANopen.h"
#include "OD.h"
#include "hpm_csr_drv.h"
#include "ext/board_can.h"

#define DUT_CAN   BOARD_CAN0_BASE
#define TEST_CAN  BOARD_CAN1_BASE
#define DUT_NODE  1U
#define BUF_MAX   4096U

static co_master_t s_master;
static uint32_t s_pass;
static uint32_t s_fail;
static uint8_t s_buf[BUF_MAX];
static uint8_t s_ref[BUF_MAX];

static uint32_t s_up1k_best_us, s_up4k_best_us, s_dn1k_best_us, s_dn4k_best_us;
static uint32_t s_exp_ops, s_exp_us;

static uint32_t now_us(void)
{
    uint32_t mhz = (s_master.cpu_mhz != 0U) ? s_master.cpu_mhz : 480U;
    return (uint32_t) (hpm_csr_get_core_mcycle() / (uint64_t) mhz);
}

static void app_service(void)
{
    co_node_reset_t rst = co_node_poll();
    if (rst == CO_NODE_RESET_COMM) {
        (void) co_node_reset_comm();
    } else if (rst == CO_NODE_RESET_APP) {
        bsp_reboot();
    }
}

static void check(const char *name, int ok, const char *detail)
{
    if (ok) {
        s_pass++;
    } else {
        s_fail++;
    }
    printf("  %-40s %s  %s\r\n", name, ok ? "PASS" : "FAIL", detail ? detail : "");
}

static void fill_pattern(uint8_t *buf, uint32_t len, uint32_t mul, uint32_t add)
{
    for (uint32_t i = 0; i < len; i++) {
        buf[i] = (uint8_t) ((i * mul + add) & 0xFFU);
    }
}

/* 分段 upload：返回最佳耗时（us），成功时校验图案 */
static int bench_upload(uint16_t index, uint32_t len, uint32_t iterations, uint32_t mul, uint32_t add,
                        uint32_t *best_us)
{
    uint32_t best = 0xFFFFFFFFU;
    int ok = 1;

    fill_pattern(s_ref, len, mul, add);
    for (uint32_t it = 0; it < iterations; it++) {
        uint32_t got_len = 0U;
        uint32_t t0 = now_us();
        int r = co_master_sdo_upload(&s_master, DUT_NODE, index, 0U, s_buf, BUF_MAX, &got_len, 5000U);
        uint32_t dt = now_us() - t0;
        if (r != 0) {
            return r;
        }
        if ((got_len != len) || (memcmp(s_buf, s_ref, len) != 0)) {
            ok = 0;
        }
        if (dt < best) {
            best = dt;
        }
    }
    *best_us = best;
    return ok ? 0 : -100;
}

/* 分段 download：返回最佳耗时（us），并回读校验 */
static int bench_download(uint16_t index, uint32_t len, uint32_t iterations, uint32_t mul, uint32_t add,
                          uint32_t *best_us)
{
    uint32_t best = 0xFFFFFFFFU;
    int ok = 1;

    fill_pattern(s_ref, len, mul, add);
    for (uint32_t it = 0; it < iterations; it++) {
        uint32_t t0 = now_us();
        int r = co_master_sdo_download(&s_master, DUT_NODE, index, 0U, s_ref, len, 5000U);
        uint32_t dt = now_us() - t0;
        if (r != 0) {
            return r;
        }
        if (dt < best) {
            best = dt;
        }
    }
    {
        uint32_t got_len = 0U;
        if (co_master_sdo_upload(&s_master, DUT_NODE, index, 0U, s_buf, BUF_MAX, &got_len, 5000U) != 0
            || got_len != len || memcmp(s_buf, s_ref, len) != 0) {
            ok = 0;
        }
    }
    *best_us = best;
    return ok ? 0 : -101;
}

static void print_row(const char *obj, uint32_t len, const char *dir, uint32_t iters, uint32_t us, int ok)
{
    double thr = (us != 0U) ? ((double) len * 1000000.0 / (double) us) : 0.0;
    printf("  %-10s %5u B %-8s %2u   %8.1f ms  %9.0f B/s  %s\r\n", obj, (unsigned) len, dir, (unsigned) iters,
           (double) us / 1000.0, thr, ok ? "OK" : "MISMATCH");
}

static void run_bench(uint32_t iterations)
{
    char buf[160];
    int r;
    uint32_t seg1071 = (1024U + 6U) / 7U;
    uint32_t seg4079 = (4096U + 6U) / 7U;
    uint32_t thr1k, thr4k;

    s_pass = 0U;
    s_fail = 0U;
    printf("\r\n=== CANopen segmented SDO bench (%u iterations, 500 kbit/s) ===\r\n", (unsigned) iterations);

    r = bench_upload(0x2001U, 1024U, iterations, 1U, 0U, &s_up1k_best_us);
    snprintf(buf, sizeof(buf), "rc=%d abort=0x%08X", r, (unsigned) s_master.last_abort_code);
    check("upload 0x2001 1024 B 图案校验", r == 0, buf);

    r = bench_upload(0x2002U, 4096U, iterations, 7U, 0U, &s_up4k_best_us);
    snprintf(buf, sizeof(buf), "rc=%d abort=0x%08X", r, (unsigned) s_master.last_abort_code);
    check("upload 0x2002 4096 B 图案校验", r == 0, buf);

    r = bench_download(0x2001U, 1024U, iterations, 3U, 1U, &s_dn1k_best_us);
    snprintf(buf, sizeof(buf), "rc=%d 回读一致", r);
    check("download 0x2001 1024 B 回读一致", r == 0, buf);

    r = bench_download(0x2002U, 4096U, iterations, 5U, 3U, &s_dn4k_best_us);
    snprintf(buf, sizeof(buf), "rc=%d abort=0x%08X", r, (unsigned) s_master.last_abort_code);
    check("download 0x2002 4096 B 回读一致", r == 0, buf);

    thr1k = (s_up1k_best_us != 0U) ? (uint32_t) (1024U * 1000000ULL / s_up1k_best_us) : 0U;
    thr4k = (s_up4k_best_us != 0U) ? (uint32_t) (4096U * 1000000ULL / s_up4k_best_us) : 0U;
    snprintf(buf, sizeof(buf), "1KB=%u B/s 4KB=%u B/s", (unsigned) thr1k, (unsigned) thr4k);
    check("分段 upload 吞吐 > 1000 B/s", (thr1k > 1000U) && (thr4k > 1000U), buf);

    printf("\r\n  对象       大小   方向     轮数  最佳耗时     吞吐        结果\r\n");
    printf("  ---------- ------ -------- ---- ------------ ------------ -------\r\n");
    print_row("0x2001", 1024U, "upload", iterations, s_up1k_best_us, 1);
    print_row("0x2002", 4096U, "upload", iterations, s_up4k_best_us, 1);
    print_row("0x2001", 1024U, "download", iterations, s_dn1k_best_us, 1);
    print_row("0x2002", 4096U, "download", iterations, s_dn4k_best_us, 1);
    printf("  段数估算: 1024 B = %u 段 x 7 B；4096 B = %u 段 x 7 B（每段 1 请求 + 1 响应）\r\n",
           (unsigned) seg1071, (unsigned) seg4079);

    /* expedited 小对象速率（0x1017，2 B） */
    {
        uint32_t n = 200U;
        uint32_t v = 0U;
        uint32_t t0 = now_us();
        uint32_t ok = 0U;
        for (uint32_t i = 0; i < n; i++) {
            if (co_master_sdo_read(&s_master, DUT_NODE, 0x1017U, 0U, &v, 200U) == 0) {
                ok++;
            }
        }
        s_exp_us = now_us() - t0;
        s_exp_ops = ok;
        snprintf(buf, sizeof(buf), "%u/%u 次, %.2f ms/次, %.0f 次/s", (unsigned) ok, (unsigned) n,
                 (double) s_exp_us / 1000.0 / (double) n, (double) ok * 1000000.0 / (double) s_exp_us);
        check("expedited read 0x1017 x200", ok == n, buf);
    }

    printf("=== RESULT: %u PASS / %u FAIL ===\r\n", (unsigned) s_pass, (unsigned) s_fail);
}

static void cmd_run(int argc, char *argv[])
{
    uint32_t iters = 5U;
    if (argc >= 2) {
        if (bsp_parse_u32(argv[1], &iters) || iters < 1U || iters > 20U) {
            printf("usage: run [iterations 1..20]\r\n");
            return;
        }
    }
    run_bench(iters);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 run, cmd_run, run segmented SDO bench: run [iterations]);

static void cmd_stat(void)
{
    co_node_stats_t st;
    co_node_get_stats(&st);
    printf("nmt=%u rx=%u tx=%u retry=%u heap=%uB canerr=0x%04X\r\n", (unsigned) st.nmt_state,
           (unsigned) st.rx_frames, (unsigned) st.tx_frames, (unsigned) st.tx_retry, (unsigned) st.heap_bytes,
           (unsigned) st.can_error_status);
    printf("sdo: up1k=%u us up4k=%u us dn1k=%u us dn4k=%u us expedited=%u/%u in %u us\r\n",
           (unsigned) s_up1k_best_us, (unsigned) s_up4k_best_us, (unsigned) s_dn1k_best_us,
           (unsigned) s_dn4k_best_us, (unsigned) s_exp_ops, 200U, (unsigned) s_exp_us);
    printf("domain: 0x2001[0..7]=");
    for (int i = 0; i < 8; i++) {
        printf("%02X", OD_RAM.x2001_vendorDomain1k[i]);
    }
    printf(" 0x2002[0..7]=");
    for (int i = 0; i < 8; i++) {
        printf("%02X", OD_RAM.x2002_vendorDomain4k[i]);
    }
    printf("\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 stat, cmd_stat, segmented SDO statistics);

static void cmd_sdo(void)
{
    uint32_t v = 0U;
    printf("SDO read 0x1000 = 0x%08X (rc=%d)\r\n",
           (unsigned) ((co_master_sdo_read(&s_master, DUT_NODE, 0x1000U, 0U, &v, 200U) == 0) ? v : 0U), 0);
    printf("SDO read 0x1017 = %u ms\r\n", (unsigned) ((co_master_sdo_read(&s_master, DUT_NODE, 0x1017U, 0U, &v, 200U) == 0) ? v : 0U));
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 sdo, cmd_sdo, quick SDO access check);

int main(void)
{
    co_node_cfg_t cfg;
    int r;

    board_init();
    bsp_init();

    printf("\r\n[app] canopen_sdo_seg (CANopenNode v%d.%d, node %u, segmented SDO)\r\n", CO_VERSION_MAJOR,
           CO_VERSION_MINOR, (unsigned) DUT_NODE);
    memset(&cfg, 0, sizeof(cfg));
    cfg.can_base = DUT_CAN;
    cfg.node_id = DUT_NODE;
    cfg.bitrate_kbps = 500U;
    cfg.heartbeat_ms = 1000U;
    cfg.nmt_start_operational = 1U;
    r = co_node_init(&cfg);
    printf("[app] co_node_init=%d\r\n", r);
    r = co_master_init(&s_master, TEST_CAN, 500U);
    printf("[app] co_master_init=%d (CAN1 master)\r\n", r);
    co_master_set_node(&s_master, DUT_NODE);
    co_master_set_service(&s_master, app_service);

    for (uint32_t i = 0U; i < sizeof(OD_RAM.x2001_vendorDomain1k); i++) {
        OD_RAM.x2001_vendorDomain1k[i] = (uint8_t) (i & 0xFFU);
    }
    for (uint32_t i = 0U; i < sizeof(OD_RAM.x2002_vendorDomain4k); i++) {
        OD_RAM.x2002_vendorDomain4k[i] = (uint8_t) ((i * 7U) & 0xFFU);
    }
    printf("[app] canopen_sdo_seg ready (shell: run [iters] | stat | sdo)\r\n");

    run_bench(5U);

    while (1) {
        bsp_shell_poll();
        app_service();
        co_master_poll(&s_master);
    }
}
