/*
 * app_lwip_eth.c - lwIP 2.1.2 netif 适配层，硬件后端为 AUTOSAR MCAL Eth 驱动
 *
 * 数据通路（与 mcal/Eth/include/Eth.h 头部的"数据通路约定"一一对应）：
 *
 *   TX:  lwIP 协议栈 -> app_lwip_eth_linkoutput(netif, pbuf)
 *          -> ops->tx_buffer_get()          == Eth_ProvideTxBuffer()（拿到帧起始指针）
 *          -> pbuf_copy_partial(p, buf, len, 0)   // 整帧含 14 B 头，一次拷贝
 *          -> ops->tx_commit(len)           == Eth_Transmit(..., LenByte=整帧长, PhysAddrPtr=NULL)
 *        与 SDK enet 驱动路径的差别：SDK 是"描述符直接指向 pbuf 净荷"（零拷贝 TX），
 *        MCAL 只给"驱动内缓冲"，所以这里必然多一次整帧 memcpy —— 第 50 章"性能"节量化。
 *
 *   RX:  Eth_GetRxBuffer() 返回 DMA 环内缓冲指针（含 14 B 头，驱动已 invalidate cache）
 *          -> 用 custom pbuf（pbuf_alloced_custom，payload 直接指向驱动缓冲）包一层
 *          -> netif->input(p, netif)   // 裸机 = ethernet_input；RTOS = tcpip_input
 *          -> lwIP 处理完释放 pbuf -> custom_free_function -> Eth_ReleaseRxBuffer()
 *        真正的零拷贝：一帧在 lwIP 里不做任何数据搬运，只有 pbuf 结构体开销。
 *
 * 并发模型：
 *   - NO_SYS=1：全部在主循环，无锁；
 *   - NO_SYS=0：收包任务（app_lwip_eth_rx_task）是唯一调用 Eth Rx 的上下文，
 *     TX 发生在 tcpip 线程；两个上下文访问 Eth 驱动由后端的 lock/unlock 串行化。
 *
 * 作者：eth-stack（task-2）；参考：SDK samples/lwip/ports/{baremetal,freertos}/ethernetif.c
 */
#include <string.h>
#include <stdio.h>

#include "lwip/opt.h"
#include "lwip/init.h"
#include "lwip/pbuf.h"
#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "lwip/raw.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/apps/lwiperf.h"
#include "lwip/sys.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"

#if !NO_SYS
#include "lwip/tcpip.h"
#include "FreeRTOS.h"
#include "task.h"
#endif

#include "app_lwip_eth.h"
#include "app_lwip_eth_hw.h"

/* CPU 周期计数（ping 的 RTT 分辨到 sub-us，sys_now() 只有 1 ms 精度不够） */
#include "hpm_csr_drv.h"
#include "hpm_clock_drv.h"

#if NO_SYS
#define APP_LWIP_CORE_LOCK()
#define APP_LWIP_CORE_UNLOCK()
#else
#if !LWIP_TCPIP_CORE_LOCKING
#error "FreeRTOS 版本请在 lwipopts 里定义 LWIP_TCPIP_CORE_LOCKING=1"
#endif
#define APP_LWIP_CORE_LOCK()   LOCK_TCPIP_CORE()
#define APP_LWIP_CORE_UNLOCK() UNLOCK_TCPIP_CORE()
#endif

/* RX custom pbuf 池大小：必须 >= 后端 rx_buf_count；留 2 倍余量给应用持包 */
#ifndef APP_LWIP_ETH_RX_PBUF_NUM
#define APP_LWIP_ETH_RX_PBUF_NUM   (32)
#endif

#define APP_LWIP_ETH_LINK_POLL_MS  (1000U)

/* 单次 RX drain 的帧数上限（50 章 §50.9.14）
 * 早期实现"一直收到环空"，在 TCP 打流时环永远不空：实测一次 app_lwip_eth_poll()
 * 里的 drain 连续跑了 2 408 360 us（2.4 s，见 logs/20261102-182443 的
 * rx_drain_max），期间 sys_check_timeouts()（RTO/延迟 ACK/persist 定时器）、
 * Eth_MainFunction()、letter-shell 全部得不到执行，TCP 上下行都表现为"冲一下停几秒"。
 * RX 环本身有 ETH_RX_BUF_COUNT(16) 个缓冲的深度，一次处理 32 帧（= 2 倍环深）
 * 既能把环排空，又能保证主循环每 ~150 us 回到协议栈定时器。 */
#ifndef APP_LWIP_ETH_RX_DRAIN_MAX
#define APP_LWIP_ETH_RX_DRAIN_MAX  (32)
#endif

/* ------------------------------------------------------------------ */
/* 状态                                                                */
/* ------------------------------------------------------------------ */
static struct netif s_netif;
static const app_lwip_eth_hw_ops_t *s_ops;
static uint8_t s_ctrl_idx;
static bool s_inited;
static uint32_t s_last_link_poll_ms;
/* 主循环耗时画像：定位"1 s 台阶"这类掉包必须靠它 */
static uint32_t s_loop_last_cyc;
static uint32_t s_loop_gap_max_cyc;
static uint32_t s_tx_max_cyc;
static uint32_t s_rx_drain_max_cyc;
/* 链路轮询耗时（EthTrcv_GetLinkState 走 MDIO，实测过慢会拖垮收包，必须有数据支撑） */
static uint32_t s_link_poll_count;
static uint32_t s_link_poll_max_cyc;
static uint32_t s_link_poll_last_cyc;

