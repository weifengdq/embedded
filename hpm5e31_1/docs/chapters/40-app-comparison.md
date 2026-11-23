# 第 40 章 应用层综合评测与选型

## 40.1 文档信息

| 项 | 内容 |
|---|---|
|   文档版本   | v1.2.0（2026-11-02，R5 复核） |
| 适用芯片/板卡 | HPM5E31IPB1 / hpm5e31_1 最小系统板 |
| 对应例程 | `samples/app/` 下全部例程（截至本书合成时 21 个，见 `docs/generated/app-matrix.md`） |
| 实测日期 | 2026-10-30 |
| 可信度 | 高：全部数字来自 `logs/` 下真实上板串口/上位机日志；未实测项在 §40.12 逐条登记 |

## 40.2 本章导览

前面第 28~39 章按"一个方向一章"的方式，把汽车、自动驾驶、机器人领域最常用的十类应用层协议在
HPM5E31 上逐个落地：CAN 隧道网关、ISO-TP、UDS、J1939、CANopen、XCP、SOME/IP、XRCE-DDS/ROS 2、
PTP/gPTP、日志与信号（DLT/VSS/candump）。本章不再增加新例程，而是**横向收口**：

- 把十类协议放回同一张分层图（图 40-1），说明它们在 OSI 模型中的位置与相互关系；
- 汇总**同一块板、同一套工具链**下测到的延迟、吞吐、资源开销（表 40-5~38-7）；
- 给出按场景选型的决策树（图 40-3）与对比矩阵（表 40-8）；
- 提炼**跨协议通用坑**（§40.9），这些坑与具体协议无关，换任何一套栈都会遇到；
- 与 STM32H7、TC397 等同类平台做对照（表 40-9），并列出全部未闭环项（§40.12）。

前置章节：第 08 章（CAN FD）、第 13 章（lwIP/以太网）、第 28 章（测试台与网关）。

## 40.3 概述：从"裸外设"到"车规应用层"

第一阶段（第 3~22 章）解决的是"外设能不能动"：GPIO、UART、SPI、I2C、CAN、以太网、ADC、DMA……
第二阶段（第 28~39 章）解决的是"**能不能和真实的车载/机器人软件栈对话**"。二者的差别不在驱动，
而在**协议语义**：同样的 CAN 帧，UDS 把它当作诊断请求，J1939 把它当作 PGN 传输单元，
CANopen 把它当作对象字典访问，XCP 把它当作标定命令。表 40-1 是本书覆盖方向的速查。

表 40-1 应用层方向与协议标准映射

| 方向 | 主要标准 | 传输 | 典型使用者 | 本书章节 |
|---|---|---|---|---|
| CAN 隧道 | cannelloni v2 | CAN ↔ UDP | 台架/上位机分析 | 第 28 章 |
| 诊断传输 | ISO 15765-2（ISO-TP） | CAN/CAN FD | UDS、OBD、刷写 | 第 29 章 |
| 诊断服务 | ISO 14229-1（UDS） | ISO-TP / DoIP | 售后诊断、ECU 刷写 | 第 30 章 |
| 商用车网络 | SAE J1939-21/71/73/81 | CAN（29 位） | 卡车、客车、农机 | 第 32 章 |
| 子系统网络 | CiA 301/303/305 | CAN | 电机、IO 模块、机器人关节 | 第 33 章 |
| 标定测量 | ASAM XCP 1.5 + A2L | CAN / UDP | 标定工程师、HIL | 第 34 章 |
| 面向服务 | SOME/IP + SD + TP | UDP/TCP | 智能座舱、域控制器 | 第 35 章 |
| 数据分发 | DDS-XRCE / ROS 2 | UDP | 机器人、自动驾驶感知 | 第 36 章 |
| 时间同步 | IEEE 1588 / 802.1AS | L2 / UDP | 多传感器融合、TSN | 第 38 章 |
| 日志与信号 | DLT / COVESA VSS / candump | UDP / CAN / 文本 | 车队日志、信号回放 | 第 39 章 |

这些协议并非互斥：一辆智能网联汽车通常同时跑 UDS（诊断）、SOME/IP（服务）、PTP（时间）、
DLT（日志），而 CANopen/J1939 则出现在不同的子系统总线上。**HPM5E31 的价值在于：一块 480 MHz
的 RISC-V MCU 加上 4 路 CAN FD 与千兆以太网，可以同时扮演其中多个角色**——本书的例程也刻意做成
"一个固件带网关 + 一个协议节点"的形态，正是为了贴近这种真实拓扑。

## 40.4 工作原理

### 40.4.1 分层映射与数据通路

![图 40-1 应用层协议栈分层与映射](figures/40/fig-40-1-app-layers.png)

图 40-1 把本书实现的十类协议放进同一张分层图。左侧是**应用层协议**（诊断、网络管理、标定、
服务通信、时间同步、日志），中间是**传输/网络/中间件**（CAN/CAN FD、lwIP、cannelloni、
XRCE Agent），右侧是**板级硬件与驱动**（MCAN、ENET+YT8011AN、GPTMR、片内 NOR）。

三条主数据通路：

1. **CAN 总线通路**：应用协议 → ISO-TP/J1939 TP/CANopen SDO/XCP → MCAN → TCAN1044 → 双绞线。
   特点是"帧小、有仲裁、有优先级"，适合硬实时控制与诊断；
2. **以太网通路**：应用协议 → lwIP UDP/TCP → ENET0 → YT8011AN → RGMII/RJ45（或 1000BASE-T1 转换盒）。
   特点是"带宽大、可组播、可面向服务"，适合大数据量与 SOA；
3. **隧道通路**：板端 cannelloni 组件把 CAN 帧封装进 UDP（图 40-1 下方的双向箭头），
   让上位机用 stock `cannelloni` 工具把 `vcan0` 当成真实 CAN 总线——这是本书所有 CAN 类
   上位机测试（python-can、udsoncan、canopen、pyxcp、candump）的基础设施。

### 40.4.2 板端软件复用架构

第二阶段新增的 `components/` 组件全部遵循"协议层与硬件解耦"的原则，形成三层复用：

```text
samples/app/<例程>/main.c          ← 业务逻辑（20~450 行）
        │
        ├── components/app_isotp|app_uds|app_j1939|app_xcp|app_someip|app_xrce|app_ptp|app_dlt
        │        （协议状态机，纯逻辑，可单测）
        │
        └── components/app_can  +  components/app_lwip  +  components/app_can_gw
                 （CAN FD 节点 / 应用层 lwIP / CAN↔UDP 网关，屏蔽 MCAN 与 ENET 细节）
```

