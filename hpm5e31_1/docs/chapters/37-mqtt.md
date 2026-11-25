# 第 37 章 MQTT 与车云遥测

## 37.1 文档信息

**表 37-1 文档信息表**

| 项 | 内容 |
|---|---|
| 章号 / 版本 | 第 37 章，v1.0（2026-11-02） |
| 目标硬件 | hpm5e31_1（HPM5E31IPB1，RV32IMAC @480 MHz，1 MB Flash / 128 KB DLM） |
| 板端软件 | `components/app_mqtt`（自写 MQTT 3.1.1 客户端，lwIP 2.1.2 raw TCP，NO_SYS=1，无 malloc） |
| 上位机软件 | mosquitto **2.0.22**（broker）+ paho-mqtt 2.1.0 + mosquitto-clients |
| 网络 | 板 `192.168.2.100/24` ↔ 主机 `enp6s0 192.168.2.1/24`，TCP **1883** |
| 对应例程 | `samples/app/mqtt_telemetry`；上位机 `host/mqtt/` |
| 上位机工具 | `host/mqtt/`（run_broker.sh / sub_check.py / pub_cmd.py / stress.py / lwt_check.py / bench.sh / run_suite.sh / pcap_latency.py） |
| 组件 | `components/app_mqtt/`（inc/app_mqtt.h、src/app_mqtt.c、app_mqtt.cmake） |
| 实测日志 | 串口：`logs/20261102-164024-mqtt_telemetry.log`、`logs/20261102-162800-mqtt_telemetry.log`、`logs/20261102-162508-mqtt_telemetry.log`；上位机与 broker：`logs/20261102-16*-host-mqtt-*.{json,log,pcap,txt}` |
| 实测日期 | 2026-11-02（板端串口 + broker 日志 + tcpdump 三侧取证） |
| 可信度 | 高：连接/时延/吞吐/LWT/keepalive 均为仪表化实测；TLS、QoS2、1080 s 长保活未做，见 38.13 |

> 本章数字全部来自 `logs/` 里的真实日志；未测项目集中登记在 38.13，不在这里编数。

## 37.2 本章导览

本章解决一个具体问题：**一块只有裸机 lwIP 的 MCU，怎样用最小的代码量接上车云 MQTT，并且能证明它可靠**。
不依赖任何 MQTT SDK，报文逐字节自己拼，因此每个数字都能追到线上抓包。

阅读顺序建议：

1. 38.3 概述：MQTT 在车云链路里的位置，为什么不直接用 HTTP；
2. 38.4 工作原理：报文结构（图 37-1）、CONNECT 逐字节（表 37-6）、主题通配符、QoS0/QoS1 时序（图 37-2）、keepalive/LWT、连接状态机（图 37-3）；
3. 38.5 组件设计与资源映射：API 表与 lwIP 缓冲预算；
4. 38.6 例程：`mqtt_telemetry` 的主题设计、下行命令、shell；
5. 38.7 上位机：一次持锁窗口跑完全部联调；
6. 38.8 实测：1 Hz 遥测、0.28 ms 单条往返、8771 msg/s 板→云吞吐（板端自报）、LWT 1.6 ms、keepalive 保活；
7. 38.9 性能与 38.10 易错点：Nagle+delayed ACK 让单条往返从 41 ms 掉到 0.28 ms 的全过程；
8. 38.11 对比 / 38.13 待确认项：MQTT 与 MQTT-SN/CoAP/HTTP/SOME-IP 的取舍。

前置章节：第 12 章（YT8011AN）、第 13 章（lwIP/iperf）、第 36 章（XRCE-DDS，另一种"MCU 上云"路线）。

## 37.3 概述

### 37.3.1 MQTT 在车云链路里站在哪

一辆车对外通常有三条链路：诊断（DoIP/UDS，第 31 章）、实时控制（CAN/CAN FD，第 46 章）、
**车云遥测**（本章）。车云链路的特点与台架完全不同：

- 带宽小、延迟大、随时掉线（隧道、地库、运营商切换）→ 需要**长连接 + 心跳 + 遗嘱**；
- 一个网关后面挂几十个 ECU → 需要**主题分级**把数据分门别类；
- 云端可能是第三方平台 → 需要**标准协议**，不能自定义二进制。

MQTT 正好是为此设计的：基于 TCP 的发布/订阅协议，头部最小 2 字节，QoS0/1/2 三档可靠性，
遗嘱（LWT）让"设备掉线"本身成为一个可被订阅的事件。它在车联网里的典型分工见表 37-2。

**表 37-2 车载以太网上的协议分工（本项目的全景）**

| 层 / 用途 | 协议 | 本书章节 | 典型端口 |
|---|---|---|---|
| 诊断刷写 | DoIP + UDS | 第 30/31 章 | TCP/UDP 13400 |
| 服务化通信（车内） | SOME/IP | 第 35 章 | UDP 30490/30501 |
| 机器人/中间件 | XRCE-DDS | 第 36 章 | UDP 2018 |
| 标定测量 | XCP on UDP | 第 34 章 | UDP 5555 |
| **车云遥测 / 远程命令** | **MQTT 3.1.1** | **本章** | **TCP 1883（TLS 8883）** |
| 时间同步 | gPTP | 第 38 章 | L2 0x88F7 |

一句话：**车内用 SOME/IP 与 XRCE，车外用 MQTT**——前者追求确定性与零拷贝，后者追求穿透性与可运维性。

### 37.3.2 本项目的落地形态

**表 37-3 本章交付物**

| 交付物 | 路径 | 说明 |
|---|---|---|
| MQTT 客户端组件 | `components/app_mqtt/` | 约 1170 行 C，静态 RAM 6228 B，无堆 |
| 板端例程 | `samples/app/mqtt_telemetry/` | 1 Hz 遥测 + 9 条下行命令 + 8 条 shell 命令 |
| 上位机工具 | `host/mqtt/` | broker 起停、订阅校验、命令、压测、LWT/keepalive、抓包分跳 |
| 本章 | `docs/chapters/37-mqtt.md` | 图 37-1~37-4，表 37-1~37-23 |