/* 本层软件统计（与后端统计分开，便于定位问题出在哪一层） */
typedef struct {
    uint32_t rx_frames;        /*!< 交给 lwIP 的帧数 */
    uint32_t rx_pbuf_alloc_fail;
    uint32_t rx_input_err;
    uint32_t tx_frames;
    uint32_t tx_err;
    uint32_t tx_drop_len;      /*!< 超过单帧上限被丢 */
    uint32_t tx_retry_ok;      /*!< 环满 → 立即回收 TxConfirmation 后重试成功 */
    uint32_t tx_retry_fail;    /*!< 回收后仍然拿不到（真丢帧） */
    uint32_t rx_pbuf_in_use;   /*!< 当前被 lwIP 持有的 custom pbuf 数（水位） */
    uint32_t rx_pbuf_peak;
} app_lwip_eth_sw_stats_t;

static app_lwip_eth_sw_stats_t s_stats;

/* ------------------------------------------------------------------ */
/* RX custom pbuf（零拷贝）                                             */
/* ------------------------------------------------------------------ */
struct app_rx_pbuf_slot {
    struct pbuf_custom pc;     /*!< 必须是第一个成员：lwIP 会把 pbuf 指针强转回来 */
    void *cookie;              /*!< 后端释放句柄（Eth BufIdx） */
    uint8_t in_use;
};

static struct app_rx_pbuf_slot s_rx_slots[APP_LWIP_ETH_RX_PBUF_NUM];

/*
 * lwIP 释放 pbuf 时调用：把 DMA 缓冲还给驱动。
 * 注意：NO_SYS=0 时这个回调在 tcpip 线程里执行，而分配发生在收包任务里，
 * 所以 in_use 必须用 SYS_ARCH_PROTECT 保护。
 */
static void app_lwip_eth_rx_pbuf_free(struct pbuf *p)
{
    struct app_rx_pbuf_slot *slot = (struct app_rx_pbuf_slot *) p;
    SYS_ARCH_DECL_PROTECT(lev);

    if (s_ops->rx_release != NULL) {
        s_ops->rx_release(s_ctrl_idx, slot->cookie);
    }
    SYS_ARCH_PROTECT(lev);
    slot->cookie = NULL;
    slot->in_use = 0U;
    if (s_stats.rx_pbuf_in_use > 0U) {
        s_stats.rx_pbuf_in_use--;
    }
    SYS_ARCH_UNPROTECT(lev);
}

static struct pbuf *app_lwip_eth_rx_pbuf_alloc(const uint8_t *frame, uint16_t len, void *cookie)
{
    struct app_rx_pbuf_slot *slot = NULL;
    struct pbuf *p;
    uint32_t i;
    SYS_ARCH_DECL_PROTECT(lev);

    SYS_ARCH_PROTECT(lev);
    for (i = 0U; i < APP_LWIP_ETH_RX_PBUF_NUM; i++) {
        if (s_rx_slots[i].in_use == 0U) {
            s_rx_slots[i].in_use = 1U;
            slot = &s_rx_slots[i];
            s_stats.rx_pbuf_in_use++;
            if (s_stats.rx_pbuf_in_use > s_stats.rx_pbuf_peak) {
                s_stats.rx_pbuf_peak = s_stats.rx_pbuf_in_use;
            }
            break;
        }
    }
    SYS_ARCH_UNPROTECT(lev);

    if (slot == NULL) {
        return NULL;    /* 池耗尽：调用方负责归还 DMA 缓冲 */
    }
    slot->cookie = cookie;
    slot->pc.custom_free_function = app_lwip_eth_rx_pbuf_free;
    p = pbuf_alloced_custom(PBUF_RAW, len, PBUF_REF, &slot->pc, (void *) frame, len);
    if (p == NULL) {
        slot->in_use = 0U;
        slot->cookie = NULL;
        s_stats.rx_pbuf_in_use--;
        return NULL;
    }
    return p;
}

/* ------------------------------------------------------------------ */
/* 链路                                                                */
/* ------------------------------------------------------------------ */
static void app_lwip_eth_link_apply(bool up)
{
#if NO_SYS
    if (up) {
        netif_set_link_up(&s_netif);
    } else {
        netif_set_link_down(&s_netif);
    }
#else
    APP_LWIP_CORE_LOCK();
    if (up) {
        netif_set_link_up(&s_netif);
    } else {
        netif_set_link_down(&s_netif);
    }
    APP_LWIP_CORE_UNLOCK();
#endif
}

