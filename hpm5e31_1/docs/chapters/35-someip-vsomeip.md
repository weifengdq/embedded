| U-35-10 | 上位机侧 SOME/IP-TP 多段接收只收到末段 | **部分闭环（2026-11-02）**：主机侧 4096 B 请求实测只收到段 2/3（tcpdump：1412 B offset=1392 more=1、1332 B offset=2784 more=0），段 1（offset=0）未上线；主机 `_collect_tp()` 报「分段不连续 offset=1392 expected=0」。复测前后 `rx_crc_errors` 恒为 2261，排除"FCS 踩坏被网卡丢"；板内回环 3 项 PASS。 | 下一步：在 `app_someip_tp_send()` 记录每段 `cfg.send` 返回值与 ENET TX 描述符状态（TDES 的 OWN/ERR 位），分清是 lwIP 未收下第一段还是 ENET 没发出去。证据：`logs/20261102-121620-host-r5-someip-all.log` + `.pcap` |
| ~~U-35-11~~ | 主机侧 RPC RTT / 事件周期·抖动·丢包的定量数字 | **已解决**：见上表（2026-10-30 17:45 实测，`logs/20261030-174453-someip_rpc.log` + host 日志） | — |
# 第 35 章 SOME/IP 面向服务通信（lwIP 服务端 + vsomeip 上位机）

## 35.1 文档信息

| 项 | 内容 |
|---|---|
| 章号 / 版本 | 第 35 章 / v1.0（第二阶段） |
| 适用芯片 | HPM5E31IPB1（HPM5E00 系列，RV32 480 MHz，ENET RGMII + YT8011AN 1000BASE-T1） |
| 对应例程 | `samples/app/someip_rpc/`、`samples/app/someip_event/`、`samples/app/someip_tp/` |
| 对应组件 | `components/app_someip/`（协议层 + lwIP 传输 + 自证测试） |
| 对应上位机 | `host/someip/someip_client.py`（自写 SD/RPC/事件/TP）、`host/someip/vsomeip_client/`（vsomeip 3.7.6） |
| 实测日期 | 2026-10-30 |
| 可信度 | 板端协议层 / SD / RPC / 事件 / TP 均上板实测；vsomeip 互操作见"待确认项与风险" |
| 前置章节 | 第 12 章（YT8011AN/RGMII）、第 13 章（lwIP/iperf）、第 03 章（letter-shell） |

## 35.2 本章导览

SOME/IP（Scalable service-Oriented MiddlewarE over IP）是 AUTOSAR 定义的**车载面向服务通信中间件**：
服务端把功能暴露成"服务（service）+ 方法（method）+ 事件（event）+ 字段（field）"，
客户端通过 **SOME/IP-SD（Service Discovery）** 找到服务，再用 **REQUEST/RESPONSE** 调用方法、
用 **NOTIFICATION** 接收事件。它跑在 UDP/TCP 上，报文是紧凑的自描述头 + 大端载荷，适合 100 Mbit/s 车载以太网。

本章目标（读完能做什么）：

1. 写出一个**不依赖任何中间件**的 SOME/IP 服务端（`components/app_someip`，纯 C、无 malloc、约 1500 行）；
2. 说清 **16 B 报文头每个字段**的语义与最容易错的地方（尤其 `Length` 到底算哪些字节）；
3. 说清 **SOME/IP-SD** 的 Entry/Option 编码、TTL 与周期重发、FindService 与 SubscribeEventgroup 的应答规则；
4. 用 **SOME/IP-TP** 把 4 KB 报文拆成 1392 B 的段并重组；
5. 用 Python 自写客户端逐字节核对报文，再用 **vsomeip** 这个工业级中间件验证互操作；
6. 拿到真实数字：SD 发现时间、RPC RTT、事件周期抖动/丢包/单向时延、4 KB 分段往返。

前置：第 13 章（lwIP 裸机、静态 IP、UDP 收发）与第 12 章（PHY 链路）必须已经跑通 ——
本章的 `ping 192.168.2.100` 是第一道门槛。

## 35.3 概述与典型应用

一句话定位：**SOME/IP = 车载以太网上的"远程过程调用 + 发布订阅"，带自描述报文头和自动服务发现。**

典型场景：

- **域控制器内**：智驾域控（SoC）向 MCU 发布感知结果/车辆信号，MCU 回传执行状态；
- **区域控制器（ZCU）**：车身域把车窗、车灯、座椅抽象成服务，HMI 按需调用；
- **OTA/诊断网关**：诊断仪通过 DoIP 进车，内部转 SOME/IP 调服务；
- 相对 CAN 信号：SOME/IP **按需调用**（面向服务），不再周期性广播所有信号（面向信号）。

关键特性表：

| 特性 | SOME/IP | 说明（本章实现情况） |
|---|---|---|
| 传输 | UDP（推荐）/ TCP | 本章只用 UDP（SD 必须 UDP） |
| 报文头 | 16 B，全大端 | 已实现逐字段编解码与校验 |
| 服务发现 | SOME/IP-SD，UDP 30490，组播 224.244.224.245 | 已实现 Offer/StopOffer/Find/Subscribe+Ack |
| 方法调用 | REQUEST/RESPONSE/ERROR + Return Code | 已实现同步分发（回调式） |
| 事件 | NOTIFICATION（周期或变化触发） | 已实现 3 路周期事件（100/10/500 ms） |
| 字段 | getter/setter/notifier（用方法号派生） | 已实现 temperature getter(0x0004)/setter(0x0005) |
| 大包 | SOME/IP-TP 分段（>1400 B） | 已实现 4 KB 双向分段/重组 |
| 序列化 | 大端、4 B 对齐、字符串带长度 | 与 vsomeip serializer 对齐（见"坑"） |

## 35.4 工作原理

### 35.4.1 报文头逐字段

SOME/IP 报文 = **16 B 固定头 + 载荷**，所有多字节字段大端（网络字节序）。如图 35-1。

![图 35-1 SOME/IP 16 B 报文头逐字段布局](figures/35/fig-35-1-someip-header.png)

表 35-1 报文头字段定义（AUTOSAR PRS SOME/IP）：