实现路线的选择：SDK 的 lwIP 里其实带了一份 `middleware/lwip/src/apps/mqtt/mqtt.c`（约 1100 行，
回调式 API）。本章**没有用它**，理由有三条：

1. 它的回调模型把"连接状态"藏在 lwIP 内部，出问题时只能看 `MQTT_DEBUG` 打印；
2. 它不支持我们需要的"发送环形队列 + QoS1 窗口 + 显式重连退避"这一套可控策略；
3. 书稿需要逐字节讲报文，自写实现才能把每个字段和实测对起来。

对照实验留在了 38.11：lwIP 自带实现的 API 形状与本章实现的差异。

## 37.4 工作原理

### 37.4.1 报文结构：固定头 + 剩余长度 + 载荷

MQTT 3.1.1 的所有报文都是同一个骨架（图 37-1）：

![图 37-1 MQTT 报文结构、CONNECT 逐字节与剩余长度变长编码](figures/37/fig-37-1-frame.png)

- **固定头**：第 1 字节高 4 位是报文类型（表 37-4），低 4 位是类型相关标志；
- **剩余长度**：1~4 字节变长编码，表示"固定头之后还有多少字节"（表 37-5）；
- **可变头 + 载荷**：随报文类型不同。

**表 37-4 MQTT 3.1.1 报文类型（本章实现覆盖 11/14 种）**

| 类型值 | 报文 | 方向 | 本章实现 | 用途 |
|---|---|---|---|---|
| 1 | CONNECT | Client→Server | ✅ | 建立会话（含遗嘱、用户名口令） |
| 2 | CONNACK | Server→Client | ✅ | 确认会话，返回码 0~5 |
| 3 | PUBLISH | 双向 | ✅ | 发布消息（QoS0/1） |
| 4 | PUBACK | 双向 | ✅ | QoS1 确认 |
| 8 | SUBSCRIBE | Client→Server | ✅ | 订阅主题过滤器 |
| 9 | SUBACK | Server→Client | ✅ | 订阅结果 |
| 10 | UNSUBSCRIBE | Client→Server | ✅ | 取消订阅 |
| 11 | UNSUBACK | Server→Client | ✅ | 取消订阅结果 |
| 12 | PINGREQ | Client→Server | ✅ | 心跳请求 |
| 13 | PINGRESP | Server→Client | ✅ | 心跳应答 |
| 14 | DISCONNECT | Client→Server | ✅ | 正常断开（不发遗嘱） |
| 5/6/7 | PUBREC/PUBREL/PUBCOMP | 双向 | ❌ | QoS2 三次握手，见 U-38-2 |

**表 37-5 剩余长度编码（每字节 7 位数据 + 最高位延续位）**

| 数值 | 线上字节 | 字节数 |
|---|---|---|
| 0 | `00` | 1 |
| 127 | `7F` | 1 |
| 128 | `80 01` | 2 |
| 143（本章 telemetry 载荷） | `8F 01` | 2 |
| 16383 | `FF 7F` | 2 |
| 16384 | `80 80 01` | 3 |
| 268435455 | `FF FF FF 7F` | 4（协议上限） |

编码器只有 8 行，容易被写错的是"先取低 7 位、再右移"的顺序；解码器还要防 4 字节以上
的恶意输入（本章实现直接判定协议错误并断链，见 38.10 易错点 4）。

### 37.4.2 连接建立与 CONNECT 逐字节

板上电后：`app_lwip_init()` 起 TCP/IP → `app_mqtt_connect()` 发 SYN → TCP 建链回调里发 CONNECT →
收到 CONNACK rc=0 后进入 CONNECTED，然后订阅下行主题、发布 retained 在线状态。

CONNECT 的 byte 0 是 `0x10`，byte 1 起是剩余长度。本板实际配置（client_id=`hpm5e31`、
遗嘱主题 `hpm5e31/status`、遗嘱载荷 47 B、keepalive 1080 s）算出来 86 B，
和串口 `mqtt hex connect` 打出来的完全一致（表 37-6）。

**表 37-6 CONNECT 报文逐字节（本板实测，86 B）**

| 偏移 | 字节 | 字段 | 本板取值与含义 |
|---|---|---|---|
| 0 | `10` | 固定头 | 类型 1（CONNECT），标志 0 |
| 1 | `54` | 剩余长度 | 84 B（= 10 + 9 + 16 + 49） |
| 2-3 | `00 04` | 协议名长度 | 4 |
| 4-7 | `4D 51 54 54` | 协议名 | "MQTT" |
| 8 | `04` | 协议级别 | 4 = MQTT 3.1.1 |
| 9 | `2E` | 连接标志 | clean(0x02) + will(0x04) + will_qos1(0x08) + will_retain(0x20) |
| 10-11 | `04 38` | keepalive | 0x0438 = 1080 s |
| 12-13 | `00 07` | client id 长度 | 7 |
| 14-20 | `68 70 6D 35 65 33 31` | client id | "hpm5e31" |
| 21-22 | `00 0E` | 遗嘱主题长度 | 14 |
| 23-36 | `...` | 遗嘱主题 | "hpm5e31/status" |
| 37-38 | `00 2F` | 遗嘱载荷长度 | 47 |
| 39-85 | `7B 22 64 ...` | 遗嘱载荷 | `{"dev":"hpm5e31","online":false,"reason":"lwt"}` |

串口原始输出（`logs/20261102-164024-mqtt_telemetry.log`）：

```text
mqtt CONNECT packet (86 B):
10 54 00 04 4D 51 54 54 04 2E 04 38 00 07 68 70
6D 35 65 33 31 00 0E 68 70 6D 35 65 33 31 2F 73
74 61 74 75 73 00 2F 7B 22 64 65 76 22 3A 22 68
70 6D 35 65 33 31 22 2C 22 6F 6E 6C 69 6E 65 22
3A 66 61 6C 73 65 2C 22 72 65 61 73 6F 6E 22 3A
22 6C 77 74 22 7D
```

