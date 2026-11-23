/*
 * plca_demo.c - shared PLCA master/slave demo logic (bare metal, HPM5E31)
 *
 * The demo keeps the node alive on a 10BASE-T1S segment in PLCA mode:
 *   - every received frame with EtherType 0x88B5 is printed and echoed back;
 *   - every 2 s a heartbeat line prints PLCA PST, frame counters and RX/TX state;
 *   - `send [n]` transmits n custom frames, `status` dumps everything.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <string.h>
#include "bsp.h"
#include "hpm_gpio_drv.h"
#include "oa_tc6.h"
#include "lan865x.h"
#include "plca_demo.h"

#define TEST_ETHERTYPE   0x88B5U
#define MAC_LEN          6U

static oa_tc6_t s_tc6;
static uint8_t s_mac[6] = {BOARD_LAN8651_MAC0, BOARD_LAN8651_MAC1, BOARD_LAN8651_MAC2,
                           BOARD_LAN8651_MAC3, BOARD_LAN8651_MAC4, BOARD_LAN8651_MAC5};
static uint8_t s_peer[6];
static bool s_peer_valid;
static uint8_t s_tx_seq;
static uint32_t s_reply_cnt;
static plca_demo_role_t s_role = PLCA_ROLE_FOLLOWER;
static lan865x_plca_cfg_t s_plca;
static uint32_t s_last_print;

static uint32_t now_ms(void) { return bsp_uptime_ms(); }

static void mac_print(const char *tag, const uint8_t *m)
{
    printf("%s%02x:%02x:%02x:%02x:%02x:%02x\r\n", tag, m[0], m[1], m[2], m[3], m[4], m[5]);
}

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

static void send_frames(uint32_t n)
{
    uint8_t dst[6];
    uint8_t frame[64];

    if (s_peer_valid) {
        memcpy(dst, s_peer, sizeof(dst));
    } else {
        memset(dst, 0xFF, sizeof(dst));   /* broadcast until the peer is learned */
    }
    for (uint32_t i = 0; i < n; i++) {
        uint16_t len = build_frame(frame, dst, s_mac, s_tx_seq++);
        int ret = oa_tc6_send_frame(&s_tc6, frame, len);
        printf("[tx] #%lu len=%u ret=%s tx_credits=%u\r\n", (unsigned long) i, len,
               lan865x_strerror(ret), s_tc6.tx_credits);
        board_delay_ms(2);
    }
}

static void handle_rx(void)
{
    uint8_t frame[OA_TC6_FRAME_MAX];
    uint16_t len;

    while ((len = oa_tc6_rx_pop(&s_tc6, frame, sizeof(frame))) > 0U) {
        printf("[rx] len=%u src=%02x:%02x:%02x:%02x:%02x:%02x type=0x%02x%02x\r\n", len,
               frame[6], frame[7], frame[8], frame[9], frame[10], frame[11], frame[12], frame[13]);
        if ((len >= 18U) && (frame[12] == (uint8_t) (TEST_ETHERTYPE >> 8)) &&
            (frame[13] == (uint8_t) TEST_ETHERTYPE)) {
            memcpy(s_peer, &frame[6], MAC_LEN);
            s_peer_valid = true;
            {
                uint8_t reply[64];
                uint16_t rlen = build_frame(reply, s_peer, s_mac, (uint8_t) (frame[16] + 0x80U));
                if (oa_tc6_send_frame(&s_tc6, reply, rlen) == 0) {
                    s_reply_cnt++;
                }
            }
        }
    }
}