/* ------------------------------------------------------------------ */
/* netif 回调                                                          */
/* ------------------------------------------------------------------ */
static err_t app_lwip_eth_linkoutput(struct netif *netif, struct pbuf *p)
{
    uint8_t *buf;
    void *cookie = NULL;
    uint16_t frame_len;
    uint32_t c0;
    uint32_t dt;

    (void) netif;
    if ((s_ops == NULL) || (p == NULL)) {
        return ERR_IF;
    }
    frame_len = (uint16_t) p->tot_len;
    if (frame_len > s_ops->max_frame_len) {
        s_stats.tx_drop_len++;
        return ERR_BUF;
    }

    c0 = (uint32_t) hpm_csr_get_core_mcycle();
    s_ops->lock(s_ctrl_idx);
    buf = s_ops->tx_buffer_get(s_ctrl_idx, frame_len, &cookie);
    if (buf == NULL) {
        /* ---- TX 环满时的立即回收（50 章 §50.9.13） -----------------------------
         * TCP 的 ACK 是在 netif->input() 里同步产生的：app_lwip_eth_rx_drain()
         * 一次循环要连收最多 16 帧，每帧都可能立刻回一个 ACK；而 Eth_TxConfirmation
         * 平时只在 app_lwip_eth_poll() -> Eth_MainFunction() 里跑。于是 drain 期间
         * 8 个 TX 缓冲被 ACK 占满后，其余 ACK 全部 BUFREQ_E_BUSY -> ERR_MEM 被 lwIP
         * 丢掉，只能等 200~500 ms 的延迟 ACK 定时器补 —— TCP 吞吐掉到几 Mbit/s 且
         * 忽高忽低（实测 1.4~11.5 Mbit/s 抖动），而同固件 UDP 完全正常。
         * 这里在拿不到缓冲时就地跑一次驱动的周期动作（Eth_MainFunction 只有
         * Eth_TxConfirmation + RxHeld 统计，不碰 RX 环），把 DMA 已发完的描述符
         * 标回 FREE 再重试一次；仍然失败才是真的无缓冲（计入 tx_retry_fail）。 */
        s_ops->unlock(s_ctrl_idx);
        if (s_ops->poll == NULL) {
            return ERR_MEM;
        }
        s_ops->poll(s_ctrl_idx);
        s_ops->lock(s_ctrl_idx);
        buf = s_ops->tx_buffer_get(s_ctrl_idx, frame_len, &cookie);
        if (buf == NULL) {
            s_ops->unlock(s_ctrl_idx);
            s_stats.tx_retry_fail++;
            return ERR_MEM;     /* 驱动 Tx 环真的满：lwIP 由 TCP 重传兜底 */
        }
        s_stats.tx_retry_ok++;
    }
    /* 整帧（含 14 B 以太网头）一次拷进驱动缓冲：Eth_Transmit(PhysAddrPtr=NULL) 语义 */
    (void) pbuf_copy_partial(p, buf, frame_len, 0);
    if (s_ops->tx_commit(s_ctrl_idx, cookie, frame_len) != 0) {
        s_ops->unlock(s_ctrl_idx);
        s_stats.tx_err++;
        return ERR_IF;
    }
    s_ops->unlock(s_ctrl_idx);
    dt = (uint32_t) hpm_csr_get_core_mcycle() - c0;
    if (dt > s_tx_max_cyc) {
        s_tx_max_cyc = dt;
    }
    s_stats.tx_frames++;
    return ERR_OK;
}

static err_t app_lwip_eth_netif_init(struct netif *netif)
{
    netif->name[0] = 'e';
    netif->name[1] = '0';
    netif->mtu = 1500;
    netif->hwaddr_len = 6U;
#if LWIP_NETIF_HOSTNAME
    netif->hostname = "hpm5e31-eth-mcal";
#endif
    s_ops->get_mac(s_ctrl_idx, netif->hwaddr);
    netif->output = etharp_output;
    netif->linkoutput = app_lwip_eth_linkoutput;
    netif->flags |= NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_IGMP;
    return ERR_OK;
}

/* ------------------------------------------------------------------ */
/* 收包                                                                */
/* ------------------------------------------------------------------ */
int app_lwip_eth_rx_drain(void)
{
    int count = 0;

    if ((s_ops == NULL) || !s_inited) {
        return 0;
    }
    for (;;) {
        const uint8_t *frame = NULL;
        uint16_t len = 0U;
        void *cookie = NULL;
        struct pbuf *p;
        int rc;

        if (count >= APP_LWIP_ETH_RX_DRAIN_MAX) {
            break;              /* 让主循环回去跑 sys_check_timeouts()/Eth_MainFunction() */
        }
        s_ops->lock(s_ctrl_idx);
        rc = s_ops->rx_get(s_ctrl_idx, &frame, &len, &cookie);
        s_ops->unlock(s_ctrl_idx);

        if (rc == 1) {
            break;              /* 没有新帧 */
        }
        if (rc < 0) {
            break;              /* 驱动报错 */
        }
        if ((frame == NULL) || (len < 14U) || (len > s_ops->max_frame_len)) {
            s_ops->rx_release(s_ctrl_idx, cookie);
            s_stats.rx_input_err++;
            continue;
        }
        p = app_lwip_eth_rx_pbuf_alloc(frame, len, cookie);
        if (p == NULL) {
            /* custom pbuf 池耗尽：立刻归还 DMA 缓冲，避免 Rx 环死锁 */
            s_ops->rx_release(s_ctrl_idx, cookie);
            s_stats.rx_pbuf_alloc_fail++;
            break;
        }
        if (s_netif.input(p, &s_netif) != ERR_OK) {
            s_stats.rx_input_err++;
            pbuf_free(p);       /* 触发 custom_free_function -> Eth_ReleaseRxBuffer */
        }
        s_stats.rx_frames++;
        count++;
    }
    return count;
}

