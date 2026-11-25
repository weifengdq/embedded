# 第 36 章 XRCE-DDS 与 ROS 2 集成

## 36.1 文档信息

**表 36-1 文档信息表**

| 项 | 内容 |
|---|---|
| 章号 / 版本 | 第 36 章，v1.0（2026-10-30） |
| 目标硬件 | hpm5e31_1（HPM5E31IPB1，RV32IMAC @480 MHz，片内 1 MB Flash / 128 KB DLM） |
| 板端软件 | Micro-XRCE-DDS Client **v3.0.2**（commit `6884d7b`）+ Micro-CDR **v2.0.2**，lwIP 2.1.2（NO_SYS=1），YT8011AN RGMII 千兆 |
| 上位机软件 | Micro-XRCE-DDS Agent **v3.0.2**（commit `a88c71`）+ ROS 2 **Lyrical**（Fast DDS 3.6.2 / Fast CDR 2.3.6） |
| 网络 | 板 `192.168.2.100/24` ↔ 主机 `192.168.2.1/24`，XRCE over UDP，Agent 端口 **2018** |
| 例程 | `samples/app/xrce_pubsub`、`samples/app/xrce_ros2_bridge` |
| 上位机工具 | `host/xrce/`（build_agent.sh / run_agent.sh / ros2_adc_sub.py / ros2_cmd_pub.py / test_ros2_bridge.sh） |
| 组件 | `components/app_xrce/`（含 third_party 源码与 LICENSE） |
| 实测日志 | 串口：`logs/20261030-172701-xrce_pubsub.log`、`logs/20261030-174309-xrce_ros2_bridge.log`；ROS 2 侧（hwrun --post 阶段完整输出）：`logs/20261030-172701-host-ros2-xrce_pubsub.log`、`logs/20261030-174309-host-ros2-xrce_ros2_bridge.log` |
| 实测日期 | 2026-10-30（板端串口 + ROS 2 双侧取证） |
| 可信度 | 高：频率/丢帧/RTT 均为仪表化实测；单向绝对时延未测（见 U-36-1） |
| 对应例程 | `samples/app/xrce_pubsub`、`samples/app/xrce_ros2_bridge`、`host/xrce/` |

> 本章所有数字都来自上板实测；未测到的项集中登记在 36.12「待确认项与风险」。

## 36.2 本章导览

本章回答一个具体问题：**一块只有裸机 lwIP 的 MCU，怎样把数据直接发布到 ROS 2 话题上，并接收 ROS 2 的控制命令？**
答案不是"在 MCU 上跑 DDS"，而是 **XRCE-DDS**（DDS for eXtremely Resource Constrained Environments，OMG DDS-XRCE 规范）：
MCU 里只放一个一百多 KB 的**客户端**，由主机上的 **Agent** 替它完成 DDS/RTPS 的发现、QoS 与序列化传输。

阅读顺序建议：

1. 36.3 概述：为什么不用"直接把 DDS 塞进 MCU"；
2. 36.4 工作原理：分层、会话/流、实体与对象 ID、请求/响应、CDR 与 ROS 2 类型名映射（本章核心）；
3. 36.5 寄存器与驱动 API 对照：XRCE 的每个动作最终落在 ENET/lwIP 的哪一层；
4. 36.6 例程：两个例程怎么接线、怎么跑；
5. 36.7 实测 / 36.8 性能：100 Hz 发布、0.10% 丢帧、2.3~3.4 ms 命令往返；
6. 36.9 易错点 / 36.10 对比 / 36.12 待确认项：遇到过的 10 个问题（表 36-12）与残留风险（表 36-14）。

## 36.3 概述

### 36.3.1 嵌入式上 DDS 的三条路

**表 36-2 嵌入式 DDS 三条实现路线对比**

| 方案 | 资源需求 | 发现机制 | 适用场景 |
|---|---|---|---|
| 完整 DDS（Fast DDS / Cyclone DDS） | 数 MB Flash、数百 KB RAM、需要 C++/动态内存/OS | RTPS 组播发现 | Linux / 高性能 SoC |
| **XRCE-DDS（本章）** | 客户端 ~126 KB Flash、~10 KB RAM、无 OS、无堆 | Client→Agent 单播 UDP，Agent 代做发现 | MCU + 主机/网关 |
| 自定义 UDP + 手写序列化 | 最小 | 无 | 单点私有协议 |