这套分层带来的直接收益：SOME/IP 从"自己搓 UDP socket"切换到 `app_lwip` 底座时，
协议层只依赖"发送 + 取时间"两个回调，3 个例程的 `main.c` **一行没改**；
ISO-TP 从板内双节点（Bench A）切到主机 vcan0（Bench B）时只换了端口层实现。
第 28 章给出了底座 API 的完整契约，第 29~39 章都引用它。

### 40.4.3 三种测试台（bench）

表 40-2 三种测试台的定义与适用范围

| bench | 拓扑 | 优点 | 局限 | 使用者 |
|---|---|---|---|---|
| A 板内双节点 | CAN0（DUT）↔ CAN1（测试节点），同一 MCU | 零外部依赖、可自证、可重复 | 测试节点与被测节点共享 CPU，严格实时性需打折看 | ISO-TP、J1939、CANopen、XCP 的早期闭环 |
| B 主机 + 网关 | 主机 vcan0 ↔ cannelloni ↔ 板 CAN1（网关）↔ CAN0（DUT） | 用真实上位机工具栈（python-can/udsoncan/canopen/pyxcp/candump） | UDP 隧道固有 0.1~0.2% 丢包、往返增加约 2 ms | UDS、CANopen、XCP、J1939 的主机联调 |
| C 以太网直连 | 主机 enp6s0 192.168.2.1 ↔ 板 192.168.2.100 | 真实网络路径、可测协议端到端 | 单板串行、需要 IP/端口规划 | SOME/IP、XRCE-DDS/ROS 2、PTP、DLT |

**Bench B 的往返延迟构成**（实测 avg 2.24 ms，见第 28 章）：CAN 帧 500 kbit/s 传输约 0.25 ms +
板端网关解析/组包 + ENET/lwIP ≈0.2 ms + 主机 UDP/cannelloni 线程调度 ≈1.5 ms。因此**用 Bench B 测
协议时延上限时要先扣除约 2 ms 基线**，这一点在第 33 章 CANopen PDO（2.30 ms）与第 36 章 XRCE
（RTT 2.7 ms）中都已注明。

## 40.5 关键寄存器与驱动 API 对照

应用层协议最终都要落到寄存器。表 40-3 只列"协议实现强相关"的部分，完整寄存器表见各章与第 26 章。

表 40-3 应用层协议 ↔ 寄存器 ↔ 驱动 API 对照

| 协议/组件 | 关键寄存器或资源 | 位域/作用 | 驱动 API | 代码位置 |
|---|---|---|---|---|
| app_can（所有 CAN 协议） | MCAN_TXBAR / MCAN_TXFQS | 发送请求/空闲 FIFO | `mcan_transmit_blocking()` | `components/app_can/src/app_can.c` |
| app_can | MCAN_RXF0C / MCAN_RXF0S | RX FIFO 配置/填充级别 | `mcan_read_rxfifo()` | 同上 |
| app_can | MCAN_SIDFC/XIDFC | 标准/扩展 ID 过滤器 | `mcan_set_rx_filter()` | 同上 |
| app_can | AHB SRAM 消息 RAM（0xF020_0000） | `mcan_set_msg_buf_attr()` 必须先于 init | `mcan_init()` | `app_can.c` / 第 28 章坑 1 |
| ISO-TP / UDS | 无新寄存器（复用 MCAN） | 流控靠软件状态机 | `app_isotp_send/recv()` | `components/app_isotp/src/app_isotp.c` |
| app_lwip（SOME/IP、XRCE、PTP、DLT） | ENET_DMA 描述符 TDES/RDES | 收发描述符 OWN/INT 位 | `enet_prepare_tx_desc()` | `components/app_lwip/src/app_lwip.c` |
| app_lwip | ENET_MAC 配置 / MDIO | RGMII 接口、PHY 读写 | `enet_set_mac_addr()`、`yt8011_*` | `app_lwip.c` + `components/enet_phy_yt8011/` |
| PTP | ENET_TS_CTRL(0x700) / SYST_SEC/NSEC | 硬件时间戳使能、系统时间 | `enet_init_ptp()`、`enet_get_rx_timestamp()` | `components/app_ptp/src/app_ptp.c` |
| PTP | ENET_TS_ADDEND(0x718) | 时钟频率微调（ppb） | `enet_adjust_ptp_time_freq()` | 同上 |
| XCP | GPTMR 比较/重载 | 事件通道 1 kHz 触发 | `gptmr_*`（见第 15 章） | `samples/app/xcp_can_daq/src/main.c` |
| UDS 刷写 | XPI NOR 控制器 | 扇区擦除/页编程 | `hpm_flash_*`（见第 21 章） | `samples/app/uds_flash/src/main.c` |
| app_can_gw | ENET + lwIP UDP PCB | 端口 4000，CAN1 映射 vcan0 | `app_udp_socket()` + `app_can_gw_poll()` | `components/app_can_gw/src/app_can_gw.c` |

**一句话经验**：应用层协议 90% 的调试时间不在协议本身，而在"寄存器/资源的隐性前提"——
MCAN 的消息 RAM 必须在 AHB SRAM（否则 `mcan_init` 返回 2）、ENET 的 PTP 时间戳必须在
`enet_init_ptp()` 之后才有效、GPTMR 事件必须先开时钟分组。第 28 章把这些前提写成了检查清单。

## 40.6 例程与章节矩阵

表 40-4 是第二阶段交付的例程总表（由 `tools/gen_app_matrix.py` 自动生成更细版本
`docs/generated/app-matrix.md`，含每条 README 与证据日志）。

表 40-4 应用层例程与章节矩阵

