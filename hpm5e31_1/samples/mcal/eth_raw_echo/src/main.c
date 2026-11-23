/*
 * eth_raw_echo —— AUTOSAR Eth/EthTrcv 驱动裸帧收发与对打测试
 *
 * 硬件：hpm5e31_1，ENET0(RGMII) + YT8011AN(1000BASE-T1) <-> 千兆车载以太网转换盒 <-> 主机 enp6s0
 * 驱动：mcal/Eth（Eth_Init/ProvideTxBuffer/Transmit/GetRxBuffer/ReleaseRxBuffer/计数器）
 *       mcal/EthTrcv（YT8011AN 链路/速率/主从）
 * 对打：host/eth_raw_echo.py（主机侧 AF_PACKET 原始套接字，EtherType 0x88B5，不需要 IP）
 *
 * 串口自证格式（便于 --expect 与文档引用）：
 *   RTT len=64 n=100 replied=100 lost=0 avg=...us min=...us max=...us
 *   TX  len=1514 n=20000 fps=... mbit=... (payload ... B)
 *   RX  secs=10 frames=... bytes=... mbit=......
 */
#include "bsp.h"
#include "Eth.h"
#include "Eth_PBcfg.h"
#include "EthTrcv.h"
#include "EthTrcv_PBcfg.h"
#include "Det.h"
#include "hpm_csr_drv.h"
#include "hpm_clock_drv.h"
#include <stdio.h>
#include <string.h>

#define CTRL            0U
#define TRCV            0U
#define TEST_ETHERTYPE  0x88B5U      /* 实验以太网类型（IEEE 802 本地实验用） */
#define ETH_HDR_LEN     14U
#define OFF_MAGIC       14U
#define OFF_SEQ         18U
#define OFF_PAYLOAD     22U
#define MAGIC0          'E'
#define MAGIC1          'T'
#define MAGIC2          'H'
#define MAGIC3          '0'
#define RTT_MAX         1000U

/*! 运行期可改的测试参数：EtherType 与目的 MAC（排查"帧是否被中间设备按类型过滤"） */
static Eth_FrameType s_ethertype = 0x88B5U;
static uint8 s_dst[6] = { 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU };
static uint32 s_cycles_per_us = 480U;

/* ---------------- 小工具 ---------------- */
static uint32 now_us(void)
{
    return (uint32) (hpm_csr_get_core_mcycle() / (uint64_t) s_cycles_per_us);
}

/*! 64 位微秒计时（长窗口/跨 2^32 cycle 回绕都安全） */
static uint64 now_us64(void)
{
    return (uint64) hpm_csr_get_core_mcycle() / (uint64_t) s_cycles_per_us;
}

static void mac_str(const uint8 *m, char *out)
{
    (void) sprintf(out, "%02X:%02X:%02X:%02X:%02X:%02X",
                   m[0], m[1], m[2], m[3], m[4], m[5]);
}

/*! 把 payload 填成可识别的测试图样：magic + seq + 递增字节 */
static void fill_payload(uint8 *p, uint16 len, uint32 seq)
{
    uint16 i;

    if (len < 4U) {
        return;
    }
    p[0] = (uint8) MAGIC0;
    p[1] = (uint8) MAGIC1;
    p[2] = (uint8) MAGIC2;
    p[3] = (uint8) MAGIC3;
    /* 序号 4 字节小端（原来漏写这一段，导致 eth rtt 永远配不上对：pattern 被当成 seq） */
    p[4] = (uint8) (seq & 0xFFU);
    p[5] = (uint8) ((seq >> 8) & 0xFFU);
    p[6] = (uint8) ((seq >> 16) & 0xFFU);
    p[7] = (uint8) ((seq >> 24) & 0xFFU);
    for (i = 8U; i < len; i++) {
        p[i] = (uint8) (i + seq);
    }
}