void app_lwip_eth_poll(void)
{
    uint32_t c0 = (uint32_t) hpm_csr_get_core_mcycle();
    if (s_loop_last_cyc != 0U) {
        uint32_t gap = c0 - s_loop_last_cyc;
        if (gap > s_loop_gap_max_cyc) {
            s_loop_gap_max_cyc = gap;
        }
    }
#if !NO_SYS
    /* RTOS 版本收包由 app_lwip_eth_rx_task 负责，这里只保留驱动周期动作 */
    if (s_ops->poll != NULL) {
        s_ops->poll(s_ctrl_idx);
    }
#else
    {
        uint32_t c0 = (uint32_t) hpm_csr_get_core_mcycle();
        (void) app_lwip_eth_rx_drain();
        {
            uint32_t dt = (uint32_t) hpm_csr_get_core_mcycle() - c0;
            if (dt > s_rx_drain_max_cyc) {
                s_rx_drain_max_cyc = dt;
            }
        }
    }
    if (s_ops->poll != NULL) {
        s_ops->poll(s_ctrl_idx);
    }
    sys_check_timeouts();
    /* lwiperf 的 UDP 客户端要每轮轮询才会真正发包（lwIP 官方样例在 main 循环里同样调用） */
    lwiperf_poll_udp_client();
#endif
    (void) app_lwip_eth_link_poll();
    s_loop_last_cyc = (uint32_t) hpm_csr_get_core_mcycle();
}

int app_lwip_eth_link_poll(void)
{
    app_lwip_eth_hw_link_t link;
    uint32_t now = sys_now();

    if ((s_ops == NULL) || (s_ops->link_get == NULL) || !s_inited) {
        return 0;
    }
    if ((s_last_link_poll_ms != 0U) && ((now - s_last_link_poll_ms) < APP_LWIP_ETH_LINK_POLL_MS)) {
        return netif_is_link_up(&s_netif) ? 1 : 0;
    }
    s_last_link_poll_ms = now;
    memset(&link, 0, sizeof(link));
    {
        uint32_t c0 = (uint32_t) hpm_csr_get_core_mcycle();
        s_ops->link_get(s_ctrl_idx, &link);
        s_link_poll_last_cyc = (uint32_t) hpm_csr_get_core_mcycle() - c0;
        s_link_poll_count++;
        if (s_link_poll_last_cyc > s_link_poll_max_cyc) {
            s_link_poll_max_cyc = s_link_poll_last_cyc;
        }
    }
    if (link.known) {
        if (link.up && !netif_is_link_up(&s_netif)) {
            app_lwip_eth_link_apply(true);
        } else if (!link.up && netif_is_link_up(&s_netif)) {
            app_lwip_eth_link_apply(false);
        }
    }
    return netif_is_link_up(&s_netif) ? 1 : 0;
}

bool app_lwip_eth_link_up(void)
{
    return netif_is_link_up(&s_netif);
}

/* ------------------------------------------------------------------ */
/* 初始化 / 查询                                                       */
/* ------------------------------------------------------------------ */
int app_lwip_eth_init(const app_lwip_eth_cfg_t *cfg)
{
    static const app_lwip_eth_cfg_t def = {
        APP_LWIP_ETH_DEFAULT_IP, APP_LWIP_ETH_DEFAULT_NETMASK, APP_LWIP_ETH_DEFAULT_GW,
        0U, APP_LWIP_ETH_DEFAULT_LINK_TIMEOUT_MS, true
    };
    ip4_addr_t ip;
    ip4_addr_t mask;
    ip4_addr_t gw;
    uint32_t t0;

    if (cfg == NULL) {
        cfg = &def;
    }
    memset(&s_stats, 0, sizeof(s_stats));
    memset(s_rx_slots, 0, sizeof(s_rx_slots));

    s_ops = app_lwip_eth_hw_mcal();
    s_ctrl_idx = cfg->ctrl_idx;
    if (s_ops == NULL) {
        return -2;
    }
    if ((s_ops->init != NULL) && (s_ops->init(s_ctrl_idx) != 0)) {
        return -2;              /* Eth 未初始化 / 控制器没进 ACTIVE */
    }
    if (s_ops->rx_buf_count > APP_LWIP_ETH_RX_PBUF_NUM) {
        if (cfg->verbose) {
            printf("[lwip-eth] WARN: rx_buf_count(%u) > RX pbuf pool(%u), 池可能耗尽\r\n",
                   (unsigned) s_ops->rx_buf_count, (unsigned) APP_LWIP_ETH_RX_PBUF_NUM);
        }
    }
    if (!ip4addr_aton((cfg->ip != NULL) ? cfg->ip : APP_LWIP_ETH_DEFAULT_IP, &ip) ||
        !ip4addr_aton((cfg->netmask != NULL) ? cfg->netmask : APP_LWIP_ETH_DEFAULT_NETMASK, &mask) ||
        !ip4addr_aton((cfg->gw != NULL) ? cfg->gw : APP_LWIP_ETH_DEFAULT_GW, &gw)) {
        return -1;
    }

#if NO_SYS
    lwip_init();
    if (netif_add(&s_netif, &ip, &mask, &gw, NULL, app_lwip_eth_netif_init, ethernet_input) == NULL) {
        return -3;
    }
    netif_set_default(&s_netif);
    netif_set_up(&s_netif);
#else
    /* NO_SYS=0：必须在 tcpip_init() 之后调用；netif input 走 tcpip_input（线程安全） */
    if (netif_add(&s_netif, &ip, &mask, &gw, NULL, app_lwip_eth_netif_init, tcpip_input) == NULL) {
        return -3;
    }
    APP_LWIP_CORE_LOCK();
    netif_set_default(&s_netif);
    netif_set_up(&s_netif);
    APP_LWIP_CORE_UNLOCK();
#endif
    s_inited = true;
    s_last_link_poll_ms = 0U;

    if (cfg->verbose) {
        printf("[lwip-eth] backend=%s ctrl=%u max_frame=%u tx_buf=%u rx_buf=%u\r\n",
               s_ops->name, (unsigned) s_ctrl_idx, (unsigned) s_ops->max_frame_len,
               (unsigned) s_ops->tx_buf_count, (unsigned) s_ops->rx_buf_count);
    }

    /* 等链路：1000BASE-T1 训练 + EthTrcv 轮询，通常 300 ms ~ 2 s */
    t0 = sys_now();
    while (cfg->link_timeout_ms != 0U) {
        if (app_lwip_eth_link_poll()) {
            break;
        }
#if NO_SYS
        sys_check_timeouts();
#endif
        if ((sys_now() - t0) >= cfg->link_timeout_ms) {
            break;
        }
    }
    if (cfg->verbose) {
        app_lwip_eth_print();
        printf("[lwip-eth] link=%s after %lu ms\r\n",
               netif_is_link_up(&s_netif) ? "UP" : "DOWN", (unsigned long) (sys_now() - t0));
    }
    return netif_is_link_up(&s_netif) ? 0 : 1;
}

