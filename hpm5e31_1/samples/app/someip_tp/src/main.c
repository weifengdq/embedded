/*
 * someip_tp - SOME/IP-TP（Transport Protocol）大包分段/重组
 *
 * 场景：SOME/IP 基于 UDP，单帧 MTU 1500 -> 一个 SOME/IP 报文载荷上限约 1400 B。
 * 超过就要用 SOME/IP-TP：Message Type 置 0x20，16 B 头后加 4 B TP 头
 * （Offset(28b, 单位 16 B) | Reserved(3b) | More(1b)），每段载荷 <= 1392 B（=87x16）。
 *
 * 本板端例程同时验证两个方向：
 *   板 -> 主机：method 0x0010 get_bulk(u32 len) -> len 字节图案（4096 B 会分成 3 段）
 *   主机 -> 板：method 0x0007 crc32(bytes) -> u32（4096 B 请求同样 3 段，板端重组后算 CRC）
 *
 * shell: stat | run | selftest | offer | ip | tpstat
 */
#include <string.h>
#include <stdio.h>
#include "bsp.h"
#include "board.h"
#include "app_someip.h"
#include "app_someip_wire.h"
#include "app_someip_sd.h"
#include "app_someip_tp.h"
#include "app_someip_net.h"
#include "common.h"   /* SDK lwIP 移植层：extern enet_desc_t desc[]（txdbg 诊断用） */

#define APP_SERVICE_ID      0x1234U
#define APP_INSTANCE_ID     0x0001U
#define APP_MAJOR_VERSION   0x01U
#define APP_MINOR_VERSION   0x00000001UL
#define APP_INTERFACE_VER   0x01U
#define APP_EVENTGROUP_ID   0x0001U

#define M_ADD          0x0001U
#define M_GET_VERSION  0x0002U
#define M_CRC32        0x0007U   /* 请求载荷任意长度，回 u32 CRC32（IEEE 802.3） */
#define M_GET_BULK     0x0010U   /* 请求 u32 len，回 len 字节图案（走 TP 分段） */

static void cmd_txdbg(int argc, char *argv[]);   /* 前置声明：cmd_burst 里会调用 */

static app_someip_t      g_srv;
static someip_tp_reasm_t g_tp;

/* 图案：byte[i] = (i*31 + 7) & 0xFF，与上位机 someip_client.py 一致 */
static void fill_pattern(uint8_t *buf, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)((i * 31U + 7U) & 0xFFU);
    }
}

static uint8_t m_add(app_someip_t *s, const someip_msg_t *req, uint8_t *reply, uint16_t cap,
                     uint16_t *reply_len, void *user)
{
    (void)s; (void)user;
    if ((req->payload_len < 8U) || (cap < 4U)) {
        return SOMEIP_RC_MALFORMED_MESSAGE;
    }
    someip_put_u32(reply, someip_get_u32(&req->payload[0]) + someip_get_u32(&req->payload[4]));
    *reply_len = 4U;
    return SOMEIP_RC_OK;
}

static uint8_t m_get_version(app_someip_t *s, const someip_msg_t *req, uint8_t *reply, uint16_t cap,
                             uint16_t *reply_len, void *user)
{
    (void)s; (void)req; (void)user;
    const char *v = app_someip_ver_string();
    uint16_t n = (uint16_t)strlen(v);
    if ((uint32_t)cap < (uint32_t)n + 4U) {
        return SOMEIP_RC_NOT_OK;
    }
    someip_put_u32(reply, n);
    memcpy(&reply[4], v, n);
    *reply_len = (uint16_t)(4U + n);
    return SOMEIP_RC_OK;
}

