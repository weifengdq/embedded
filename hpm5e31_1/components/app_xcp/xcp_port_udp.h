/*
 * xcp_port_udp.h —— XCP on Ethernet(UDP) 传输层接口（XCPonEth）
 *
 * 与 xcp_port_can.c 对等：只负责"字节流 <-> UDP 数据报"的搬运，不做任何 XCP 语义。
 * 线上格式（ASAM XCP 1.5 XCPonEth，与 vectorgrp/XCPlite 的 xcptl_cfg.h 一致）：
 *
 *   一个 UDP 载荷 = 一条或多条 XCP 消息首尾相接，每条消息是
 *       LEN(2 B, 小端) | CTR(2 B, 小端) | XCP 协议层报文(LEN 字节) | 填充(0~3 B)
 *   - LEN  = 协议层报文长度，**不含** 4 B 传输层头（见第 35 章 U-35-9）；
 *   - CTR  = 发送方逐消息递增的包计数（回绕到 0），接收方用它发现 UDP 丢包；
 *   - 填充 = 把下一条消息的 LEN/CTR 对齐到 4 B；仅"后面还有消息"时才填，
 *            最后一条消息 LEN 精确、不填充（与 XCPlite 的 CRM 处理一致）；
 *   - 一个 UDP 数据报最多 1472 B 载荷（1500 MTU - 20 IPv4 - 8 UDP），故一包可装
 *     多条 DAQ DTO（"单包多 ODT"），实测见第 35 章表 35-13。
 *
 * 依赖：调用前必须已经 app_lwip_init() 起 lwIP（app_lwip 组件提供静态 IP 192.168.2.100）。
 */
#ifndef XCP_PORT_UDP_H
#define XCP_PORT_UDP_H

#include "xcp.h"

#include <stdint.h>
#include <stdbool.h>

#define XCP_UDP_DEFAULT_PORT    (5555u)     /* XCPonEth 标准端口（pyxcp 默认 5555） */
#define XCP_UDP_HDR_LEN         (4u)        /* LEN(2) + CTR(2) */
#define XCP_UDP_ALIGN           (4u)        /* 传输层消息 4 B 对齐 */
#define XCP_UDP_PAYLOAD_MAX     (1472u)     /* 1500 MTU - 20 IPv4 - 8 UDP */

/* 传输层统计（xcp_udp_get_stats 输出；书稿表 35-12 的数据来源） */
typedef struct {
    /* 接收侧 */
    uint32_t rx_datagrams;      /* 收到的 UDP 数据报数 */
    uint32_t rx_msgs;           /* 收到的 XCP 消息数（一个数据报可含多条） */
    uint32_t rx_bytes;          /* 收到的 UDP 载荷字节（含 4 B 头/填充） */
    uint32_t rx_framing_err;    /* LEN 越界/截断/未知消息 */
    uint32_t rx_ctr_gap;        /* CTR 间隙：主机 -> 从站 丢的消息数 */
    uint32_t rx_ctr_dup;        /* CTR 回退/重复次数 */
    /* 发送侧 */
    uint32_t tx_datagrams;      /* 发出的 UDP 数据报数 */
    uint32_t tx_msgs;           /* 发出的 XCP 消息数（DAQ DTO + 响应/事件） */
    uint32_t tx_bytes;          /* 发出的 UDP 载荷字节（含头/填充） */
    uint32_t tx_drop;           /* 因发送失败/无对端而丢的消息数 */
    uint32_t tx_err;            /* udp_sendto / pbuf_alloc 失败次数 */
    uint32_t flush_count;       /* 实际 flush 次数 */
    uint32_t max_msgs_per_pkt;  /* 单包最多装过多少条消息（多 ODT 能力实测） */
    uint32_t max_pkt_bytes;     /* 单包最大载荷字节 */
    /* 计数与对端 */
    uint16_t tx_ctr;            /* 下一个要用的发送 CTR */
    uint16_t rx_ctr_last;       /* 最近一次收到的 CTR */
    uint8_t  rx_ctr_valid;      /* 是否已收到过 CTR */
    uint8_t  peer_valid;        /* 是否学到主机地址（从最近一条命令的源地址学习） */
    uint8_t  peer_ip[4];        /* 主机 IPv4（小端字节序，与 ip4_addr_t 一致） */
    uint16_t peer_port;         /* 主机 UDP 源端口 */
} xcp_udp_stats_t;

/* 初始化并 bind UDP 端口；返回 0 成功，<0 失败（lwIP 未初始化/端口被占/pcb 耗尽） */
int  xcp_udp_init(uint16_t port);

/* 关闭 socket（可重复调用）；关闭后 xcp_udp_ops 的 send_dto 返回 -1 */
void xcp_udp_close(void);

bool xcp_udp_ready(void);

/* 立即把聚合缓冲里的消息发出去（命令响应后由接收回调调用；主循环由 xcp_poll 调用） */
void xcp_udp_flush(void);

void xcp_udp_get_stats(xcp_udp_stats_t *st);
void xcp_udp_reset_stats(void);

/* 交给 xcp_set_transport() 用 */
extern const xcp_transport_ops_t xcp_udp_ops;

#endif /* XCP_PORT_UDP_H */