三条路线的取舍见表 36-2。

XRCE 的关键设计是**把 DDS 的"重"部分搬到 Agent**：MCU 只实现一个精简的 **Client**，
通过 XRCE 协议（创建实体 / 写数据 / 读数据 / 删除实体 + 心跳）与 Agent 通信；
Agent 在主机上把这些请求翻译成真正的 DDS 实体（Participant/Topic/DataWriter/DataReader），
于是 **ROS 2 节点看到的就和自己发布的 DDS 话题完全一样**。

### 36.3.2 本项目的落地形态

- 板端：`components/app_xrce/`（Client + lwIP UDP custom transport + 适配层），**无堆、静态内存**；
- Agent：`host/xrce/build_agent.sh` 用 ref2 源码 v3.0.2 + 系统 Fast DDS 3.6.2 构建；
- ROS 2：`rclpy` 订阅 `/hpm5e31/adc`（`std_msgs/UInt16MultiArray`）、发布 `/hpm5e31/cmd`（`std_msgs/UInt8`）。

## 36.4 工作原理

### 36.4.1 分层：DDS / RTPS / XRCE

![图 36-1 XRCE-DDS 在 ROS 2 与 MCU 之间的分层架构](figures/36/fig-36-1-arch.png)

自右向左看：

- **ROS 2 节点**：用 `rclpy`/`rclcpp` 编程，只认识 DDS 话题与 QoS；
- **Agent**：既是一个 DDS Participant（用 Fast DDS 3.6.2 加入 DDS 域），又是一个 XRCE 服务端（UDP 2018）；
- **网络**：只跑 UDP，一条 XRCE 报文 ≤512 B（与 `UXR_CONFIG_CUSTOM_TRANSPORT_MTU` 一致）；
- **板端 Client**：把"发布一个样本"翻译成 `WRITE_DATA` 子消息，把"订阅"翻译成 `READ_DATA`。

DDS 侧真正的线上协议是 **RTPS**（Real-Time Publish-Subscribe），XRCE 只是"MCU 与 Agent 之间"的精简协议，
所以**章节标题里的 DDS/RTPS/XRCE 三个词分别对应：ROS 2 的数据模型 / Agent 的线上协议 / MCU 的接入协议**。

### 36.4.2 会话与流（Session / Stream）

一个 Client 与 Agent 之间只有一个 **Session**（由 `client_key` 标识，本工程 `0x484D5035`）。
Session 内可以有多条**流（Stream）**，每条流是一个单字节 ID（表 36-3）：

**表 36-3 XRCE 流的类型与 ID 分配**

| 流类型 | ID 范围 | 语义 | 本工程用法 |
|---|---|---|---|
| none（Best-Effort 的特例） | `0x00` | 无保证、无序号 | 未用 |
| best-effort | `0x01`~`0x7F` | 有序、可能丢 | ADC 100 Hz 数据（`0x01`） |
| reliable | `0x80`~`0xFF` | 有序、带 ACK/NACK 与重传 | hello 10 Hz、实体创建、命令回执（`0x80`） |

可靠流的工作方式：Client 给每个 `WRITE_DATA` 编号，Agent 用 `ACKNACK` 确认；
超过窗口未确认则由 HEARTBEAT 触发重传。**这就是板上 `acked/retrans` 两个统计量的来源**。

### 36.4.3 实体与对象 ID

![图 36-2 XRCE 实体模型与对象 ID/流 ID 的对应关系](figures/36/fig-36-2-entities.png)

XRCE 实体与 DDS 实体一一对应，用 **2 字节对象 ID**（`ID 域 + 类型域`）寻址（表 36-4）：

**表 36-4 XRCE 实体与对象 ID**

| XRCE 实体 | 类型域常量 | DDS 对应物 | 本工程对象 ID |
|---|---|---|---|
| Participant | `0x01` | DomainParticipant | `0x01/0x01` |
| Topic | `0x02` | Topic | `0x01/0x02`、`0x02/0x02`、`0x03/0x02` |
| Publisher | `0x03` | Publisher | `0x01/0x03` |
| Subscriber | `0x04` | Subscriber | `0x01/0x04` |
| DataWriter | `0x05` | DataWriter | `0x01/0x05`、`0x02/0x05` |
| DataReader | `0x06` | DataReader | `0x03/0x06` |