/* 大包响应：len 由请求 u32 指定（1..4096），自动被 app_someip_send_msg 切成 TP 段 */
static uint8_t m_get_bulk(app_someip_t *s, const someip_msg_t *req, uint8_t *reply, uint16_t cap,
                          uint16_t *reply_len, void *user)
{
    (void)s; (void)user;
    if (req->payload_len < 4U) {
        return SOMEIP_RC_MALFORMED_MESSAGE;
    }
    uint32_t len = someip_get_u32(req->payload);
    if (len == 0U) {
        len = 1U;
    }
    if (len > SOMEIP_MAX_PAYLOAD) {
        len = SOMEIP_MAX_PAYLOAD;       /* 4 KB 上限：重组缓冲就是 4 KB */
    }
    if (cap < (uint16_t)len) {
        return SOMEIP_RC_NOT_OK;
    }
    fill_pattern(reply, (uint16_t)len);
    *reply_len = (uint16_t)len;
    return SOMEIP_RC_OK;
}

/* 大包请求：板端做 TP 重组后算 CRC32，回 4 B */
static uint8_t m_crc32(app_someip_t *s, const someip_msg_t *req, uint8_t *reply, uint16_t cap,
                       uint16_t *reply_len, void *user)
{
    (void)s; (void)user;
    if (cap < 4U) {
        return SOMEIP_RC_NOT_OK;
    }
    someip_put_u32(reply, someip_crc32(req->payload, req->payload_len));
    *reply_len = 4U;
    return SOMEIP_RC_OK;
}
/* ---------------- 板端自测：TP 分段 <-> 重组 闭环 ---------------- */
#define TP_SLOTS    4
#define TP_SLOT_LEN 1500

static struct {
    uint8_t  buf[TP_SLOTS][TP_SLOT_LEN];
    uint16_t len[TP_SLOTS];
    uint32_t count;
} s_tcap;

static int tp_cap_send(void *ctx, const uint8_t ip[4], uint16_t port, const uint8_t *buf, uint16_t len)
{
    (void)ctx; (void)ip; (void)port;
    if ((s_tcap.count < TP_SLOTS) && (len <= TP_SLOT_LEN)) {
        memcpy(s_tcap.buf[s_tcap.count], buf, len);
        s_tcap.len[s_tcap.count] = len;
    }
    s_tcap.count++;
    return 0;
}

static int cmd_run(int argc, char *argv[])
{
    (void)argc; (void)argv;
    int fails = 0;
    someip_send_fn saved_send = g_srv.cfg.send;
    void *saved_ctx = g_srv.cfg.net_ctx;
    static uint8_t payload[SOMEIP_MAX_PAYLOAD];

    printf("[run] ---- SOME/IP-TP 分段/重组自测（%u B）----\r\n", (unsigned)SOMEIP_MAX_PAYLOAD);
    g_srv.cfg.send = tp_cap_send;
    g_srv.cfg.net_ctx = &s_tcap;
    fill_pattern(payload, (uint16_t)sizeof(payload));
    const uint32_t crc = someip_crc32(payload, (uint16_t)sizeof(payload));

    /* 1) 分段：4096 B -> 1392 + 1392 + 1312 = 3 段 */
    s_tcap.count = 0;
    int segs = app_someip_tp_send(&g_srv, g_srv.cfg.local_ip, APP_SOMEIP_SERVICE_PORT, M_GET_BULK,
                                  0x00A5U, 0x0001U, SOMEIP_MT_RESPONSE, SOMEIP_RC_OK,
                                  payload, (uint16_t)sizeof(payload));
    bool ok = (segs == 3) && (s_tcap.count == 3U);
    printf("[run] 分段 4096 B -> %d 段      : %s\r\n", segs, ok ? "PASS" : "FAIL");
    fails += ok ? 0 : 1;

    /* 2) 重组：把捕获到的分段按顺序喂回去 */
    ok = ok && (g_srv.tp != NULL);
    int total = 0;
    if (ok) {
        app_someip_tp_reasm_reset(g_srv.tp);
        for (uint32_t i = 0; i < s_tcap.count; i++) {
            someip_msg_t m;
            if (app_someip_decode(s_tcap.buf[i], s_tcap.len[i], &m) != 0) {
                ok = false;
                break;
            }
            int r = app_someip_tp_feed(g_srv.tp, &m, i * 10U);
            if (r < 0) {
                ok = false;
                break;
            }
            if (r > 0) {
                total = r;
            }
        }
        ok = ok && (total == (int)sizeof(payload)) &&
             (someip_crc32(g_srv.tp->buf, (uint16_t)total) == crc) &&
             (memcmp(g_srv.tp->buf, payload, sizeof(payload)) == 0);
    }
    printf("[run] 重组 %d B CRC 0x%08lX     : %s\r\n", total, (unsigned long)crc, ok ? "PASS" : "FAIL");
    fails += ok ? 0 : 1;

    /* 3) 乱序段必须被丢弃（不能拼出错误数据） */
    ok = false;
    if (g_srv.tp != NULL) {
        someip_msg_t m;
        app_someip_tp_reasm_reset(g_srv.tp);
        if ((s_tcap.count >= 2U) && (app_someip_decode(s_tcap.buf[1], s_tcap.len[1], &m) == 0)) {
            int r = app_someip_tp_feed(g_srv.tp, &m, 0U);
            ok = (r < 0) && (!g_srv.tp->active);
        }
    }
    printf("[run] 乱序段被拒绝             : %s\r\n", ok ? "PASS" : "FAIL");
    fails += ok ? 0 : 1;

    g_srv.cfg.send = saved_send;
    g_srv.cfg.net_ctx = saved_ctx;
    printf("[run] %s (%d failed)\r\n", (fails == 0) ? "PASS" : "FAIL", fails);
    return fails;
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 run, cmd_run, SOME/IP-TP segment + reassemble selftest);