/*! 提交一帧（SWS 语义：驱动建头，净荷写在 BufPtr+14） */
static Std_ReturnType raw_send(const uint8 *dst, uint16 payload_len, uint32 seq)
{
    Eth_BufIdxType idx;
    uint8 *buf = NULL;
    uint16 grant = payload_len;
    uint8 retry;

    for (retry = 0U; retry < 200U; retry++) {
        if (Eth_ProvideTxBuffer(CTRL, 0U, &idx, &buf, &grant) == BUFREQ_OK) {
            uint8 *pl = &buf[ETH_HDR_LEN];
            fill_payload(pl, payload_len, seq);
            if (Eth_Transmit(CTRL, idx, s_ethertype, TRUE,
                             payload_len, dst) == E_OK) {
                return E_OK;
            }
            return E_NOT_OK;
        }
        Eth_TxConfirmation(CTRL);   /* 回收已发完的缓冲 */
    }
    return E_NOT_OK;
}

/*! 取一帧测试帧；非测试帧直接归还并跳过 */
static boolean raw_recv(uint8 *frame, uint16 *len, uint16 cap, Eth_BufIdxType *handle)
{
    const uint8 *data = NULL;
    uint16 l = 0U;
    Eth_FrameType type = 0U;

    while (Eth_GetRxBuffer(CTRL, 0U, handle, &data, &l, &type) != ETH_NOT_RECEIVED) {
        if ((type == s_ethertype) && (l > 0U) && (l <= cap)) {
            (void) memcpy(frame, data, l);
            *len = l;
            return TRUE;      /* 由调用方 ReleaseRxBuffer */
        }
        Eth_ReleaseRxBuffer(CTRL, *handle);
    }
    return FALSE;
}

/* ---------------- 子命令 ---------------- */
static void cmd_info(void)
{
    uint8 mac[6];
    Eth_ModeType mode = ETH_MODE_DOWN;
    EthTrcv_LinkStateType link = ETHTRCV_LINK_STATE_DOWN;
    EthTrcv_BaudRateType baud = ETHTRCV_BAUD_RATE_1000MBIT;
    EthTrcv_DuplexModeType dup = ETHTRCV_DUPLEX_MODE_FULL;
    char ms[24];

    Eth_GetPhysAddr(CTRL, mac);
    (void) Eth_GetControllerMode(CTRL, &mode);
    (void) EthTrcv_GetLinkState(TRCV, &link);
    (void) EthTrcv_GetBaudRate(TRCV, &baud);
    (void) EthTrcv_GetDuplexMode(TRCV, &dup);
    mac_str(mac, ms);

    printf("[eth] ctrl=%u mac=%s mode=%s\r\n", (unsigned) CTRL, ms,
           (mode == ETH_MODE_ACTIVE) ? "ACTIVE" : "DOWN");
    printf("[eth] trcv: link=%s speed=%s duplex=%s\r\n",
           (link == ETHTRCV_LINK_STATE_ACTIVE) ? "UP" : "DOWN",
           (baud == ETHTRCV_BAUD_RATE_1000MBIT) ? "1000M" :
           ((baud == ETHTRCV_BAUD_RATE_100MBIT) ? "100M" : "?"),
           (dup == ETHTRCV_DUPLEX_MODE_FULL) ? "FULL" : "HALF");
    printf("[eth] buf: tx=%u x %u B, rx=%u x %u B, ethertype=0x%04X\r\n",
           (unsigned) ETH_TX_BUF_COUNT, (unsigned) ETH_TX_BUF_SIZE,
           (unsigned) ETH_RX_BUF_COUNT, (unsigned) ETH_RX_BUF_SIZE,
           (unsigned) TEST_ETHERTYPE);
}