broker 侧对同一次连接的记录（`logs/20261102-160234-host-mqtt-broker-basic.log`）：

```text
New client connected from 192.168.2.100:49153 as hpm5e31 (p2, c1, k1080).
		hpm5e31/status
Sending CONNACK to hpm5e31 (0, 0)
Received SUBSCRIBE from hpm5e31
		hpm5e31/cmd/# (QoS 1)
Sending SUBACK to hpm5e31
Received PUBLISH from hpm5e31 (d0, q1, r1, m2, 'hpm5e31/status', ... (54 bytes))
```

`k1080` 说明 broker 正确解析了 keepalive；`r1` 表示这条 status 是 retained；`q1` 是 QoS1；
`m2` 是 broker 分配的 packet id。CONNACK 返回码含义见表 37-7。

**表 37-7 CONNACK 返回码**

| rc | 含义 | 本章处理 |
|---|---|---|
| 0 | 连接已接受 | 进入 CONNECTED，订阅 + 发 retained 在线状态 |
| 1 | 协议版本不支持 | 记 `connack_refused`，断链，按退避重试 |
| 2 | client id 被拒绝 | 同上（本板 client id 固定 hpm5e31） |
| 3 | 服务不可用 | 同上 |
| 4 | 用户名或口令错误 | 同上（本板测试 broker 匿名，未触发） |
| 5 | 未授权 | 同上 |

### 37.4.3 主题、通配符与 retained

主题是斜杠分隔的 UTF-8 字符串，订阅时可以用两种通配符（表 37-8）。MQTT 的匹配是
**broker 做**的，但客户端也需要一份匹配逻辑：本板收到 `hpm5e31/cmd/#` 下发的一条消息后，
要按"具体是哪条命令"分流（`cmd/led`、`cmd/period`……），用的是同一套规则。

**表 37-8 主题通配符规则与本章实现**

| 过滤器 | 匹配 | 不匹配 | 本章实现要点 |
|---|---|---|---|
| `hpm5e31/cmd/#` | `hpm5e31/cmd/led`、`hpm5e31/cmd/a/b`、`hpm5e31/cmd` | `hpm5e31/status` | `#` 只能在最后一级 |
| `hpm5e31/+/led` | `hpm5e31/cmd/led` | `hpm5e31/cmd/x/led` | `+` 必须独占一级 |
| `hpm5e31/#` | `hpm5e31`、`hpm5e31/status` | — | 父级也匹配（§4.7.1.2） |
| `#` | 所有普通主题 | `$SYS/...` | `$` 开头不被前导通配符匹配 |
| `a/+/c` | `a/b/c` | `a/c`、`a/b/d` | 逐级比较 |

本板的主题分配见表 37-9。设计原则是：**上行遥测用 QoS1、状态用 retained、快速回声用 QoS0**——
把"必须到的"和"越多越好的"分开，避免一个 topic 里混两种可靠性。

**表 37-9 本工程主题与可靠性选择**

| 主题 | 方向 | QoS | retain | 为什么这么选 |
|---|---|---|---|---|
| `hpm5e31/telemetry` | 上行 | 1 | 否 | 遥测丢一条无所谓、重一条可接受；QoS1 已足够 |
| `hpm5e31/status` | 上行 | 1 | **是** | 新订阅者一进来就要知道"现在在线/离线"，必须 retained |
| `hpm5e31/ack` | 上行 | 1 | 否 | 命令回执，需要到达 |
| `hpm5e31/echo/reply` | 上行 | 0 | 否 | 只用于测 RTT，丢一条不算错，QoS0 最快 |
| `hpm5e31/bench` | 上行 | 1 | 否 | 吞吐测试，QoS1 才能验证 packet id/PUBACK 路径 |
| `hpm5e31/cmd/#` | 下行 | 1 | — | 命令不能丢；broker 侧订阅 QoS1 |
| `hpm5e31/status`（遗嘱） | 上行 | 1 | 是 | 设备异常掉线时由 broker 代发 `online:false` |

### 37.4.4 QoS0/QoS1 与 packet id

MQTT 的三档 QoS 里，车云遥测最常用的是 QoS0 和 QoS1（表 37-10）。QoS2 的"恰好一次"需要
PUBREC/PUBREL/PUBCOMP 三次往返，收益主要是去重，代价是延迟翻倍，本章不做（U-38-2）。

**表 37-10 QoS 档位对比与本章实现**

| 档位 | 握手 | 可靠性 | 重复可能 | 本章实现 |
|---|---|---|---|---|
| 0 | PUBLISH | 最多一次 | 不重发 | ✅ 不占 packet id，不入未确认表 |
| 1 | PUBLISH⇄PUBACK | 至少一次 | 可能重复（DUP=1 重发） | ✅ 未确认表 8 条，5 s 超时重发，最多 3 次 |
| 2 | PUBLISH⇄PUBREC⇄PUBREL⇄PUBCOMP | 恰好一次 | 不会 | ❌ 收到即判协议错误断链 |

QoS1 的 8 条未确认窗口对应 `APP_MQTT_INFLIGHT_MAX=8`。窗口满时 `app_mqtt_publish()` 返回 -3，
调用者要 `app_mqtt_poll()` 等 PUBACK 腾位置——这不是 bug，是背压（例程的 bench 循环就是这么写的）。
packet id 在 1~65535 之间分配，跳过 0 和未确认表中的 id，避免"重发 ACK 认错人"。

QoS1 与 LWT/keepalive 的完整时序见图 37-2（数字是本章实测值）。

![图 37-2 QoS1 发布/订阅、keepalive 与 LWT 时序](figures/37/fig-37-2-qos1.png)

### 37.4.5 keepalive、LWT 与 clean session

**表 37-11 三个"保命"机制**