| 例程目录 | 协议 | bench | 上位机工具 | 关键实测结论 | 章节 |
|---|---|---|---|---|---|
| `app/can_gateway` | cannelloni v2 | B | stock cannelloni + candump | 往返 2.24 ms，300 帧/s 0 丢包 | 29 |
| `app/isotp_echo` | ISO-TP | A | host/uds/isotp_test.py | 经典 8/8、FD 189.6 KB/s、29 位、27 组 BS/STmin | 30 |
| `app/uds_ecu` | UDS | A→B | udsoncan | 13/13 PASS，单服务 RTT 0.47~3.09 ms，P2* 续期/S3 超时 PASS | 31 |
| `app/uds_flash` | UDS 0x34/36/37 | A | mini_flasher | 32 KB 镜像 1330 KB/s（含擦除），流程骨架打通 | 31 |
| `app/j1939_ecu` | J1939-71/81 | A | host/j1939 监测 | 地址声明 251 ms、9/9 PASS | 33 |
| `app/j1939_tp` | J1939-21 TP | A | j1939_tp_test.py | BAM 140.8 B/s vs RTS/CTS 6867.9 B/s | 33 |
| `app/j1939_dm` | J1939-73 DM | A | dm1_decode.py | DM1/DM2/DM3/DM11 灯态时间线 PASS | 33 |
| `app/canopen_node` | CiA 301 | A+B | python-canopen | Bench B 14/0 PASS、心跳 206 ms | 34 |
| `app/canopen_pdo` | CiA 301 PDO | A+B | co_pdo_bench.py | Bench A 延迟 341 µs σ8 µs、Bench B 2.30 ms | 34 |
| `app/canopen_sdo_seg` | CiA 301 SDO | A+B | co_sdo_seg.py | 分段 SDO 14 KB/s；UDP 网关下 4 KB 超时（U-33-11） | 34 |
| `app/xcp_can_daq` | XCP on CAN | A | pyxcp（待接） | 1 kHz DAQ、0 丢帧、3.93 µs/事件 | 35 |
| `app/xcp_can_cal` | XCP 标定 | A | pyxcp | 在线标定读回一致、越界拒绝 | 35 |
| `app/someip_rpc` | SOME/IP + SD | C | vsomeip / Python | RPC 平均 0.103 ms、SD 发现 0.8 ms | 36 |
| `app/someip_event` | SOME/IP 事件 | C | Python 客户端 | 100/10/500 ms 事件 σ≈0.1 ms、0 丢包 | 36 |
| `app/someip_tp` | SOME/IP-TP | C | Python 客户端 | 4 KB 三段重组 CRC 一致 | 36 |
| `app/xrce_pubsub` | XRCE-DDS | C | Agent + ROS 2 | ROS 2 侧 100 Hz、σ0.5 ms | 37 |
| `app/xrce_ros2_bridge` | XRCE-DDS/ROS 2 | C | rclpy | 100.07 Hz、RTT 2.5~3.5 ms | 37 |
| `app/ptp_slave` | IEEE 1588 | C | linuxptp | offset −15.5 ns σ363 ns、路径延迟 13.835 µs | 39 |
| `app/ptp_master` | IEEE 1588 | C | linuxptp | 主机 offset +98.2 ns σ260 ns | 39 |
| `app/gptp_demo` | 802.1AS 风格 | C | linuxptp -P | 板端 +54.1 ns σ194 ns、P2P 延迟 σ0.5 ns | 39 |
| `app/can_logger` | candump 格式 | A | host/canlog | 环形缓冲记录/回放/顺序校验 | 40 |
| `app/multiproto_coexist` | J1939 + CANopen + 网关 | A+B | host/j1939 + cannelloni | 4 通道并发：EEC1 max 10.05 ms、SYNC max 12.2 ms、SDO 490 µs、网关 drop=0；DLM 72.6% | 41 |
| `app/doip_server` | DoIP（ISO 13400-2） | C | host/doip/doip_client.py | UDP 发现 RTT 163.7~318.4 µs、路由激活 1.33 ms、0x8002 ACK 171 µs；UDS over TCP 大小报文与异常用例见第 31 章 | 32 |
| `app/xcp_eth` | XCP on UDP | C | host/xcp/xcp_eth_client.py | DAQ 990 Hz / 3960 msg/s、压测 9 724 Hz / 77.8k msg/s（6.85 Mbit/s）、RTT p50 109.9 µs | 35 |
| `app/mqtt_telemetry` | MQTT 3.1.1 | C | mosquitto + paho-mqtt | QoS1 遥测 1.000 Hz、PUBACK RTT 1.1 ms、LWT/重连/keepalive 见第 37 章 | 38 |

> 说明：`uds_ecu` 后期已接 Bench B（主机 udsoncan），`canopen_*` 三个例程都补了 `gw on` 模式；
> `xcp_can_daq` 的 pyxcp over vcan0 依赖网关，第 34 章给出了接入步骤与已完成的板内自证数据。
>
> 第七轮（v1.4.0）新增三个应用层例程：`doip_server`（第 31 章）、`xcp_eth`（第 34 章 XCP on UDP 大节）、
> `mqtt_telemetry`（第 37 章）；三者都跑在 ENET0 + YT8011AN 链路（板 192.168.2.100）。

## 40.7 上板实测数据汇总

### 40.7.1 延迟

表 40-5 端到端延迟/周期实测汇总

| 指标 | 数值 | 条件 | 证据 |
|---|---|---|---|
| cannelloni 往返延迟 | avg 2.24 ms / p99 2.48 ms | 100 次，`-t 1000` | `logs/20261030-174321-can_gateway.log` |
| cannelloni 默认参数对照 | avg 79.89 ms / p99 101 ms | `-t 100000`（100 ms 攒包） | 同上 |
| ISO-TP 经典 4095 B 往返 | 273.5 ms（29.2 KB/s） | 586 帧，BS=32/STmin=0 | `logs/20261030-165808-isotp_echo.log` |
| ISO-TP CAN FD 4095 B 往返 | 42.2 ms（189.6 KB/s） | 66 帧，1M/2M | 同上 |
| J1939 请求/应答 | 972 µs 平均 | SA 0x00→0xF9，250 kbit/s | `logs/20261030-170249-j1939_ecu.log` |
| UDS 单服务（板内 tester） | 0.47~3.09 ms（简单 0.47 / 读 DID 1.17 / 多 DID 3.09） | 500 kbit/s，ISO-TP BS=0 STmin=0 | `logs/20261030-175935-uds_ecu.log` |
| CANopen PDO（Bench A） | 341 µs 平均 / p99 375 µs | SYNC 10 ms，500 帧 | `logs/20261030-173319-canopen_pdo.log` |
| CANopen PDO（Bench B） | 2.30 ms 平均 / p99 2.75 ms | 经 cannelloni 网关 | `logs/20261030-174516-canopen_node.log` |
| SOME/IP RPC | 0.103 ms 平均 / p99 0.117 ms | n=50，UDP 直连 | `logs/20261030-174530-someip_rpc.log` |
| XRCE 命令往返 | 2.51/3.40/3.48 ms | Agent + ROS 2 | `logs/20261030-174309-xrce_ros2_bridge.log` |
| PTP 从钟 offset | −15.5 ns（σ363 ns） | 硬件打戳，主机 ptp4l 主钟 | `logs/20261030-170813-ptp_slave.log` |
| PTP 主钟 offset（主机侧） | +98.2 ns（σ260 ns） | 板为主钟 | `logs/20261030-174216-host-ptp4l-slave.log` |
| gPTP/P2P offset | +54.1 ns（σ194 ns） | P2P 延迟机制 | `logs/20261030-174427-gptp_demo.log` |

