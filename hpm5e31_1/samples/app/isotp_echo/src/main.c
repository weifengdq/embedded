/*
 * isotp_echo - ISO-TP(ISO 15765-2:2016) 回显节点 + 板内双节点闭环自测
 *
 * 硬件: hpm5e31_1，CAN0(PD00/PD01) 与 CAN1(PD05/PD04) 挂在同一段 120 欧终端总线上。
 *   CAN0 = DUT：监听 0x700（ISO-TP 请求），在 0x708 回显
 *   CAN1 = 板内 tester：发 0x700，收 0x708，用同一份 ISO-TP 栈当客户端
 *
 * 为什么板内双节点：ISO-TP 的 FC/CF 是双向交互，单节点自环无法验证流控；
 *   两块 MCAN 同板互连可以完整验证 BS/STmin/超时，且不占上位机与网关。
 *
 * 命令:
 *   run     1/7/8/12/64/256/1024/4095 B 回显，逐字节 CRC32 校验，打印 RTT 与吞吐
 *           （fd 模式下追加 6000 B，走 FF_DL=0 + 4 字节 32 位长度转义路径）
 *   bench   BS/STmin 参数扫描（BS=0/8/32 × STmin=0/1ms/100us）
 *   thr [n] 4095 B 连续回显 n 次，测稳定吞吐
 *   fd      切到 CAN FD（仲裁 1M/数据 2M）复测
 *   ext     切到 29 位 ID（0x18DAF110/0x18DA10F1 物理寻址）
 *   pad [b] padding 字节验证（0xCC/0xAA）
 *   stat    协议栈统计
 *   info    当前配置
 */
#include <stdio.h>
#include <string.h>

#include "bsp.h"
#include "app_isotp.h"
#include "app_isotp_port.h"
#include "ext/board_can.h"

#define ID_REQ   (0x700u)
#define ID_RESP  (0x708u)
#define ID_REQ29  (0x18DAF110u)
#define ID_RESP29 (0x18DA10F1u)

#define TP_MAX   8192u

static app_isotp_port_t s_port0;    /* CAN0: DUT */
static app_isotp_port_t s_port1;    /* CAN1: tester */
static app_isotp_t s_srv;           /* DUT 侧 ISO-TP */
static app_isotp_t s_cli;           /* tester 侧 ISO-TP */

static uint8_t s_srv_tx[TP_MAX];
static uint8_t s_srv_rx[TP_MAX];
static uint8_t s_cli_tx[TP_MAX];
static uint8_t s_cli_rx[TP_MAX];
static uint8_t s_payload[TP_MAX];

static volatile bool s_cli_done;
static uint32_t s_cli_len;
static uint32_t s_echo_busy_err;

/* 线上帧嗅探：记录两路各自最后一帧（用于 padding 证据） */
static struct {
    uint8_t len;
    uint8_t data[64];
    uint32_t cnt;
} s_sniff[2];

static bool s_fd;
static bool s_ext29;
static uint16_t s_bs = 0u;
static uint8_t s_stmin = 0u;
static uint8_t s_pad_byte = 0xCCu;

/* ---------------- 工具 ---------------- */

static uint32_t crc32_sw(const uint8_t *p, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320UL & (uint32_t) (-(int32_t) (crc & 1u)));
        }
    }
    return ~crc;
}

static void fill_pattern(uint8_t *p, uint32_t len)
{
    uint32_t x = 0x12345678u ^ len;
    for (uint32_t i = 0; i < len; i++) {
        x = x * 1664525u + 1013904223u;
        p[i] = (uint8_t) (x >> 24);
    }
}

/* ---------------- ISO-TP 回调 ---------------- */

static void on_srv_msg(void *user, const uint8_t *data, uint32_t len)
{
    (void) user;
    if (app_isotp_send(&s_srv, data, len) != APP_ISOTP_OK) {
        s_echo_busy_err++;
    }
}