int plca_demo_init(plca_demo_role_t role, uint8_t node_cnt, uint8_t to_tmr,
                   uint8_t burst_cnt, uint8_t burst_tmr)
{
    uint32_t src;
    int ret;

    s_role = role;
    init_lan8651_pins();
    src = board_init_spi_clock(BOARD_LAN8651_SPI);
    if (src == 0U) {
        src = clock_get_frequency(clock_spi2);
    }
    if (status_success != oa_tc6_spi_init(&s_tc6, BOARD_LAN8651_SPI, src, 2000000UL)) {
        printf("[init] SPI2 init failed\r\n");
        return -1;
    }
    /* transport auto-detect: SPI2 controller first, bit-banged SPI as fallback */
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
    if (ret != 0) {
        printf("[init] LAN8651 bring-up failed: %s (%d)\r\n", lan865x_strerror(ret), ret);
        return ret;
    }
    (void) lan865x_mac_set_promisc(&s_tc6, true);

    s_plca.enabled = true;
    s_plca.node_id = (role == PLCA_ROLE_COORDINATOR) ? 0U : 1U;
    s_plca.node_cnt = node_cnt;
    s_plca.to_tmr = to_tmr;
    s_plca.burst_cnt = burst_cnt;
    s_plca.burst_tmr = burst_tmr;
    if (lan865x_plca_configure(&s_tc6, &s_plca) != 0) {
        printf("[init] PLCA configure failed\r\n");
        return -2;
    }
    printf("[init] %s: SCLK %lu Hz, MAC ", s_tc6.use_bb ? "bit-bang" : "SPI2",
           (unsigned long) (s_tc6.use_bb ? oa_tc6_bb_measure_sclk(&s_tc6) : s_tc6.sclk_hz));
    mac_print("", s_mac);
    printf("[init] PLCA %s: node_id=%u node_cnt=%u to_tmr=%u burst_cnt=%u burst_tmr=%u\r\n",
           (role == PLCA_ROLE_COORDINATOR) ? "COORDINATOR (node 0)" : "FOLLOWER (node 1)",
           s_plca.node_id, s_plca.node_cnt, s_plca.to_tmr, s_plca.burst_cnt, s_plca.burst_tmr);
    printf("[init] expected read-back: status=0x8000 (PST=1) once the BEACON is seen\r\n");
    return 0;
}

void plca_demo_status(void)
{
    uint16_t sts1 = 0;
    lan865x_plca_cfg_t cfg = s_plca;

    if (lan865x_plca_read(&s_tc6, &cfg) == 0) {
        printf("[status] PLCA en=%u node_id=%u node_cnt=%u to_tmr=%u burst_cnt=%u burst_tmr=%u status=0x%04x (PST=%u)\r\n",
               cfg.enabled, cfg.node_id, cfg.node_cnt, cfg.to_tmr, cfg.burst_cnt, cfg.burst_tmr,
               cfg.status, (unsigned) ((cfg.status >> 15) & 1U));
    } else {
        printf("[status] PLCA read-back failed\r\n");
    }
    if (lan865x_phy_sts1(&s_tc6, &sts1) == 0) {
        printf("[status] PHY STS1=0x%04x (bit11 PLCA change, bit13 link change)\r\n", sts1);
    }
    printf("[status] TC6 ctrl=%lu err=%lu | data=%lu | tx_frames=%lu tx_bytes=%lu | rx_frames=%lu rx_bytes=%lu dropped=%lu replies=%lu\r\n",
           (unsigned long) s_tc6.stat_ctrl_xfer, (unsigned long) s_tc6.stat_ctrl_err,
           (unsigned long) s_tc6.stat_data_xfer,
           (unsigned long) s_tc6.stat_tx_frames, (unsigned long) s_tc6.stat_tx_bytes,
           (unsigned long) s_tc6.stat_rx_frames, (unsigned long) s_tc6.stat_rx_bytes,
           (unsigned long) s_tc6.stat_rx_dropped, (unsigned long) s_reply_cnt);
    if (s_peer_valid) {
        mac_print("[status] peer MAC ", s_peer);
    }
}

