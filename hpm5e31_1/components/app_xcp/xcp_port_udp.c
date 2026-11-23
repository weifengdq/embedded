/*
 * xcp_port_udp.c —— XCP on Ethernet(UDP) 传输层（XCPonEth）
 *
 * 为什么这样设计（对照 vectorgrp/XCPlite 的 src/xcpethtl.c / xcptl_cfg.h）：
 *   1) XCPlite 的 "segment"（一个 UDP 数据报）可以装多条 transport message，
 *      每条 message 自带 LEN+CTR 头；DAQ 时一条 message = 一个 ODT（PID+数据）。
 *      本实现沿用同一模型，因此单包多 ODT 不需要协议层改动 —— 核心 DAQ 队列里
 *      本来就是"一帧一个 ODT"，这里只负责把连续几帧拼进同一个数据报。
 *   2) 4 B 对齐：XCPlite 的 XCPTL_PACKET_ALIGNMENT=4，DAQ 消息的 LEN 是**含填充**的
 *      （xcpethtl.c:891 注释 "Update the transport layer header (ctr+len)" 之后
 *      assert(l % 4 == 0)）；命令响应（CRM）不填充。本实现按同一规则做：
 *      非最后一条消息 LEN 补到 4 的倍数，最后一条保持精确长度。
 *   3) UDP 没有重传：CTR 只用于统计丢包，丢了就是丢了，由主机侧按 CTR 间隙和
 *      测量变量（g_tick_cnt 等）的连续性双重判定（第 35 章表 35-13）。
 *
 * 线程模型：裸机单线程（NO_SYS=1）。接收回调由 app_lwip_poll() 在 主循环上下文里
 * 触发，回调里直接跑 xcp_rx_cto() 并把响应 flush 出去（命令都是几十 us 级）；
 * DAQ 的采样仍在定时器中断里完成，发送由主循环 xcp_poll() 驱动。
 */
#include "xcp_port_udp.h"

#include "app_lwip.h"

#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* 内部状态                                                            */
/* ------------------------------------------------------------------ */

static struct udp_pcb *s_pcb;
static bool     s_ready;
static ip_addr_t s_peer;
static uint16_t s_peer_port;
static bool     s_peer_valid;

/* 发送聚合缓冲：直接按"线上格式"拼（LEN|CTR|payload|fill），flush 时一次发出 */
static uint8_t  s_tx[XCP_UDP_PAYLOAD_MAX];
static uint16_t s_tx_len;
static uint16_t s_last_off;         /* 最后一条消息的头偏移 */
static uint16_t s_last_len;         /* 最后一条消息的协议层长度 */
static uint16_t s_msg_cnt;

/* 接收缓冲：pbuf 可能是链式的，先拷平再解析 */
static uint8_t  s_rx[XCP_UDP_PAYLOAD_MAX];

static xcp_udp_stats_t s_st;

/* ------------------------------------------------------------------ */
/* 小工具                                                              */
/* ------------------------------------------------------------------ */

static uint16_t rd_u16le(const uint8_t *p)
{
    return (uint16_t) ((uint16_t) p[0] | ((uint16_t) p[1] << 8));
}

static void wr_u16le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t) (v & 0xFFu);
    p[1] = (uint8_t) (v >> 8);
}

static uint16_t align4(uint16_t n)
{
    return (uint16_t) ((n + (XCP_UDP_ALIGN - 1u)) & (uint16_t) ~(XCP_UDP_ALIGN - 1u));
}

/* 该偏移处是否是一个语法上合法的消息头（用于兼容"按 4 B 填充"的主机） */
static bool msg_header_ok(const uint8_t *b, uint16_t n, uint16_t off)
{
    uint16_t len;

    if ((uint32_t) off + XCP_UDP_HDR_LEN > n) {
        return false;
    }
    len = rd_u16le(b + off);
    if ((len == 0u) || (len > (uint16_t) XCP_MAX_CTO)) {
        return false;
    }
    return ((uint32_t) off + XCP_UDP_HDR_LEN + len <= n);
}

/* ------------------------------------------------------------------ */
/* 发送                                                                */
/* ------------------------------------------------------------------ */

static void tx_reset(void)
{
    s_tx_len = 0u;
    s_last_off = 0u;
    s_last_len = 0u;
    s_msg_cnt = 0u;
}