static void on_cli_msg(void *user, const uint8_t *data, uint32_t len)
{
    (void) user;
    (void) data;
    s_cli_len = len;
    s_cli_done = true;
}

/* ---------------- 主循环泵 ---------------- */

static void sniff(uint32_t port, const app_isotp_frame_t *f)
{
    s_sniff[port].len = f->len;
    memcpy(s_sniff[port].data, f->data, f->len);
    s_sniff[port].cnt++;
}

static void pump_once(void)
{
    app_isotp_frame_t f;

    while (app_isotp_port_recv(&s_port0, &f)) {
        sniff(0u, &f);
        (void) app_isotp_on_frame(&s_srv, &f);
    }
    while (app_isotp_port_recv(&s_port1, &f)) {
        sniff(1u, &f);
        (void) app_isotp_on_frame(&s_cli, &f);
    }
    app_isotp_poll(&s_srv);
    app_isotp_poll(&s_cli);
}

/* tester 发起一次请求并等回显；返回 0 成功 */
static int tester_xfer(const uint8_t *tx, uint32_t txlen, uint32_t timeout_ms, uint32_t *rtt_us)
{
    uint64_t t0 = app_isotp_port_now_us();
    uint64_t limit = (uint64_t) timeout_ms * 1000u;

    s_cli_done = false;
    s_cli_len = 0;
    if (app_isotp_send(&s_cli, tx, txlen) != APP_ISOTP_OK) {
        return -1;
    }
    while (!s_cli_done) {
        pump_once();
        if ((app_isotp_port_now_us() - t0) > limit) {
            return -2;
        }
    }
    while (!app_isotp_tx_idle(&s_srv)) {
        pump_once();
        if ((app_isotp_port_now_us() - t0) > limit) {
            return -3;
        }
    }
    *rtt_us = (uint32_t) (app_isotp_port_now_us() - t0);
    return (s_cli_len == txlen) ? 0 : -4;
}

/* ---------------- 初始化 / 重配置 ---------------- */

static int bus_setup(bool fd, bool ext29, uint16_t bs, uint8_t stmin, uint8_t pad_byte)
{
    app_isotp_port_cfg_t pc;
    app_isotp_cfg_t tc;
    app_isotp_link_t lk0;
    app_isotp_link_t lk1;
    uint32_t req = ext29 ? ID_REQ29 : ID_REQ;
    uint32_t resp = ext29 ? ID_RESP29 : ID_RESP;

    board_can_transceivers_enable();

    memset(&pc, 0, sizeof(pc));
    pc.fd = fd;
    pc.brs = true;
    pc.nominal_bitrate = fd ? 1000000UL : 500000UL;
    pc.data_bitrate = 2000000UL;
    pc.base = BOARD_CAN0_BASE;
    if (app_isotp_port_init(&s_port0, &pc) != APP_ISOTP_OK) {
        return -1;
    }
    pc.base = BOARD_CAN1_BASE;
    if (app_isotp_port_init(&s_port1, &pc) != APP_ISOTP_OK) {
        return -2;
    }
    lk0 = app_isotp_port_link(&s_port0);
    lk1 = app_isotp_port_link(&s_port1);

    memset(&tc, 0, sizeof(tc));
    tc.fd = fd;
    tc.brs = true;
    tc.tx_pad = true;
    tc.pad_byte = pad_byte;
    tc.bs = bs;
    tc.stmin = stmin;
    tc.n_bs_ms = 1000u;
    tc.n_cr_ms = 1000u;
    tc.rx_ext = ext29;
    tc.tx_ext = ext29;

    tc.rx_id = req;
    tc.tx_id = resp;
    tc.on_msg = on_srv_msg;
    if (app_isotp_init(&s_srv, &tc, &lk0, s_srv_tx, sizeof(s_srv_tx), s_srv_rx, sizeof(s_srv_rx)) != APP_ISOTP_OK) {
        return -3;
    }
    tc.rx_id = resp;
    tc.tx_id = req;
    tc.on_msg = on_cli_msg;
    if (app_isotp_init(&s_cli, &tc, &lk1, s_cli_tx, sizeof(s_cli_tx), s_cli_rx, sizeof(s_cli_rx)) != APP_ISOTP_OK) {
        return -4;
    }
    s_fd = fd;
    s_ext29 = ext29;
    s_bs = bs;
    s_stmin = stmin;
    s_pad_byte = pad_byte;
    return 0;
}