struct netif *app_lwip_eth_netif(void)
{
    return &s_netif;
}

bool app_lwip_eth_parse_ip(const char *s, ip_addr_t *out)
{
    ip4_addr_t v;

    if ((s == NULL) || (out == NULL)) {
        return false;
    }
    if (!ip4addr_aton(s, &v)) {
        return false;
    }
    ip_addr_copy_from_ip4(*out, v);
    return true;
}

/* ------------------------------------------------------------------ */
/* 打印 / 统计                                                         */
/* ------------------------------------------------------------------ */
void app_lwip_eth_print(void)
{
    app_lwip_eth_hw_link_t link;

    if (!s_inited) {
        printf("[lwip-eth] not initialized\r\n");
        return;
    }
    printf("[lwip-eth] netif=%c%c idx=%u MAC=%02X:%02X:%02X:%02X:%02X:%02X\r\n",
           s_netif.name[0], s_netif.name[1], (unsigned) netif_get_index(&s_netif),
           s_netif.hwaddr[0], s_netif.hwaddr[1], s_netif.hwaddr[2],
           s_netif.hwaddr[3], s_netif.hwaddr[4], s_netif.hwaddr[5]);
    /* ipaddr_ntoa() 返回静态缓冲，一行里调多次会全部变成最后一个值，故拆行 */
    printf("[lwip-eth] IP=%s\r\n", ipaddr_ntoa(netif_ip_addr4(&s_netif)));
    printf("[lwip-eth] MASK=%s\r\n", ipaddr_ntoa(netif_ip_netmask4(&s_netif)));
    printf("[lwip-eth] GW=%s  link=%s\r\n", ipaddr_ntoa(netif_ip_gw4(&s_netif)),
           netif_is_link_up(&s_netif) ? "UP" : "DOWN");
    if (s_ops->link_get != NULL) {
        memset(&link, 0, sizeof(link));
        s_ops->link_get(s_ctrl_idx, &link);
        printf("[lwip-eth] phy: link=%s known=%u speed=%u Mbps duplex=%s role=%s\r\n",
               link.up ? "UP" : "DOWN", (unsigned) link.known, (unsigned) link.speed_mbps,
               link.full_duplex ? "full" : "half", link.master ? "MASTER" : "SLAVE");
    }
    if (s_ops->print != NULL) {
        s_ops->print(s_ctrl_idx);
    }
}