| 机制 | 协议行为 | 本章实现 | 实测 |
|---|---|---|---|
| keepalive | 客户端保证任意两次发包间隔 ≤ keepalive；空闲则发 PINGREQ，broker 回 PINGRESP；broker 在 1.5×keepalive 没收到包就判死链 | 空闲达到 keepalive−1 s 发 PINGREQ；PINGRESP 超时（keepalive<5 s 用 keepalive，否则 5 s）断链重连 | keepalive=10 s、上行空闲 30 s：PINGREQ/PINGRESP 各 3 次 |
| LWT 遗嘱 | CONNECT 时登记 will topic/payload/qos/retain，**异常断链**由 broker 代发；正常 DISCONNECT 不发 | 遗嘱 = `hpm5e31/status {"online":false,"reason":"lwt"}`，QoS1 + retained | abortive kill：broker 1.6 ms 内发出遗嘱 |
| clean session | 1 = 每次连接重建会话，离线消息不保留 | 固定 1（本板不做本地会话缓存） | 重连后自动重新 SUBSCRIBE |

一个容易忽略的细节：**有 1 Hz 遥测时，keepalive 永远不会触发 PINGREQ**——因为任何 PUBLISH 都算"有包"。
所以验证心跳必须先把上行弄空闲（本章用 `period 60000` 命令），否则会得到"PINGREQ=0"的假结论（38.10 易错点 7）。

### 37.4.6 连接状态机与重连退避

![图 37-3 app_mqtt 连接状态机](figures/37/fig-37-3-fsm.png)

状态机只有 6 个状态（图 37-3），全部推进都在 `app_mqtt_poll()` 里做；lwIP 回调只负责
"收到什么就处理什么"，不重连、不打印，保证回调短（约定见 `docs/应用层开发约定.md` §4.2）。

关键设计：

- **断链统一走 `mq_schedule_reconnect()`**：清理 QoS1 未确认表 → 回调通知应用 → 指数退避（1 s 起，翻倍，封顶 30 s）；
- **重连成功后退避归位到 1 s**，避免"偶发断链后下次重连要等 30 s"；
- 主动 `DISCONNECT` 时关闭 auto_reconnect，进入 CLOSED 不再重试；
- 应用在 `on_state(CONNECTED)` 里重新订阅——组件不保存订阅列表（clean session=1 下会话已重建）。

## 37.5 组件设计与资源映射

**表 37-12 组件 API（`components/app_mqtt/inc/app_mqtt.h`）**

| API | 作用 | 返回 |
|---|---|---|
| `app_mqtt_init(m, cfg)` | 拷贝配置（字符串）、清零状态 | 0 / -1 |
| `app_mqtt_set_state_cb` / `set_message_cb` | 注册状态/消息回调 | — |
| `app_mqtt_connect(m)` | 发起 TCP 连接（非阻塞） | 0 / -1 / -2 |
| `app_mqtt_poll(m)` | 推进 TCP 发送、keepalive、QoS1 重发、重连、超时 | — |
| `app_mqtt_disconnect(m, graceful)` | 优雅 DISCONNECT / 直接 abort（LWT 测试） | — |
| `app_mqtt_publish(m, topic, payload, len, qos, retain)` | 发布 | ≥0 packet id；-2 未连接；-3 窗口/队列满；-4 QoS2 |
| `app_mqtt_subscribe` / `unsubscribe` | 订阅 / 取消订阅 | 0 / -2 / -3 |
| `app_mqtt_is_connected` / `state` / `state_str` | 查询状态 | — |
| `app_mqtt_get_stats` / `reset_stats` | 统计快照 / 清零 | — |
| `app_mqtt_last_packet` / `debug_connect` | 取最近报文 / 构造 CONNECT 供 hexdump | 长度 |
| `app_mqtt_remlen_encode` / `decode` | 剩余长度编解码（纯函数） | 字节数 / 0 / -1 |
| `app_mqtt_topic_match(filter, topic)` | 主题通配符匹配（纯函数） | bool |

**表 37-13 编译期容量与静态内存**

| 项 | 值 | 说明 |
|---|---|---|
| `APP_MQTT_MAX_PACKET` | 512 B | 单条报文上限，超长断链 |
| `APP_MQTT_RX_BUF` | 2048 B | 接收重组缓冲（必须 ≥ 一个 TCP MSS 1460 + 最大报文） |
| `APP_MQTT_TXQ_SIZE` | 1024 B | 发送环形队列（TCP sndbuf 满时暂存） |
| `APP_MQTT_INFLIGHT_MAX` | 8 | QoS1 未确认窗口 |
| `APP_MQTT_INFLIGHT_PAYLOAD` | 192 B | 单条 QoS1 未确认载荷上限 |
| `app_mqtt_t` 实例 | **6228 B** | `nm -S demo.elf` 的 `g_mq` 符号实测 |
| 组件代码（.text） | **10 424 B** | `app_mqtt.c.obj`，工程统一 -O3 |
| 整机固件 | FLASH **137 912 B**（text 137 392 + data 520）/ DLM 95 168 B | `riscv32-unknown-elf-size build/mqtt_telemetry-debug/output/demo.elf`（2026-11-02 当前 ELF） |

**表 37-14 关键配置项与 lwIP 缓冲（"寄存器级"对照）**

| 配置 | 位置 | 本板取值 | 影响 |
|---|---|---|---|
| `MEM_SIZE`（lwIP 堆） | `components/app_lwip/inc/lwipopts_app.h` | 24 KB | TCP 控制块、pbuf 批量拷贝 |
| `PBUF_POOL_SIZE` | 同上 | 16 | 与 ENET TX 描述符数一致，避免发送持有耗尽池 |
| `TCP_SND_BUF` / `TCP_WND` | 同上 | 8×MSS / 8×MSS（≈11 680 B） | 决定单连接在途窗口；也是 bench 首轮卡顿的根因之一 |
| `TCP_SND_QUEUELEN` | lwIP 内部按 `2*TCP_SND_BUF/TCP_MSS` 算出 | 16 段 | `tcp_write` 返回 ERR_MEM 的阈值 |
| 收发缓冲数量 | `components/app_lwip/inc/common_cfg.h` | ENET TX/RX 各 16×1536 B | 千兆下行（第 13 章）实测配置 |
| mqtt 任务周期 | 例程主循环 | 无固定周期（忙轮询） | `app_lwip_poll+app_mqtt_poll` 单次 <10 µs |

