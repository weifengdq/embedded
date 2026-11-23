/*
 * lan8651_raw_frame - 10BASE-T1S 裸以太网帧收发（不跑 IP 协议栈）
 *
 * 用途：在不依赖 lwIP 的前提下，验证 LAN8651 + TC6 数据通道（68 B chunk、
 * TX credits、RX chunk footer）是否真的能把以太网帧送到对端主机。
 *
 * 对端：主机 USB-10BASE-T1S 适配器 enx001ec0d1c337（192.168.1.1/24），
 *       用 python raw socket 收发自定义 EtherType 0x88B5。见 README.md。
 *
 * 板侧行为：
 *   - 收到 EtherType = 0x88B5 的帧 -> 打印长度/前 32 字节，并回一个应答帧
 *   - 命令 send [n] 主动发 n 个帧（默认 1）
 *   - 命令 stats/plca/mac 查看状态
 */
#include <string.h>
#include "bsp.h"
#include "oa_tc6.h"
#include "lan865x.h"

#define TEST_ETHERTYPE   0x88B5U
#define MAC_LEN          6U

static oa_tc6_t s_tc6;
static uint8_t s_mac[6] = {BOARD_LAN8651_MAC0, BOARD_LAN8651_MAC1, BOARD_LAN8651_MAC2,
                           BOARD_LAN8651_MAC3, BOARD_LAN8651_MAC4, BOARD_LAN8651_MAC5};
static uint8_t s_peer[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00};  /* learned from rx */
static bool s_peer_valid;
static uint8_t s_tx_seq;
static uint32_t s_reply_cnt;

static uint32_t now_ms(void) { return bsp_uptime_ms(); }

static void mac_print(const char *tag, const uint8_t *m)
{
    printf("%s%02x:%02x:%02x:%02x:%02x:%02x\r\n", tag, m[0], m[1], m[2], m[3], m[4], m[5]);
}

static void hexdump16(const uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        printf("%02x ", p[i]);
        if ((i % 16U) == 15U) {
            printf("\r\n");
        }
    }
    if (n % 16U) {
        printf("\r\n");
    }
}

/* build a custom-EtherType frame: dst | src | ethertype | seq | 40 byte pattern */
static uint16_t build_frame(uint8_t *buf, const uint8_t *dst, const uint8_t *src, uint8_t seq)
{
    uint16_t len = 14U + 4U + 40U;
    memcpy(&buf[0], dst, MAC_LEN);
    memcpy(&buf[6], src, MAC_LEN);
    buf[12] = (uint8_t) (TEST_ETHERTYPE >> 8);
    buf[13] = (uint8_t) TEST_ETHERTYPE;
    buf[14] = 0xA5;
    buf[15] = 0x5A;
    buf[16] = seq;
    buf[17] = (uint8_t) (now_ms() & 0xFFU);
    for (uint32_t i = 0; i < 40U; i++) {
        buf[18U + i] = (uint8_t) (i + seq);
    }
    return len;
}

static void handle_rx(void)
{
    uint8_t frame[OA_TC6_FRAME_MAX];
    uint16_t len;

    while ((len = oa_tc6_rx_pop(&s_tc6, frame, sizeof(frame))) > 0U) {
        printf("[rx] len=%u dst=%02x:%02x:%02x:%02x:%02x:%02x src=%02x:%02x:%02x:%02x:%02x:%02x type=0x%02x%02x\r\n",
               len,
               frame[0], frame[1], frame[2], frame[3], frame[4], frame[5],
               frame[6], frame[7], frame[8], frame[9], frame[10], frame[11],
               frame[12], frame[13]);
        if ((len >= 18U) && (frame[12] == (uint8_t) (TEST_ETHERTYPE >> 8)) &&
            (frame[13] == (uint8_t) TEST_ETHERTYPE)) {
            memcpy(s_peer, &frame[6], MAC_LEN);
            s_peer_valid = true;
            printf("      payload: ");
            hexdump16(&frame[14], (len > 46U) ? 32U : (uint16_t) (len - 14U));
            /* echo back to the sender */
            {
                uint8_t reply[64];
                uint16_t rlen = build_frame(reply, s_peer, s_mac, (uint8_t) (frame[16] + 0x80U));
                if (oa_tc6_send_frame(&s_tc6, reply, rlen) == 0) {
                    s_reply_cnt++;
                    printf("      replied with %u bytes (total replies %lu)\r\n",
                           rlen, (unsigned long) s_reply_cnt);
                } else {
                    printf("      reply TX failed\r\n");
                }
            }
        }
    }
}