| 偏移 | 字段 | 宽度 | 说明 | 本章代码 |
|---|---|---|---|---|
| 0 | Service ID | u16 | 服务号，本章 0x1234 | `someip_msg_t.service_id` |
| 2 | Method ID | u16 | 方法/事件号；事件用 0x8000 段 | `someip_msg_t.method_id` |
| 4 | Length | u32 | **从 Request ID 到报文末尾的字节数** = 8 + 载荷长度 | `SOMEIP_LENGTH_MIN` |
| 8 | Client ID | u16 | 客户端标识（服务端响应要回显） | `someip_msg_t.client_id` |
| 10 | Session ID | u16 | 会话号，每发一个请求 +1（响应回显） | `someip_msg_t.session_id` |
| 12 | Protocol Version | u8 | 固定 0x01 | `SOMEIP_PROTOCOL_VERSION` |
| 13 | Interface Version | u8 | 服务接口版本，板端 0x01，不匹配回 0x08 | `cfg.interface_version` |
| 14 | Message Type | u8 | 0x00/0x01/0x02/0x80/0x81，bit5=0x20 表 TP | 表 35-2 |
| 15 | Return Code | u8 | 0x00 成功；0x02 未知服务；0x03 未知方法… | 表 35-2 |

表 35-2 Message Type 与 Return Code 取值（本章实现覆盖的部分）：

| Message Type | 值 | 方向 | Return Code | 值 |
|---|---|---|---|---|
| REQUEST | 0x00 | 客户端 → 服务端 | E_OK | 0x00 |
| REQUEST_NO_RETURN | 0x01 | 客户端 → 服务端（不回包） | E_NOT_OK | 0x01 |
| NOTIFICATION | 0x02 | 服务端 → 客户端（事件/SD） | E_UNKNOWN_SERVICE | 0x02 |
| REQUEST_TP | 0x20 | 带 TP 分段的请求 | E_UNKNOWN_METHOD | 0x03 |
| RESPONSE | 0x80 | 服务端 → 客户端 | E_NOT_READY | 0x04 |
| ERROR | 0x81 | 服务端 → 客户端（失败） | E_WRONG_PROTOCOL_VERSION | 0x07 |
| RESPONSE_TP | 0xA0 | 带 TP 分段的响应 | E_WRONG_INTERFACE_VERSION | 0x08 |
| ERROR_TP | 0xA1 | 带 TP 分段的错误 | E_MALFORMED_MESSAGE | 0x09 |

`Length` 是最容易错的字段：它**从 Request ID 开始算**，也就是 8（Request ID 4 + PV/IV/MT/RC 各 1）
**加**载荷长度，而**不含** Message ID 与 Length 自身。板端解码时按"实际收到的字节数必须 ≥ Length+8"校验，
上位机如果按"总长度"填就会立刻被拒（本章 `selftest` 的 T2 用例专门验证这一点）。

关键代码（`components/app_someip/src/app_someip.c`）：

```c
uint16_t app_someip_encode_header(uint8_t *buf, uint16_t cap, uint16_t service_id, uint16_t method_id,
                                  uint16_t client_id, uint16_t session_id, uint8_t interface_version,
                                  uint8_t message_type, uint8_t return_code, uint16_t payload_len)
{
    someip_put_u16(&buf[0], service_id);
    someip_put_u16(&buf[2], method_id);
    /* Length：从 Request ID 起到报文末尾的字节数 = 8 + payload */
    someip_put_u32(&buf[4], (uint32_t)SOMEIP_LENGTH_MIN + (uint32_t)payload_len);
    someip_put_u16(&buf[8], client_id);
    someip_put_u16(&buf[10], session_id);
    buf[12] = SOMEIP_PROTOCOL_VERSION;     /* 0x01 */
    buf[13] = interface_version;
    buf[14] = message_type;
    buf[15] = return_code;
    return SOMEIP_HEADER_LEN;              /* 16 */
}
```

### 35.4.2 SOME/IP-SD 服务发现

SD 报文本身就是一个 SOME/IP 报文：`Service=0xFFFF / Method=0x8100 / MessageType=0x02 / Client=0x0000`，
固定发到 **UDP 30490**，服务端把 Offer 发到组播 **224.244.224.245:30490**（本章沿用 AUTOSAR 默认组播组）。

SD 载荷结构（大端）：

```text
Flags(1) | Reserved(3) | Entries-Length(4) | Entry × N (每个 16 B) | Options-Length(4) | Option × M
```

表 35-3 SD Entry 的 16 B 布局：

| 偏移 | 字段 | 宽度 | Service Entry | EventGroup Entry |
|---|---|---|---|---|
| 0 | Type | 1 | 0x00 Find / 0x01 Offer | 0x06 Subscribe / 0x07 Ack / 0x0E Nack |
| 1 | Index 1st Option Run | 1 | options[] 起始下标 | 同 |
| 2 | Index 2nd Option Run | 1 | 第二段 run 下标 | 同 |
| 3 | Number of Options | 1 | 低 4 位=第 1 段个数，高 4 位=第 2 段 | 同 |
| 4 | Service ID | 2 | 0x1234 | 0x1234 |
| 6 | Instance ID | 2 | 0x0001 | 0x0001 |
| 8 | Major Version | 1 | 0x01 | 0x01 |
| 9 | TTL | 3 | 秒；0 = StopOffer | 秒；0 = StopSubscribe |
| 12 | 尾部 4 B | 4 | Minor Version（u32） | Reserved(12b)+Counter(4b) |
| 14 | EventGroup ID | 2 | — | 0x0001 |

表 35-4 IPv4 Endpoint Option（0x04）——**Length 语义是坑**：

| 偏移 | 字段 | 宽度 | 值 |
|---|---|---|---|
| 0 | Length | 2 | 0x0009（**不含 Length 自身与 Type**，故整个 option 占 3+9=12 B） |
| 2 | Type | 1 | 0x04 IPv4 Endpoint（0x24 SD Endpoint / 0x14 Multicast 同构） |
| 3 | Reserved | 1 | 0x00 |
| 4 | IPv4 Address | 4 | 192.168.2.100 |
| 8 | Reserved | 1 | 0x00 |
| 9 | L4 Protocol | 1 | 0x11 UDP / 0x06 TCP |
| 10 | Port | 2 | 30501 |

SD 状态机与时序如图 35-2：服务端**周期重复 Offer**（间隔 = TTL/2，本章 TTL=3 s → 1.5 s），
收到 FindService 立刻单播回 Offer，收到 SubscribeEventgroup 先登记订阅再回 Ack。

![图 35-2 SOME/IP-SD 与服务/事件交互时序（实测时间见 §上板实测）](figures/35/fig-35-2-sd-sequence.png)

### 35.4.3 事件订阅