![图 40-2 实测延迟与吞吐对比](figures/40/fig-40-2-perf-compare.png)

### 40.7.2 吞吐

表 40-6 有效吞吐实测汇总

| 链路/协议 | 有效载荷吞吐 | 说明 | 证据 |
|---|---|---|---|
| ISO-TP 经典 500 kbit/s | 29.2 KB/s 峰值 | 多帧 + 流控，应用层 | `logs/20261030-165657-isotp_echo.log` |
| ISO-TP CAN FD 1M/2M | 189.6 KB/s | 比经典快 6.5× | `logs/20261030-165808-isotp_echo.log` |
| J1939 BAM（广播 TP） | 140.8 B/s | 50 ms 包间隔的代价 | `logs/20261030-170808-j1939_tp.log` |
| J1939 RTS/CTS（点对点） | 6867.9 B/s | 比 BAM 快 49× | 同上 |
| CANopen 分段 SDO | 14.0 KB/s | 每段固定开销主导 | `logs/20261030-173345-canopen_sdo_seg.log` |
| CANopen expedited SDO | 2036 次/s | 小对象高频访问 | 同上 |
| XCP DAQ | 3003 帧/s（3 ODT） | 1 kHz 事件 × 3 ODT | `logs/20261030-172706-xcp_can_daq.log` |
| SOME/IP 事件 | 10.6 kbit/s（392 条/6 s） | 100 Hz counter + 10 ms ADC | `logs/20261030-174530-someip_rpc.log` |
| XRCE-DDS | 100 Hz × 2 主题 | ADC 4×u16 + hello | `logs/20261030-174309-xrce_ros2_bridge.log` |
| cannelloni 网关 | 300 帧/s（8 B 帧） | 0 丢包（第二轮） | `logs/20261030-174321-can_gateway.log` |
| XCP on UDP（DAQ 聚合） | 6.85 Mbit/s（77.8k msg/s，10 kHz 事件） | 单包多 ODT（8 条/包），UDP 链路丢包 3.09% | 第 34 章 |
| MQTT 3.1.1 遥测 | 1.000 Hz × 149 B（≈1.2 kbit/s） | QoS1，PUBACK RTT 1.1 ms | 第 37 章 |
| DoIP 诊断（UDP 发现） | 5 次/秒级请求响应 | 发现 RTT 164~318 µs；诊断走 TCP 13400 | 第 31 章 |

**吞吐量级排序**（同一条物理链路上）：SOME/IP（千兆 UDP，轻松 10 Mbit/s 级）≫ ISO-TP FD（189.6 KB/s）
> ISO-TP 经典（29.2 KB/s）> CANopen SDO（14 KB/s）> J1939 RTS/CTS（6.9 KB/s）> J1939 BAM（0.14 KB/s）。
这个排序由**协议开销**决定，而不是由 MCU 算力决定：BAM 每 32 字节要等 50 ms，是 J1939-21 为
"广播不压垮总线"付出的代价；CANopen 分段 SDO 每 7 字节一段还要握手一次；ISO-TP FD 之所以快，
是因为单帧能装 64 字节、流控窗口 BS=32。

### 40.7.3 资源开销

表 40-7 各协议栈的资源开销（Flash/RAM，-O3，XIP）

| 组件/例程 | Flash 占用 | 静态 RAM | 堆 | 备注 |
|---|---|---|---|---|
| app_can + app_lwip + app_can_gw | 138.4 KB | DLM 72.6%（95.2 KB） | 0 | 网关固件，含 lwIP |
| CANopenNode + OD | +26 KB（相对裸机） | 6.2 KB OD + 2.9 KB 堆 | 2.9 KB | `co_new` 堆 |
| SOME/IP 服务端 | 139 KB 整镜像 | — | 0 | 含 lwIP 底座 |
| XRCE-DDS 客户端 | 整镜像 ~130 KB | .bss 112 KB（应用自用 11 KB） | 0 次 malloc | 含 Agent 协议栈 |
| PTPd v2 + app_ptp | 整镜像 136 KB | DLM 68 KB（52%） | 0 | 含 lwIP |
| XCP 从站（CAN 构型） | 整镜像 145.6 KB（app_xcp 净增 11.5 KB；其余为 lwIP 网关 + ADC/GPTMR 演示） | app_xcp 3.8 KB（含 2.5 KB MCAN 消息 RAM） | 0 | `app/xcp_can_daq`，第 34 章 |
| **DoIP 服务端（ISO 13400-2）** | **整镜像 137.7 KB**（app_doip 11.0 KB + UDS 13.2 KB 净增） | **150.7 KB**（.data+.bss；app_doip 4.0 KB、UDS 0.5 KB，余为 lwIP/收发缓冲） | 0 | `app/doip_server`，第 31 章；UDP 车辆发现 + TCP 13400 |
| **XCPonEth（UDP 传输）** | **整镜像 130.1 KB**（app_xcp 13.3 KB，其中 UDP 传输层 2.5 KB） | **136.5 KB**（app_xcp 15.2 KB：MAX_DTO=256 × 32 帧队列 8.2 KB + DAQ 表） | 0 | `app/xcp_eth`，第 34 章；10 kHz/8 ODT 实测 77791 msg/s |
| **MQTT 3.1.1 客户端** | **整镜像 134.6 KB**（app_mqtt 净增 10.2 KB） | **137.9 KB**（.data+.bss；app_mqtt 自身 0，余为 lwIP 静态池/CAN/ADC） | 0 | `app/mqtt_telemetry`，第 37 章；lwIP TCP + CAN 遥测 |
| **MCAL Eth + lwIP 底座** | **整镜像 115.9 KB**（MCAL Eth 7.4 KB + app_lwip_eth 7.1 KB 净增） | **132.7 KB**（Eth 驱动 37.0 KB 收发缓冲在 noncacheable 段 + lwIP 静态池） | 0 | `mcal/eth_lwip`，第 49 章；FreeRTOS 版见 `mcal/eth_freertos_lwip` |
| ISO-TP + UDS | 整镜像 ~110 KB | — | 0 | — |
| **三合一**：J1939 + CANopenNode + 网关（4 通道） | 171.8 KB | DLM 95.2 KB（72.6%）+ AXI 62.3 KB + AHB 17.9 KB | CANopen 2.9 KB | `app/multiproto_coexist`，见 §40.7.4 |