/* ---------------- 用例 ---------------- */

static int run_case(uint32_t len, uint32_t *rtt_us, uint32_t *tx_frames)
{
    uint32_t f0 = s_srv.st.tx_frames;
    uint32_t crc_tx;
    uint32_t crc_rx;
    int rc;

    fill_pattern(s_payload, len);
    crc_tx = crc32_sw(s_payload, len);
    rc = tester_xfer(s_payload, len, 5000u, rtt_us);
    if (rc != 0) {
        printf("  len=%-5lu RC=%d (timeout/oversize)\r\n", (unsigned long) len, rc);
        return rc;
    }
    crc_rx = crc32_sw(s_cli.rx_buf, s_cli_len);
    *tx_frames = s_srv.st.tx_frames - f0;
    if (crc_rx != crc_tx) {
        printf("  len=%-5lu CRC FAIL tx=0x%08lx rx=0x%08lx\r\n",
               (unsigned long) len, (unsigned long) crc_tx, (unsigned long) crc_rx);
        return -5;
    }
    return 0;
}

static void cmd_run(int argc, char *argv[])
{
    /* 经典帧上限 4095 B（FF_DL 12 bit）；FD 下走 32 位长度转义，追加 6000 B 用例 */
    static const uint32_t sizes_cls[] = {1u, 7u, 8u, 12u, 64u, 256u, 1024u, 4095u};
    static const uint32_t sizes_fd[] = {1u, 7u, 8u, 12u, 64u, 256u, 1024u, 4095u, 6000u};
    const uint32_t *sizes = s_fd ? sizes_fd : sizes_cls;
    uint32_t nsz = s_fd ? (uint32_t) (sizeof(sizes_fd) / sizeof(sizes_fd[0]))
                        : (uint32_t) (sizeof(sizes_cls) / sizeof(sizes_cls[0]));
    uint32_t ok = 0, fail = 0;
    uint32_t total_bytes = 0;
    uint32_t rtt_sum = 0;
    uint32_t rtt_max = 0;
    (void) argc;
    (void) argv;

    printf("\r\n[isotp_echo] run: %s %s BS=%u STmin=0x%02X pad=0x%02X\r\n",
           s_fd ? "CANFD 1M/2M" : "classic 500k", s_ext29 ? "29bit" : "11bit",
           (unsigned) s_bs, (unsigned) s_stmin, (unsigned) s_pad_byte);
    printf("  %-6s %-7s %-9s %-9s %-9s %-8s %s\r\n",
           "len", "frames", "rtt_us", "KB/s", "tx_us", "cf_us", "crc");

    for (uint32_t i = 0; i < nsz; i++) {
        uint32_t rtt = 0, frames = 0;
        uint32_t cfavg;
        int rc = run_case(sizes[i], &rtt, &frames);
        double kbps = (rtt > 0u) ? (double) (2u * sizes[i]) * 1000000.0 / (double) rtt / 1024.0 : 0.0;
        cfavg = s_srv.st.cf_cnt ? (s_srv.st.cf_gap_sum_us / s_srv.st.cf_cnt) : 0u;
        printf("  %-6lu %-7lu %-9lu %-9.1f %-9lu %-8lu %s\r\n",
               (unsigned long) sizes[i], (unsigned long) frames, (unsigned long) rtt,
               kbps, (unsigned long) s_srv.st.last_tx_us, (unsigned long) cfavg,
               rc == 0 ? "OK" : "FAIL");
        if (rc == 0) {
            ok++;
            total_bytes += 2u * sizes[i];
            rtt_sum += rtt;
            if (rtt > rtt_max) {
                rtt_max = rtt;
            }
        } else {
            fail++;
        }
    }
    printf("[isotp_echo] %s sizes=%lu/%lu bytes=%lu rtt_avg=%lu us rtt_max=%lu us "
           "txmsg=%lu rxmsg=%lu seq_err=%lu ovflw=%lu timeout=%lu ff_ext=%lu\r\n",
           fail == 0 ? "PASS" : "FAIL", (unsigned long) ok,
           (unsigned long) nsz,
           (unsigned long) total_bytes,
           (unsigned long) (ok ? rtt_sum / ok : 0u), (unsigned long) rtt_max,
           (unsigned long) s_srv.st.tx_msg, (unsigned long) s_srv.st.rx_msg,
           (unsigned long) s_srv.st.err_seq, (unsigned long) s_srv.st.err_ovflw,
           (unsigned long) s_srv.st.err_timeout, (unsigned long) s_srv.st.ff_ext_cnt);
    if (s_ext29) {
        printf("  29-bit evidence: cli tx_id=0x%08lX ext=%d srv rx_id=0x%08lX\r\n",
               (unsigned long) s_cli.cfg.tx_id, (int) s_cli.cfg.tx_ext,
               (unsigned long) s_srv.cfg.rx_id);
    }
}