/* ---------------- shell ---------------- */
/* ---------------- 诊断：连发大帧（U-35-10 定位用） ----------------
 * 把"SOME/IP-TP 连发 3 个 1412 B 段"退化成"裸 UDP 连发 3 个 1412 B 报文"，
 * 排除协议层与上位机因素，只留 ENET/lwIP 发送路径。
 *   burst   组1: 3 x 1412 B 背靠背 -> 主机 30601
 *           组2: 3 x 1412 B，帧间忙等 200 us -> 主机 30602
 * 判据（主机 tcpdump -nn udp and '(port 30601 or port 30602)'）：
 *   两组各 3 个 -> 连发没问题，问题在 SOME/IP 出包/客户端侧
 *   30601 只有 1 个、30602 有 3 个 -> ENET 背靠背大帧丢失，需要帧间间隔/等描述符
 *   两组都只有 1 个 -> 驱动只发出最后一帧
 */
#define BURST_LEN 1412U
static uint8_t  s_burst_buf[BURST_LEN];
static uint8_t  s_burst_dst[4] = { 192U, 168U, 2U, 1U };   /* 主机 enp6s0 */

static int burst_group(uint16_t port, uint32_t n, uint32_t gap_us)
{
    int ok = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (gap_us != 0U) {
            uint32_t t0 = (uint32_t)hpm_csr_get_core_mcycle();
            while (((uint32_t)hpm_csr_get_core_mcycle() - t0) < (gap_us * 240U)) {
                /* 480 MHz: 1 us = 240 cycles */
            }
        }
        int rc = app_someip_net_send(NULL, s_burst_dst, port, s_burst_buf, BURST_LEN);
        printf("[burst] :%u frame %lu rc=%d\r\n", (unsigned)port, (unsigned long)i, rc);
        app_someip_net_poll();   /* 让 TX 完成/描述符回收有机会推进 */
        if (rc == 0) {
            ok++;
        }
    }
    return ok;
}