void app_lwip_eth_print_stats(void)
{
    app_lwip_eth_hw_stats_t hw;

    memset(&hw, 0, sizeof(hw));
    if ((s_ops != NULL) && (s_ops->stats_get != NULL)) {
        s_ops->stats_get(s_ctrl_idx, &hw);
    }
    printf("[lwip-eth] sw : rx_frames=%lu rx_input_err=%lu rx_pbuf_fail=%lu | tx_frames=%lu tx_err=%lu tx_drop_len=%lu\r\n",
           (unsigned long) s_stats.rx_frames, (unsigned long) s_stats.rx_input_err,
           (unsigned long) s_stats.rx_pbuf_alloc_fail, (unsigned long) s_stats.tx_frames,
           (unsigned long) s_stats.tx_err, (unsigned long) s_stats.tx_drop_len);
    printf("[lwip-eth] tx recycle: retry_ok=%lu retry_fail=%lu\r\n",
           (unsigned long) s_stats.tx_retry_ok, (unsigned long) s_stats.tx_retry_fail);
    printf("[lwip-eth] loop: gap_max=%lu us tx_max=%lu us rx_drain_max=%lu us\r\n",
           (unsigned long) (s_loop_gap_max_cyc / 480U),
           (unsigned long) (s_tx_max_cyc / 480U),
           (unsigned long) (s_rx_drain_max_cyc / 480U));
    printf("[lwip-eth] link poll: n=%lu last=%lu us max=%lu us (MDIO 阻塞会直接掉包)\r\n",
           (unsigned long) s_link_poll_count,
           (unsigned long) (s_link_poll_last_cyc / 480U),
           (unsigned long) (s_link_poll_max_cyc / 480U));
    printf("[lwip-eth] rx pbuf pool: in_use=%lu peak=%lu/%u\r\n",
           (unsigned long) s_stats.rx_pbuf_in_use, (unsigned long) s_stats.rx_pbuf_peak,
           (unsigned) APP_LWIP_ETH_RX_PBUF_NUM);
    printf("[lwip-eth] hw : rx_frames=%lu rx_bytes=%lu rx_empty=%lu rx_err=%lu | tx_frames=%lu tx_bytes=%lu tx_busy=%lu tx_err=%lu\r\n",
           (unsigned long) hw.rx_frames, (unsigned long) hw.rx_bytes, (unsigned long) hw.rx_empty,
           (unsigned long) hw.rx_err, (unsigned long) hw.tx_frames, (unsigned long) hw.tx_bytes,
           (unsigned long) hw.tx_busy, (unsigned long) hw.tx_err);
}

void app_lwip_eth_stats_reset(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
}

/* ------------------------------------------------------------------ */
/* iperf2（lwIP lwiperf，端口 5001）                                    */
/* ------------------------------------------------------------------ */
/* 句柄拆分（R10.1）：旧实现把"服务端监听"和"客户端会话"共用一个 s_iperf_session，并在
 * 报告回调里无条件清空（R9 为修 ABORTED 悬空指针）。但 lwiperf 的 server 模式里，报告回调是
 * "每个被接受的连接"结束时调用的，监听本身（server_pcb / listener state）不随连接结束释放。
 * 连接一结束就清句柄 ⇒ 监听句柄丢失 ⇒ 下个周期 arm_server 再发 eth iserver 会重新走
 * lwiperf_start_tcp_server_impl → tcp_bind(5001) 失败；而该 SDK 版本的 bind 失败路径不释放
 * 已分配的 lwiperf state 与 tcp_pcb，于是**每个周期泄漏 1 个 TCP_PCB + 192 B HEAP**
 * （R10 15 h 实测：22 个孤儿 pcb、池 24 耗尽后 SYN 被静默丢弃 → 板子挂死）。
 * 修法：客户端、TCP 服务端、UDP 服务端各一个句柄；报告回调只清客户端句柄（服务端连接的
 * 终态报告里它本来就是 NULL，无副作用），监听句柄常驻，重臂时返回"已监听"，istop 统一释放。 */
static void *s_iperf_client;       /* 单次客户端会话（DONE/ABORTED 报告后清空） */
static void *s_iperf_tcp_server;   /* TCP 5001 监听（常驻） */
static void *s_iperf_udp_server;   /* UDP 5001 服务端状态（常驻） */

static void app_lwip_eth_iperf_report(void *arg, enum lwiperf_report_type report_type,
                                      const ip_addr_t *local_addr, u16_t local_port,
                                      const ip_addr_t *remote_addr, u16_t remote_port,
                                      u32_t bytes_transferred, u32_t ms_duration,
                                      u32_t bandwidth_kbitpsec)
{
    (void) arg;
    (void) local_addr;
    (void) local_port;
    printf("[iperf] type=%d remote=%s:%u bytes=%lu ms=%lu => %lu kbit/s (%.2f Mbit/s)\r\n",
           (int) report_type, ipaddr_ntoa(remote_addr), (unsigned) remote_port,
           (unsigned long) bytes_transferred, (unsigned long) ms_duration,
           (unsigned long) bandwidth_kbitpsec, (double) bandwidth_kbitpsec / 1000.0);
    /* 客户端会话任何终态（DONE_* 或 ABORTED_*）都清句柄：R9 的悬空指针问题就修在这里。
     * 服务端监听句柄 s_iperf_tcp_server/s_iperf_udp_server 不受报告影响。 */
    s_iperf_client = NULL;
}

int app_lwip_eth_iperf_server(bool udp)
{
    void **slot = udp ? &s_iperf_udp_server : &s_iperf_tcp_server;

    if (*slot != NULL) {
        return -1;      /* 已监听：不要重复 lwiperf_start_*（会走 bind 失败并泄漏） */
    }
    if (udp) {
        *slot = lwiperf_start_udp_server(netif_ip_addr4(&s_netif), LWIPERF_UDP_PORT_DEFAULT,
                                         app_lwip_eth_iperf_report, NULL);
    } else {
        *slot = lwiperf_start_tcp_server_default(app_lwip_eth_iperf_report, NULL);
    }
    return (*slot != NULL) ? 0 : -2;
}