static int board_bring_up(void)
{
    uint32_t src;
    int ret;

    init_lan8651_pins();
    src = board_init_spi_clock(BOARD_LAN8651_SPI);
    if (src == 0U) {
        src = clock_get_frequency(clock_spi2);
    }
    if (status_success != oa_tc6_spi_init(&s_tc6, BOARD_LAN8651_SPI, src, 10000000UL)) {
        printf("SPI2 init failed\r\n");
        return -1;
    }
    /* The SPI2 controller on hpm5e31_1 rev.1 does not produce a usable control
     * channel (see chapter 10); the bit-banged transport is used when requested
     * with 'trans bb'.  Default: SPI2 controller. */
    /* transport auto-detect: hardware SPI2 first, bit-banged SPI as fallback */
    {
        uint32_t v = 0;
        if ((oa_tc6_read_reg(&s_tc6, OA_TC6_REG_STDCAP, &v) != 0) ||
            (memcmp(&s_tc6.ctrl_rx[4], s_tc6.ctrl_tx, 4) != 0)) {
            printf("[init] SPI2 controller gives no echo -> bit-banged transport\r\n");
            oa_tc6_use_bitbang(&s_tc6, true);
        } else {
            printf("[init] SPI2 controller echo OK\r\n");
        }
    }
    ret = lan865x_full_init(&s_tc6, s_mac);
    printf("[init] LAN8651: %s (%d), %s SCLK %lu Hz\r\n", lan865x_strerror(ret), ret,
           s_tc6.use_bb ? "bit-bang" : "SPI2",
           (unsigned long) (s_tc6.use_bb ? oa_tc6_bb_measure_sclk(&s_tc6) : s_tc6.sclk_hz));
    if (ret != 0) {
        return ret;
    }
    (void) lan865x_mac_set_promisc(&s_tc6, true);
    {
        lan865x_plca_cfg_t plca = {
            .enabled = false, .node_id = 0, .node_cnt = BOARD_PLCA_NODE_CNT_DEFAULT,
            .to_tmr = BOARD_PLCA_TO_TMR_DEFAULT, .burst_cnt = BOARD_PLCA_BURST_CNT_DEFAULT,
            .burst_tmr = BOARD_PLCA_BURST_TMR_DEFAULT,
        };
        (void) lan865x_plca_configure(&s_tc6, &plca);
        printf("[init] PLCA off (CSMA/CD), node_cnt=%u to_tmr=%u\r\n",
               (unsigned) BOARD_PLCA_NODE_CNT_DEFAULT, (unsigned) BOARD_PLCA_TO_TMR_DEFAULT);
    }
    mac_print("[init] local MAC ", s_mac);
    printf("[init] EtherType 0x%04x, frame length 58 bytes, waiting for peer frames\r\n",
           (unsigned) TEST_ETHERTYPE);
    return 0;
}