static void cmd_stat(void)
{
    Eth_CounterType c;
    Eth_RxStatsType rx;
    Eth_TxStatsType tx;
    Std_VersionInfoType v;

    (void) Eth_GetCounterValues(CTRL, &c);
    (void) Eth_GetRxStats(CTRL, &rx);
    (void) Eth_GetTxStats(CTRL, &tx);
    Eth_GetVersionInfo(&v);

    printf("[stat] det=%u rx_isr=%u\r\n", (unsigned) Det_GetErrorCount(),
           (unsigned) Eth_GetRxIsrCount(CTRL));
    printf("[stat] tx: frames=%u bytes=%u err=%u drop_busy=%u underflow=%u\r\n",
           (unsigned) tx.TxUniCastPkts, (unsigned) tx.TxNumberOfOctets,
           (unsigned) c.ErrOtbdPkt, (unsigned) c.DiscOtbdPkt, (unsigned) c.HwDepCtr3);
    printf("[stat] rx: frames=%u octets=%u crc=%u overflow=%u err=%u drop=%u\r\n",
           (unsigned) rx.RxStatsPkts, (unsigned) rx.RxStatsOctets,
           (unsigned) rx.RxStatsCrcAlignErrors, (unsigned) c.DropPktBufOverrun,
           (unsigned) c.ErrInbdPkt, (unsigned) c.DiscInbdPkt);
    printf("[stat] rx buckets: 64=%u 65-127=%u 128-255=%u 256-511=%u 512-1023=%u 1024-1518=%u\r\n",
           (unsigned) rx.RxStatsPkts64Octets, (unsigned) rx.RxStatsPkts65to127Octets,
           (unsigned) rx.RxStatsPkts128to255Octets, (unsigned) rx.RxStatsPkts256to511Octets,
           (unsigned) rx.RxStatsPkts512to1023Octets, (unsigned) rx.RxStatsPkts1024to1518Octets);
    printf("[stat] Eth sw %u.%u.%u module=%u\r\n", (unsigned) v.sw_major_version,
           (unsigned) v.sw_minor_version, (unsigned) v.sw_patch_version, (unsigned) v.moduleID);
}

/*! RTT：逐帧 ping-pong（发一帧等一帧），避免突发把主机 Python 打爆 */
static void cmd_rtt(uint32 len, uint32 n)
{
    uint32 i, replied = 0U, lost = 0U;
    uint64 sum = 0U;
    uint32 tmin = 0xFFFFFFFFU, tmax = 0U;
    static uint8 frame[ETH_MAX_FRAME_LEN + 8];
    Eth_BufIdxType h;
    uint16 flen = 0U;
    uint64 t0, all0, all1;

    if ((len < 8U) || (len > ETH_MAX_PAYLOAD)) {
        printf("[rtt] bad len %u (8..%u)\\r\\n", (unsigned) len, (unsigned) ETH_MAX_PAYLOAD);
        return;
    }
    all0 = now_us64();
    for (i = 0U; i < n; i++) {
        uint64 deadline;
        boolean got = FALSE;

        t0 = now_us64();
        if (raw_send(s_dst, (uint16) len, i) != E_OK) {
            lost++;
            continue;
        }
        deadline = t0 + 200000U;          /* 单帧最多等 200 ms */
        while (now_us64() < deadline) {
            if (raw_recv(frame, &flen, sizeof(frame), &h)) {
                uint32 seq = (uint32) frame[OFF_SEQ] | ((uint32) frame[OFF_SEQ + 1U] << 8) |
                             ((uint32) frame[OFF_SEQ + 2U] << 16) | ((uint32) frame[OFF_SEQ + 3U] << 24);
                Eth_ReleaseRxBuffer(CTRL, h);
                if (i < 3U) {
                    unsigned k;
                    printf("  RXDBG want=%u got=%u len=%u hdr:", (unsigned) i, (unsigned) seq, (unsigned) flen);
                    for (k = 0U; (k < 30U) && (k < flen); k++) {
                        printf(" %02X", (unsigned) frame[k]);
                    }
                    printf("\r\n");
                }
                if (seq == i) {
                    uint32 rtt = (uint32) (now_us64() - t0);
                    replied++;
                    sum += rtt;
                    if (rtt < tmin) { tmin = rtt; }
                    if (rtt > tmax) { tmax = rtt; }
                    got = TRUE;
                    break;
                }
            }
            Eth_TxConfirmation(CTRL);
        }
        if (!got) {
            lost++;
        }
    }
    all1 = now_us64();
    printf("RTT len=%u n=%u replied=%u lost=%u avg=%uus min=%uus max=%uus span=%ums\\r\\n",
           (unsigned) len, (unsigned) n, (unsigned) replied, (unsigned) lost,
           (replied != 0U) ? (unsigned) (sum / replied) : 0U,
           (replied != 0U) ? (unsigned) tmin : 0U,
           (replied != 0U) ? (unsigned) tmax : 0U, (unsigned) ((all1 - all0) / 1000U));
}

