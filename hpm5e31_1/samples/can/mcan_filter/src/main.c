/*
 * mcan_filter - MCAN 过滤器：经典/范围/双 ID、标准/扩展、FIFO0/FIFO1、GFC 拒收
 *
 * 硬件: hpm5e31_1，MCAN0 内部环回（不需要外部节点/总线），用 8 个探针 ID 验证
 *       "过滤器命中 -> 进哪个 FIFO / 被丢弃"。
 * 命令: info / test
 */
#include "bsp.h"
#include "hpm_mcan_drv.h"
#include "ext/board_can.h"

#define NODE BOARD_CAN0_BASE

#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
ATTR_PLACE_AT(".ahb_sram") static uint32_t s_msg_buf[MCAN_MSG_BUF_SIZE_IN_WORDS];
#endif

static uint32_t s_pass;
static uint32_t s_fail;

typedef struct {
    uint32_t id;
    uint8_t ext;
    int8_t expect; /* 0=FIFO0, 1=FIFO1, 2=reject */
} probe_t;

static const char *fifo_name(int8_t f)
{
    return (f == 0) ? "FIFO0" : ((f == 1) ? "FIFO1" : "reject");
}

static hpm_stat_t filter_init(const mcan_filter_elem_t *std_f, uint32_t n_std,
                             const mcan_filter_elem_t *ext_f, uint32_t n_ext)
{
    mcan_config_t cfg;
#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
    mcan_msg_buf_attr_t attr = {(uint32_t) s_msg_buf, sizeof(s_msg_buf)};
    mcan_set_msg_buf_attr(NODE, &attr);
#endif
    mcan_get_default_config(NODE, &cfg);
    cfg.mode = mcan_mode_loopback_internal;
    cfg.enable_canfd = false;
    /* 未命中帧一律拒收，才能验证过滤器本身 */
    cfg.all_filters_config.global_filter_config.accept_non_matching_std_frame_option =
        MCAN_ACCEPT_NON_MATCHING_FRAME_OPTION_REJECT;
    cfg.all_filters_config.global_filter_config.accept_non_matching_ext_frame_option =
        MCAN_ACCEPT_NON_MATCHING_FRAME_OPTION_REJECT;
    cfg.all_filters_config.global_filter_config.reject_remote_std_frame = false;
    cfg.all_filters_config.global_filter_config.reject_remote_ext_frame = false;
    cfg.all_filters_config.ext_id_mask = 0x1FFFFFFFUL;
    cfg.all_filters_config.std_id_filter_list.filter_elem_list = std_f;
    cfg.all_filters_config.std_id_filter_list.mcan_filter_elem_count = n_std;
    cfg.all_filters_config.ext_id_filter_list.filter_elem_list = ext_f;
    cfg.all_filters_config.ext_id_filter_list.mcan_filter_elem_count = n_ext;
    board_init_can(NODE);
    uint32_t clk = board_init_can_clock(NODE);
    mcan_deinit(NODE);
    return mcan_init(NODE, &cfg, clk);
}

/* 内部环回发一帧，返回它实际落入的位置：0/1=FIFO，2=被丢弃 */
static int8_t probe_send(const probe_t *p)
{
    mcan_tx_frame_t tx;
    mcan_rx_message_t rx;
    __builtin_memset(&tx, 0, sizeof(tx));
    tx.dlc = 8U;
    if (p->ext) {
        tx.use_ext_id = 1U;
        tx.ext_id = p->id & 0x1FFFFFFFUL;
    } else {
        tx.std_id = (uint16_t) (p->id & 0x7FFU);
    }
    for (uint8_t i = 0; i < 8U; i++) {
        tx.data_8[i] = (uint8_t) (i + 1U);
    }
    if (mcan_transmit_blocking(NODE, &tx) != status_success) {
        return (int8_t) 3; /* 发送失败 */
    }
    bool got0 = (mcan_read_rxfifo(NODE, 0, &rx) == status_success);
    bool got1 = (mcan_read_rxfifo(NODE, 1, &rx) == status_success);
    if (got0 && got1) {
        /* 两个 FIFO 都有帧说明配置错误，按失败处理 */
        mcan_read_rxfifo(NODE, 0, &rx);
        return (int8_t) 4;
    }
    return got0 ? 0 : (got1 ? 1 : 2);
}

static void run_scenario(const char *name, const mcan_filter_elem_t *std_f, uint32_t n_std,
                         const mcan_filter_elem_t *ext_f, uint32_t n_ext,
                         const probe_t *probes, uint32_t n_probe)
{
    printf("[scenario] %s\r\n", name);
    hpm_stat_t st = filter_init(std_f, n_std, ext_f, n_ext);
    if (st != status_success) {
        printf("  init failed st=%d\r\n", (int) st);
        s_fail++;
        return;
    }
    for (uint32_t i = 0; i < n_probe; i++) {
        int8_t act = probe_send(&probes[i]);
        bool ok = (act == probes[i].expect);
        if (ok) {
            s_pass++;
        } else {
            s_fail++;
        }
        printf("  probe id=0x%08X ext=%u expect=%-6s actual=%-6s %s\r\n",
               (unsigned) probes[i].id, (unsigned) probes[i].ext, fifo_name(probes[i].expect),
               fifo_name(act), ok ? "PASS" : "FAIL");
    }
}