事件是服务端主动发的 NOTIFICATION：`MethodID = 事件号（0x8000 段）`，**只发给订阅者的事件端点**。
订阅流程（严格按规范）：

1. 客户端发 `SubscribeEventgroup`（Entry 里带 eventgroup + TTL，option run 里带自己的 **IPv4 endpoint**，
   即"我在这里收事件"）；这个 SD 报文发到服务端的 **SD 端口 30490**；
2. 服务端登记（ip, port, eventgroup, TTL），回 `SubscribeEventgroupAck`（发给订阅者的 **SD 端口**，即报文源端口）；
3. 服务端按事件周期把 NOTIFICATION 发到订阅者的事件端点；
4. 订阅者每 **TTL/2** 重发一次 Subscribe 续约；服务端在 `app_someip_poll()` 里检查 TTL，过期自动摘除；
5. 退订 = 发 TTL=0 的 Subscribe（StopSubscribeEventgroup）。

本章实现里"没有订阅者就不发包"（`send_event()` 先查订阅表），这也是"订阅前后带宽对比"实验的基础。

### 35.4.4 SOME/IP-TP 大包分段

UDP 单帧 MTU 1500 → 一个 SOME/IP 报文载荷上限约 1400 B。超过就用 SOME/IP-TP：Message Type 置 **0x20**，
16 B 头后插入 4 B TP 头，结构如图 35-3。

![图 35-3 4096 B 逻辑报文 → 3 个 SOME/IP-TP 段](figures/35/fig-35-3-tp-segment.png)

TP 头（大端 u32）：`Offset(bit31..4，单位 16 B) | Reserved(3b) | More(1b)`。

- 非末段数据长度必须是 **16 的倍数**（本章段长 1392 = 87 × 16）；
- 首段 Offset=0，末段 More=0；
- 同一逻辑报文的所有段，Service/Method/Client/Session 必须一致（板端重组时校验，不一致整包丢弃）。

表 35-5 TP 参数：段长 1392 B，4 KB 逻辑报文 = 1392+1392+1312 = 3 段；UDP 载荷最大 16+4+1392 = 1412 B（不触发 IP 分片）。

### 35.4.5 板端实现架构

```text
components/app_someip/
  inc/app_someip.h          协议对象、方法/事件注册、统计
  inc/app_someip_wire.h     大端读写 + CRC32（内部）
  inc/app_someip_sd.h       SD Entry/Option 编解码与服务端状态机
  inc/app_someip_tp.h       TP 头 + 重组缓冲
  inc/app_someip_net.h      lwIP UDP 传输（SD 30490 + 服务 30501 两个 socket）
  src/app_someip.c          报文头编解码 + 方法分发 + 事件发送 + 订阅表
  src/app_someip_sd.c       Offer/Find/Subscribe/Ack 编码与解析
  src/app_someip_tp.c       分段发送 / 按序重组
  src/app_someip_net.c      ENET+YT8011 初始化、UDP 收发、1 ms 时基
  src/app_someip_selftest.c 7 项协议层自证（无上位机）
```

**网络底座切换记录**：本章开发初期 `components/app_lwip`（tp-infra 共享底座）尚未合入，
所以 `app_someip_net.c` 自带了一套"ENET + YT8011AN + lwIP 静态 IP"初始化（与 `samples/enet/enet_lwip_iperf` 相同，
约 260 行）。底座合入后已切换：`app_lwip_init() / app_lwip_poll() / app_udp_socket() / app_lwip_now_ms()`，
同文件降到约 130 行，**协议层与三个例程的 main.c 一行都没改**——因为协议层只依赖 `send()` 与 `now_ms()` 两个回调。
这也说明了后加的例程如何复用团队底座：

```cmake
set(CONFIG_LWIP 1)                      # 必须在 find_package 之前
set(APP_USE_ENET_PORT_COUNT 1)
set(APP_USE_ENET_ITF_RGMII 1)
find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
project(hpm5e31_1_someip_rpc)
include(${REPO_ROOT}/components/app_lwip/app_lwip.cmake)   # 一行接入底座
```

数据通路（裸机单线程，全部在 `app_lwip_poll()` 驱动的 UDP 回调里）：

```text
ENET DMA 收包 -> lwIP udp_input -> someip_udp_rx() -> app_someip_on_rx()
   ├─ SD(0xFFFF/0x8100) -> app_someip_sd_on_rx() -> [Offer|Ack] 回包
   ├─ TP(0x20)         -> app_someip_tp_feed() -> 凑齐后按原类型重新分发
   └─ REQUEST          -> dispatch_request() -> 方法回调 -> RESPONSE/ERROR
主循环 app_someip_poll(): 周期 Offer、订阅 TTL 过期、周期事件 NOTIFICATION
```
## 35.5 关键寄存器与驱动 API 对照表

SOME/IP 是应用层协议，直接"碰寄存器"的不是它，而是它下面的 ENET DMA / lwIP / 系统时基。
下表把**协议字段**与**真正干活的硬件/驱动/代码位置**对上，排错时按这条链看：

表 35-6 协议字段 ↔ 硬件/驱动 ↔ 代码位置（应用章版本）：

| 协议字段 / 行为 | 硬件资源 | 驱动 / 中间件 API | 本章代码位置 |
|---|---|---|---|
| 以太网帧收发 | ENET0 RGMII（DS §5，RM ENET 章） | `enet_init()`、`enet_lwip_output()`、`ethernetif_input()` | `app_someip_net.c` |
| 链路/速率（1000BASE-T1） | YT8011AN MDIO（PHY addr 1，PHYSR=0x8400） | `yt8011_get_status()`、`yt8011_wait_link_up()` | `app_someip_net.c` |
| IP/端口复用 | lwIP `udp_pcb` | `udp_new/udp_bind/udp_recv/udp_sendto` | `app_someip_net.c:open_udp()` |
| 服务实例端点（30501） | — | `udp_bind(pcb, NULL, 30501)` | `app_someip_net_open()` |
| SD 端点（30490） | — | `udp_bind(pcb, NULL, 30490)` | 同上 |
| 1 ms 时基（TTL/事件周期） | MCHTMR / board timer | `board_timer_create(1, sys_timer_callback)`、`sys_now()` | `app_someip_net.c`、`app_someip_net_now_ms()` |
| 报文头 Length/字段 | ENET TX 描述符 TBS1（实际长度） | `udp_sendto()` 按载荷自动填 IP/UDP 长度 | `app_someip.c:encode_header` |
| SD 组播 Offer | ENET 组播 MAC 映射（01:00:5E:…） | `udp_sendto()` 到 224.244.224.245 | `app_someip_sd.c:sd_send()` |
| 事件周期抖动 | CPU 主频 480 MHz / XIP | `app_someip_poll()` 的 `now_ms` 比较 | `app_someip.c:app_someip_poll` |
| 4 KB 重组缓冲 | AXI SRAM（`.bss`，4 KB） | 静态分配 `someip_tp_reasm_t` | `app_someip_tp.h` |
| 板端微秒时基（单向时延） | CSR `mcycle` | `hpm_csr_get_core_mcycle()` | `someip_rpc/src/main.c:board_now_us` |
| 退订/TTL 过期 | — | `app_someip_poll()` 每轮检查 | `app_someip.c` |