## 37.6 例程：samples/app/mqtt_telemetry

例程把"遥测上行 + 命令下行 + 板内自测"放在一个固件里。除了 MQTT，它还用现有共享组件
采三路数据：CAN（`app_can`：CAN0 发、CAN1 收，板内两两相连）、ADC（PF26）、片内温度（TSNS）。
这样每条遥测都有真实来源，不是硬编码常量。

**表 37-15 下行命令（`hpm5e31/cmd/<cmd>`，QoS1）**

| 命令 | 载荷 | 动作 | 回执字段 |
|---|---|---|---|
| `led` | `on`/`off`/`toggle` | 控制 PA02 用户灯 | `"led":0/1` |
| `period` | 100~60000 | 遥测周期（ms） | `"period":<ms>` |
| `cansend` | `0x123,11,22,33` | CAN0 发一帧（CAN1 收） | `"can_id":..,"can_len":..` |
| `keepalive` | 秒 | 改 keepalive（下次 CONNECT 生效） | `"keepalive":<s>` |
| `reconnect` | 任意 | 优雅断开后重连（**无**遗嘱） | `"reconnect":1` |
| `kill` | 任意 | `tcp_abort`（**有**遗嘱），1 s 后重连 | 无（连接已断） |
| `reboot` | 任意 | `ppor_sw_reset()` 软复位 | 无（来不及发） |
| `echo` | 任意 | 原样回 `hpm5e31/echo/reply`（QoS0） | 无 |
| `bench` | `<n>,<qos>` | 向 `hpm5e31/bench` 连发 n 条 | `hpm5e31/bench/done` |

shell 侧（letter-shell，`SHELL_TYPE_CMD_MAIN`）：

```text
mqtt                      # 状态 + 统计（连接/发布/重传/PING/丢弃/协议错误）
mqtt conn|close|kill      # 重连 / 优雅断开（无遗嘱）/ 直接断链（有遗嘱）
mqtt hex connect          # 打印 CONNECT 报文 HEX（表 37-6 的来源）
mqtt match                # 主题通配符 6 组自测（不依赖 broker）
mqtt cfg period 500 | qos 1 | keepalive 10
mqtt pub/sub/unsub/bench/stat/reset
mtele                     # 立即发一条遥测
mstat                     # 状态汇总 + PASS/FAIL 结论行
```

`mqtt match` 的输出（`logs/20261102-164024-mqtt_telemetry.log`，冒烟测试也用它）：

```text
  match(hpm5e31/cmd/#, hpm5e31/cmd/led) = 1
  match(hpm5e31/+/led, hpm5e31/cmd/led) = 1
  match(hpm5e31/+/led, hpm5e31/cmd/x/led) = 0
  match(hpm5e31/#, hpm5e31) = 1
  match(#, $SYS/broker/uptime) = 0
  match(a/+/c, a/b/c) = 1
```

主循环骨架只有 4 行（完整代码见 `samples/app/mqtt_telemetry/src/main.c`）：

```c
while (1) {
    bsp_shell_poll();
    app_lwip_poll();          /* ENET 收包 + lwIP 定时器 */
    app_mqtt_poll(&g_mq);     /* TCP 发送 / keepalive / QoS1 重发 / 重连 */
    /* 周期任务：ADC/温度/CAN → publish_telemetry() */
}
```

## 37.7 上位机工具：host/mqtt

**表 37-16 上位机脚本（全部只用 paho-mqtt + mosquitto-clients + 标准库）**

| 脚本 | 作用 | 实测证据 |
|---|---|---|
| `run_broker.sh` | 起停只监听 `192.168.2.1:1883` 的测试 broker | brokers 日志 `$HOME/tmp2/mosq_test.log` |
| `sub_check.py` | 订阅遥测/状态/回执，校验 1 Hz、seq、字段、retained | `logs/20261102-160244-host-mqtt-sub-check.json` |
| `pub_cmd.py` | 下行命令 + 回执 RTT + 回声 RTT | `logs/20261102-161551-host-mqtt-pub-echo.json` |
| `stress.py` | 1000 条吞吐 + 逐条 RTT（burst / `--rate` 限速） | `logs/20261102-162009-host-mqtt-stress.json` |
| `lwt_check.py` | LWT / 自动重连 / keepalive / 软复位 | `logs/20261102-1625*-host-mqtt-lwt-*.json` |
| `bench.sh` | 板→主机 QoS1 吞吐（C 客户端计数，避开 paho 瓶颈） | `logs/20261102-162341-mqtt_telemetry.log` |
| `run_suite.sh` | 一次持锁窗口按阶段跑完（basic/stress/lwt/keepalive/reboot/lwtall/bench） | 串口 + broker 切片 + pcap 自动归档 |
| `pcap_latency.py` | 把 tcpdump 文本拆成"上位机→broker / broker→板 / broker→上位机"三跳时延 | 38.9 的 41 ms 归因 |

一条命令跑完"烧写 + 串口 + 订阅校验 + 命令 + 回声"（整机锁内，避免日志串味）：

```bash
bash scripts/hwrun.sh app/mqtt_telemetry --secs 12 --send "mstat" --lock-wait 1800 \
     --post "bash host/mqtt/run_suite.sh basic"
```

## 37.8 上板实测

### 37.8.1 连接与遥测

**表 37-17 连接与 1 Hz 遥测（`logs/20261102-160244-host-mqtt-sub-check.json`）**