/*! TX 突发：按最大能力发 n 帧，测帧率/吞吐 */
static void cmd_tx(uint32 len, uint32 n)
{
    uint32 i, ok = 0U, t0, t1;
    double mbit;

    if ((len < 8U) || (len > ETH_MAX_PAYLOAD) || (n == 0U)) {
        printf("[tx] bad args len=%u n=%u\r\n", (unsigned) len, (unsigned) n);
        return;
    }
    t0 = now_us();
    for (i = 0U; i < n; i++) {
        if (raw_send(s_dst, (uint16) len, i) == E_OK) {
            ok++;
        }
        Eth_TxConfirmation(CTRL);
    }
    Eth_TxConfirmation(CTRL);
    t1 = now_us();
    if (t1 == t0) {
        t1 = t0 + 1U;
    }
    mbit = ((double) ok * (double) (len + ETH_HDR_LEN + 4U) * 8.0) / ((double) (t1 - t0));
    printf("TX len=%u n=%u ok=%u fps=%u mbit=%.1f dur=%uus\r\n",
           (unsigned) len, (unsigned) n, (unsigned) ok,
           (unsigned) ((ok * 1000000U) / (t1 - t0)), mbit, (unsigned) (t1 - t0));
}

/*! RX 洪泛窗口：统计接收帧率/吞吐（长窗口用 bsp_uptime_ms，避免 mcycle 32 位回绕） */
static void cmd_rx(uint32 secs)
{
    uint32 t0, t1;
    uint32 frames = 0U;
    uint64 bytes = 0U;
    uint32 lensum = 0U;
    double mbit;
    static uint8 frame[ETH_MAX_FRAME_LEN + 8];
    Eth_BufIdxType h;
    uint16 flen = 0U;

    if ((secs == 0U) || (secs > 300U)) {
        printf("[rx] bad secs %u (1..300)\r\n", (unsigned) secs);
        return;
    }
    t0 = bsp_uptime_ms();
    while ((bsp_uptime_ms() - t0) < (secs * 1000U)) {
        if (raw_recv(frame, &flen, sizeof(frame), &h)) {
            frames++;
            bytes += (uint64) flen;
            lensum += flen;
            Eth_ReleaseRxBuffer(CTRL, h);
        }
    }
    t1 = bsp_uptime_ms();
    mbit = ((double) bytes * 8.0) / ((double) (t1 - t0) * 1000.0);
    printf("RX secs=%u frames=%u bytes=%u mbit=%.1f avg_len=%u dur=%ums\r\n",
           (unsigned) secs, (unsigned) frames, (unsigned) bytes, mbit,
           (frames != 0U) ? (unsigned) (lensum / frames) : 0U, (unsigned) (t1 - t0));
}

/*! 回显模式：收一帧测试帧就原样回给发送方（主机侧配合测 RTT/丢包） */
static void cmd_echo(uint32 secs)
{
    uint32 t0;
    uint32 n = 0U;
    static uint8 frame[ETH_MAX_FRAME_LEN + 8];
    Eth_BufIdxType h;
    uint16 flen = 0U;

    t0 = bsp_uptime_ms();
    while ((bsp_uptime_ms() - t0) < (secs * 1000U)) {
        if (raw_recv(frame, &flen, sizeof(frame), &h)) {
            /* 交换源/目的 MAC 后回发；净荷原样带回 */
            (void) raw_send(&frame[6], (uint16) (flen - ETH_HDR_LEN), flen);
            Eth_ReleaseRxBuffer(CTRL, h);
            n++;
        }
        Eth_TxConfirmation(CTRL);
    }
    printf("ECHO secs=%u frames=%u\r\n", (unsigned) secs, (unsigned) n);
}

static void cmd_link(uint32 wait_ms)
{
    uint32 waited = 0U;
    EthTrcv_LinkStateType link = ETHTRCV_LINK_STATE_DOWN;

    while (waited < wait_ms) {
        (void) EthTrcv_MainFunction();
        (void) EthTrcv_GetLinkState(TRCV, &link);
        if (link == ETHTRCV_LINK_STATE_ACTIVE) {
            break;
        }
        board_delay_ms(10U);
        waited += 10U;
    }
    printf("LINK state=%s waited=%ums\r\n",
           (link == ETHTRCV_LINK_STATE_ACTIVE) ? "UP" : "DOWN", (unsigned) waited);
}

