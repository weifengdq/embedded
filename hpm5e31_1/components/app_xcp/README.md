# components/app_xcp —— XCP 从站组件（CAN / UDP 传输无关）

ASAM MCD-1 XCP 协议层 + DAQ 引擎的最小可移植实现，为 hpm5e31_1（HPM5E31）写成，
不依赖 SDK 之外的东西；传输层用回调注册，已有 CAN（`xcp_port_can.c`）与
XCPonEth/UDP（`xcp_port_udp.c`）两份。

## 文件

| 文件 | 内容 |
|---|---|
| `xcp.h` | 配置结构 / API / 协议常量（命令码、错误码、DAQ 模式位、传输层 ops） |
| `xcp_core.c` | 协议层：命令解析、内存访问（区域白名单）、标定页、DAQ 列表/ODT、事件采样、统计 |
| `xcp_port_can.c` / `.h` | CAN 传输层：MCAN 经典帧 500 kbit/s，CMD 0x555 / RES+DAQ 0x556，MAX_DTO=8 |
| `xcp_port_udp.c` / `.h` | XCPonEth 传输层：lwIP UDP 5555、4 B 头（LEN/CTR，小端）、单包多 ODT、CTR/帧错误统计 |

## 接入（3 步）

CAN：

~~~c
#include "xcp.h"
#include "xcp_port_can.h"

xcp_set_transport(&xcp_can_ops);                       /* 1) 选传输层 */
xcp_can_init(BOARD_CAN0_BASE, 0x555u, 0x556u, false);  /* 2) 起 CAN（含 AHB 消息缓冲区） */
xcp_init(&cfg);                                        /* 3) 协议层初始化 */

/* 事件中断里 */  xcp_event(0u, now_us());   /* EVT0 = 1 kHz */
/* 主循环里   */  xcp_poll();                /* 先发 DAQ 帧，再收 CTO */
~~~

XCPonEth（UDP）：

~~~c
#include "xcp.h"
#include "xcp_port_udp.h"
#include "app_lwip.h"

app_lwip_init(NULL);                    /* 1) 起 ENET0 RGMII + YT8011AN，静态 IP 192.168.2.100 */
xcp_udp_init(XCP_UDP_DEFAULT_PORT);     /* 2) bind UDP 5555，注册 lwIP recv 回调 */
xcp_set_transport(&xcp_udp_ops);
xcp_init(&cfg);                         /* 3) 同一份协议层 */

/* 主循环：app_lwip_poll() 驱动收包（回调里跑 CTO，响应立即回），xcp_poll() 发 DAQ */
while (1) { app_lwip_poll(); xcp_poll(); }
~~~

`xcp_cfg_t` 必填三项：`regions`（可读写内存区域白名单）、`events`（事件通道表）、`now_us`（1 µs 单调时基）。
可选项：`id_string`（GET_ID 返回值）、`seed`/`seed_len`（种子保护，0 = 不保护）、`on_connect/on_disconnect`。

## XCPonEth 传输层要点

线上格式（ASAM XCP 1.5，与 vectorgrp/XCPlite、pyxcp 一致）：

    UDP 载荷 = 一条或多条消息首尾相接：
        LEN(2 B, 小端) | CTR(2 B, 小端) | 协议层报文(LEN B) | 填充(0~3 B)

- **LEN 不含 4 B 头**；非最后一条消息按 4 B 对齐（填充计入它的 LEN），最后一条精确长度；
- **CTR 逐消息 +1**，接收侧按连续性统计丢包（UDP 没有重传，丢包不回填）；
- 单包载荷上限 1472 B（1500 MTU - 20 IPv4 - 8 UDP）；1472 B 可装 92 条 9 B ODT 或 122 条 5 B ODT；
- 从站按最近一条命令的源地址回包；只支持 IPv4；
- 统计：`xcp_udp_get_stats()` 给出 rx/tx 报文/消息/字节、CTR 间隙、帧错误、发送失败、单包最大消息数。