| 指标 | 实测值 | 说明 |
|---|---|---|
| TCP 建链 + CONNACK | 1 次成功，conn=1 / ok=1 | `logs/20261102-160000-mqtt_telemetry.log` 的 STAT 行 |
| keepalive 协商 | **k1080** | `logs/20261102-162529-host-mqtt-broker-lwtall.log`（`New client connected ... k1080`） |
| SUBSCRIBE | `hpm5e31/cmd/#` QoS1，SUBACK 正常 | broker 日志 |
| retained 在线状态 | 新订阅者**立刻**收到 `retain=1`、`online:true` | sub_check 首次回调 |
| 遥测速率 | **1.000 Hz**（10.00 s / 11 条） | 周期 1000 ms |
| 周期抖动 | 间隔 999.7~1000.3 ms（10 s 窗口） | host 控制台输出，未写入 JSON（U-38-12）；周期用绝对截止时刻调度，不累积误差 |
| 载荷大小 | **148 B**（含 JSON） | `{"dev":...}` |
| seq 连续性 | 0 缺口 | seq 递增校验 |
| 字段完整性 | 11 个字段全在 | dev/seq/up_ms/can_tx/can_rx/adc/temp_mc/loop_us/loop_max_us/mqtt_rx/mqtt_tx |
| CAN 计数 | can_tx=N、can_rx=N−1 | 先发后收，同一周期差 1 属正常 |
| 主循环耗时 | 常规 1.2~2.4 ms，bench 时 115 ms | `loop_max_us` 字段 |

### 37.8.2 时延

**表 37-18 时延实测（broker 开 `set_tcp_nodelay true`）**

| 测试 | 样本 | 结果 | 日志 |
|---|---|---|---|
| 命令回执（led） | 1 | **1.19 ms** | `logs/20261102-160246-host-mqtt-pub-led.json` |
| 命令回执（period） | 1 | 1.05 ms | `logs/20261102-160247-host-mqtt-pub-period.json` |
| 回声 RTT | 30 | avg **0.412** / p50 0.331 / p95 0.814 / max 1.17 ms | `logs/20261102-161551-host-mqtt-pub-echo.json` |
| 逐条 RTT（QoS1） | 200 | avg **0.271** / p50 0.282 / p95 0.345 / max 0.536 ms | `logs/20261102-162009-host-mqtt-stress.json` |
| 1000 条 burst 的 e2e | 1000 | avg 26.49 / p50 26.0 / p95 32.02 ms（含发布端排队） | 同上 |
| Nagle 未关时逐条 RTT | 200 | avg 47.06 / p50 40.93 ms | `logs/20261102-161106-host-mqtt-stress.json` |

![图 37-4 分跳时延拆解与关键指标](figures/37/fig-37-4-measured.png)

### 37.8.3 吞吐

**表 37-19 1000 条消息吞吐**

| 方向 / 模式 | 结果 | 计算 | 日志 |
|---|---|---|---|
| 板→主机 QoS1（板端自报，有日志） | **1000/1000，114 ms → 8 771 msg/s** | `[mqtt] BENCH n=1000 qos=1 sent=1000 ... rate=8771 msg/s => PASS` | `logs/20261102-161054-host-mqtt-serial-stress.log` |
| 板→主机 QoS1（主机侧 mosquitto_sub 计时，单次） | 1000/1000，55 ms → 18 179 msg/s（**仅供参考**） | 1000 条 / 0.055 s；该次 host 输出未单独归档（U-38-12） | 2026-11-02 16:23 hwrun `--post` 控制台 |
| 主机→板→主机回声 QoS1 | 1000/1000，0 丢 | 发布 1000 条用时 0.020 s（49 347 msg/s 是上位机写入速度） | `logs/20261102-162009-host-mqtt-stress.json` |
| 主机→板→主机回声 QoS0（限速 200 msg/s） | 739/1000，26.1% 丢 | 上位机 paho 单线程处理瓶颈，不是板端丢包（见 38.10 易错点 6） | `logs/20261102-162042-host-mqtt-stress.json` |

吞吐的瓶颈不在 MCU：板端自报 8 771 msg/s 对应单条 28 B 载荷约 **2.0 Mbit/s** 应用层速率（主机侧单次计时到过 18 179 msg/s，见 U-38-12），
而板端主循环在 bench 期间几乎全部时间在 `tcp_write`（`loop_max_us`=114 843 µs 就是整段 bench 的耗时）。
再往上要提速，得改发送策略（批量攒包/多连接），属于 38.13 的待确认项。

### 37.8.4 LWT、keepalive 与断线重连

**表 37-20 可靠性行为实测**

| 场景 | 操作 | 实测结果 | 日志 |
|---|---|---|---|
| abortive 断链（kill） | 板端 `tcp_abort()`，不发 DISCONNECT | broker **1.6 ms** 后发出遗嘱 `{"online":false,"reason":"lwt"}` | `logs/20261102-162530-host-mqtt-lwt-kill.json` |
| 断线重连 | 同上，板端 auto_reconnect | 1 s 退避后重新 CONNECT，重新 SUBSCRIBE，retained 在线状态恢复 | `logs/20261102-162529-host-mqtt-broker-lwtall.log` |
| 软复位（reboot） | `ppor_sw_reset()`，keepalive 已降为 10 s | broker **304 ms** 后发遗嘱；复位后重新 `online:true` | `logs/20261102-162620-host-mqtt-lwt-reboot.json` |
| 优雅断开（reconnect） | 先发 DISCONNECT 再重连 | broker **不发**遗嘱（协议规定） | 同上 broker 日志 |
| keepalive 保活 | keepalive=10 s，上行空闲 30 s | PINGREQ **3** 次 / PINGRESP **3** 次，无意外遗嘱 | `logs/20261102-162915-host-mqtt-lwt-keepalive.json` |
| keepalive 1080 s | 连接参数 | broker 记录 `k1080`，140 s 连续运行 0 次断链 | `logs/20261102-162529-host-mqtt-broker-lwtall.log`、`logs/20261102-161054-host-mqtt-serial-stress.log` |

## 37.9 性能与调优：41 ms 的 40 ms 卡在哪

第一版实测单条 MQTT 往返 **41 ms**，比 CAN 的 478 µs 慢两个数量级，而且非常稳定（p50 40.93 ms）。
用 `tcpdump` 在主机上按方向拆解（`host/mqtt/pcap_latency.py`）后，答案很清楚（表 37-21）：

**表 37-21 单条往返的三跳拆解（p50，pcap 实测）**