int app_lwip_eth_iperf_client(const char *remote_ip, bool udp)
{
    ip_addr_t remote;

    if (s_iperf_client != NULL) {
        return -1;
    }
    if (!app_lwip_eth_parse_ip(remote_ip, &remote)) {
        return -3;
    }
    APP_LWIP_CORE_LOCK();
    if (udp) {
        s_iperf_client = lwiperf_start_udp_client(netif_ip_addr4(&s_netif), LWIPERF_UDP_PORT_DEFAULT,
                                                  &remote, LWIPERF_UDP_PORT_DEFAULT,
                                                  LWIPERF_CLIENT, -1000, 1000U * 1024U * 1024U, 0,
                                                  app_lwip_eth_iperf_report, NULL);
    } else {
        s_iperf_client = lwiperf_start_tcp_client_default(&remote, app_lwip_eth_iperf_report, NULL);
    }
    APP_LWIP_CORE_UNLOCK();
    return (s_iperf_client != NULL) ? 0 : -2;
}

void app_lwip_eth_iperf_stop(void)
{
    APP_LWIP_CORE_LOCK();
    if (s_iperf_client != NULL) {
        lwiperf_abort(s_iperf_client);
        s_iperf_client = NULL;
    }
    if (s_iperf_tcp_server != NULL) {
        lwiperf_abort(s_iperf_tcp_server);
        s_iperf_tcp_server = NULL;
    }
    if (s_iperf_udp_server != NULL) {
        lwiperf_abort(s_iperf_udp_server);
        s_iperf_udp_server = NULL;
    }
    APP_LWIP_CORE_UNLOCK();
}

bool app_lwip_eth_iperf_running(void)
{
    return (s_iperf_client != NULL) || (s_iperf_tcp_server != NULL) ||
           (s_iperf_udp_server != NULL);
}

/* ------------------------------------------------------------------ */
/* ICMP echo（ping）：测 RTT 与抖动                                     */
/* ------------------------------------------------------------------ */
#define APP_LWIP_ETH_PING_DATA_LEN   (32U)
#define APP_LWIP_ETH_PING_ID         (0xA5A5U)

typedef struct {
    struct raw_pcb *pcb;
    ip_addr_t dst;
    volatile bool got;
    volatile uint16_t seq_got;
    uint64_t t_send_cyc;
    uint64_t rtt_cyc;
} app_lwip_eth_ping_ctx_t;

static u8_t app_lwip_eth_ping_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p,
                                   const ip_addr_t *addr)
{
    app_lwip_eth_ping_ctx_t *ctx = (app_lwip_eth_ping_ctx_t *) arg;
    struct icmp_echo_hdr *iecho;
    u16_t ip_hdr_len = IP_HLEN;

    (void) pcb;
    (void) addr;
    if ((p == NULL) || (ctx == NULL) ||
        (p->tot_len < (u16_t) (ip_hdr_len + sizeof(struct icmp_echo_hdr)))) {
        return 0;               /* 不是我们的包，继续往上递 */
    }
    iecho = (struct icmp_echo_hdr *) ((u8_t *) p->payload + ip_hdr_len);
    if ((iecho->type == ICMP_ER) && (iecho->id == APP_LWIP_ETH_PING_ID) &&
        (iecho->seqno == lwip_htons(ctx->seq_got))) {
        ctx->rtt_cyc = hpm_csr_get_core_mcycle() - ctx->t_send_cyc;
        ctx->got = true;
        pbuf_free(p);
        return 1;               /* 已消费 */
    }
    return 0;
}

static void app_lwip_eth_ping_delay(uint32_t ms)
{
#if NO_SYS
    uint32_t t0 = sys_now();
    while ((sys_now() - t0) < ms) {
        app_lwip_eth_poll();
    }
#else
    vTaskDelay((ms == 0U) ? 1U : ms);
#endif
}