static void cmd_trcv(void)
{
    EthTrcv_LinkStateType link = ETHTRCV_LINK_STATE_DOWN;
    EthTrcv_BaudRateType baud = ETHTRCV_BAUD_RATE_1000MBIT;
    EthTrcv_MacMethodType mac = ETHTRCV_MAC_TYPE_UNKNOWN;
    uint32 oui = 0U, sqi = 0U;
    uint8 model = 0U, rev = 0U;
    uint8 idx = TRCV;

    (void) EthTrcv_GetLinkState(TRCV, &link);
    (void) EthTrcv_GetBaudRate(TRCV, &baud);
    (void) EthTrcv_GetPhyIdentifier(TRCV, &oui, &model, &rev);
    (void) EthTrcv_GetMacMethod(&idx, &mac);
    (void) EthTrcv_GetPhySignalQuality(TRCV, &sqi);
    printf("TRCV link=%s baud=%s mac=%s oui=0x%06X model=%u rev=%u sqi=0x%04X\r\n",
           (link == ETHTRCV_LINK_STATE_ACTIVE) ? "UP" : "DOWN",
           (baud == ETHTRCV_BAUD_RATE_1000MBIT) ? "1000M" :
           ((baud == ETHTRCV_BAUD_RATE_100MBIT) ? "100M" : "?"),
           (mac == ETHTRCV_MAC_TYPE_RGMII) ? "RGMII" : "?",
           (unsigned) oui, (unsigned) model, (unsigned) rev, (unsigned) sqi);
}

/*! 寄存器/描述符转储：排查 TX/RX 不通时看 OWN 位与 MAC/DMA 使能位（实现在 Eth.c） */
static void cmd_regs(void)
{
    Eth_DebugDump(CTRL);
}

/*! 延时命令：让"主机先起监听、板端再发"的时序对齐（毫秒） */
static void cmd_sleep(uint32 ms)
{
    uint32 t0 = bsp_uptime_ms();
    while ((bsp_uptime_ms() - t0) < ms) {
        Eth_MainFunction();
    }
    printf("SLEEP done %ums\r\n", (unsigned) ms);
}

static void cmd_mode(const char *sub)
{
    Eth_ModeType mode = ETH_MODE_DOWN;

    if (strcmp(sub, "down") == 0) {
        (void) Eth_SetControllerMode(CTRL, ETH_MODE_DOWN);
    } else if (strcmp(sub, "active") == 0) {
        (void) Eth_SetControllerMode(CTRL, ETH_MODE_ACTIVE);
    } else {
        printf("usage: eth mode active|down\r\n");
        return;
    }
    (void) Eth_GetControllerMode(CTRL, &mode);
    printf("MODE %s\r\n", (mode == ETH_MODE_ACTIVE) ? "ACTIVE" : "DOWN");
}