MCU 侧只维护"对象 ID → 请求号"的映射，所有状态都是静态数组（`app_xrce.c` 的 `s_pubs[]`/`s_subs[]`）。

### 36.4.4 请求/响应（CREATE / WRITE / READ / DELETE）

![图 36-3 一次会话的 XRCE 请求/响应时序](figures/36/fig-36-3-seq.png)

所有操作都是 **Client 发子消息 → Agent 回 STATUS** 的请求/响应模型（表 36-5）：

**表 36-5 XRCE 子消息与客户端 API 对应**

| 子消息 | 方向 | 作用 | 本工程调用 |
|---|---|---|---|
| `CREATE_CLIENT` | C→A | 建会话，携带 `client_key` 与 `session_id` | `uxr_create_session()` |
| `CREATE` | C→A | 建实体，三种表示：by reference / XML / **binary** | `uxr_buffer_create_*_bin()` |
| `WRITE_DATA` | C→A | 发布一个样本（载荷为 CDR 体） | `uxr_prepare_output_stream()` |
| `READ_DATA` | C→A | 请求订阅数据（可带 `max_samples` 等投递控制） | `uxr_buffer_request_data()` |
| `DATA` | A→C | 订阅数据回传 | `on_topic` 回调 |
| `DELETE` | C→A | 删实体/会话 | `uxr_delete_session()` |
| `HEARTBEAT` / `ACKNACK` | 双向 | 可靠流的确认与重传 | 客户端内部自动 |

> **本工程选 binary 表示**（`REPRESENTATION_IN_BINARY`），因为这是 micro-ROS 走的同一条 Agent 代码路径
> （`FastDDSMiddleware::create_topic_by_bin`），topic 名与类型名会**原样**成为 DDS 名字，不需要 Agent 上的 XML 配置。

### 36.4.5 CDR 序列化与 ROS 2 类型名

板端不链接任何 IDL 生成代码，**CDR 由应用手写**（`samples/app/xrce_pubsub/src/main.c` 的 `cdr_encode_*`）。
以 `std_msgs/UInt16MultiArray` 为例（20 字节）：

```c
/* MultiArrayLayout: dim 序列长度 0、data_offset 0；data: 序列长度 + 4×u16 */
static uint32_t cdr_u16_multiarray(uint8_t *buf, const uint16_t *v, uint32_t n)
{
    uint32_t o = 0;
    put_u32le(&buf[o], 0U); o += 4U;   /* layout.dim.size()  = 0 */
    put_u32le(&buf[o], 0U); o += 4U;   /* layout.data_offset = 0 */
    put_u32le(&buf[o], n);  o += 4U;   /* data.size()        = 4 */
    for (uint32_t i = 0; i < n; i++) { /* 小端 u16，自然对齐 */
        buf[o++] = (uint8_t)(v[i] & 0xFFU);
        buf[o++] = (uint8_t)((v[i] >> 8) & 0xFFU);
    }
    return o;                          /* 20 字节 */
}
```

两个容易出错的点：

1. **不要自己加 4 字节封装头**。Agent 的 `TopicPubSubType::serialize()` 会在载荷前插入
   `00 01 00 00`（CDR_LE 封装），收到的 `DATA` 也会先剥掉这 4 字节再回调。板上抓到的
   `WRITE_DATA` 载荷是 `00 00 00 00 00 00 00 00 04 00 00 00 ...`（正是"无头"的 20 字节）。
2. **topic 名要带 `rt/` 前缀**。ROS 2 的 `rmw_fastrtps` 把 `/hpm5e31/adc` 映射为 DDS 话题
   `rt/hpm5e31/adc`，类型名是 `std_msgs::msg::dds_::UInt16MultiArray_`；板端两者都必须写成 DDS 形式，
   Agent 才会在同一 DDS 域里与 ROS 2 节点匹配。

### 36.4.6 Agent 到 ROS 2 的映射

**表 36-6 Agent 内部实体与 ROS 2 可见对象**

映射关系见表 36-6。

| 板端（XRCE） | Agent 内部 | ROS 2 侧可见 |
|---|---|---|
| Participant（domain 0） | `FastDDSParticipant` | 同一 `ROS_DOMAIN_ID` 的 Participant |
| Topic(`rt/hpm5e31/adc`, `UInt16MultiArray_`) | `FastDDSTopic` + `TopicPubSubType` | `/hpm5e31/adc`，类型 `std_msgs/msg/UInt16MultiArray` |
| DataWriter（RELIABLE, depth 8） | `FastDDSDataWriter` | 发布者（`ros2 topic info -v` 可见） |
| DataReader（BEST_EFFORT） | `FastDDSDataReader` | 订阅者（`/hpm5e31/cmd` 的对应 reader） |