static void cmd_burst(int argc, char *argv[])
{
    (void)argc; (void)argv;
    static uint8_t warm[64];
    for (uint32_t i = 0; i < BURST_LEN; i++) {
        s_burst_buf[i] = (uint8_t)((i * 31U + 7U) & 0xFFU);
    }
    /* 预热：先发一个小包触发 ARP，再跑一会儿主循环把 ARP 回复处理掉。
     * 不预热的话，紧接着的 6 个大帧会全部压在 lwIP 的 ARP 队列里（甚至被丢弃），
     * 实验就测不到"连发"本身。 */
    (void)app_someip_net_send(NULL, s_burst_dst, 30600U, warm, (uint16_t)sizeof(warm));
    for (int i = 0; i < 200; i++) {
        app_someip_net_poll();
    }
    board_delay_ms(30U);
    for (int i = 0; i < 100; i++) {
        app_someip_net_poll();
    }
    printf("[burst] warmup done (ARP 已建立)\r\n");
    printf("[burst] 组1 背靠背 3 x %u B -> 192.168.2.1:30601\r\n", (unsigned)BURST_LEN);
    int ok1 = burst_group(30601U, 3U, 0U);
    board_delay_ms(5U);
    printf("[burst] 组2 帧间 200 us 3 x %u B -> 192.168.2.1:30602\r\n", (unsigned)BURST_LEN);
    int ok2 = burst_group(30602U, 3U, 200U);
    board_delay_ms(5U);
    printf("[burst] 结果: 组1 %d/3、组2 %d/3（udp_sendto 返回 0 的个数）\r\n", ok1, ok2);
    for (int i = 0; i < 2000; i++) {
        app_someip_net_poll();
    }
    cmd_txdbg(0, NULL);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 burst, cmd_burst, bare-UDP burst 3x1412B (U-35-10 diag));

/* burstloop：每 500 ms 发一组 3 个 1412 B（共 20 组 ≈ 10 s）。
 * 为什么需要它：hwrun 的 --post 阶段串口已关闭，没法再发 shell 命令；而 tcpdump 只能在
 * --post 里启动（锁内）。有了 burstloop，用 --send "burstloop" 触发后，主机侧 90 s 的抓包
 * 一定能覆盖到多组，实验不再依赖时序运气。 */