**编译期上限**：CAN 构型默认 `XCP_MAX_CTO=8 / XCP_MAX_DTO=8`；XCPonEth 例程用
`-DXCP_MAX_DTO=256 -DXCP_MAX_CTO=255` 放大（`samples/app/xcp_eth/CMakeLists.txt`）。
其余：`XCP_MAX_DAQ_LISTS=4`、`XCP_MAX_ODT_PER_LIST=4`、`XCP_MAX_ENTRY_PER_ODT=8`、
`XCP_MAX_EVENTS=4`、`XCP_DAQ_TX_QUEUE_LEN=16`（eth 例程放大到 8/16/32）。

## 内存区域白名单（必配）

XCP 按**绝对地址**读写内存，配错就是总线错误/死机。本板实测的合法区域（来自链接脚本）：

| 区域 | 范围 | 权限 | 说明 |
|---|---|---|---|
| AXI SRAM | 0x01200000 ~ 0x01240000 | R/W | `.data`/`.bss`（所有标定量/观测量都在这里） |
| AHB SRAM | 0xF0200000 ~ 0xF0204000 | R/W | 需要放这里的变量 |
| XIP Flash | 0x80000000 ~ 0x80100000 | R | `.text`/`.rodata`（GET_ID 字符串、常量） |

越界访问返回 ERR_WRITE_PROTECTED(0x23) 并计数 `mem_denied`，不会真的去访存。

## 能力与限制

- 支持：CONNECT/DISCONNECT/GET_STATUS/SYNCH/GET_COMM_MODE_INFO/GET_ID/SET_REQUEST/GET_SEED/UNLOCK/
  SET_MTA/UPLOAD/SHORT_UPLOAD/DOWNLOAD/MODIFY_BITS、标定页命令（单段单页 RAM）、
  DAQ 全套（动态列表、预分频、时间戳、过载 EVENT）；
- 不支持：PGM（Flash 编程）、STIM、块传输、CHECKSUM、PID_OFF、PACKED_MODE。不支持的命令返回 ERR_CMD_UNKNOWN(0x20)。

## 踩过的坑（务必先看）

1. **HPM5E31 的 MCAN 消息 RAM 在 AHB SRAM**：不调 `mcan_set_msg_buf_attr()` 时 `mcan_init()` 返回 2
   （status_invalid_argument），CAN 完全不通。本组件在 `xcp_can_init()` 里为从站节点注册了
   一份 `ATTR_PLACE_AT(".ahb_sram")` 的 640 word 缓冲区；**第二个 MCAN 实例要自己再注册一份**。
2. **CAN 帧被填充到 DLC=8**：固定长度命令必须用 `len < 期望` 判断，不能用 `!=`（详见第 34 章坑 2）。
3. **DAQ 采样与发送分离**：`xcp_event()` 只在中断里打包入队，`xcp_poll()` 才发；队列满计 `daq_dropped`，
   TX FIFO 满计 `daq_overflow` 并上报一次 EVENT 0x06（UDP 侧 `tx_drop/tx_err` 单独统计）。
4. **`ALLOC_ODT_ENTRY` 是 6 字节 CRO**（cmd, mode, list_lo, list_hi, odt, count）。
5. **XCPonEth 的 LEN 不含头、4 B 对齐填充要计入非最后一条的 LEN**：详见第 34 章"坑 9/10"，
   实现对照 `xcp_port_udp.c:eth_append_msg()` 与 vectorgrp/XCPlite `xcpethtl.c`。
6. **UDP 回调在主循环上下文**（`app_lwip_poll()` 里）：不要在回调里 printf/阻塞；DAQ 打包仍在中中断，
   发送在主循环 `xcp_poll()`。

## 实测

- CAN：`samples/app/xcp_can_daq` 板内自证（`logs/20261030-172706-xcp_can_daq.log`）41 项检查全 PASS，
  1 kHz 事件率 999.0 Hz、3097 DAQ 帧/s、丢帧 0、每事件打包 3.93 µs；
- XCPonEth：`samples/app/xcp_eth`（`logs/20261102-155848-xcp_eth.log` + host 日志）1 kHz/4 ODT 990 pkt/s、
  3960 msg/s；10 kHz/8 ODT 9724 pkt/s、77791 msg/s、6.85 Mbit/s、帧错误 0；板端 drop/overload 全 0。
  细节见书稿第 34 章"XCP on UDP"一节与表 34-14/34-15。