## 36.5 寄存器与驱动 API 对照

XRCE 是应用层协议，寄存器层面只落在 ENET/lwIP 上；表 36-7 把"协议动作 → 网络层 → 驱动/寄存器 → 代码位置"串起来：

**表 36-7 协议字段 ↔ 网络层 ↔ 驱动/寄存器 ↔ 代码位置对照**

| XRCE 动作/字段 | 网络层表现 | 驱动 / 寄存器 / 协议字段 | 代码位置 |
|---|---|---|---|
| `CREATE_CLIENT`（24 B） | 1 个 UDP 数据报，源端口 2019 | `udp_send()` → ARP + ENET TX 描述符（TBS1/INT） | `app_xrce_transport.c: xrce_udp_write()` |
| `WRITE_DATA` 载荷 | 48~64 B UDP，100 Hz | lwIP `pbuf_alloc(PBUF_TRANSPORT)` + `pbuf_take()` | 同上 |
| 接收 `STATUS`/`DATA` | 以太网帧 → pbuf 链 | ENET RX 描述符（RBS1/RDES1）→ `ethernetif_input()` | `common.c: enet_common_handler()` |
| 接收回调 | pbuf → 4 槽环形缓冲 | `udp_recv()` 回调 + `pbuf_copy_partial()` | `app_xrce_transport.c: xrce_udp_recv_cb()` |
| `MTU=512` | 单个 UDP 载荷上限 | `UXR_CONFIG_CUSTOM_TRANSPORT_MTU`，超过需分片 | `third_party/microxrcedds_client/include/uxr/client/config.h` |
| 会话 ID / client_key | 报文头 8 字节 | XRCE 报文头（session_id, stream_id, seq, key） | 上游 `xrce_header.c` |
| 流序号 / ACK | HEARTBEAT 报文 | `uxrOutputReliableStream.last_sent/last_acknown` | 统计读于 `app_xrce.c: app_xrce_poll()` |
| 定时/心跳节拍 | 1 ms 轮询 | `mcycle` CSR（480 MHz）→ `uxr_millis()` | `app_xrce_platform.c` |
| 链路与速率 | 1000BASE-T1 | YT8011AN（MDIO addr1，PHYSR=0x8400） | `samples/app/*/src/phy_glue.c` |

## 36.6 例程

两个例程共用 `components/app_xrce`，差别只在实体集合与话题命名（表 36-8），板端 shell 命令见表 36-9。

**表 36-8 例程一览**

| 例程 | 路径 | 覆盖特性 | 关键配置 | 实测现象 | 日志 |
|---|---|---|---|---|---|
| xrce_pubsub | `samples/app/xrce_pubsub/` | 2 发布 + 1 订阅；可靠流与 BE 流并存；ACK/重传统计 | hello 10 Hz（可靠流 0x80）、adc 100 Hz（BE 流 0x01）、cmd 订阅 | ROS 2 侧 `/hpm5e31/adc` 100 Hz、`/hpm5e31/hello` 10 Hz、`/hpm5e31/cmd` 可控 LED | `logs/20261030-172701-xrce_pubsub.log` |
| xrce_ros2_bridge | `samples/app/xrce_ros2_bridge/` | 面向 ROS 2 的裁剪版：adc/cmd/status 三实体；状态串带板端计数 | 本地端口 2020、`client_key=0x42524944`、status 1 Hz | ROS 2 侧 100.07 Hz、0 丢帧；`ros2 topic echo /hpm5e31/status` 读到 `adc=6300 cmd=6 ack=63 up=65067` | `logs/20261030-174309-xrce_ros2_bridge.log` |

运行步骤（Bench C：主机 `enp6s0` = 192.168.2.1）：