> 测量口径（2026-11-02 补测，与表内既有行一致）：Flash = `riscv32-size` 的 `.text + .data`；
> 静态 RAM = `.data + .bss`（跨 DLM/AXI/AHB，不含 16 KB 启动栈）；"净增"= 组件目录下全部 `.obj` 之和
> （例如 `components/app_doip/**`），不含 lwIP/SDK 底座。四个新例程的 ELF 与对象文件均来自
> `build/<sample>-debug/`，-O3、XIP，与表内 2026-10-30 的数据同口径。
>
> 补充：四个新例程的 **DLM 都停在约 92.9 KB**（lwIP `MEM_SIZE` + 16 KB 堆 + 16 KB 栈基线，与网关固件同量级），
> 协议新增缓冲主要落在 AXI SRAM / 专用段：doip_server 56.8 KB（3×4 KB 连接接收 + 拼包 + UDS 队列）、
> xcp_eth 42.6 KB（MAX_DTO=256 的 32 帧队列 + DAQ 表）、mqtt_telemetry 34.0 KB（lwIP TCP + CAN/ADC）、
> mcal/eth_lwip 的 Eth 驱动 37.0 KB 收发缓冲放在 noncacheable 段。

结论：**HPM5E31 的 1 MB 片内 Flash 与 512 KB AXI SRAM 足以同时容纳 lwIP + 一个完整协议栈**，
真正的约束是 **DLM（128 KB）**——lwIP 的 pbuf 池与协议栈缓冲都放 DLM，网关固件单独就占到 72.6%。

更进一步，§40.7.4 的"三合一"固件（J1939 栈 + CANopenNode + 网关，4 路 CAN 同时工作）实测
DLM 仍是 **95.2 KB（72.6%）**、整镜像 171.8 KB：原因是 J1939 栈（9.8 KB）与 CANopen OD
（6.2 KB + 2.9 KB 堆）落在 AXI SRAM/AHB SRAM，没有把 DLM 推过线。所以"网关 + 两类协议 + 应用"三合一
在本板上是可行的；真正的余量只剩约 36 KB DLM，再加一个大协议栈（如 SOME/IP + XRCE 同时）就必须压缩 lwIP 缓冲。

### 40.7.4 多协议 / 4 通道共存（U-39-1 / U-39-6）

前面的数据都是"单协议、单通道"。为回答"十类协议同时跑还有多少余量"，新增例程
`app/multiproto_coexist`：同一颗 HPM5E31 上同时运行 **J1939 ECU（CAN0）+ cannelloni 网关（CAN1）
+ CANopenNode 从站（CAN2）+ CANopen 主站（CAN3）**，4 路 CAN 全部 500 kbit/s，两段总线各自独立
（CAN0↔CAN1、CAN2↔CAN3），唯一共享的是 CPU / RAM / lwIP。

固件上电自动跑两组对比：`mix`（4 通道全开）与 `mixj`（暂停 CANopen 任务，只跑 J1939+网关），
用同一套 mcycle 时基统计周期抖动、CPU 空闲循环、网关丢弃与 4 路 MCAN 错误计数。

**资源占用**（表 40-10）：三合一后 DLM 仍为 72.6%，CANopen 的 OD/堆落在 AXI SRAM，J1939 栈 9.8 KB；
Flash 171.8 KB（含 lwIP+网关），距离 1 MB 很远。

表 40-10 三合一固件的资源占用（`build/multiproto_coexist-debug`，-O3，XIP）

| 区域 | 用量 | 占比 | 主要占用者 |
|---|---|---|---|
| FLASH | 171 760 B | 16.38% | lwIP + 网关 + CANopenNode + J1939 |
| DLM | 95 168 B | 72.61% | lwIP pbuf/heap + 协议栈 .bss |
| AXI SRAM | 62 336 B | 31.71% | CANopen OD、lwIP 大缓冲、J1939 TP 会话 |
| AHB SRAM | 17 920 B | 54.69% | 4 路 MCAN 消息 RAM（每实例独立） |
| ILM | 1 544 B | 1.18% | 启动/热代码 |

**CPU 与周期抖动**（表 40-11）：4 通道全开时空闲循环从 27.8 万次/秒降到 15.6 万次/秒（**-44%**），
但 J1939 EEC1 的 10 ms 周期几乎不受影响（max 10004 µs → 10053 µs，仅 +49 µs），
说明 CANopen 的 SYNC/SDO 任务与 J1939 周期发送在同一主循环里能保持确定性；SYNC 自身最大间隔
12.2 ms（+2.2 ms），主要来自 lwIP/网关与 SDO 的偶发长循环（单次循环 max 1.03 ms）。

表 40-11 4 通道共存 vs 只跑 J1939 的 CPU/抖动对比（500 kbit/s，15 s/8 s 窗口）

| 指标 | mix（4 通道全开） | mixj（只 J1939+网关） | 差值 |
|---|---|---|---|
| 主循环空闲计数 | 155 575 次/s | 277 707 次/s | **-44.0%** |
| 单次循环最大耗时 | 1 029 µs | 594 µs | +435 µs |
| J1939 EEC1 帧数 | 1 500（15 s） | 800（8 s） | 均 100/s |
| EEC1 间隔 min/avg/max | 9 068 / 9 999 / 10 053 µs | 9 409 / 9 999 / 10 004 µs | max +49 µs |
| CANopen SYNC 间隔 avg/max | 10 000 / 12 231 µs | —（暂停） | — |
| CANopen SDO 读 0x2102 | 30/30 成功，avg 490 µs | — | — |
| 4 路 MCAN 错误 | LEC/TEC/REC/bus-off 全 0 | 全 0 | 0 |

**网关与跨协议注入**（表 40-12，Bench B）：主机 vcan0 8 s 收到 896 帧，EEC1 平均 10.013 ms
（max 20.06 ms 是 cannelloni 攒包批量）、DM1/ET1 各 8 帧、CCVS1 80 帧，DM1 正确解出
lamp=0x10(AWL)/SPN=100/FMI=4/OC=1。主机反向注入 402 帧 11 位帧时：网关 tx_can=402（全部落到 CAN0）、
drop_can=0、bad_pkt=0，CAN0 的 J1939 端口把这 402 帧计为 bus.dropped（跨协议帧，不进协议栈）——
证明"4 通道互不串扰"在软件层成立：29 位 J1939 与 11 位 CANopen/注入帧按 ID 类型隔离，
两段总线各自的错误计数保持 0。