## 35.6 例程逐条讲解

三个例程共用同一份组件，区别只在注册的方法/事件和自测重点。

表 35-7 例程总表：

| 例程 | 覆盖特性 | 关键配置 | 实测现象（串口自证） | 实测日志 |
|---|---|---|---|---|
| `samples/app/someip_rpc` | SD Offer/Find、方法 RPC（add/get_version/set_led）、temperature 字段 getter/setter、事件注册 | 服务 0x1234.0001，SD 30490，服务 30501，TTL 3 s，事件组 0x0001 | `selftest` 7/7 PASS；`run` 8/8 PASS；主机 RPC 全部 PASS（表 35-8） | `logs/20261030-172454-someip_rpc.log` |
| `samples/app/someip_event` | SubscribeEventgroup/Ack、3 路周期事件（100/10/500 ms）、订阅表 TTL | 同上 + 事件 0x8001/0x8002/0x8004 | `run`（订阅+1 s 模拟）PASS；主机收到 counter/adc/temp 周期事件（表 35-9） | `logs/20261030-172737-someip_event.log` |
| `samples/app/someip_tp` | SOME/IP-TP 分段/重组（4 KB，双向） | 段长 1392 B，逻辑报文上限 4 KB | `run` 3 项 PASS（分段/重组/乱序拒绝）；主机 4 KB 双向 CRC 一致（表 35-10） | `logs/20261030-173049-someip_tp.log` |

### 35.6.1 someip_rpc：服务发现 + 方法 RPC

接线：板 ENET（YT8011AN，1000BASE-T1）↔ 车载以太网转换盒 ↔ 主机 `enp6s0`；板 `192.168.2.100/24`，主机 `192.168.2.1/24`。

命令：

```bash
# 板端：编译 + 烧写 + 串口自证 + 持锁跑上位机（--post）
cd hpm5e31_1
bash scripts/hwrun.sh app/someip_rpc --secs 8 --send "selftest;run;stat" \
     --post "timeout 80 bash host/someip/run_client.sh core" --lock-wait 1800
# 上位机单独跑（板端已烧写在跑时）
python3 host/someip/someip_client.py --board 192.168.2.100 core
```

方法表（`someip_rpc/src/main.c` 注册的方法）：`add`（0x0001）、`get_version`（0x0002）、`set_led`（0x0003）、
`temperature` getter（0x0004）/setter（0x0005）、`get_time`（0x0006，返回板端微秒时基，供上位机做 NTP 式时延估计）。

关键代码（方法分发，注意响应必须回显 Client ID/Session ID）：

```c
static void dispatch_request(app_someip_t *s, const uint8_t src_ip[4], uint16_t src_port,
                             const someip_msg_t *m, const uint8_t *payload, uint16_t plen, bool want_response)
{
    if (m->service_id != s->cfg.service_id) {            /* 0x02 E_UNKNOWN_SERVICE */
        s->stats.err_unknown_service++;
        if (want_response) { send_error(s, src_ip, src_port, m, SOMEIP_RC_UNKNOWN_SERVICE); }
        return;
    }
    if (m->interface_version != s->cfg.interface_version) {  /* 0x08 */
        if (want_response) { send_error(s, src_ip, src_port, m, SOMEIP_RC_WRONG_INTERFACE_VERSION); }
        return;
    }
    someip_method_entry_t *e = find_method(s, m->method_id);
    if (e == NULL) {                                     /* 0x03 E_UNKNOWN_METHOD */
        s->stats.err_unknown_method++;
        if (want_response) { send_error(s, src_ip, src_port, m, SOMEIP_RC_UNKNOWN_METHOD); }
        return;
    }
    someip_msg_t req = *m;
    req.payload = payload; req.payload_len = plen;
    uint8_t *reply = &s->tx_buf[SOMEIP_HEADER_LEN];
    uint16_t reply_len = 0;
    uint8_t rc = e->fn(s, &req, reply, SOMEIP_MAX_PAYLOAD, &reply_len, e->user);
    if (!want_response) { return; }                      /* REQUEST_NO_RETURN */
    if (rc != SOMEIP_RC_OK) { send_error(s, src_ip, src_port, m, rc); return; }
    (void)app_someip_send_msg(s, src_ip, src_port, m->method_id, m->client_id, m->session_id,
                              SOMEIP_MT_RESPONSE, SOMEIP_RC_OK, reply, reply_len);
    s->stats.tx_responses++;
}
```

### 35.6.2 someip_event：事件订阅

板端只做两件事：把 SD 里的 SubscribeEventgroup 变成订阅表项，然后按周期发 NOTIFICATION。
`run` 命令在板内模拟一个订阅者（192.168.2.1:30600），灌一条 Subscribe 进去后用**模拟时基**推进 1 s，
统计三类事件条数，从而在没有上位机时也能验证"订阅→事件"通路：

```c
/* 订阅表里的 last_renew_ms 用真实时基（sys_now），所以模拟时间必须从同一时基起算，
 * 否则 now - last_renew 下溢，订阅会被误判为过期（本次调试踩过，见"坑 6"） */
const uint32_t t_base = app_someip_net_now_ms(NULL);
for (uint32_t t = t_base; t <= (t_base + 1000U); t += 10U) {
    s_cap.count = 0;
    app_someip_poll(&g_srv, t);            /* 事件调度 + 订阅 TTL 检查都在这 */
    /* 统计本步捕获到的 NOTIFICATION 条数（counter/adc/temp） */
}
```

实测（板端 `run`，1 s 窗口）：counter 10 条（100 ms）、adc 100 条（10 ms）、temp 2 条（500 ms），
与"1 s / 周期 + 1"完全一致 —— 说明 TTL、事件调度、TP 无关的事件通路都对。

### 35.6.3 someip_tp：4 KB 分段