static void cmd_burstloop(int argc, char *argv[])
{
    (void)argc; (void)argv;
    static uint8_t warm[64];
    for (uint32_t i = 0; i < BURST_LEN; i++) {
        s_burst_buf[i] = (uint8_t)((i * 31U + 7U) & 0xFFU);
    }
    (void)app_someip_net_send(NULL, s_burst_dst, 30600U, warm, (uint16_t)sizeof(warm));
    for (int i = 0; i < 200; i++) {
        app_someip_net_poll();
    }
    printf("[burstloop] start: 20 组 x (3 x %u B 背靠背 + 500 ms)，目的 192.168.2.1:30601\r\n",
           (unsigned)BURST_LEN);
    for (int g = 0; g < 20; g++) {
        int ok = burst_group(30601U, 3U, 0U);
        printf("[burstloop] group %d ok=%d/3\r\n", g, ok);
        for (int i = 0; i < 3000; i++) {
            app_someip_net_poll();
        }
        board_delay_ms(500U);
    }
    printf("[burstloop] done\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 burstloop, cmd_burstloop, loop burst groups for tcpdump (U-35-10 diag));

/* ---------------- 诊断：ENET TX 描述符 / DMA 状态（U-35-10 定位用） ----------------
 * 区分"lwIP 没把帧交给驱动"与"驱动交给了硬件但硬件没发出去"：
 *   OWN=1 的描述符个数 ≈ 还在硬件手里的帧数
 *   OWN=0 且 cur 追上 head -> 硬件都已发完（帧确实上线了，问题在主机侧/ARP/组播）
 */
static void cmd_txdbg(int argc, char *argv[])
{
    (void)argc; (void)argv;
    enet_desc_t *d = &desc[0];
    uint32_t n = d->tx_buff_cfg.count;
    int own_cnt = 0;
    printf("[txdbg] tx_buff count=%lu size=%u head=%p cur=%p\r\n",
           (unsigned long)n, (unsigned)d->tx_buff_cfg.size,
           (void *)d->tx_desc_list_head, (void *)d->tx_desc_list_cur);
    for (uint32_t i = 0; i < n; i++) {
        if (d->tx_desc_list_head[i].tdes0_bm.own) {
            own_cnt++;
        }
    }
    printf("[txdbg] OWN=1 描述符 %d / %lu（= 仍在硬件手里的帧数）\r\n", own_cnt, (unsigned long)n);
    printf("[txdbg] ENET DMA_STATUS=0x%08lx DMA_BUS_STATUS=0x%08lx\r\n",
           (unsigned long)ENET->DMA_STATUS, (unsigned long)ENET->DMA_BUS_STATUS);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 txdbg, cmd_txdbg, show ENET TX descriptors / DMA status (U-35-10 diag));

/* tpgap：运行时改 TP 段间保护间隔，用于 A/B 因果验证（0 = 背靠背，200 = 每段留 200 us） */
static void cmd_tpgap(int argc, char *argv[])
{
    uint32_t us = 100;
    if (argc >= 2) {
        (void)bsp_parse_u32(argv[1], &us);
    }
    g_srv.cfg.tp_seg_gap_us = us;
    printf("[tp] tp_seg_gap_us = %lu（0 = 段背靠背；>0 = 每段后忙等，给 ENET 发完上一帧的时间）\r\n",
           (unsigned long)us);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 tpgap, cmd_tpgap, set SOME/IP-TP inter-segment gap in us (U-35-10 A/B));

static void cmd_stat(int argc, char *argv[])
{
    (void)argc; (void)argv;
    const someip_stats_t *st = &g_srv.stats;
    printf("[someip] link=%s uptime=%lu ms offer_state=%u\r\n",
           app_someip_net_link_up() ? "UP" : "DOWN", (unsigned long)bsp_uptime_ms(), (unsigned)g_srv.offer_state);
    printf("[someip] rx total=%lu req=%lu sd=%lu malformed=%lu | tx rsp=%lu offer=%lu\r\n",
           (unsigned long)st->rx_total, (unsigned long)st->rx_requests, (unsigned long)st->rx_sd,
           (unsigned long)st->err_malformed, (unsigned long)st->tx_responses,
           (unsigned long)st->tx_sd_offer);
    printf("[someip] TP seg_rx=%lu msg_rx=%lu seg_tx=%lu drop=%lu | max_rx=%lu B\r\n",
           (unsigned long)st->tp_seg_rx, (unsigned long)st->tp_msg_rx, (unsigned long)st->tp_seg_tx,
           (unsigned long)st->tp_drop, (unsigned long)st->max_rx_len);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 stat, cmd_stat, show SOME/IP + TP counters);

static void cmd_tpstat(int argc, char *argv[])
{
    (void)argc; (void)argv;
    const someip_stats_t *st = &g_srv.stats;
    printf("[tp] 段长上限 %u B（%u x 16），逻辑报文上限 %u B\r\n",
           (unsigned)SOMEIP_TP_SEG_SIZE, (unsigned)(SOMEIP_TP_SEG_SIZE / 16U), (unsigned)SOMEIP_MAX_PAYLOAD);
    printf("[tp] 收：段 %lu / 完整报文 %lu / 丢弃 %lu  |  发：段 %lu\r\n",
           (unsigned long)st->tp_seg_rx, (unsigned long)st->tp_msg_rx, (unsigned long)st->tp_drop,
           (unsigned long)st->tp_seg_tx);
    if (g_srv.tp != NULL) {
        printf("[tp] 重组器：active=%d 已收 %u B，期望 offset %u\r\n", (int)g_srv.tp->active,
               (unsigned)g_srv.tp->total_len, (unsigned)g_srv.tp->expect_off);
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 tpstat, cmd_tpstat, show SOME/IP-TP state);

static void cmd_selftest(int argc, char *argv[])
{
    (void)argc; (void)argv;
    (void)app_someip_selftest(&g_srv, true);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 selftest, cmd_selftest, protocol level selftest);

static void cmd_offer(int argc, char *argv[])
{
    (void)argc; (void)argv;
    int r = app_someip_offer(&g_srv);
    printf("[someip] offer %s (224.244.224.245:30490 ttl=%lu s)\r\n",
           (r == 0) ? "sent" : "FAILED", (unsigned long)g_srv.cfg.offer_ttl_s);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 offer, cmd_offer, send OfferService now);

static void cmd_ip(int argc, char *argv[])
{
    (void)argc; (void)argv;
    app_someip_net_info();
    printf("[net] rx_drop=%lu\r\n", (unsigned long)app_someip_net_rx_drop());
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 ip, cmd_ip, show netif info);

int main(void)
{
    someip_cfg_t cfg;

    bsp_init();
    printf("\r\n=== someip_tp: SOME/IP-TP（4 KB 分段/重组）===\r\n");
    printf("[someip] service 0x%04X instance 0x%04X, %s\r\n", (unsigned)APP_SERVICE_ID,
           (unsigned)APP_INSTANCE_ID, app_someip_ver_string());
    printf("[someip] method 0x0010 get_bulk(u32 len)->bytes（板->主机分段）, 0x0007 crc32(bytes)->u32（主机->板分段）\r\n");

    if (app_someip_net_init() != 0) {
        printf("[net] ENET/lwIP init FAILED\r\n");
        while (1) {
            bsp_shell_poll();
        }
    }
    app_someip_net_info();

    memset(&cfg, 0, sizeof(cfg));
    cfg.service_id = APP_SERVICE_ID;
    cfg.instance_id = APP_INSTANCE_ID;
    cfg.major_version = APP_MAJOR_VERSION;
    cfg.interface_version = APP_INTERFACE_VER;
    cfg.minor_version = APP_MINOR_VERSION;
    cfg.local_ip[0] = APP_SOMEIP_LOCAL_IP0;
    cfg.local_ip[1] = APP_SOMEIP_LOCAL_IP1;
    cfg.local_ip[2] = APP_SOMEIP_LOCAL_IP2;
    cfg.local_ip[3] = APP_SOMEIP_LOCAL_IP3;
    cfg.sd_port = APP_SOMEIP_SD_PORT;
    cfg.service_port = APP_SOMEIP_SERVICE_PORT;
    cfg.offer_ttl_s = 3U;
    cfg.sd_multicast_ip[0] = APP_SOMEIP_MCAST_IP0;
    cfg.sd_multicast_ip[1] = APP_SOMEIP_MCAST_IP1;
    cfg.sd_multicast_ip[2] = APP_SOMEIP_MCAST_IP2;
    cfg.sd_multicast_ip[3] = APP_SOMEIP_MCAST_IP3;
    cfg.sd_multicast_port = APP_SOMEIP_SD_PORT;
    cfg.send = app_someip_net_send;
    cfg.now_ms = app_someip_net_now_ms;

    app_someip_init(&g_srv, &cfg, &g_tp);
    app_someip_register_method(&g_srv, M_ADD, m_add, NULL);
    app_someip_register_method(&g_srv, M_GET_VERSION, m_get_version, NULL);
    app_someip_register_method(&g_srv, M_CRC32, m_crc32, NULL);
    app_someip_register_method(&g_srv, M_GET_BULK, m_get_bulk, NULL);

    if (app_someip_net_open(&g_srv, APP_SOMEIP_SD_PORT, APP_SOMEIP_SERVICE_PORT) != 0) {
        printf("[someip] UDP socket open FAILED\r\n");
        while (1) {
            bsp_shell_poll();
        }
    }

    uint32_t t0 = bsp_uptime_ms();
    while (!app_someip_net_link_up() && ((bsp_uptime_ms() - t0) < 3000U)) {
        app_someip_net_poll();
    }
    (void)app_someip_offer(&g_srv);
    printf("[someip] OfferService -> 224.244.224.245:30490, endpoint 192.168.2.100:30501, ttl=%u s\r\n",
           (unsigned)g_srv.cfg.offer_ttl_s);
    printf("[someip] shell: stat | tpstat | run | selftest | offer | ip\r\n");

    while (1) {
        app_someip_net_poll();
        app_someip_poll(&g_srv, app_someip_net_now_ms(NULL));
        bsp_shell_poll();
    }
    return 0;
}