/* 真正调用 lwIP 发送；返回 0 成功 */
static int eth_flush_internal(void)
{
    struct pbuf *p;
    err_t err;

    if (!s_ready) {
        return -1;
    }
    if (s_tx_len == 0u) {
        return 0;
    }
    if (!s_peer_valid) {                    /* 主机还没发过命令：DAQ 不该跑到这里 */
        s_st.tx_drop = s_st.tx_drop + s_msg_cnt;
        tx_reset();
        return -1;
    }
    p = pbuf_alloc(PBUF_TRANSPORT, s_tx_len, PBUF_RAM);
    if (p == NULL) {
        s_st.tx_err++;
        s_st.tx_drop = s_st.tx_drop + s_msg_cnt;
        tx_reset();
        return -1;
    }
    if (pbuf_take(p, s_tx, s_tx_len) != ERR_OK) {
        pbuf_free(p);
        s_st.tx_err++;
        s_st.tx_drop = s_st.tx_drop + s_msg_cnt;
        tx_reset();
        return -1;
    }
    err = udp_sendto(s_pcb, p, &s_peer, s_peer_port);
    pbuf_free(p);

    if (err != ERR_OK) {
        s_st.tx_err++;
        s_st.tx_drop = s_st.tx_drop + s_msg_cnt;
        tx_reset();
        return -1;
    }
    s_st.tx_datagrams++;
    s_st.tx_msgs = s_st.tx_msgs + s_msg_cnt;
    s_st.tx_bytes = s_st.tx_bytes + s_tx_len;
    if (s_msg_cnt > s_st.max_msgs_per_pkt) {
        s_st.max_msgs_per_pkt = s_msg_cnt;
    }
    if (s_tx_len > s_st.max_pkt_bytes) {
        s_st.max_pkt_bytes = s_tx_len;
    }
    s_st.flush_count++;
    tx_reset();
    return 0;
}

/*
 * 追加一条消息到当前数据报。
 *   - payload[0] <= 0xFB 的是 DAQ DTO：非最后一条时要补 4 B 填充，LEN 含填充；
 *   - RES/ERR/EV/SERV 是控制报文：一般不填充（XCPlite 的 CRM 也不填充）。
 * 返回 0 = 已进入聚合缓冲，-1 = 失败（已计入 tx_drop）。
 */
static int eth_append_msg(const uint8_t *payload, uint16_t len)
{
    uint16_t need;
    uint16_t pad_prev = 0u;
    uint16_t hdr_off;

    if (!s_ready) {
        return -1;
    }
    if ((len == 0u) || (len > (uint16_t) XCP_MAX_DTO)) {
        s_st.tx_drop++;
        return -1;
    }
    if (s_msg_cnt > 0u) {
        pad_prev = (uint16_t) (align4(s_last_len) - s_last_len);
    }
    need = (uint16_t) (XCP_UDP_HDR_LEN + len);
    if ((uint32_t) s_tx_len + pad_prev + need > XCP_UDP_PAYLOAD_MAX) {
        /* 缓冲装不下：先把已有消息发走（此时上一条是最后一条，无需填充） */
        if (eth_flush_internal() != 0) {
            return -1;
        }
        pad_prev = 0u;
    }
    if ((s_msg_cnt > 0u) && (pad_prev > 0u)) {
        /* 把上一条补到 4 B 对齐，并把填充计入它的 LEN（pyxcp 按 LEN 顺序解析） */
        memset(&s_tx[s_tx_len], 0, pad_prev);
        s_tx_len = (uint16_t) (s_tx_len + pad_prev);
        wr_u16le(&s_tx[s_last_off], (uint16_t) (s_last_len + pad_prev));
    }
    hdr_off = s_tx_len;
    wr_u16le(&s_tx[hdr_off], len);                          /* LEN：最后一条精确长度 */
    wr_u16le(&s_tx[hdr_off + 2u], s_st.tx_ctr);             /* CTR：逐消息递增 */
    s_st.tx_ctr = (uint16_t) (s_st.tx_ctr + 1u);
    if (len > 0u) {
        memcpy(&s_tx[hdr_off + XCP_UDP_HDR_LEN], payload, len);
    }
    s_tx_len = (uint16_t) (s_tx_len + need);
    s_last_off = hdr_off;
    s_last_len = len;
    s_msg_cnt++;
    return 0;
}

static int udp_send_dto(const uint8_t *buf, uint16_t len)
{
    return eth_append_msg(buf, len);
}

static void udp_poll(void)
{
    (void) eth_flush_internal();
}

/* ------------------------------------------------------------------ */
/* 接收                                                                */
/* ------------------------------------------------------------------ */