表 40-12 Bench B 网关与跨协议注入实测

| 项 | 注入前 | 注入后（402 帧 11 位） |
|---|---|---|
| 网关 peer | 192.168.2.1:4000 | 同左 |
| CAN1→UDP 帧数 | 4 086 | 4 828 |
| UDP→CAN 帧数 | 2 | 402 |
| drop_can / bad_pkt | 0 / 0 | 0 / 0 |
| J1939 bus.dropped（11 位跨协议计数） | 2 | 402 |
| 主循环 max_loop | 1 444 µs | 5 062 µs |
| vcan0 侧解码 | EEC1 10.013 ms、DM1 1 s | 同左（注入期间无 J1939 丢帧） |

> 证据：`logs/20261102-102105-multiproto_coexist.log`（Bench A）、
> `logs/20261102-102326-multiproto_coexist-host.log`（Bench B）。
> 复跑：`hwrun.sh samples/app/multiproto_coexist --secs 34`（Bench A）或
> `--post "bash samples/app/multiproto_coexist/host_benchb.sh"`（Bench B）。
> 已知统计缺陷：CANopenNode 的 `co_node_get_stats().rx/tx` 在本版本恒为 0（`canopen_node` 例程同样），
> 因此 CANopen 闭环用主站侧 rx=1 559 / hb=14 / SDO 30/30 验证。

## 40.8 性能分析与调优

1. **CAN 类协议的天花板是总线，不是 CPU**：500 kbit/s 经典帧下，ISO-TP 有效吞吐 29.2 KB/s 已达
   物理极限的 47%（8 字节数据 + 帧头/填充/流控帧）。想更快只有两条路：换 CAN FD（实测 6.5×）
   或换以太网（SOME/IP/XRCE）。
2. **网关隧道会引入约 2 ms 的确定性延迟**：其中 cannelloni 的攒包参数 `-t` 是最大变量
   （1 ms vs 100 ms 相差 35 倍，表 40-5）。做实时联调时应显式设 `-t 1000`。
3. **协议实现的 CPU 开销普遍很小**：XCP 打包 3.93 µs/事件、SOME/IP RPC 0.103 ms 往返、
   XRCE 主循环 904 Hz——对 480 MHz 的 HPM5E31 来说，应用层协议不是瓶颈，**调度与缓冲策略**才是。
4. **时间同步是"另一条赛道"**：PTP 的 offset 已经到 ns 级（σ 数百 ns），而 CAN/以太网协议的
   时间戳精度受限于中断与软件队列（µs 级）。做多传感器融合时应以 PTP 时基为准，协议时间戳只作排序。
5. **调优优先级**（按收益/成本比）：
   - 先排除 `-t`、`vcan` 限速、`tc` 等**配置类**因素（收益最大、成本最低）；
   - 再优化缓冲深度与批处理（如 lwIP pbuf 数量、CAN TX FIFO 深度）；
   - 最后才考虑协议栈本身的代码级优化（如零拷贝、跳过 cache 回写）。
6. **多协议共存时 CPU 是第二约束**：§40.7.4 实测 4 通道全开让空闲循环率下降 44%，但 J1939 的
   10 ms 周期只漂移 49 µs；真正会被拖慢的是"软件定时 + 轮询"的轻量任务（CANopen SYNC max 12.2 ms）。
   若应用对周期抖动敏感，应把周期发送搬到 GPTMR 硬件节拍中断（第 15 章），主循环只做收发。

## 40.9 坑与注意事项（跨协议通用）

1. **MCAN 消息 RAM 必须在 AHB SRAM**：`mcan_set_msg_buf_attr()` 要在 `mcan_init()` 之前，
   否则返回 status_invalid_argument（=2）。第 29/33/34 章都踩过。
2. **SPI/I2C 引脚的 IOC LOOP_BACK 位**（第 6/7 章的坑，与本章协议无关但同样致命）：
   HPM 的 SPI 主机靠 SCLK pad 内部回环取移位时钟、I2C 开漏也依赖回环，手写 pinmux 漏了这位
   就会出现"能发不能收 / 一个边沿都不发"。
3. **`ipaddr_ntoa()` 返回静态缓冲区**：一行 printf 打三个地址会全部变成最后一个，调试时极易误判。
4. **letter-shell 没有 sleep 命令**：`hwrun --send` 只是按分号拆行连续发送；需要延时要么拆两次
   hwrun，要么在固件里注册保持轮询的 `sleep` 命令（第 34/36 章都用后者）。
5. **`bsp_uptime_ms()` 旧实现会冻结**：按调用间隔整数累加，主循环每轮 <1 ms 时永远是 0。
   已改为 mcycle 绝对时基（第 35/36 章联合发现，Lead 修复）。
6. **板锁是共享资源**：多 agent 并行时任何一次"烧写+主机联调"都要原子化（`hwrun --post`），
   否则固件会被别人的测试换掉，出现"JTAG: CPU is not halted"之类的假故障。
7. **UDP 隧道的丢包必须按协议处理**：cannelloni 是 fire-and-forget，0.1~0.2% 丢包是正常的；
   CANopen 分段 SDO 这类"无重传"协议在网关下会超时（U-33-11），要用 TCP 模式或上层重试。
8. **J1939 与 Linux 内核栈的 CTS 布局差异**：内核 `j1939_session_tx_cts()` 的 dat[1]/dat[2]
   与 J1939-21 不同，标准 CTS 会被 Abort reason=5 取消（U-32-1）。跨栈互通时必须抓包核对。
9. **PTP 的 transportSpecific 字段**：PTPd 上游硬编码 0x80，linuxptp 默认按 0 过滤，会静默丢弃
   Delay_Req（表现为"路径延迟恒 0"）。跨实现互通必须核对首字节。
10. **主机 PHC 会被上一次从钟经历改写**：切回主钟前要 `phc2sys` 复位，否则把错误绝对时间发给对端。
11. **lwIP 应用不要在回调里 printf**：串口输出会阻塞 lwIP 轮询，导致丢包/重传。
12. **资源预算要算 DLM 而不是总 RAM**：网关固件 DLM 72.6%，再叠加协议栈很容易超限。

## 40.10 选型指南

![图 40-3 应用层协议选型决策树](figures/40/fig-40-3-selection.png)

表 40-8 典型场景选型矩阵