| 跳 | mosquitto 默认 | `set_tcp_nodelay true` | 结论 |
|---|---|---|---|
| 上位机 → broker（lo） | 0.14 ms | 0.01 ms | 本机回环，可忽略 |
| broker → 板（enp6s0） | 0.12 ms | 0.10 ms | 板端回环 0.1 ms 级，MCU 不慢 |
| **broker → 上位机（lo）** | **39.97 ms** | **0.02 ms** | **卡在 broker 发往订阅者的这一跳** |
| 单条 RTT（合计） | 41.0 ms | 0.282 ms | 40 ms 全在这一跳 |

根因是 **Nagle 算法 + Linux delayed ACK**：broker 的客户端 socket 默认开 Nagle，前一个几十字节的
PUBLISH 还在等 ACK 时，后一个小包被压住；而对端 Linux 对小包延迟 40 ms 才回 ACK，两者一叠加就是
"每条消息多 40 ms"。批量流水线时这个延迟被并发掩盖（1000 条 burst 的 e2e 只有 26 ms），
但一问一答的命令通道会被直接拖垮。

处理分两端（都做了）：

```c
/* 板端：TCP 建链回调里关 Nagle（components/app_mqtt/src/app_mqtt.c） */
tcp_nagle_disable(tpcb);
```

```text
# broker 端：测试实例配置（host/mqtt/mosquitto_test.conf）
set_tcp_nodelay true
```

效果：单条 RTT p50 从 **40.93 ms → 0.282 ms**（约 145 倍），命令回执从 42 ms 级降到 1 ms 级。
这条经验可以直接搬到真实车云网关：**MQTT 控制面一定要关 Nagle，数据面再考虑批量**。

## 37.10 易错点

1. **不清 Nagle 会多 40 ms**：现象是"每条命令都刚好 20~42 ms"；原因是 PUBACK 之后紧跟的小 PUBLISH 被 Nagle 压住，
   对端 delayed ACK 又等 40 ms。解决：板端 `tcp_nagle_disable()`，broker 侧 `set_tcp_nodelay true`（表 37-21）。
2. **接收缓冲必须 ≥ 一个 TCP MSS**：`APP_MQTT_RX_BUF` 原来是 1 KB，1000 条压测时一个 1460 B 的段进来就
   `rx_len+plen > 1024`，被判协议错误断链（broker 日志里两次 `Client hpm5e31 closed its connection`）。
   改成 2048 B 后消失。**这是本章最隐蔽的 bug**——单条测试永远碰不到。
3. **QoS1 窗口只有 8，-3 是正常背压**：bench 循环里 `app_mqtt_publish()` 返回 -3 时必须
   `app_lwip_poll()` 让 PUBACK 进来再重试，否则计数会瞬间打满（实测 fail=2885 次而 sent=1000）。
4. **剩余长度解码要防 4 字节以上**：恶意/错位数据可能给出 5 个延续位，必须判非法并断链，不能无限等。
5. **报文大于 512 B 会断链**：本组件用固定 512 B 报文上限，超长 PUBLISH 直接判协议错误（车云场景报文都小，
   但下行带文件就会遇到，见 38.13 U-38-8）。
6. **上位机 paho 自己是瓶颈**：1000 条瞬时压测时，Python 单线程回调处理不过来，e2e 时延被拖到 1.4 s，
   甚至触发 QoS0 丢包（739/1000）。测板端吞吐要用 C 客户端（`bench.sh` 的 `mosquitto_sub`）或 `--rate` 限速。
7. **keepalive 有流量就不会发 PINGREQ**：1 Hz 遥测下 keepalive=10 s 也测不到 PINGREQ，必须先把上行弄空闲
   （`period 60000`）再观察，否则误判"心跳没生效"。
8. **Ubuntu 的 mosquitto 被 AppArmor 限制**：profile 只允许读 `/etc/mosquitto/**`，用 `/tmp` 或 home 下的
   conf 会报 `Unable to open config file`。解决：把二进制复制到工作目录下运行（`run_broker.sh` 就这么做），
   不改系统策略、不动系统服务。
9. **`mqtt bench` 第一次可能只发出几十条**：板端首次 bench 时 `publish` 大量返回 -3（tx 队列满），
   板端自报 `sent=62~107 / fail=21001`；同参数第二次即可 1000/1000、18 179 msg/s。
   已定位到 lwIP 发送队列（`TCP_SND_QUEUELEN=16`）与 broker delayed ACK 的起步阶段叠加（U-38-5）。
10. **例程命令名别撞 BSP**：`stat`/`reboot` 之类可能已被 shell 占用，本示例统一加 `mqtt`/`m` 前缀。

## 37.11 对比：MQTT、MQTT-SN、CoAP、HTTP、SOME/IP

**表 37-22 车云/车内协议对比（★ 为本项目实测口径）**

| 协议 | 传输 | 头部开销 | 可靠性 | 订阅模型 | 适用场景 | 本项目 |
|---|---|---|---|---|---|---|
| **MQTT 3.1.1** | TCP | 最小 2 B | QoS0/1/2 | 主题树 + 通配符 + retained + LWT | 车云遥测、远程命令 | ★ 本章 |
| MQTT-SN | UDP | 最小 3 B | QoS0/1/2（+ 预定义主题） | 同 MQTT | 低功耗传感器、CAN 上跑 | 未做，见 U-38-11 |
| CoAP | UDP | 4 B | CON/NON + 重传 | 资源路径 + Observe | 受限设备、LWM2M | 未做 |
| HTTP/1.1 | TCP | 数百 B | TCP 保证 | 无（轮询/长轮询） | 文件、OTA、非实时 | 未做 |
| SOME/IP | UDP/TCP | 16 B | 应用层 RPC 错误码 | 服务/事件组 | 车内服务化 | 第 35 章 |
| XRCE-DDS | UDP | ~4 B | 流可靠/尽力 | DDS 实体 | ROS 2 机器人 | 第 36 章 |

选型结论：