/* ------------------------------ shell commands ---------------------------- */
static void cmd_send(int argc, char *argv[])
{
    uint32_t n = 1;
    uint8_t dst[6];
    uint8_t frame[64];
    uint16_t len;

    if (argc >= 2 && bsp_parse_u32(argv[1], &n) == 0) {
        /* ok */
    }
    if (s_peer_valid) {
        memcpy(dst, s_peer, sizeof(dst));
    } else {
        memset(dst, 0xFF, sizeof(dst));   /* broadcast until the peer is known */
    }
    for (uint32_t i = 0; i < n; i++) {
        int ret;
        len = build_frame(frame, dst, s_mac, s_tx_seq++);
        ret = oa_tc6_send_frame(&s_tc6, frame, len);
        printf("[tx] #%lu len=%u ret=%s tx_credits=%u\r\n",
               (unsigned long) i, len, lan865x_strerror(ret), s_tc6.tx_credits);
        board_delay_ms(2);
        handle_rx();
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 send, cmd_send, send custom ethertype frames: send [count]);

static void cmd_stats(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("[stats] ctrl xfers=%lu err=%lu | data xfers=%lu | tx_frames=%lu tx_bytes=%lu | rx_frames=%lu rx_bytes=%lu dropped=%lu\r\n",
           (unsigned long) s_tc6.stat_ctrl_xfer, (unsigned long) s_tc6.stat_ctrl_err,
           (unsigned long) s_tc6.stat_data_xfer,
           (unsigned long) s_tc6.stat_tx_frames, (unsigned long) s_tc6.stat_tx_bytes,
           (unsigned long) s_tc6.stat_rx_frames, (unsigned long) s_tc6.stat_rx_bytes,
           (unsigned long) s_tc6.stat_rx_dropped);
    printf("[stats] ext_status=%lu hdr_bad=%lu cfg_sync_lost=%lu last_STATUS0=0x%08lx replies=%lu\r\n",
           (unsigned long) s_tc6.stat_ext_status, (unsigned long) s_tc6.stat_hdr_bad,
           (unsigned long) s_tc6.stat_cfg_sync_lost, (unsigned long) s_tc6.stat_status0,
           (unsigned long) s_reply_cnt);
    if (s_peer_valid) {
        mac_print("[stats] peer MAC ", s_peer);
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 stats, cmd_stats, show TC6 data-path statistics);

static void cmd_plca(int argc, char *argv[])
{
    lan865x_plca_cfg_t cfg = {
        .enabled = true, .node_id = 1, .node_cnt = BOARD_PLCA_NODE_CNT_DEFAULT,
        .to_tmr = BOARD_PLCA_TO_TMR_DEFAULT, .burst_cnt = BOARD_PLCA_BURST_CNT_DEFAULT,
        .burst_tmr = BOARD_PLCA_BURST_TMR_DEFAULT,
    };
    uint32_t v = 0;
    if (argc >= 2) {
        cfg.enabled = (argv[1][0] == 'o') && (argv[1][1] == 'n');
        if (argc > 2 && bsp_parse_u32(argv[2], &v) == 0) cfg.node_id = (uint8_t) v;
        if (argc > 3 && bsp_parse_u32(argv[3], &v) == 0) cfg.node_cnt = (uint8_t) v;
        if (argc > 4 && bsp_parse_u32(argv[4], &v) == 0) cfg.to_tmr = (uint8_t) v;
        if (argc > 5 && bsp_parse_u32(argv[5], &v) == 0) cfg.burst_cnt = (uint8_t) v;
        if (argc > 6 && bsp_parse_u32(argv[6], &v) == 0) cfg.burst_tmr = (uint8_t) v;
    } else {
        printf("usage: plca <on|off> [node_id] [node_cnt] [to_tmr] [burst_cnt] [burst_tmr]\r\n");
        return;
    }
    if (lan865x_plca_configure(&s_tc6, &cfg) != 0) {
        printf("PLCA configure failed\r\n");
        return;
    }
    printf("PLCA: en=%u node_id=%u node_cnt=%u to_tmr=%u burst_cnt=%u burst_tmr=%u status=0x%04x (PST=%u)\r\n",
           cfg.enabled, cfg.node_id, cfg.node_cnt, cfg.to_tmr, cfg.burst_cnt, cfg.burst_tmr,
           cfg.status, (unsigned) ((cfg.status >> 15) & 1U));
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 plca, cmd_plca, configure PLCA: plca <on|off> [id] [cnt] [to] [bc] [bt]);

/* TX throughput: send n frames back to back and measure with the core cycle
 * counter (480 MHz) - isolates the TC6/SPI path from the 10BASE-T1S line rate */
static void cmd_bench(int argc, char *argv[])
{
    uint32_t n = 100;
    uint32_t flen = 58;
    uint8_t dst[6];
    static uint8_t frame[1600];
    uint16_t len;
    uint64_t t0, t1;
    uint32_t ok = 0;

    if (argc >= 2 && bsp_parse_u32(argv[1], &n) == 0) {
        /* n frames */
    }
    if (argc >= 3 && bsp_parse_u32(argv[2], &flen) == 0) {
        if (flen < 60U) { flen = 60U; }
        if (flen > 1518U) { flen = 1518U; }
    }
    if (n == 0 || n > 2000) {
        printf("usage: bench [1..2000]\r\n");
        return;
    }
    if (s_peer_valid) {
        memcpy(dst, s_peer, sizeof(dst));
    } else {
        memset(dst, 0xFF, sizeof(dst));
    }
    len = build_frame(frame, dst, s_mac, 0);
    memset(frame, 0x5A, sizeof(frame));
    memcpy(&frame[0], dst, 6);
    memcpy(&frame[6], s_mac, 6);
    frame[12] = (uint8_t) (TEST_ETHERTYPE >> 8);
    frame[13] = (uint8_t) TEST_ETHERTYPE;
    len = (uint16_t) flen;
    t0 = hpm_csr_get_core_mcycle();
    for (uint32_t i = 0; i < n; i++) {
        frame[16] = (uint8_t) i;
        if (oa_tc6_send_frame(&s_tc6, frame, len) == 0) {
            ok++;
        }
    }
    t1 = hpm_csr_get_core_mcycle();
    {
        uint64_t cyc = t1 - t0;
        uint32_t us = (uint32_t) (cyc / 480U);
        uint32_t fps = (us != 0U) ? (uint32_t) ((uint64_t) ok * 1000000ULL / us) : 0U;
        printf("[bench] %lu/%lu frames of %u B in %lu us -> %lu frame/s, %lu kbit/s payload\r\n",
               (unsigned long) ok, (unsigned long) n, len, (unsigned long) us, (unsigned long) fps,
               (unsigned long) ((uint64_t) fps * len * 8ULL / 1000ULL));
        printf("[bench] transport=%s SCLK=%lu Hz\r\n", s_tc6.use_bb ? "bit-bang" : "SPI2",
               (unsigned long) (s_tc6.use_bb ? oa_tc6_bb_measure_sclk(&s_tc6) : s_tc6.sclk_hz));
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 bench, cmd_bench, TX throughput benchmark: bench [frames]);

static void cmd_speed(int argc, char *argv[])
{
    uint32_t hz = 0;
    if (argc < 2 || bsp_parse_u32(argv[1], &hz) != 0 || hz < 100000UL || hz > 25000000UL) {
        printf("usage: speed <hz> (current %lu)\r\n", (unsigned long) s_tc6.sclk_hz);
        return;
    }
    if (s_tc6.use_bb) {
        printf("bit-bang transport active, run 'trans hw' first\r\n");
        return;
    }
    if (status_success != oa_tc6_spi_init(&s_tc6, BOARD_LAN8651_SPI,
                                          clock_get_frequency(clock_spi2), hz)) {
        printf("spi init failed\r\n");
        return;
    }
    printf("SPI2 SCLK = %lu Hz\r\n", (unsigned long) oa_tc6_spi_get_sclk(&s_tc6, clock_get_frequency(clock_spi2)));
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 speed, cmd_speed, set SPI2 SCLK: speed <hz>);

static void cmd_trans(int argc, char *argv[])
{
    if (argc >= 2 && argv[1][0] == 'h') {
        oa_tc6_use_bitbang(&s_tc6, false);
        printf("transport = SPI2 controller\r\n");
    } else {
        oa_tc6_use_bitbang(&s_tc6, true);
        printf("transport = bit-bang, SCLK ~%lu Hz\r\n", (unsigned long) oa_tc6_bb_measure_sclk(&s_tc6));
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 trans, cmd_trans, select transport: trans <bb|hw>);

static void cmd_poll(int argc, char *argv[])
{
    (void) argc; (void) argv;
    (void) oa_tc6_poll_rx(&s_tc6);
    handle_rx();
    printf("[poll] rx_chunks=%u tx_credits=%u\r\n", s_tc6.rx_chunks_available, s_tc6.tx_credits);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 poll, cmd_poll, service the RX path once and drain received frames);

int main(void)
{
    uint32_t last_poll = 0;
    uint32_t last_print = 0;

    bsp_init();
    printf("\r\n=== hpm5e31_1 LAN8651 raw 10BASE-T1S frame test ===\r\n");
    if (board_bring_up() != 0) {
        printf("[init] bring-up failed, commands still available for debugging\r\n");
    }
    printf("commands: send [n] | poll | stats | plca <on|off> [..] | probe\r\n");

    while (1) {
        bsp_shell_poll();
        /* service the receive path at least every 1 ms */
        if (now_ms() != last_poll) {
            last_poll = now_ms();
            (void) oa_tc6_poll_rx(&s_tc6);
            handle_rx();
        }
        if (now_ms() - last_print >= 2000U) {
            last_print = now_ms();
            printf("[alive] t=%lums rx=%lu tx=%lu replies=%lu\r\n",
                   (unsigned long) now_ms(), (unsigned long) s_tc6.stat_rx_frames,
                   (unsigned long) s_tc6.stat_tx_frames, (unsigned long) s_reply_cnt);
        }
    }
    return 0;
}