```bash
# 1) 主机：构建并启动 Agent（只需一次构建）
bash host/xrce/build_agent.sh
bash host/xrce/run_agent.sh -d          # 后台，UDP4 2018

# 2) 上板：烧写 + 抓 25 s 串口 + 串口抓完仍持锁跑 ROS 2 验证（一次占锁完成双侧取证）
cd hpm5e31_1
bash scripts/hwrun.sh app/xrce_pubsub --secs 25 --send "stat" \
     --post "timeout 75 bash host/xrce/test_ros2_bridge.sh" --lock-wait 1800

# 3) 单独看 ROS 2 侧
source /opt/ros/lyrical/setup.bash
ros2 topic list | grep hpm5e31
ros2 topic hz /hpm5e31/adc
ros2 topic echo /hpm5e31/adc --once
python3 host/xrce/ros2_adc_sub.py --duration 10
python3 host/xrce/ros2_cmd_pub.py --value 2 --repeat 6
```

板端 shell 命令（letter-shell）：

**表 36-9 板端 shell 命令**

| 命令 | 作用 |
|---|---|
| `xrce` | 打印会话/实体/统计（pubsub 例程） |
| `stat` | 打印计数器与一行 `STAT ... => PASS/FAIL` 结论 |
| `pub <text>` | 手动发一条 String（调试通道是否活着） |
| `rst` | 清零计数器 |
| `brstat` / `brrst` | 桥接例程的统计/清零 |

## 36.7 实测

### 36.7.1 板端串口（发布侧）

```text
[xrce] state=connected up=22074 ms hello=200 adc=2000 cmd=0 led=0
[xrce] tx: samples=2200 dg=2214 bytes=70126 drop=0 err=2 | rx: dg=215 bytes=2810 samples=0 ovr=0
[xrce] cpu0=480 MHz bsp_up=22128 ms loop=904/s adc_cost=0 us poll_cost=999 us
[xrce] reliable: acked=204 retrans=0 reconnects=0 pub_err=0
[xrce] STAT hello=9.1/s adc=90.6/s acked=204 => PASS
```

> 板端 `adc=90.6/s` 的分母是整段 uptime（含约 2 s 的建会话/建实体时间），所以偏小；
> **权威频率以 ROS 2 侧 `ros2 topic hz` 的 100 Hz 为准**（见下表）。

### 36.7.2 ROS 2 侧（接收侧）

本节数字由 `host/xrce/test_ros2_bridge.sh` 在 `hwrun --post` 阶段采集，**完整命令输出已归档**到
`logs/20261030-172701-host-ros2-xrce_pubsub.log`（与串口日志 `logs/20261030-172701-xrce_pubsub.log` 成对）。

**表 36-10 ROS 2 侧接收指标实测**

ROS 2 侧接收指标见表 36-10。

| 指标 | 实测值 | 采集方式 |
|---|---|---|
| `/hpm5e31/adc` 频率（3 个 hz 窗口） | **100.25 / 99.78 / 99.85 Hz** | `ros2 topic hz --window 40` |
| 到达间隔 | min 9 ms / max 11 ms / σ≈0.20~0.26 ms | 同上 |
| 10 s 订阅统计 | **991 样本 / 9.91 s = 99.89 Hz**，抖动 σ=0.49 ms | `ros2_adc_sub.py` |
| 计数器反推丢帧 | **1 帧 / 991（0.10%）** | 第 4 通道计数器差值 |
| 命令往返 RTT（`/cmd` → 板端 → `/hello` ack） | **min 2.29 / 中位 2.70 / max 3.44 ms** | `ros2_cmd_pub.py` |
| 单向时延上界（RTT/2） | **1.72 ms** | 同上（未做时间同步，见 36.12） |
| LED 控制 | 6 次命令收到 5 次 ack；串口 `[cmd] rx UInt8=2 -> LED ON/OFF` 同步变化 | 串口日志 + `ros2_cmd_pub.py` |
| `/hpm5e31/adc` 内容 | `layout.dim=[]`, `data=[34932,28398,20428,4969]`（3 路 ADC + 序号） | `ros2 topic echo --once` |

### 36.7.3 桥接固件（xrce_ros2_bridge）双侧复测

完整 ROS 2 侧输出：`logs/20261030-174309-host-ros2-xrce_ros2_bridge.log`；串口侧：`logs/20261030-174309-xrce_ros2_bridge.log`。