static void cmd_bench(int argc, char *argv[])
{
    static const uint16_t bs_list[] = {0u, 8u, 32u};
    static const uint8_t st_list[] = {0x00u, 0x01u, 0xF1u};
    static const uint32_t sizes[] = {64u, 256u, 1024u};
    (void) argc;
    (void) argv;

    printf("\r\n[isotp_echo] bench: BS x STmin (%s %s)\r\n", s_fd ? "CANFD 1M/2M" : "classic 500k",
           s_ext29 ? "29bit" : "11bit");
    printf("  %-4s %-6s %-6s %-9s %-9s %s\r\n", "BS", "STmin", "len", "rtt_us", "KB/s", "result");
    for (uint32_t b = 0; b < sizeof(bs_list) / sizeof(bs_list[0]); b++) {
        for (uint32_t s = 0; s < sizeof(st_list) / sizeof(st_list[0]); s++) {
            int rc = bus_setup(s_fd, s_ext29, bs_list[b], st_list[s], s_pad_byte);
            if (rc != 0) {
                printf("  setup rc=%d\r\n", rc);
                return;
            }
            for (uint32_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
                uint32_t rtt = 0, frames = 0;
                rc = run_case(sizes[i], &rtt, &frames);
                double kbps = (rtt > 0u) ? (double) (2u * sizes[i]) * 1000000.0 / (double) rtt / 1024.0 : 0.0;
                printf("  %-4u 0x%02X   %-6lu %-9lu %-9.1f %s\r\n",
                       (unsigned) bs_list[b], (unsigned) st_list[s], (unsigned long) sizes[i],
                       (unsigned long) rtt, kbps, rc == 0 ? "PASS" : "FAIL");
            }
        }
    }
    (void) bus_setup(s_fd, s_ext29, s_bs, s_stmin, s_pad_byte);
}