int app_lwip_eth_ping(const char *dst_ip, uint32_t count, uint32_t interval_ms,
                      app_lwip_eth_ping_result_t *out)
{
    app_lwip_eth_ping_ctx_t ctx;
    struct pbuf *p;
    struct icmp_echo_hdr *iecho;
    uint32_t i;
    uint32_t cpu_hz = (uint32_t) clock_get_frequency(clock_cpu0);
    uint64_t prev_rtt = 0U;
    uint64_t jitter_sum = 0U;
    uint32_t jitter_n = 0U;
    uint32_t rtt_min = 0xFFFFFFFFU;
    uint32_t rtt_max = 0U;
    uint64_t rtt_sum = 0U;
    uint32_t recv = 0U;

    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
    if ((dst_ip == NULL) || (count == 0U)) {
        return -1;
    }
    memset(&ctx, 0, sizeof(ctx));
    if (!app_lwip_eth_parse_ip(dst_ip, &ctx.dst)) {
        return -1;
    }
    APP_LWIP_CORE_LOCK();
    ctx.pcb = raw_new(IP_PROTO_ICMP);
    APP_LWIP_CORE_UNLOCK();
    if (ctx.pcb == NULL) {
        return -2;
    }
    APP_LWIP_CORE_LOCK();
    raw_recv(ctx.pcb, app_lwip_eth_ping_recv, &ctx);
    raw_bind(ctx.pcb, netif_ip_addr4(&s_netif));
    APP_LWIP_CORE_UNLOCK();

    for (i = 0U; i < count; i++) {
        ctx.got = false;
        ctx.seq_got = (uint16_t) (i + 1U);
        p = pbuf_alloc(PBUF_IP,
                       (u16_t) (sizeof(struct icmp_echo_hdr) + APP_LWIP_ETH_PING_DATA_LEN),
                       PBUF_RAM);
        if (p == NULL) {
            break;
        }
        iecho = (struct icmp_echo_hdr *) p->payload;
        iecho->type = ICMP_ECHO;
        iecho->code = 0U;
        iecho->id = APP_LWIP_ETH_PING_ID;
        iecho->seqno = lwip_htons(ctx.seq_got);
        memset(((u8_t *) p->payload + sizeof(struct icmp_echo_hdr)), 0x5A,
               APP_LWIP_ETH_PING_DATA_LEN);
        iecho->chksum = 0U;
        iecho->chksum = inet_chksum(iecho, p->len);

        ctx.t_send_cyc = hpm_csr_get_core_mcycle();
        APP_LWIP_CORE_LOCK();
        (void) raw_sendto(ctx.pcb, p, &ctx.dst);
        APP_LWIP_CORE_UNLOCK();
        pbuf_free(p);

        /* 等回包：最多 500 ms（1 G 链路 + 调度抖动足够） */
        {
            uint32_t waited = 0U;
            while (!ctx.got && (waited < 500U)) {
                app_lwip_eth_ping_delay(1U);
                waited += 1U;
            }
        }
        if (ctx.got) {
            uint32_t us = (uint32_t) ((ctx.rtt_cyc * 1000000ULL) / (uint64_t) cpu_hz);
            recv++;
            rtt_sum += us;
            if (us < rtt_min) {
                rtt_min = us;
            }
            if (us > rtt_max) {
                rtt_max = us;
            }
            if (prev_rtt != 0U) {
                jitter_sum += (uint64_t) ((us > prev_rtt) ? (us - prev_rtt) : (prev_rtt - us));
                jitter_n++;
            }
            prev_rtt = us;
            printf("[ping] seq=%u %s: rtt=%lu us\r\n", (unsigned) i, dst_ip, (unsigned long) us);
        } else {
            printf("[ping] seq=%u %s: timeout\r\n", (unsigned) i, dst_ip);
        }
        if ((interval_ms != 0U) && ((i + 1U) < count)) {
            app_lwip_eth_ping_delay(interval_ms);
        }
    }

    APP_LWIP_CORE_LOCK();
    raw_remove(ctx.pcb);
    APP_LWIP_CORE_UNLOCK();

    if (out != NULL) {
        out->sent = count;
        out->recv = recv;
        out->lost = count - recv;
        out->rtt_min_us = (recv != 0U) ? rtt_min : 0U;
        out->rtt_avg_us = (recv != 0U) ? (uint32_t) (rtt_sum / recv) : 0U;
        out->rtt_max_us = rtt_max;
        out->rtt_jitter_us = (jitter_n != 0U) ? (uint32_t) (jitter_sum / jitter_n) : 0U;
    }
    printf("[ping] %lu/%lu received, rtt min/avg/max = %lu/%lu/%lu us, jitter(avg |d|) = %lu us\r\n",
           (unsigned long) recv, (unsigned long) count,
           (unsigned long) ((out != NULL) ? out->rtt_min_us : rtt_min),
           (unsigned long) ((out != NULL) ? out->rtt_avg_us : 0U),
           (unsigned long) ((out != NULL) ? out->rtt_max_us : rtt_max),
           (unsigned long) ((out != NULL) ? out->rtt_jitter_us : 0U));
    return (recv == count) ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/* FreeRTOS 任务                                                       */
/* ------------------------------------------------------------------ */
#if !NO_SYS
void app_lwip_eth_rx_task(void *arg)
{
    uint32_t idle_loops = 0U;

    (void) arg;
    for (;;) {
        int n = app_lwip_eth_rx_drain();

        if (s_ops->poll != NULL) {
            s_ops->poll(s_ctrl_idx);
        }
        /* lwiperf 的 UDP 客户端要主动轮询才会发包（RTOS 版没有主循环，放这里） */
        lwiperf_poll_udp_client();
        if (n == 0) {
            idle_loops++;
            (void) app_lwip_eth_link_poll();
            /* 没包时让出 CPU：连续 64 次空转后睡 1 tick；有包时连续排空不睡，
               否则 1 Gbit/s 下 16 个 Rx 缓冲不到 1 ms 就溢出。 */
            if (idle_loops >= 64U) {
                idle_loops = 0U;
                vTaskDelay(1);
            } else {
                taskYIELD();
            }
        } else {
            idle_loops = 0U;
        }
    }
}

void app_lwip_eth_mon_task(void *arg)
{
    uint32_t period_ms = (uint32_t) (uintptr_t) arg;

    if (period_ms == 0U) {
        period_ms = 5000U;
    }
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(period_ms));
        (void) app_lwip_eth_link_poll();
        app_lwip_eth_print();
        app_lwip_eth_print_stats();
    }
}
#endif /* !NO_SYS */