| 指标 | 实测值 | 说明 |
|---|---|---|
| 板端自报 ADC 频率 | 101.1 → 100.5 → 100.3 Hz（3 / 7 / 12 s 窗口） | 串口 `[bridge] STAT adc_hz=...`，分母已扣除建会话时间 |
| `ros2 topic hz` | **100.07 / 99.93 / 100.06 Hz** | 3 个窗口 |
| 10 s 订阅统计 | 994 样本 = **100.01 Hz**，抖动 σ=0.29 ms，**丢帧 0.00%** | `ros2_adc_sub.py` |
| 命令往返 RTT | **2.51 / 3.40 / 3.48 ms**（min/中位/max），6 发 6 ack | `ros2_cmd_pub.py --ack-topic /hpm5e31/status` |
| 单向时延上界 | **1.74 ms** | RTT/2 |
| status 话题内容 | `adc=6300 cmd=6 ack=63 up=65067` | 板端计数与 ROS 2 侧命令次数一致（6 次） |
| 串口 | 12 s 时 `tx dg=1528 bytes=49217 drop=0 retrans=0` | 无丢包、无重传 |

![图 36-4 实测频率与命令往返时延](figures/36/fig-36-4-measured.png)

## 36.8 性能

资源与性能实测汇总见表 36-11。

**表 36-11 资源与性能实测**

| 指标 | 数值 | 说明 |
|---|---|---|
| 发布频率 | 100 Hz（10 ms 周期，实测 99.78~100.25 Hz） | ADC 3 路 oneshot 采样 + 发布 |
| 单样本开销 | ADC 3 路 < 100 µs；CDR 编码 ~2 µs；`WRITE_DATA` 入流 ~10 µs | 主循环其余时间被 1 ms 会话监听占满 |
| 主循环频率 | **904 Hz**（1.1 ms/轮） | 其中 `app_xrce_poll()` ≈ 999 µs（`uxr_run_session_time(1)`） |
| Flash | pubsub `text=128792 B`（125.8 KB）；bridge `text=127008 B` | 参考 `enet_lwip_iperf` 为 124296 B，即 **XRCE 客户端净增约 3.5 KB** |
| 静态 RAM | `.bss=115016 B`（112.3 KB） | 应用自用：可靠流 4 KB + BE 流 1 KB + 接收环 2 KB + 实体状态 <1 KB |
| 堆（malloc） | **XRCE 层 0 次** | Client/microcdr/适配层无 `malloc`；仅 lwIP 使用其静态内存池 |
| 带宽 | 80 B/样本 × 100 Hz ≈ **64 kbit/s**（含 UDP/IP 头约 110 kbit/s） | 千兆链路占用可忽略 |
| 可靠流重传 | 25 s 内 `retrans=0` | 有线直连无丢包 |

结论：**XRCE 客户端对资源的消耗主要不是算力，而是"每个主循环 1 ms 的会话监听"**；
如果应用需要更细的实时控制，可以把 `uxr_run_session_time(&session, 1)` 的超时改小，
或改成"每 N 轮跑一次会话"（本工程已把会话等待改成可配 `poll_timeout_ms`，见 36.12 U-36-5）。

## 36.9 易错点

表 36-12 是本章遇到过的 10 个问题，按"现象 → 根因 → 解决"排列。

**表 36-12 易错点清单**