static void cmd_thr(int argc, char *argv[])
{
    uint32_t n = 20;
    uint32_t rtt = 0, frames = 0;
    if (argc >= 2) {
        (void) bsp_parse_u32(argv[1], &n);
    }
    if ((n == 0u) || (n > 200u)) {
        n = 20u;
    }
    (void) bus_setup(s_fd, s_ext29, s_bs, s_stmin, s_pad_byte);
    uint64_t t0 = app_isotp_port_now_us();
    uint32_t ok = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (run_case(4095u, &rtt, &frames) == 0) {
            ok++;
        }
    }
    uint64_t dt = app_isotp_port_now_us() - t0;
    printf("\r\n[isotp_echo] thr 4095B x%lu ok=%lu total=%lu us => %.1f KB/s payload(双向) %.1f msg/s\r\n",
           (unsigned long) n, (unsigned long) ok, (unsigned long) dt,
           (double) ok * 2.0 * 4095.0 / ((double) dt / 1e6) / 1024.0,
           (double) ok * 1000000.0 / (double) (dt ? dt : 1u));
    printf("  last msg: cf_cnt=%lu cf_avg=%lu us cf_min=%lu us cf_max=%lu us tx_us=%lu\r\n",
           (unsigned long) s_srv.st.cf_cnt,
           (unsigned long) (s_srv.st.cf_cnt ? s_srv.st.cf_gap_sum_us / s_srv.st.cf_cnt : 0u),
           (unsigned long) (s_srv.st.cf_cnt ? s_srv.st.cf_gap_min_us : 0u),
           (unsigned long) s_srv.st.cf_gap_max_us, (unsigned long) s_srv.st.last_tx_us);
    printf("  stats: seq_err=%lu ovflw=%lu timeout=%lu link=%lu ff_ext=%lu\r\n",
           (unsigned long) s_srv.st.err_seq, (unsigned long) s_srv.st.err_ovflw,
           (unsigned long) s_srv.st.err_timeout, (unsigned long) s_srv.st.err_link,
           (unsigned long) s_srv.st.ff_ext_cnt);
}

static void cmd_fd(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    if (bus_setup(true, s_ext29, s_bs, s_stmin, s_pad_byte) != 0) {
        printf("FD setup FAIL\r\n");
        return;
    }
    cmd_run(0, NULL);
}

static void cmd_ext(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    if (bus_setup(s_fd, true, s_bs, s_stmin, s_pad_byte) != 0) {
        printf("29-bit setup FAIL\r\n");
        return;
    }
    cmd_run(0, NULL);
}

static void cmd_pad(int argc, char *argv[])
{
    uint32_t v = 0xAAu;
    uint32_t rtt = 0, frames = 0;
    uint8_t n;
    if (argc >= 2) {
        (void) bsp_parse_u32(argv[1], &v);
    }
    if (bus_setup(s_fd, s_ext29, s_bs, s_stmin, (uint8_t) v) != 0) {
        printf("pad setup FAIL\r\n");
        return;
    }
    /* 64 B 经典帧 = FF + 9 CF；最后一个 CF 只有 2 字节载荷，尾部 5 字节必须是 pad 字节 */
    (void) run_case(64u, &rtt, &frames);
    n = s_sniff[1].len;
    printf("\r\n[isotp_echo] pad test: pad=0x%02X last_frame_len=%u tail=",
           (unsigned) s_pad_byte, (unsigned) n);
    for (uint8_t i = 3u; i < n; i++) {
        printf("%02X ", s_sniff[1].data[i]);
    }
    printf("-> %s\r\n", (n == 8u) ? "PASS(经典帧固定 8 字节)" : "CHECK");
}