| 场景 | 首选 | 备选 | 理由 |
|---|---|---|---|
| ECU 售后诊断/刷写 | UDS + ISO-TP | DoIP（以太网） | 标准统一、工具链成熟；本书第 29/30 章 |
| 商用车车队数据 | J1939 | — | 法规与行业事实标准；第 32 章 |
| 电机/关节/IO 子系统 | CANopen | J1939（商用车） | 对象字典 + PDO 实时性好；第 33 章 |
| 标定与在线测量 | XCP + A2L | CCP（逐步淘汰） | ASAM 标准、工具生态好；第 34 章 |
| 域控制器 SOA | SOME/IP | DDS（更强 QoS） | 车载以太网主流；第 35 章 |
| 机器人/自动驾驶中间件 | ROS 2 + XRCE-DDS | SOME/IP | 与 ROS 2 生态无缝；第 36 章 |
| 多传感器时间同步 | PTP/gPTP | NTP（ms 级） | ns~百 ns 级；第 38 章 |
| 日志与信号回放 | DLT + VSS + candump | 自定义二进制 | 工具链（dlt-viewer/canplayer）成熟；第 39 章 |
| 上位机看真实 CAN | cannelloni 网关 | slcan/candleLight USB | 不占 USB、可多通道；第 28 章 |
| 车外以太网诊断/OTA | DoIP（ISO 13400-2） | UDS over CAN（需网关转） | 车辆发现 + 路由激活 + TCP 13400；大 payload 刷写；第 31 章 |
| 大数据量在线标定/测量 | XCP on UDP（XCPonEth） | XCP on CAN（8 B DTO 墙） | 10 kHz/8 ODT 实测 6.85 Mbit/s，是 CAN 500k 的 20~35 倍；第 34 章 |
| 云端/远程遥测 | MQTT 3.1.1 | SOME/IP（仅车内）、XRCE-DDS（ROS 2） | 轻量 pub/sub、QoS/遗嘱/保留消息，适合弱网；第 37 章 |

## 40.11 优劣评价与同类对比

表 40-9 同类 MCU 平台的应用层协议承载能力对比

| 平台 | 内核/主频 | CAN FD | 以太网 | 时间同步 | 适用判断 |
|---|---|---|---|---|---|
| HPM5E31（本书） | RV32-IMAFDCBP @480 MHz | 4 路 | 1× 千兆 + RGMII | ENET 硬件 1588 | 应用层协议全栈可跑；DLM 128 KB 是主要约束 |
| STM32H743 | Cortex-M7 @480 MHz | 2 路 | 1× 千兆 | 部分型号有 1588 | 生态与工具链更成熟，CAN 路数少 |
| TC397 | TriCore @300 MHz | 多路 | 千兆 | 有 1588 | 车规安全等级高，成本与开发门槛高 |
| i.MX RT1170 | Cortex-M7+M4 | 2 路 | 千兆 | 有 1588 | 主频高，软件栈偏 Linux 生态 |

**开源协议栈对比**（本书选用/参考的）：

- **CANopenNode**（Apache-2.0）：移植成本低（只需 CO_driver 适配），第 33 章 3 个例程全 PASS；
- **PTPd v2**（BSD）：比 flexPTP 更贴近 linuxptp 的报文习惯，配合 SDK 的 lwIP 钩子可零改动硬件打戳；
- **Micro-XRCE-DDS Client**（Apache-2.0）：零 malloc、可裁剪，第 36 章实测 100 Hz 稳定；
- **vsomeip**（MPL-2.0）：功能全（SD/事件/字段/TP），但依赖 boost，主机侧构建较重；
- **XCPlite**：只做 XCP on Ethernet，参考其 A2L 运行时生成思路，CAN 传输需自己实现。

### 40.11.1 本轮新增三协议的适用边界（DoIP / XCP on UDP / MQTT）

表 40-9 的平台对比回答"哪颗芯片能跑"，这三个协议回答"什么场景该选它、什么场景不该"。它们不是
"又一个协议栈"，而是各自补上既有协议拼图里缺的那一块：DoIP 补"车外以太网诊断入口"、XCP on UDP 补
"XCP 的大数据量传输层"、MQTT 补"跨网/上云遥测"。

表 40-13 DoIP / XCP on UDP / MQTT 的定位与边界（2026-11-02 实测）

| 维度 | DoIP（ISO 13400-2） | XCP on UDP（XCPonEth） | MQTT 3.1.1 |
|---|---|---|---|
| 它解决什么问题 | 车外诊断仪/OTA 经以太网访问 ECU：车辆发现、路由激活、诊断消息 | XCP 的以太网传输层：大 DTO、单包多 ODT、高采样率 | 弱网/跨网的发布订阅遥测：QoS、遗嘱、保留消息 |
| 与既有协议的关系 | **DoIP 只做传输，诊断语义仍是 UDS**（第 30 章）；UDS over CAN（ISO-TP）与 DoIP 是同一套 UDS 的两个传输层，二选一 | **同一份 XCP 协议层**（A2L/DAQ/命令完全兼容），只换传输层；与 XCP on CAN 是同一协议的两个传输层 | 与 SOME/IP（车内 SOA）、XRCE-DDS（ROS 2）互补，不替代；MQTT 面向车外/云，SOME/IP 面向车内 |
| 何时该用 | 车外诊断、刷写大文件（>4 KB payload）、需要 IP 寻址与多 ECU 路由 | 10 kHz 级采样、几十个变量、需要 6~7 Mbit/s 载荷吞吐 | 4G/以太网弱网遥测、对接云平台/数据库、需要 QoS/遗嘱 |
| 何时不该用 | 车内实时控制（TCP 握手/重传带来 ms 级不确定）；没有以太网 PHY 的老 ECU | 确定性零丢帧的闭环控制；没有 lwIP/以太网的板子；A2L 工具只支持 CAN | 车内硬实时控制（TCP 队头阻塞、broker 单点）；需要服务发现/多播的车内 SOA |
| 本板实测 | 整镜像 137.7 KB；UDP 车辆发现 + TCP 13400，单包 payload 最大 4096 B | 整镜像 130.1 KB；10 kHz/8 ODT **77791 msg/s、6.85 Mbit/s**；UDP 链路丢包 0.87~3.09% | 整镜像 134.6 KB；QoS0/1、LWT、保留消息；5 s 周期遥测 |
| 本书章节 | 第 31 章 | 第 34 章 | 第 37 章 |

三条经验：