| # | 易错点 | 现象 | 根因 | 解决 |
|---|---|---|---|---|
| 1 | Agent 依赖 spdlog 1.9.2 | 用系统 spdlog 1.15 编译 Agent → `static assertion failed: Cannot format an argument` | Agent 3.0.2 用 `fmt::format` 直接格式化 `CustomEndPoint`，fmt 10 取消了 ostream 回退 | 编译 spdlog 1.9.2（自带 fmt 8）并 `UAGENT_USE_SYSTEM_LOGGER=ON` |
| 2 | spdlog 静态库必须 PIC | 链接 `libmicroxrcedds_agent.so` 报 `relocation R_X86_64_TPOFF32 ... local-exec` | 静态库默认不开 `-fPIC` | `-DCMAKE_POSITION_INDEPENDENT_CODE=ON` 重编 |
| 3 | Agent 的 P2P/Discovery profile 依赖 microxrcedds_client | cmake 报 `Could not find microxrcedds_clientConfig.cmake` | 这两个 profile 需要客户端库（v3.0.1） | `-DUAGENT_P2P_PROFILE=OFF -DUAGENT_DISCOVERY_PROFILE=OFF` |
| 4 | BSP 的 `bsp_uptime_ms()` 曾会丢精度 | 周期任务实际只有配置值的一半 | 旧实现每次调用都做 `ms += (t-last)*1000/cpu`，**每次截断**，调用越频繁损失越大（主循环 <1 ms 时甚至不涨） | 团队已把 `boards/hpm5e31_1/bsp/bsp.c` 改为 mcycle 绝对时基（2026-10-30）；本组件仍保留自带 `app_xrce_now_ms()`，以便换板时不依赖 BSP |
| 5 | "now - last >= period" 会多丢一个主循环 | 10 ms 周期实测 12.2 ms（82 Hz） | 判断发生时已过去一个循环周期 | 改**绝对截止时刻** `next += period` |
| 6 | CDR 封装头重复 | ROS 2 侧字段全错位 | 板端若自己加 `00 01 00 00`，Agent 会再加一次 | 板端只发"CDR 体" |
| 7 | topic 名缺 `rt/` 前缀 | `ros2 topic list` 看不到、QoS 匹配失败 | ROS 2 的 DDS 话题名是 `rt/<topic>` | 板端 topic 名直接写 `rt/hpm5e31/xxx` |
| 8 | QoS 不匹配 | `ros2 topic hz` 一直是 0，但 Agent 日志显示 DDS write 成功 | 板端 DataWriter 若是 BEST_EFFORT，默认 RELIABLE 的 ROS 2 订阅者不匹配 | DataWriter QoS 设 `RELIABLE`（数据仍可走 BE 流省窗口） |
| 9 | `READ_DATA` 是一次性的 | 只收到第一条 cmd，之后收不到 | Agent 完成一次读请求后不再推送 | 在 `on_topic` 回调里立即重新 `uxr_buffer_request_data()` |
| 10 | KEEP_LAST 深度太小 | 高频话题偶发丢帧 | 深度 1 时 Agent 侧只保留最新一帧 | DataWriter/Reader `depth=8` |

## 36.10 对比

**表 36-13 四种应用层协议横向对比**

| 维度 | XRCE-DDS（本章） | SOME/IP（第 35 章） | MQTT | 自定义 UDP |
|---|---|---|---|---|
| 服务发现 | Agent 代做 RTPS 发现，MCU 无感知 | SD 报文（UDP 30490 组播） | Broker 订阅关系 | 无 |
| 数据模型 | DDS 话题 + 丰富 QoS（可靠性/历史/期限） | 方法/事件/字段 | 主题字符串 | 自定义 |
| ROS 2 原生互通 | **是**（ROS 2 的标准 MCU 接入方式） | 需桥接 | 需 bridge | 需自写节点 |
| MCU 资源 | 中（126 KB Flash / ~10 KB RAM / 无堆） | 低（~20 KB） | 低 | 最低 |
| 端到端延迟 | 实测命令往返 2.3~3.4 ms | 亚毫秒级（同网段） | 取决于 Broker | 亚毫秒级 |
| 依赖 | 需要主机跑 Agent + Fast DDS | 无 | 需要 Broker | 无 |
| 适用 | 机器人/ROS 2 生态、需要 QoS 与工具链 | 车载 SOA | 云边上报 | 极简私有链路 |

四种协议的横向对比见表 36-13。选择建议：**要不要接 ROS 2 生态**是第一判据——要接就用 XRCE（本章）；纯车载 SOA 用 SOME/IP；
跨公网/云端上报用 MQTT；只有两三个固定报文、且对端也自己写，才用自定义 UDP。

## 36.11 参考来源

- OMG **DDS-XRCE 1.0** 规范（DDS for eXtremely Resource Constrained Environments）：
  <https://www.omg.org/spec/DDS-XRCE/1.0/>
- eProsima **Micro-XRCE-DDS Client** v3.0.2：<https://github.com/eProsima/Micro-XRCE-DDS-Client>
  （关键文件 `examples/CustomTransports`、`include/uxr/client/profile/transport/custom/custom_transport.h`）
- eProsima **Micro-XRCE-DDS Agent** v3.0.2：<https://github.com/eProsima/Micro-XRCE-DDS-Agent>
  （关键文件 `src/cpp/middleware/fastdds/FastDDSMiddleware.cpp`、`src/cpp/types/TopicPubSubType.cpp`）