void plca_demo_run(void)
{
    uint32_t last_poll = 0;

    s_last_print = 0;
    while (1) {
        bsp_shell_poll();
        if (now_ms() != last_poll) {           /* service RX at least every 1 ms */
            last_poll = now_ms();
            (void) oa_tc6_poll_rx(&s_tc6);
            handle_rx();
        }
        if (now_ms() - s_last_print >= 2000U) {
            s_last_print = now_ms();
            (void) lan865x_plca_read(&s_tc6, &s_plca);
            printf("[alive] t=%lums PLCA en=%u node_id=%u PST=%u | rx=%lu tx=%lu replies=%lu\r\n",
                   (unsigned long) now_ms(), s_plca.enabled, s_plca.node_id,
                   (unsigned) ((s_plca.status >> 15) & 1U),
                   (unsigned long) s_tc6.stat_rx_frames, (unsigned long) s_tc6.stat_tx_frames,
                   (unsigned long) s_reply_cnt);
        }
    }
}

/* ------------------------------ shell commands ---------------------------- */
static void cmd_status(int argc, char *argv[])
{
    (void) argc; (void) argv;
    plca_demo_status();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 status, cmd_status, dump PLCA status (PST) + PHY STS1 + TC6 statistics);

static void cmd_send(int argc, char *argv[])
{
    uint32_t n = 1;
    if (argc >= 2 && bsp_parse_u32(argv[1], &n) == 0) {
        /* ok */
    }
    send_frames(n);
    handle_rx();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 send, cmd_send, send custom ethertype frames: send [count]);

static void cmd_plca(int argc, char *argv[])
{
    lan865x_plca_cfg_t cfg = s_plca;
    uint32_t v = 0;

    if (argc < 2) {
        printf("usage: plca <on|off> [node_id] [node_cnt] [to_tmr] [burst_cnt] [burst_tmr]\r\n");
        return;
    }
    cfg.enabled = (argv[1][0] == 'o') && (argv[1][1] == 'n');
    if (argc > 2 && bsp_parse_u32(argv[2], &v) == 0) cfg.node_id = (uint8_t) v;
    if (argc > 3 && bsp_parse_u32(argv[3], &v) == 0) cfg.node_cnt = (uint8_t) v;
    if (argc > 4 && bsp_parse_u32(argv[4], &v) == 0) cfg.to_tmr = (uint8_t) v;
    if (argc > 5 && bsp_parse_u32(argv[5], &v) == 0) cfg.burst_cnt = (uint8_t) v;
    if (argc > 6 && bsp_parse_u32(argv[6], &v) == 0) cfg.burst_tmr = (uint8_t) v;
    if (lan865x_plca_configure(&s_tc6, &cfg) != 0) {
        printf("PLCA configure failed\r\n");
        return;
    }
    s_plca = cfg;
    printf("PLCA: en=%u node_id=%u node_cnt=%u to_tmr=%u burst_cnt=%u burst_tmr=%u status=0x%04x (PST=%u)\r\n",
           cfg.enabled, cfg.node_id, cfg.node_cnt, cfg.to_tmr, cfg.burst_cnt, cfg.burst_tmr,
           cfg.status, (unsigned) ((cfg.status >> 15) & 1U));
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 plca, cmd_plca, reconfigure PLCA: plca <on|off> [id] [cnt] [to] [bc] [bt]);

static void cmd_trans(int argc, char *argv[])
{
    if (argc >= 2 && argv[1][0] == 'h') {
        oa_tc6_use_bitbang(&s_tc6, false);
        printf("transport = SPI2 controller\r\n");
    } else {
        oa_tc6_use_bitbang(&s_tc6, true);
        printf("transport = bit-bang, SCLK ~%lu Hz\r\n",
               (unsigned long) oa_tc6_bb_measure_sclk(&s_tc6));
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 trans, cmd_trans, select transport: trans <bb|hw>);

static void cmd_levels(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("nINT PA24 = %lu, nRST PA19 = %lu\r\n",
           (unsigned long) gpio_read_pin(BOARD_LAN8651_INT_GPIO_CTRL, BOARD_LAN8651_INT_GPIO_INDEX,
                                         BOARD_LAN8651_INT_GPIO_PIN),
           (unsigned long) gpio_read_pin(BOARD_LAN8651_RST_GPIO_CTRL, BOARD_LAN8651_RST_GPIO_INDEX,
                                         BOARD_LAN8651_RST_GPIO_PIN));
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 levels, cmd_levels, print nRST / nINT pin levels);