1. **DoIP 不是"第二套诊断协议"**。它把 UDS 报文装进 TCP 13400，前面加车辆发现（UDP 13400）与路由激活；
   所以第 30 章的 UDS 服务、DID、例程号在 DoIP 下原样复用。如果项目里已有 UDS over CAN，加 DoIP 的
   增量主要是"以太网传输 + 连接管理"（本板 11 KB Flash），不是重写诊断。
2. **XCP on UDP 的收益是"载荷吞吐"，代价是"丢包确定性"**。同一份 `xcp_core.c`，CAN 构型 3.8 KB RAM，
   以太网构型 15.2 KB RAM（MAX_DTO 放大到 256 + 32 帧队列）；换来的 10 kHz/8 ODT 6.85 Mbit/s 是
   CAN 500k 的 20~35 倍。但 UDP 无重传，实测链路丢 0.87%~3.09%——标定写命令必须幂等重发，
   测量侧用 CTR + `g_tick_*` 统计丢帧，不能假设"零丢包"。
3. **MQTT 与 SOME/IP 不冲突，选错场景才冲突**。MQTT 适合"车 → 云"的弱网遥测（QoS 1 重发、LWT 掉线通知、
   保留消息），SOME/IP 适合"车内 ECU ↔ ECU"的服务调用（SD 服务发现、事件组、零拷贝）。如果需求是
   "车内 10 ms 级闭环"，MQTT 的 broker 单点与 TCP 队头阻塞会让它成为错误答案；反过来，把 SOME/IP
   直接暴露到公网也不是它设计的目标。

**本书方案的不足**（如实记录）：

1. XCP 主机侧 pyxcp over vcan0 的最终联调数据在第 34 章标注为待补（板内自证已完成）；
2. SOME/IP 主机侧 SOME/IP-TP 多段接收仍有末段问题（U-35-10）；
3. J1939 与 Linux 内核 can-j1939 的 TP 完全互操作未通过（U-32-1）；
4. CANopen 分段 SDO 在 UDP 网关下不可用（U-33-11）；
5. DLT 的 dlt-daemon 直连链路与 dlt-viewer 集成仍在第 39 章标注待确认。

## 40.12 参考来源

- 标准文本：ISO 15765-2 <https://www.iso.org/standard/66574.html>、
  ISO 14229-1 <https://www.iso.org/standard/72439.html>、
  SAE J1939 <https://www.sae.org/standards/content/j1939_202208/>、
  CiA 301 <https://www.can-cia.org/can-knowledge/canopen/>、
  ASAM XCP/A2L <https://www.asam.net/standards/detail/mcd-1-xcp/>、
  AUTOSAR Classic Platform <https://www.autosar.org/standards/classic-platform/>、
  IEEE 1588 <https://standards.ieee.org/ieee/1588/6825/>、
  COVESA VSS <https://github.com/COVESA/vehicle_signal_specification>。
- 开源实现：CANopenNode <https://github.com/CANopenNode/CANopenNode>、
  XCPlite <https://github.com/vectorgrp/XCPlite>、
  Micro-XRCE-DDS <https://github.com/eProsima/Micro-XRCE-DDS>、
  vsomeip <https://github.com/COVESA/vsomeip>、
  cannelloni <https://github.com/mguentner/cannelloni>、
  dlt-daemon <https://github.com/COVESA/dlt-daemon>、
  can-utils <https://github.com/linux-can/can-utils>、
  linuxptp <https://github.com/richardcochran/linuxptp>。
- 本仓库：`components/app_*`、`samples/app/*`、`host/*`、`logs/*`、
  `docs/generated/app-matrix.md`（脚本生成）、第 28~41 章各自的"参考来源"节。
- 上位机工具链：pyxcp <https://github.com/christoph2/pyxcp>、
  python-can <https://github.com/hardbyte/python-can>、
  udsoncan <https://github.com/pylessard/python-udsoncan>、
  canopen <https://github.com/christoph2/canopen>、
  cantools <https://github.com/cantools/cantools>、
  ROS 2 <https://docs.ros.org/>、
  a2ltool <https://github.com/DanielT/a2ltool>。

## 40.13 待确认项与风险

> 本章待确认项已逐条复核（2026-11-02）：能实测的给出数据与日志，能定位的写清根因，确实无法实测的降级为"已知限制（原因 + 一条命令级验证方法）"。
| 编号 | 项 | 状态 | 验证方法 |
|---|---|---|---|
| U-39-1 | 十类协议**同时**运行（网关 + 协议栈 + 应用）的 DLM/实时性 | **已实测（§40.7.4）**：J1939 + CANopenNode + 网关三合一，DLM 95 168 B（72.6%）、Flash 171.8 KB；4 通道全开时 EEC1 max 10.053 ms、SYNC max 12.2 ms、SDO 490 µs、网关 drop=0 | `logs/20261102-102105-multiproto_coexist.log` | 再加一个大协议栈（SOME/IP/XRCE）同跑，观察 DLM 是否过 90% |
| U-39-2 | 绝对时间精度（PTP + 转换盒不对称） | 已知限制 | 飞线 PPS，用示波器对比主机 PPS 与板 PPS 相位 |
| U-39-3 | 24 h 长稳（全部例程循环） | 已知限制 | 扩展第 25 章 soak_test 覆盖应用层 |
| U-39-4 | 与真实车规 ECU（非本板）的互操作 | 已知限制 | 需要实车/台架 ECU，当前只有板内与主机工具互测 |
| U-39-5 | TC397/STM32H7 同协议栈的横向性能对比 | 已知限制 | 需第二块板；当前仅做参数级对比（表 40-9） |
| U-39-6 | 多 CAN 通道同时跑不同协议（4 路独立） | **已实测（§40.7.4）**：CAN0=J1939 / CAN1=网关 / CAN2=CANopen 从站 / CAN3=CANopen 主站，4 路 MCAN 错误计数全 0；CPU 空闲循环 155 575 次/s（比只跑 J1939 低 44%）；跨协议注入 402 帧 0 丢失 | `logs/20261102-102326-multiproto_coexist-host.log` | 把 CAN2/3 换成 ISO-TP+UDS 或 XCP，重复本表 |
| U-39-7 | XRCE-DDS 与 SOME/IP 共存时的 UDP 端口/缓冲冲突 | 已知限制 | 合并固件 + 双协议同时收发，观察 pbuf 耗尽 |

> 本章所有"实测"数字均可在 `logs/` 下同名日志中复查；未实测项一律标注"待确认"并给出验证方法，
> 这是本书的写作纪律，也是后续接手者继续推进的入口。