static void cmd_eth(int argc, char *argv[])
{
    const char *sub = (argc >= 2) ? argv[1] : "info";
    uint32 a = 0U, b = 0U;

    if (strcmp(sub, "info") == 0) {
        cmd_info();
    } else if (strcmp(sub, "stat") == 0) {
        cmd_stat();
    } else if (strcmp(sub, "rtt") == 0) {
        a = (argc >= 3) ? (uint32) strtoul(argv[2], NULL, 0) : 64U;
        b = (argc >= 4) ? (uint32) strtoul(argv[3], NULL, 0) : 100U;
        cmd_rtt(a, b);
    } else if (strcmp(sub, "tx") == 0) {
        a = (argc >= 3) ? (uint32) strtoul(argv[2], NULL, 0) : 512U;
        b = (argc >= 4) ? (uint32) strtoul(argv[3], NULL, 0) : 10000U;
        cmd_tx(a, b);
    } else if (strcmp(sub, "rx") == 0) {
        a = (argc >= 3) ? (uint32) strtoul(argv[2], NULL, 0) : 5U;
        cmd_rx(a);
    } else if (strcmp(sub, "echo") == 0) {
        a = (argc >= 3) ? (uint32) strtoul(argv[2], NULL, 0) : 5U;
        cmd_echo(a);
    } else if (strcmp(sub, "link") == 0) {
        a = (argc >= 3) ? (uint32) strtoul(argv[2], NULL, 0) : 2000U;
        cmd_link(a);
    } else if (strcmp(sub, "trcv") == 0) {
        cmd_trcv();
    } else if (strcmp(sub, "regs") == 0) {
        cmd_regs();
    } else if (strcmp(sub, "sleep") == 0) {
        a = (argc >= 3) ? (uint32) strtoul(argv[2], NULL, 0) : 1000U;
        cmd_sleep(a);
    } else if (strcmp(sub, "type") == 0) {
        s_ethertype = (Eth_FrameType) ((argc >= 3) ? (uint32) strtoul(argv[2], NULL, 16) : 0x88B5U);
        printf("ETHERTYPE 0x%04X\r\n", (unsigned) s_ethertype);
    } else if (strcmp(sub, "dst") == 0) {
        unsigned a[6];
        int k;
        if ((argc >= 3) && (sscanf(argv[2], "%x:%x:%x:%x:%x:%x", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]) == 6)) {
            for (k = 0; k < 6; k++) { s_dst[k] = (uint8) a[k]; }
            printf("DST %02X:%02X:%02X:%02X:%02X:%02X\r\n", s_dst[0], s_dst[1], s_dst[2], s_dst[3], s_dst[4], s_dst[5]);
        } else {
            printf("usage: eth dst xx:xx:xx:xx:xx:xx\r\n");
        }
    } else if (strcmp(sub, "mode") == 0) {
        cmd_mode((argc >= 3) ? argv[2] : "");
    } else if (strcmp(sub, "ver") == 0) {
        Std_VersionInfoType v;
        Eth_GetVersionInfo(&v);
        printf("Eth sw %u.%u.%u module=%u vendor=0x%04X\r\n", (unsigned) v.sw_major_version,
               (unsigned) v.sw_minor_version, (unsigned) v.sw_patch_version,
               (unsigned) v.moduleID, (unsigned) v.vendorID);
        EthTrcv_GetVersionInfo(&v);
        printf("EthTrcv sw %u.%u.%u module=%u vendor=0x%04X\r\n", (unsigned) v.sw_major_version,
               (unsigned) v.sw_minor_version, (unsigned) v.sw_patch_version,
               (unsigned) v.moduleID, (unsigned) v.vendorID);
    } else {
        printf("unknown sub: %s (info|stat|rtt|tx|rx|echo|link|trcv|mode|regs|type|dst|sleep|ver)\r\n", sub);
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 eth, cmd_eth, ETH raw frame test: eth info|stat|rtt [len] [n]|tx [len] [n]|rx [s]|echo [s]|link [ms]|trcv|mode|ver);

int main(void)
{
    uint32 cpu_mhz;

    bsp_init();
    cpu_mhz = clock_get_frequency(clock_cpu0) / 1000000U;
    s_cycles_per_us = (cpu_mhz != 0U) ? cpu_mhz : 480U;

    printf("\r\n=== eth_raw_echo: AUTOSAR Eth/EthTrcv, ENET0 + YT8011AN 1000BASE-T1 ===\r\n");
    printf("[boot] cpu=%u MHz, ethertype=0x%04X, tx=%u x %uB, rx=%u x %uB\r\n",
           (unsigned) cpu_mhz, (unsigned) TEST_ETHERTYPE,
           (unsigned) ETH_TX_BUF_COUNT, (unsigned) ETH_TX_BUF_SIZE,
           (unsigned) ETH_RX_BUF_COUNT, (unsigned) ETH_RX_BUF_SIZE);

    Det_Init();
    Det_ClearErrorCount();

    Eth_Init(&Eth_Config);
    printf("[init] Eth_Init done: ctrl=%u base=%p\r\n", (unsigned) Eth_Config.CtrlCount,
           Eth_Config.CtrlConfigPtr[0].BaseAddress);
    EthTrcv_Init(&EthTrcv_Config);
    printf("[init] EthTrcv_Init done: trcv=%u phyaddr=%u\r\n",
           (unsigned) EthTrcv_Config.TrcvCount,
           (unsigned) EthTrcv_Config.TrcvConfigPtr[0].PhyAddr);
    (void) Eth_SetControllerMode(CTRL, ETH_MODE_ACTIVE);
    printf("[init] controller ACTIVE, det=%u\r\n", (unsigned) Det_GetErrorCount());

    /* 启动阶段只打印静态信息，不做阻塞等待：保证串口 shell 能尽快响应 hwrun 的命令
     * （链路等待/PHY 状态查询用 'eth link' / 'eth trcv' 显式触发） */
    cmd_info();

    while (1) {
        Eth_MainFunction();
        EthTrcv_MainFunction();
        bsp_shell_poll();
    }
}