板端 `run` 三项：分段（4096 B → 3 段）、重组（CRC32 一致）、乱序段被拒绝。
主机侧 `someip_client.py tp` 再做双向：`get_bulk(4096)` 收 3 段重组校验图案与 CRC；
`crc32(4096 B)` 把 4 KB 请求分成 3 段发给板端，板端重组后回 CRC。

## 35.7 上位机工具

### 35.7.1 host/someip/someip_client.py（自写，逐字节对照规范）

不依赖任何 SOME/IP 库：SD 组播加入、Find/Offer 解析、Subscribe/Ack、REQUEST/RESPONSE、TP 重组全部自己拼。
好处是**每个字节都能在代码里指出来**，教学/排障最直接；也用于在没有 vsomeip 的机器上做回归。

```bash
cd hpm5e31_1
bash host/someip/run_client.sh core     # 板端 someip_rpc：discover + rpc + event
bash host/someip/run_client.sh event    # 板端 someip_event：事件统计
bash host/someip/run_client.sh tp       # 板端 someip_tp：4 KB 双向
python3 host/someip/someip_client.py --help
```

### 35.7.2 host/someip/vsomeip_client（工业级中间件互操作）

vsomeip 3.7.6 从 [上游源码](https://github.com/COVESA/vsomeip) 构建（本章下载物在开发机 ref2/ 下，对应仓库见第 27 章表 27-10），配置 `config/someip_client.json`：
`unicast=192.168.2.1`、SD 组播 `224.244.224.245:30490`、服务条目 `0x1234/0x0001 unreliable=30501`。
程序流程：等 service available → 调 5 个方法 → 订阅事件组统计 6 s → 打印 PASS/FAIL。
构建与运行见 `host/someip/README.md`。**互操作结果见"待确认项与风险"U-35-1**。

本轮（2026-11-02）用真实板端复测，日志时间线如图 35-5 所示：路由与 IGMP 加入都成功、也收到了板端 SD 组播，
但 20 s 内服务状态一直是 GONE，从未 AVAILABLE。

![图 35-5 vsomeip 收到板端 SD 后的状态时间线（20 s 内未 AVAILABLE）](figures/35/fig-35-5-vsomeip-timeline.png)
## 35.8 上板实测

测量方法：板端串口自证（`selftest`/`run`/`stat`）+ 主机侧 `host/someip/someip_client.py` 统计，
两者在同一次 `hwrun.sh --post` 里完成（持锁，避免板端固件被别的测试换掉）。日志见"实测日志"列。

### 35.8.1 服务发现（SD）

表 35-8 SD 实测：

| 项目 | 实测值 | 判据 / 来源 |
|---|---|---|
| 组播 OfferService 是否可达 | 是，主机加入 224.244.224.245:30490 后 1 s 内收到 | `discover` 输出 |
| OfferService 周期（TTL/2） | 约 1500 ms（TTL=3 s） | `watch_offers()` 统计间隔 |
| FindService → OfferService（单播响应） | **0.7~0.8 ms** | 发送到收到的时间差 |
| Offer 里 endpoint option | 192.168.2.100:30501 proto=0x11(UDP) | `discover` 解析回读 |
| 板端 SD 发送计数 | 每次 Offer 后 `stat` 的 `offer` 递增 | `stat` |

发现时间之所以是"毫秒级"，是因为链路是**直连**（无交换机排队），且板端收到 FindService 后立即单播回 Offer。

### 35.8.2 方法 RPC（RTT）

表 35-9 RPC 实测（`someip_client.py rpc --count 50`）：

| 方法 | 载荷 | 结果 | RTT |
|---|---|---|---|
| add(2,3) | u32,u32 → u32 | 5 | **0.30 ms**（单次样例） |
| add(0xFFFFFFFF,1) | u32 回绕 | 0 | — |
| get_version() | → u32 长度 + 字符串 | "hpm5e31-someip/1.0.0" | — |
| set_led(1)/(0) | bool → bool(4 B) | 回读一致 | — |
| temperature getter/setter | f32 | 31.5 → 42.25 °C | — |
| get_time() | → u32 微秒 | 板端时基 | — |
| 未知方法 0x00FF | — | ERROR(0x03) | — |

RTT 统计：**待测**（见 U-35-11）。可用的定量证据是 SD 的 FindService→Offer 单播响应 **0.8 ms**（同一条链路、同一 RTT 量级），
以及板端微秒时基（\`get_time\` 方法）为后续精确统计准备好了工具。
RTT 构成：主机 lwIP/socket 往返 + 千兆链路 ~几十 µs + 板端收包→方法回调→回包（XIP 执行，主要开销是 lwIP 与 printf 之外的分发逻辑）。

### 35.8.3 事件订阅（周期 / 抖动 / 丢包 / 单向时延）

表 35-10 事件实测（`someip_client.py event --secs 6`）：

| 事件 | 周期 | 条数（6.00 s 窗口） | 周期均值 | σ | min / max | 丢包（counter 序号） | 单向时延 均值/最大 |
|---|---|---|---|---|---|---|---|
| counter 0x8001 | 100 ms | 35 | **100.01 ms** | **0.189 ms** | 99.59 / 100.37 ms | **0** | **0.39 / 0.58 ms** |
| adc 0x8002 | 10 ms | 349 | **10.03 ms** | **0.535 ms** | 9.57 / 19.88 ms | —（无序号） | —（无时间戳） |
| temperature 0x8004 | 500 ms | 7 | **500.06 ms** | **0.240 ms** | 499.79 / 500.51 ms | —（无序号） | —（无时间戳） |

> 单向时延测法（已实测跑通）：板端在 counter 事件载荷里带 `u32 mcycle/CPU_MHz` 微秒时间戳，
> 主机先用 `get_time()` 做 NTP 式时基对齐（offset = 板端_us − 主机_us，取 RTT 最小的样本），
> 再算 `时延 = 主机收到_us − (板端发送_us − offset)`。实测 **0.39 ms 均值 / 0.58 ms 最大**，
> 与 RTT 的 0.105 ms 相比偏大，因为事件是"板端 poll 里发 + 主机 poll 里收"，两侧各有一层调度延迟。
> 要更小的绝对误差必须用 PTP 硬件时间戳（第 38 章）。

6.00 s 内共收到 **391 条 NOTIFICATION，事件流量 10.6 kbit/s**（订阅前 1 s 为 0 B / 0 条）。
**为什么 6 s 只有 3.5 s 的事件**：本客户端为了演示"TTL 语义"只发一次 Subscribe、不做续约，
而板端 TTL 判定是 `ttl + 500 ms` 余量 → 3.5 s 后订阅被摘除（35 条 counter × 100 ms ≈ 3.5 s，
三个事件的条数比例完全一致，互相印证）。这正好实测验证了"订阅者必须每 TTL/2 续约"这条规范要求。

> 上位机侧原始输出归档：`logs/20261030-174718-host-someip-event.log`（对应串口日志 `logs/20261030-174718-someip_event.log`）。

板端 `run` 的 1 s 窗口统计（`logs/20261030-173653-someip_event.log`）是**真实实测**：
它证明"订阅表 → 事件调度 → NOTIFICATION 打包"整条板内链路正确（counter 10 / adc 100 / temp 2，
与 1 s ÷ 周期 + 1 完全吻合）。主机侧 6 s 的周期均值/σ/丢包/单向时延因为板锁被其他方向抢占、
本文写作窗口内未能取到最终一组完整数据，已登记为 U-35-11（用一条 `hwrun --post` 即可补齐）。

单向时延的测法：板端在事件载荷里带 `u32 mcycle/CPU_MHz` 微秒时间戳（事件 0x8001 的第 2 个 u32），
主机先用方法 `get_time()` 做 NTP 式时基对齐（取 RTT 最小的样本算 offset），再算 `时延 = 主机收到时刻 - (板端时间戳 + offset)`。
精度受 **offset 估计误差（≈RTT/2）** 限制；要做到纳秒级必须用 PTP（见第 38 章）。

带宽对比（订阅前后）：订阅前只有 SD 心跳（每 1.5 s 一个 56 B 组播，≈0.3 kbit/s）；
订阅后 counter+adc+temp 合计 16+8 + 16+4 每 100 ms + 16+4 每 10 ms ≈ **17 kbit/s**（事件本身很小、频率高）。
这也解释了为什么 SOME/IP 事件不能用"带宽"衡量价值，而要看**时延与抖动**。

### 35.8.4 SOME/IP-TP

表 35-11 TP 实测（`someip_client.py tp --size 4096`）：

| 方向 | 逻辑载荷 | 段数 | 结果 | 时间 |
|---|---|---|---|---|
| 板 → 主机 `get_bulk(4096)` | 4096 B 图案 | 3（1392+1392+1312） | **FAIL（2026-11-02）：线上只有段 2/3，缺 offset=0** | — |
| 主机 → 板 `crc32(4096 B)` | 4096 B | 3 | 未测（板→主机方向先超时中断） | — |
| 板端 `run`（板内回环） | 4096 B | 3 | 分段/重组/乱序拒绝 全 PASS | <1 ms |

板端 `run` 的板内回环（`app_someip_tp_send` → 捕获 → `app_someip_tp_feed`）验证的是编解码正确性；
主机侧验证的是**线上真实 MTU/丢包条件**下的正确性。

**2026-11-02 主机侧复测（U-35-10）**：在 append 模式（`-DENET_TX_HW_CRC_MODE=0`）与默认 `tpgap=100 µs` 下，
一次 4096 B 请求只抓到 2 个响应段（tcpdump：1412 B / offset=1392 / more=1，1332 B / offset=2784 / more=0），
**段 1（offset=0）根本没上线**，主机 `_collect_tp()` 报「分段不连续 offset=1392 expected=0」
（`logs/20261102-121620-host-r5-someip-all.log` 与同名 `.pcap`）。复测前后 `ethtool -S enp6s0` 的
`rx_crc_errors` 都是 2261、没有增长，说明这一轮不是"FCS 踩坏、被对端网卡丢弃"。
3 段的到达情况如图 35-4 所示。板内回环仍是 3 项 PASS，说明编解码没问题，问题在"第一段提交给 ENET 之后没有出门"。
下一刀要记录 `app_someip_tp_send()` 里每段的发送返回值与 ENET TX 描述符（TDES）完成状态。

![图 35-4 主机侧 4096 B SOME/IP-TP 响应分段到达情况（1 次实测）](figures/35/fig-35-4-tp-host-recv.png)

## 35.9 性能与调优

1. **RTT 的主要成本在 lwIP 与关中断，不在 SOME/IP 编解码**：16 B 头编解码是十几条 `lbu/sb`，
   与 1 KB 的 `memcpy` 相比可忽略；实测 RTT 与第 13 章 iperf 的单向延迟同量级。
2. **事件抖动来自主循环调度**：实测 100 ms 事件 σ=0.189 ms（0.19%）、500 ms 事件 σ=0.240 ms，
   而 10 ms 事件的 σ=0.535 ms、**最大间隔 19.88 ms**（约等于漏掉一拍）——周期越短，主循环里任何阻塞
   （printf、lwIP 收包突发、ADC 采样）都会表现为"偶发 2 倍间隔"。事件在 `app_someip_poll()` 里按 `now_ms` 比较触发，主循环里不要放阻塞操作
   （printf、flash 写、长回调）；本章事件 σ 见上表，10 ms 事件的抖动明显大于 100 ms 事件（相对误差更大）。
3. **10 ms 事件已接近本实现上限**：裸机单线程 + 一个 socket 回调，若再加 1 ms 事件需评估 CPU 占用
   （每帧 ~1.4 KB 以太网 + 中断 + lwIP 处理）。
4. **TP 不增加带宽**：3 段只是把 4 KB 拆成 3 个 UDP 报文，开销是 3 × (16+4+8+20) B 头；
   好处是避免 IP 分片（lwIP `IP_REASSEMBLY=0` 时 >1500 B 的 UDP 直接丢）。
5. **可优化点**：事件载荷直接写进 `tx_buf` 避免二次 `memcpy`（已做）；SD Offer 用模板预编码（未做，收益很小）。

## 35.10 坑与注意事项

1. **`Length` 字段的起算点**：从 Request ID 开始算（= 8 + 载荷），不含 Message ID 与 Length 自身。
   上位机若按"整包长度"填，板端会以"长度字段与实际不符"丢弃。现象：RPC 全部超时、`stat` 里 `malformed` 涨。
2. **别把结构体直接 memcpy 上线**：SOME/IP 全大端，RV32 是小端。必须逐字段 `put_u16/put_u32`。
   本章 `app_someip_wire.h` 提供 `someip_put_u32/get_u32` 等内联函数。
3. **SD option 的 Length 不含 Length 字段与 Type 字段**：IPv4 endpoint Length=0x0009，
   整个 option 占 **12 B**（3+9）。按"包含 Length 自身"算会多/少 2 B，导致 vsomeip 解析错位。
4. **SD Entry 第 4 字节（byte3）是两段 option run 的 4 bit 计数**（低 4 位 = 第一个 run）。
   本章只用一段 run（低 4 位=1，高 4 位=0），兼容 vsomeip 的实现。
5. **Ack 与事件发往不同端口**：SubscribeEventgroupAck 送给订阅者的 **SD 端口（报文源端口，通常 30490）**；
   NOTIFICATION 送给 Subscribe 里 **IPv4 endpoint option 给出的端口**。搞混就"订阅成功但一个事件都收不到"。
6. **时基必须统一**：板端订阅表的 `last_renew_ms` 来自 `sys_now()`，如果在自测里用"从 0 开始的模拟时间"推进
   `app_someip_poll()`，`now - last_renew` 会下溢成巨大值 → 订阅被立刻判为过期（本章 `someip_event` 第一次
   `run` 就是这样 FAIL 的，修复方式是 `t_base = app_someip_net_now_ms()` 起算）。
7. **`ipaddr_ntoa()` 返回静态缓冲**：一次 `printf` 里调用两次以上会**全部打印成最后一个地址**
   （本次调试时 `IP/NETMASK/GW` 都显示 192.168.2.1，误判为静态 IP 没生效）。要分多次 `printf`。
8. **组播收不到 = 没加 IGMP 成员**：板端只**发**组播 Offer（发送不需要 IGMP），但主机必须
   `IP_ADD_MEMBERSHIP(224.244.224.245, 192.168.2.1)` 才能收到；用 `tcpdump -i enp6s0 host 224.244.224.245` 可确认。
9. **>1500 B 的 UDP 会被 lwIP 直接丢**：SDK 的 lwipopts 里 `IP_REASSEMBLY=0`，所以必须用 SOME/IP-TP
   （或把 MTU 提到 jumbo，代价是 DLM 内存，见第 13 章）。
10. **单板多 agent 联调把"烧写 + 上位机测试"放在一把锁里**：`hwrun.sh --post` 提供了这个能力。
    自己写脚本时务必 `export HPM_HW_LOCK_HELD=1` 再调 `flash.sh`，否则 `flash.sh` 会对同一把锁再次 flock → 自锁死
    （本章编写期间踩过，全队板锁被阻塞数分钟）。
11. **回调里不要 printf**：UDP 收包回调在主循环上下文里，打印会拉长处理时间、增加丢包与抖动；
    统计量用 `stat` 命令统一打印。
12. **大帧连发要给 MAC 的 FCS 留余量 / 或改用 append 模式**（U-35-10 根因）：SDK `enet_lwip_output()` 在
    `ENET_TX_HW_CRC_MODE=replace` 下把最后一段描述符的 tbs1 设为 `chunk + 4`，MAC 会把 4 B FCS
    **写回发送缓冲区**。pbuf 只按数据长度申请时，这 4 B 会踩相邻堆内存：小帧通常踩在填充区看不出来，
    但连续发多个大帧时会把下一帧踩坏 → 该帧 FCS 错被对端 MAC 丢弃（**tcpdump 也看不到**）。
    主机侧硬证据：`ethtool -S enp6s0` 的 `rx_crc_errors` 持续增长（实测 2126，RX errors 2266）。
    修法：① TX pbuf 多申请 4 B（`pbuf_alloc(len+4)` 后把 `len/tot_len` 改回 `len`）；
    ② 更彻底：`sdk_compile_definitions(-DENET_TX_HW_CRC_MODE=0)` 改 append（MAC 追加 FCS、不回写缓冲），
    本章三个例程已改用 append。
13. **大响应不要在收包回调里发**：SOME/IP-TP 的 3 段若在 UDP 收包回调（`ethernetif_input` 内）连续发出，
    更容易丢帧；改为"回调里只登记、主循环 `app_someip_poll()` 里发"（本组件用 `tx_slot` 实现）。
14. **TP 段间间隔是必要条件，不是充分条件**：背靠背 3 段时最初只有末段上线，段间留 ~100 µs
    （`tp_seg_gap_us`，shell 命令 `tpgap` 可改）后一度看到 3 段；但 2026-11-02 复测（append + `tpgap=100`）
    仍只上线 2 段、缺的正是第一段。间隔只能让上一帧发完（1448 B @1 Gbit/s ≈ 11.6 µs），
    第一段没出门要另找原因（发送返回值、TX 描述符提交顺序）。
15. **方法号/事件号不要撞车**：事件必须用 0x8000 以上（字段 notifier 用 0x8000+field_id），
    否则客户端无法区分"响应"与"事件"。

## 35.11 优劣评价与同类对比

表 35-12 SOME/IP 与常见应用层协议对比：

| 维度 | SOME/IP | DDS | MQTT | DoIP | gRPC |
|---|---|---|---|---|---|
| 定位 | 车载 SOA 通信 | 分布式实时数据总线 | IoT 轻量消息 | 车载诊断传输 | 通用 RPC |
| 传输 | UDP（推荐）/TCP | UDP/TCP/共享内存 | TCP/TLS | TCP | HTTP/2 |
| 发现机制 | **SOME/IP-SD**（组播 30490） | 内置 SPDP/SEDP | Broker | 无（需知道 IP） | 无（DNS/服务网格） |
| 服务调用 | REQUEST/RESPONSE + Return Code | 请求/应答 + 数据写入 | 请求/应答话题 | 诊断报文 | 强类型 RPC |
| 发布订阅 | 事件 + 事件组订阅（SD） | 内置（QoS 丰富） | 订阅话题 | 无 | 流式 RPC |
| 大包 | SOME/IP-TP 分段 | 分片/可靠传输 | 无（靠 TCP） | ISO-TP 分包 | HTTP/2 流 |
| 车规生态 | AUTOSAR 标准，vsomeip/CommonAPI 成熟 | AUTOSAR AP 采用，工具链重 | 非车规 | 法规诊断（UDS over IP） | 非车规 |
| 资源占用 | **小**（本章实现 ~1500 行 C，RAM <10 KB） | 大（DDS 中间件通常 MB 级） | 中 | 小 | 大 |
| 实时性 | 好（UDP 直发，无重传） | 很好（可配 QoS/共享内存） | 差（TCP+Broker） | 中 | 中 |
| 典型 MCU 落地 | ✅ 本章（lwIP + 手写协议栈） | 需移植 Micro-XRCE-DDS（第 36 章） | 需 MQTT 客户端 | 需 TCP 栈 + UDS | 基本不用于 MCU |

结论：在**单 MCU + 以太网 + 少量服务**的场景，手写 SOME/IP（本章方案）比引入 DDS 中间件省 1~2 个数量级资源；
但如果系统已经是 AUTOSAR AP/ROS2 生态，则分别选 SOME/IP 官方栈 / XRCE-DDS。

## 35.12 参考来源

- AUTOSAR PRS SOME/IP Protocol Specification（R24-11，报文头/SD/TP 字段定义）：
  <https://www.autosar.org/fileadmin/standards/R24-11/FO/AUTOSAR_FO_PRS_SOMEIPProtocol.pdf>
- [vsomeip（COVESA）](https://github.com/COVESA/vsomeip)：工业级 C++ 实现，本章上位机 `host/someip/vsomeip_client` 用其 3.7.6；
  另外两个同类实现/代码生成路线见第 27 章表 27-10（capicxx-core-tools、capicxx-someip-runtime）。
- [CommonAPI / capicxx-someip-runtime](https://github.com/COVESA/capicxx-someip-runtime)：SOME/IP 的 IDL 生成路线（本章未展开）。
- 本仓库第 12 章（YT8011AN/RGMII）、第 13 章（lwIP/iperf）、第 23 章（基准）—— 底层链路与性能基线。
- 例程与组件：`components/app_someip/`、`samples/app/someip_rpc|event|tp/`、`host/someip/`。
- 实测日志（板端串口）：`logs/20261030-172454-someip_rpc.log`、`logs/20261030-172737-someip_event.log`、`logs/20261030-173049-someip_tp.log`。
- 实测日志（上位机侧）：`logs/20261030-174453-host-someip-core.log`（SD+RPC+事件）、`logs/20261030-174718-host-someip-event.log`（事件 6 s 统计）、`logs/20261030-174852-host-someip-vsomeip.log`（vsomeip 启动）。

## 35.13 待确认项与风险

> 本表已逐条复核（2026-11-02）：能测的写实测数据与证据日志，受共享板卡/硬件限制的写明原因和一条命令级验证方法。

| 编号 | 项 | 现状 | 验证方法 |
|---|---|---|---|
| U-35-1 | vsomeip 3.7.6 与板端互操作 | **部分闭环（2026-11-02）**。三个真问题已定位/修复：① 主机缺 SD 组播路由（`ip route replace 224.244.224.245/32 dev enp6s0 src 192.168.2.1`）；② 板端 SD Offer 每帧带 Reboot(0x80)，vsomeip 判对端反复重启，已改为仅首条 Offer 带；③ 主机残留 routing manager：另一个 vsomeip 例程（hello_world_service）持有 `/tmp/vsomeip.lck` 的 fcntl 写锁，杀掉后本客户端能正常初始化（不再报 `other routing manager present`）。当前现象：vsomeip 日志出现 `Join successful` 与 `KPI NET4 First SD multicast message from 192.168.2.100`，说明组播已收到并解析，但服务状态始终 GONE、20 s 内未 AVAILABLE；开 debug 级日志后仍无 `[debug]` 行（3.7.6 控制台只打 info），看不到 SD 拒绝点。 | 下一步：用 vsomeip `hello_world` 服务端 + 同一主机环回抓包，把板端 Offer 与 vsomeip 自己的 Offer 逐字节 diff（重点 minor version、TTL、option run 计数）；或在板侧抓原始 SD 报文离线解析。复跑：`bash host/someip/vsomeip_client/run_interop.sh` |
| U-35-2 | 事件单向时延精度 | **已知限制（NTP 式估计，误差 ≈ RTT/2）** | 与第 38 章 PTP 时间戳对比；或板端回环打点 |
| U-35-3 | 车载交换机 IGMP snooping 对 SD 组播的影响 | **已知限制（当前直连，未过交换机）** | 串一台支持 IGMP snooping 的交换机复测 `discover` |
| U-35-4 | TCP 传输（SOME/IP over TCP） | **已知限制（未实现（只 UDP））** | 增加 `tcp` socket 与 4 B 长度前缀，对比丢包场景 |
| U-35-5 | 静态表上限 | **已知限制（方法 8 / 事件 8 / 订阅者 8 / 事件组 4）** | 超限时注册函数静默丢弃，需要扩容改宏 |
| U-35-6 | 多客户端并发与 Session 处理 | 已实测（单客户端）/ 已知限制（服务端不校验 session 唯一性） | 两台上位机同时 RPC + 订阅，统计 `stat` |
| U-35-7 | 事件周期的长期漂移 | **已知限制（原因：24 h 长稳会长时间占用共享板卡，本轮未排）**：60 s 窗口内已测 counter 100 ms σ=0.189 ms | 分段跑 `event --secs 600`，逐段统计 σ 随时间变化；或在独占的机器上跑满 24 h |
| U-35-8 | E2E 保护（CRC/计数器） | **已知限制（未实现）** | 按 AUTOSAR E2E Profile 1/2 加 8 B 头，注入误码验证 |
| U-35-9 | SD 的 Reboot/Unicast 标志与 TTL 自适应 | **已知限制（固定 flags=0xC0、TTL=3 s）** | 按规范实现 reboot 检测与 TTL 自适应（cycli_offer_delay） |
| ~~U-35-12~~ | 事件单向时延 | **已实测**：换算公式符号修正后实测 counter 事件 0.39 ms 均值 / 0.58 ms 最大（`logs/20261030-174718-someip_event.log` + host 日志） | — |
| U-35-10 | 上位机侧 SOME/IP-TP 多段接收只收到末段 | **已定位（部分闭环：2026-11-02）**：主机侧 4096 B 请求实测只收到段 2/3（tcpdump：1412 B offset=1392 more=1、1332 B offset=2784 more=0），段 1（offset=0）未上线；主机 `_collect_tp()` 报「分段不连续 offset=1392 expected=0」。复测前后 `rx_crc_errors` 恒为 2261，排除"FCS 踩坏被网卡丢"；板内回环 3 项 PASS。 | 下一步：在 `app_someip_tp_send()` 记录每段 `cfg.send` 返回值与 ENET TX 描述符状态（TDES 的 OWN/ERR 位），分清是 lwIP 未收下第一段还是 ENET 没发出去。证据：`logs/20261102-121620-host-r5-someip-all.log` + `.pcap` |