static void udp_recv_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                        const ip_addr_t *addr, u16_t port)
{
    uint16_t n;
    uint16_t off = 0u;

    (void) arg;
    (void) pcb;

    if (p == NULL) {
        return;
    }
    if (p->tot_len > XCP_UDP_PAYLOAD_MAX) {     /* 超过本板 MTU 预算：直接丢 */
        s_st.rx_framing_err++;
        pbuf_free(p);
        return;
    }
    n = (uint16_t) pbuf_copy_partial(p, s_rx, (u16_t) p->tot_len, 0);
    if (n != p->tot_len) {
        s_st.rx_framing_err++;
        pbuf_free(p);
        return;
    }
    pbuf_free(p);

    s_st.rx_datagrams++;
    s_st.rx_bytes = s_st.rx_bytes + n;

    /* 从最近一条命令学习主机地址/端口（XCPonEth 从站按源地址回包） */
    if ((addr != NULL) && IP_IS_V4(addr)) {
        const ip4_addr_t *ip4 = ip_2_ip4(addr);
        s_peer = *addr;
        s_peer_port = port;
        s_peer_valid = true;
        s_st.peer_valid = 1u;
        s_st.peer_port = port;
        memcpy(s_st.peer_ip, &ip4->addr, 4);
    } else {
        return;                                 /* 本板只支持 IPv4 */
    }

    while ((uint32_t) off + XCP_UDP_HDR_LEN <= n) {
        uint16_t len = rd_u16le(&s_rx[off]);
        uint16_t ctr = rd_u16le(&s_rx[off + 2u]);
        uint16_t next;

        if ((len == 0u) || (len > (uint16_t) XCP_MAX_CTO) ||
            ((uint32_t) off + XCP_UDP_HDR_LEN + len > n)) {
            s_st.rx_framing_err++;
            break;
        }
        /* CTR 连续性：UDP 无重传，间隙 = 主机 -> 从站 丢了消息 */
        if (s_st.rx_ctr_valid != 0u) {
            uint16_t expect = (uint16_t) (s_st.rx_ctr_last + 1u);
            if (ctr != expect) {
                if ((int16_t) (ctr - expect) > 0) {
                    s_st.rx_ctr_gap = s_st.rx_ctr_gap + (uint16_t) (ctr - expect);
                } else {
                    s_st.rx_ctr_dup++;
                }
            }
        }
        s_st.rx_ctr_last = ctr;
        s_st.rx_ctr_valid = 1u;
        s_st.rx_msgs++;

        xcp_rx_cto(&s_rx[off + XCP_UDP_HDR_LEN], len);

        next = (uint16_t) (off + XCP_UDP_HDR_LEN + len);
        /* 兼容按 4 B 对齐填充的主机（XCPlite 风格）：下一位置不是合法头就跳过填充 */
        if ((next < n) && !msg_header_ok(s_rx, n, next)) {
            uint16_t alt = (uint16_t) (off + XCP_UDP_HDR_LEN + align4(len));
            if (msg_header_ok(s_rx, n, alt)) {
                next = alt;
            } else if ((uint32_t) (n - next) < XCP_UDP_ALIGN) {
                next = n;                       /* 尾部 0~3 B 填充 */
            }
        }
        off = next;
    }

    /* 命令响应立即发出（DAQ 聚合缓冲也一并带走） */
    (void) eth_flush_internal();
}

/* ------------------------------------------------------------------ */
/* 初始化 / 统计                                                       */
/* ------------------------------------------------------------------ */

int xcp_udp_init(uint16_t port)
{
    if (s_ready) {
        xcp_udp_close();
    }
    s_pcb = app_udp_socket(NULL, port);
    if (s_pcb == NULL) {
        return -1;
    }
    udp_recv(s_pcb, udp_recv_cb, NULL);

    memset(&s_st, 0, sizeof(s_st));
    tx_reset();
    s_peer_valid = false;
    memset(&s_peer, 0, sizeof(s_peer));
    s_peer_port = 0u;
    s_ready = true;
    return 0;
}

void xcp_udp_close(void)
{
    if (s_pcb != NULL) {
        udp_remove(s_pcb);
        s_pcb = NULL;
    }
    s_ready = false;
    s_peer_valid = false;
    tx_reset();
}

bool xcp_udp_ready(void)
{
    return s_ready;
}

void xcp_udp_flush(void)
{
    (void) eth_flush_internal();
}

void xcp_udp_get_stats(xcp_udp_stats_t *st)
{
    if (st != NULL) {
        *st = s_st;
    }
}

void xcp_udp_reset_stats(void)
{
    uint16_t tx_ctr = s_st.tx_ctr;
    uint16_t rx_ctr = s_st.rx_ctr_last;
    uint8_t rx_valid = s_st.rx_ctr_valid;
    uint8_t peer_valid = s_st.peer_valid;
    uint8_t peer_ip[4];
    uint16_t peer_port = s_st.peer_port;

    memcpy(peer_ip, s_st.peer_ip, sizeof(peer_ip));
    memset(&s_st, 0, sizeof(s_st));
    s_st.tx_ctr = tx_ctr;                       /* CTR 不能清零：主机按它判丢包 */
    s_st.rx_ctr_last = rx_ctr;
    s_st.rx_ctr_valid = rx_valid;
    s_st.peer_valid = peer_valid;
    s_st.peer_port = peer_port;
    memcpy(s_st.peer_ip, peer_ip, sizeof(peer_ip));
}

const xcp_transport_ops_t xcp_udp_ops = {
    .name = "ETH-UDP",
    .poll = udp_poll,
    .send_dto = udp_send_dto,
};