- 车云链路首选 MQTT：穿透 NAT 强、broker 生态成熟、遗嘱/retained 直接对应"设备在线状态"这个运维刚需；
- 如果链路是 CAN 或极低功耗无线，考虑 MQTT-SN（UDP、支持预定义主题，报文更短）；
- 如果只是"命令-响应"且要求低延迟，CoAP 更轻；但多设备、多主题的遥测还是 MQTT 更顺；
- HTTP 不适合 1 Hz 遥测：头部开销和连接管理都太重（同一条 148 B 遥测，HTTP 至少要 300 B 头部）。

与 lwIP 自带 `mqtt.c` 的对比：它把连接状态机封在内部、回调只有 `connect/publish/subscribe` 三件套，
没有发送队列与显式退避；本章实现多了 4 个可控点（发送队列、QoS1 窗口、退避策略、状态回调），
代价是约 +400 行代码与 6.2 KB 静态 RAM。

## 37.12 参考来源

1. OASIS, *MQTT Version 3.1.1*, OASIS Standard, 2014-10-29 —
   <https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html>（§2 报文格式、§3 各报文、§4.7 主题通配符）
2. Eclipse Mosquitto 2.0.22 文档（`set_tcp_nodelay`、`listener`、`log_type`）—
   <https://mosquitto.org/man/mosquitto-conf-5.html>
3. lwIP 2.1.2 raw TCP API（`tcp_write`/`tcp_recved`/`tcp_nagle_disable`）—
   <https://www.nongnu.org/lwip/2_1_x/group__tcp__raw.html>
4. HPM SDK lwIP 自带 MQTT 客户端参考实现 —
   `$HOME/sdk_env_v1.13.0/hpm_sdk/middleware/lwip/src/apps/mqtt/mqtt.c`
5. paho-mqtt 2.1.0（上位机客户端，回调 API v2）— <https://pypi.org/project/paho-mqtt/>
6. 本书第 12 章（YT8011AN RGMII）、第 13 章（lwIP/iperf 935 Mbit/s 基线）、第 35 章（SOME/IP）、第 36 章（XRCE-DDS）
7. 实测证据（仓库内）：`logs/20261102-164024-mqtt_telemetry.log`（CONNECT hexdump）、
   `logs/20261102-160244-host-mqtt-sub-check.json`、`logs/20261102-162009-host-mqtt-stress.json`、
   `logs/20261102-161551-host-mqtt-pub-echo.json`、`logs/20261102-162530-host-mqtt-lwt-kill.json`、
   `logs/20261102-162915-host-mqtt-lwt-keepalive.json`、`logs/20261102-161054-host-mqtt-stress.txt`（默认 Nagle 抓包）、
   `logs/20261102-161616-host-mqtt-stress.txt`（TCP_NODELAY 抓包）

## 37.13 待确认项与风险

**表 37-23 待确认项（只允许 已实测 / 已定位 / 已知限制 三态）**

| 编号 | 项 | 状态 | 依据 / 复跑方式 |
|---|---|---|---|
| U-38-1 | TLS（MQTTS 8883）未实现 | 已知限制 | 需要 mbedTLS + 证书存储；本章只做明文 1883。复跑：暂无（未实现） |
| U-38-2 | QoS2 未实现 | 已知限制 | 收到 QoS2 PUBLISH 判协议错误断链；车云遥测用 QoS1 已足够。复跑：`mosquitto_pub -q 2 -t hpm5e31/cmd/led -m on` 观察板端 protocol_errors 增加 |
| U-38-3 | clean_session=0 会话持久化未实现 | 已知限制 | 组件固定 clean_session=1，断链后未确认消息丢弃（stats.drops 增加） |
| U-38-4 | 单条往返 41 ms（Nagle+delayed ACK） | **已实测** | 修复前后都有数字：40.93 ms → 0.282 ms（表 37-21）；pcap 分跳 broker→上位机 p50 39.97 ms→0.02 ms |
| U-38-5 | 首次 `mqtt bench` 只发出 62~107 条 | **已定位**（未完全消除） | lwIP `TCP_SND_QUEUELEN=16` + broker delayed ACK 起步叠加；第二次同参数 1000/1000。复跑：`bash host/mqtt/run_suite.sh bench` 连跑两次对比 |
| U-38-6 | keepalive=1080 s 未做 18 min 长测 | 已知限制 | 板卡整机独占纪律不允许单次 18 min；已用 "broker 记录 k1080 + 140 s 无断链 + keepalive=10 s 心跳 3/3" 组合佐证。复跑（单条命令，自带整机锁）：`bash scripts/hwrun.sh app/mqtt_telemetry --secs 1140 --send "mstat" --lock-wait 1800`，同时另开终端 `tail -f $HOME/tmp2/mosq_test.log` |
| U-38-7 | 不支持 broker 域名解析 | 已知限制 | `app_mqtt_connect` 只接受 IPv4 点分十进制；要域名需 `dns_gethostbyname` 异步解析（lwIP 已开 DNS 即可加） |
| U-38-8 | 单报文上限 512 B | 已知限制 | 超过即判协议错误断链；下行大文件建议走 HTTPS/分片。复跑：`mosquitto_pub -t hpm5e31/cmd/echo -s` 发 1 KB 观察断链 |
| U-38-9 | 单向绝对时延未测 | 已知限制 | 只有 RTT（往返）；绝对时延需要 PTP 对时（第 38 章）或主机侧纳秒时间戳。复跑见第 38 章 gPTP |
| U-38-10 | 未做 broker 侧持久化/集群 | 已知限制 | 测试实例 `persistence false`；生产需要持久化 + 多 broker + 桥接 |
| U-38-11 | MQTT-SN 未实现 | 已知限制 | 本章只做 MQTT 3.1.1 over TCP；MQTT-SN 需要 UDP 传输 + 预定义主题映射，属另一条实现路线 |
| U-38-12 | 主机侧部分控制台输出未归档 | 已知限制 | 18 179 msg/s（单次计时）与周期抖动 999.7~1000.3 ms 只出现在 hwrun `--post` 控制台；正式口径用板端自报 8 771 msg/s 与 sub_check JSON 的 1.000 Hz。复跑：`bash host/mqtt/bench.sh 1000 1 1 \| tee logs/\$(date +%Y%m%d-%H%M%S)-host-mqtt-bench.log` |