- eProsima **Micro-CDR** v2.0.2（`ucdr` CDR 编解码）：<https://github.com/eProsima/Micro-CDR>
- ROS 2 Lyrical 文档：<https://docs.ros.org/en/lyrical/>（`rmw_fastrtps` 话题/类型名映射、`std_msgs` 消息定义）
- 本仓库：第 12 章（YT8011AN RGMII）、第 13 章（lwIP/iperf）、`docs/应用层开发约定.md` §6（端口分配）
- 文档站：<https://micro-xrce-dds.docs.eprosima.com/>；三件套与 Fast DDS 的仓库清单见第 27 章表 27-10
- 实测日志：`logs/20261030-172701-xrce_pubsub.log`、`logs/20261030-172211-xrce_pubsub.log`、`logs/20261030-174309-xrce_ros2_bridge.log`；ROS 2 侧：`logs/20261030-172701-host-ros2-xrce_pubsub.log`、`logs/20261030-174309-host-ros2-xrce_ros2_bridge.log`

## 36.12 待确认项与风险

剩余风险与验证方法汇总在表 36-14。

**表 36-14 待确认项与风险清单**

> 已逐条复核（2026-11-02）：能测的给实测，受共享板卡窗口限制的写明原因与验证命令。

| # | 项 | 现状 | 验证方法 |
|---|---|---|---|
| U-36-1 | 单向绝对时延 | **已知限制（原因：板端与主机还没有共同时基）**：只有命令往返 RTT/2 ≤1.72 ms 的上界 | 接第 38 章 PTP 让两侧共时基后比对单向时延；或板端回传主机时间戳 |
| U-36-2 | 第 4 路 ADC 通道 | 已定位（板上可用 3 路 ADC 输入：PF26/PF28/PF30；第 4 路用采样序号） | 按 DS Rev0.4 引脚表确认其余 ADC0_INx 引出焊盘后改 `ADC_CHx` 宏并复测 |
| U-36-3 | 高频下的丢帧边界 | **已知限制（已具备测法，未取数）**：`ADC_PERIOD_MS` 支持命令行覆盖（`-DXRCE_ADC_PERIOD_MS=2/1`，`build/xrce_pubsub-500hz` 已编译通过）；本轮共享板卡排队，未取得 500 Hz/1 kHz 实测。 | 烧 500 Hz 版后跑 `python3 host/xrce/ros2_adc_sub.py`，看丢帧率与 `retrans`；与 100 Hz 的 0.10% 对比 |
| U-36-4 | 断线重连 | **已知限制（已就绪未实测，2026-11-02）**：5 s 无 ACK 判定掉线 → 2 s 重试的固件逻辑未改；一次性验证脚本 `host/xrce/r5_w2_xrce.sh`（kill Agent → 8 s → 重启 → `ros2 topic hz`）已写好。本轮板卡排队未取得 `reconnects` 增量。 | `bash scripts/hwrun.sh app/xrce_pubsub --secs 45 --post "bash host/xrce/r5_w2_xrce.sh"`；判据：串口 `reconnects` +1、ROS 2 频率恢复 100 Hz |
| U-36-5 | `poll_cost ≈ 1 ms` 的实时性影响 | **已知限制（已改为可配置但未测，2026-11-02）**：`app_xrce_cfg_t` 新增 `poll_timeout_ms`，`uxr_run_session_time()` 不再硬编码 1 ms；两个样本显式填 1（回归行为不变），填 0 即事件驱动。本轮板卡窗口未排上，改后的 `loop=`/`poll_cost=` 未测。 | 把 `cfg.poll_timeout_ms` 改 0 重编译后 `bash scripts/hwrun.sh app/xrce_pubsub --secs 30 --send stat`，与改前 `loop=904/s poll_cost=999 us` 对比 |
| U-36-6 | Agent 高负载下的表现 | **已知限制（原因：需独占板卡做 1000 Hz 压测，本轮排队未排上）**：Agent 单线程 CPU 占用未采样 | `pidstat -p $(pgrep MicroXRCEAgent) 1` 配合 1000 Hz 发布；可先用 500 Hz 版观察 Agent CPU |
| U-36-7 | 多 Client 并发 | **已知限制（原因：本轮无独占窗口）**：仍只有一个 Client | 同时跑 `xrce_pubsub`(2019) 与 `xrce_ros2_bridge`(2020)，`ss -unp | grep 2018` 看 Agent 会话数、两路话题是否互相干扰 |
| U-36-8 | 安全/加密 | **已知限制（XRCE 明文 UDP，无认证（Agent 侧 `client_key` 可伪造））** | 生产环境应放在隔离 VLAN，或启用 Agent 的 security profile（需证书，本工程未启用） |