/* 场景 A：标准帧经典过滤器（精确匹配 0x123，mask=0x7FF） */
static void scenario_classic(void)
{
    static const mcan_filter_elem_t f[] = {
        {MCAN_FILTER_TYPE_CLASSIC_FILTER, MCAN_FILTER_ELEM_CFG_STORE_IN_RX_FIFO0_IF_MATCH, 0, 0,
         .filter_id = 0x123U, .filter_mask = 0x7FFU},
    };
    static const probe_t p[] = {
        {0x123U, 0U, 0}, {0x124U, 0U, 2},
    };
    run_scenario("classic std exact 0x123/mask 0x7FF", f, 1, NULL, 0, p, 2);
}

/* 场景 B：标准帧范围过滤器 [0x200,0x20F] */
static void scenario_range(void)
{
    static const mcan_filter_elem_t f[] = {
        {MCAN_FILTER_TYPE_RANGE_FILTER, MCAN_FILTER_ELEM_CFG_STORE_IN_RX_FIFO0_IF_MATCH, 0, 0,
         .start_id = 0x200U, .end_id = 0x20FU},
    };
    static const probe_t p[] = {
        {0x200U, 0U, 0}, {0x20FU, 0U, 0}, {0x210U, 0U, 2},
    };
    run_scenario("range std [0x200,0x20F]", f, 1, NULL, 0, p, 3);
}

/* 场景 C：标准帧双 ID 过滤器 0x300 / 0x301 */
static void scenario_dual(void)
{
    static const mcan_filter_elem_t f[] = {
        {MCAN_FILTER_TYPE_SPECIFIED_ID_FILTER, MCAN_FILTER_ELEM_CFG_STORE_IN_RX_FIFO0_IF_MATCH, 0, 0,
         .id1 = 0x300U, .id2 = 0x301U},
    };
    static const probe_t p[] = {
        {0x300U, 0U, 0}, {0x301U, 0U, 0}, {0x302U, 0U, 2},
    };
    run_scenario("dual-id std 0x300/0x301", f, 1, NULL, 0, p, 3);
}

/* 场景 D：标准帧进 FIFO0（0x400），另一条规则 0x401 进 FIFO1 */
static void scenario_fifo_split(void)
{
    static const mcan_filter_elem_t f[] = {
        {MCAN_FILTER_TYPE_CLASSIC_FILTER, MCAN_FILTER_ELEM_CFG_STORE_IN_RX_FIFO0_IF_MATCH, 0, 0,
         .filter_id = 0x400U, .filter_mask = 0x7FFU},
        {MCAN_FILTER_TYPE_CLASSIC_FILTER, MCAN_FILTER_ELEM_CFG_STORE_IN_RX_FIFO1_IF_MATCH, 0, 0,
         .filter_id = 0x401U, .filter_mask = 0x7FFU},
    };
    static const probe_t p[] = {
        {0x400U, 0U, 0}, {0x401U, 0U, 1}, {0x402U, 0U, 2},
    };
    run_scenario("std 0x400->FIFO0, 0x401->FIFO1", f, 2, NULL, 0, p, 3);
}

/* 场景 E：标准帧 [0x000,0x7FF]->FIFO0，扩展帧范围 [0x1ABCDE00,0x1ABCDEFF]->FIFO1 */
static void scenario_ext(void)
{
    static const mcan_filter_elem_t sf[] = {
        {MCAN_FILTER_TYPE_RANGE_FILTER, MCAN_FILTER_ELEM_CFG_STORE_IN_RX_FIFO0_IF_MATCH, 0, 0,
         .start_id = 0x000U, .end_id = 0x7FFU},
    };
    static const mcan_filter_elem_t ef[] = {
        {MCAN_FILTER_TYPE_RANGE_FILTER, MCAN_FILTER_ELEM_CFG_STORE_IN_RX_FIFO1_IF_MATCH, 1, 0,
         .start_id = 0x1ABCDE00U, .end_id = 0x1ABCDEFFU},
    };
    static const probe_t p[] = {
        {0x010U, 0U, 0}, {0x1ABCDE12U, 1U, 1}, {0x1ABCDD00U, 1U, 2},
    };
    run_scenario("std any->FIFO0, ext [0x1ABCDE00,0x1ABCDEFF]->FIFO1", sf, 1, ef, 1, p, 3);
}

static void run_all(void)
{
    s_pass = 0;
    s_fail = 0;
    printf("\r\n=== mcan_filter: filter routing (internal loopback, GFC=reject) ===\r\n");
    scenario_classic();
    scenario_range();
    scenario_dual();
    scenario_fifo_split();
    scenario_ext();
    printf("SUMMARY: pass=%u fail=%u %s\r\n", (unsigned) s_pass, (unsigned) s_fail,
           (s_fail == 0U) ? "ALL PASS" : "FAILED");
}

static void cmd_info(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    printf("sample: mcan_filter, MCAN0 internal loopback, GFC rejects non-matching frames\r\n");
    printf("scenarios: classic/range/dual-id/FIFO0+FIFO1/std+ext range\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 info, cmd_info, show filter sample info);

static void cmd_test(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    run_all();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 test, cmd_test, run all filter scenarios);

int main(void)
{
    bsp_init();
    printf("\r\n[boot] mcan_filter\r\n");
    board_can_init_pins();
    run_all();
    printf("type 'test' to rerun, 'info' for details\r\n");
    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