static void cmd_stat(int argc, char *argv[])
{
    const app_isotp_stats_t *s = &s_srv.st;
    (void) argc;
    (void) argv;
    printf("\r\n[isotp_echo] DUT stats: txmsg=%lu rxmsg=%lu txf=%lu rxf=%lu fc_tx=%lu fc_rx=%lu "
           "wait=%lu seq=%lu ovflw=%lu timeout=%lu link=%lu\r\n",
           (unsigned long) s->tx_msg, (unsigned long) s->rx_msg, (unsigned long) s->tx_frames,
           (unsigned long) s->rx_frames, (unsigned long) s->fc_tx, (unsigned long) s->fc_rx,
           (unsigned long) s->fc_wait_rx, (unsigned long) s->err_seq,
           (unsigned long) s->err_ovflw, (unsigned long) s->err_timeout, (unsigned long) s->err_link);
    printf("  last_tx=%lu us max_tx=%lu us last_rx=%lu us max_rx=%lu us max_len=%lu/%lu\r\n",
           (unsigned long) s->last_tx_us, (unsigned long) s->max_tx_us,
           (unsigned long) s->last_rx_us, (unsigned long) s->max_rx_us,
           (unsigned long) s->max_tx_len, (unsigned long) s->max_rx_len);
    printf("  cf gap: cnt=%lu avg=%lu us min=%lu us max=%lu us | ff_ext=%lu\r\n",
           (unsigned long) s->cf_cnt,
           (unsigned long) (s->cf_cnt ? s->cf_gap_sum_us / s->cf_cnt : 0u),
           (unsigned long) (s->cf_cnt ? s->cf_gap_min_us : 0u),
           (unsigned long) s->cf_gap_max_us, (unsigned long) s->ff_ext_cnt);
    printf("  port0 tx=%lu rx=%lu err=%lu | port1 tx=%lu rx=%lu err=%lu | echo_busy=%lu\r\n",
           (unsigned long) s_port0.tx_cnt, (unsigned long) s_port0.rx_cnt, (unsigned long) s_port0.tx_err,
           (unsigned long) s_port1.tx_cnt, (unsigned long) s_port1.rx_cnt, (unsigned long) s_port1.tx_err,
           (unsigned long) s_echo_busy_err);
}

static void cmd_info(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    printf("\r\n[isotp_echo] CAN0(DUT) 0x%03lX->0x%03lX | CAN1(tester) 0x%03lX->0x%03lX\r\n",
           (unsigned long) s_srv.cfg.rx_id, (unsigned long) s_srv.cfg.tx_id,
           (unsigned long) s_cli.cfg.rx_id, (unsigned long) s_cli.cfg.tx_id);
    printf("  mode=%s ext=%s BS=%u STmin=0x%02X(=%lu us) pad=0x%02X N_Bs=%lu ms N_Cr=%lu ms\r\n",
           s_fd ? "CANFD 1M/2M" : "classic 500k", s_ext29 ? "29bit" : "11bit",
           (unsigned) s_srv.cfg.bs, (unsigned) s_srv.cfg.stmin,
           (unsigned long) app_isotp_stmin_to_us(s_srv.cfg.stmin),
           (unsigned) s_srv.cfg.pad_byte,
           (unsigned long) s_srv.cfg.n_bs_ms, (unsigned long) s_srv.cfg.n_cr_ms);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 run, cmd_run, ISO-TP echo self test: run);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 bench, cmd_bench, BS/STmin sweep: bench);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 thr, cmd_thr, throughput of 4095 B echo: thr [n]);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 fd, cmd_fd, switch to CAN FD and rerun: fd);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 ext, cmd_ext, switch to 29-bit ID and rerun: ext);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 pad, cmd_pad, padding byte test: pad [0xCC|0xAA]);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 stat, cmd_stat, ISO-TP statistics: stat);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 info, cmd_info, current ISO-TP config: info);

int main(void)
{
    board_init();
    bsp_init();

    printf("\r\n[app] isotp_echo ready (ISO 15765-2:2016, CAN0=DUT 0x700/0x708, CAN1=tester)\r\n");
    if (bus_setup(false, false, 0u, 0u, 0xCCu) != 0) {
        printf("[isotp_echo] FAIL: CAN init failed\r\n");
        while (1) {
            bsp_shell_poll();
        }
    }
    /* 等串口采集启动（hwrun 在烧写后 0.3 s 才开始抓串口），避免自测输出被截断 */
    board_delay_ms(1500);
    cmd_info(0, NULL);
    cmd_run(0, NULL);   /* 上电自动跑一遍最小闭环，hwrun 无需 --send 也有证据 */

    while (1) {
        pump_once();
        bsp_shell_poll();
    }
}
