# 第 28 章 应用层测试台：CAN 网关、cannelloni 协议与上位机链路

## 28.1 文档信息

| 项 | 内容 |
|---|---|
|   章节版本   | v1.2.0（2026-11-02，R5 复核） |
| 适用芯片 / 板卡 | HPM5E31IPB1 / hpm5e31_1 最小系统板 |
| 对应例程 | `samples/app/can_gateway/`（板端固件）、`host/can_bridge/`（上位机链路） |
| 共享组件 | `components/app_can/`（CAN FD 节点）、`components/app_lwip/`（应用层 lwIP）、`components/app_can_gw/`（cannelloni v2 网关） |
| 实测日期 | 2026-10-30（第 1 次上板，详见"上板实测"节与 `logs/`） |
| 实测环境 | 主机 Ubuntu 26.04 / python-can 4.6.1 / can-utils / cannelloni 2.1.2；板端 480 MHz，lwIP 2.1.2 |
| 可信度 | 协议格式、ping/心跳/回显/延迟/丢包为本板实测；第 403 行起的表 28-8 已逐条复核，遗留项均写明已知限制与验证方法 |

## 28.2 本章导览

读者读完本章能：

1. 在 **三种测试台**（MCU 双节点 / 上位机+CAN 网关 / 以太网直连）之间按协议类型做选择；
2. 看懂 **cannelloni v2** 的 UDP 包格式，并能自己实现一个兼容的网关（板端 C 或主机 Python）；
3. 知道 CAN↔UDP 的**时延预算**花在哪里、怎么把往返延迟从 ~100 ms 压到个位数 ms；
4. 用 `host/can_bridge/` 里的三行脚本把主机 `vcan0` 接到板端真实 CAN 总线，
   让 python-can / candump / cansend / udsoncan / canopen / pyxcp **直接当"CAN 分析仪"用**；
5. 避开应用层联调最常见的易错点：vcan 不限速、cannelloni 默认攒包 100 ms、MCAN 消息 RAM 在 AHB SRAM、
   `ipaddr_ntoa()` 静态缓冲区、板锁 flock 死锁。

**前置章节**：第 08 章（CAN FD 原理）、第 12 章（YT8011AN/RGMII）、第 13 章（lwIP 与 iperf）、
第 24 章（总线吞吐）。**后续章节**：第 29~40 章的所有 CAN 类与以太网类应用协议都跑在本章的测试台上。

## 28.3 概述

应用层协议（UDS、J1939、CANopen、XCP、SOME/IP、XRCE-DDS、PTP、DLT）本身不关心"帧是谁发的"，
但**调试阶段必须能从上位机看到真实总线**，否则只能靠串口 printf 猜。工程上成熟的做法分三类：

| 类别 | 典型协议 | 上位机接口 | 对板端的额外要求 |
|---|---|---|---|
| CAN 类 | UDS/ISO-TP、J1939、CANopen、XCP | SocketCAN（`can0` / `vcan0`） | 一路 CAN 做"网关"，把帧搬到以太网 |
| 以太网类 | SOME/IP、XRCE-DDS、PTP、DLT | UDP/TCP socket | ENET + lwIP（或 RTOS + 协议栈） |
| 裸帧/诊断 | 自研协议、Bootloader | 串口 / USB-CDC / J-Link | 无 |

本章交付的**共享底座**就是让前两类在 hpm5e31_1 上一行代码就能起：

```c
app_can_init(&can0, BOARD_CAN0_BASE, &cfg);   /* CAN FD 节点（经典 500k 或 FD 1M/2M） */
app_lwip_init(NULL);                          /* 192.168.2.100/24, 等 link up */
app_can_gw_init(NULL);                        /* CAN1 <-> UDP 4000 <-> 主机 vcan0 */
```

### 28.3.1 表 28-1 三种标准测试台对比

| 维度 | Bench A：MCU 双节点 | Bench B：上位机 + CAN 网关 | Bench C：以太网直连 |
|---|---|---|---|
| 拓扑 | CAN0(DUT) ↔ CAN1(tester) | 主机 vcan0 ↔ UDP ↔ CAN1 ↔ CAN0(DUT) | 主机 ↔ 千兆以太网 ↔ 板 ENET |
| 额外硬件 | 无（板内两路 CAN 已互连） | 无（板载 ENET + YT8011AN） | 无（车载以太网盒已接） |
| 优点 | 最快、最稳、无主机依赖；适合协议栈早期 | 上位机工具齐全（candump/cansend/python-can/udsoncan） | 直接测应用层，接近量产链路 |
| 缺点 | 只能自测，不能复用上位机生态 | 多一层 UDP，时延/丢包需评估；vcan 需限速 | 与 CAN 无关；SD/服务发现需主机配合 |
| 典型用途 | ISO-TP 分片、CANopen 状态机、XCP DAQ | UDS 诊断、J1939、CANopen SDO、CAN 日志回放 | SOME/IP、XRCE-DDS、PTP、DLT |
| 本章实测 | 第 8/09 章已有 | **本章主角** | 第 35~39 章 |

![图 28-1 三种应用层测试台架构](figures/28/fig-28-1-bench-arch.png)

## 28.4 工作原理

### 28.4.1 Bench A：MCU 双节点（无上位机）

板内 **CAN0(PD00/PD01) 与 CAN1(PD05/PD04) 物理连在同一段总线**（两端各 120 Ω），
一个固件里同时初始化两个 MCAN 实例即可做"发—收—校验—回显"闭环：

```c
/* DUT 在 CAN0，tester 在 CAN1；两者共用一条总线 */
app_can_init(&dut,    BOARD_CAN0_BASE, &cfg_500k);
app_can_init(&tester, BOARD_CAN1_BASE, &cfg_500k);
```

优点是没有 UDP/主机环节，**测出来的数字只含 CAN 本身**（第 08 章经典 500k 双节点 4305 frame/s，
FD 1M/2M 64 B 往返 1580 次/s）；缺点是不能用 candump/udsoncan 等现成工具。

### 28.4.2 Bench B：上位机 + CAN 网关（CAN 类协议的标准做法）

数据通路见 图 28-1 的中间一幅。三个关键设计点：

1. **板端是"被动方"**：上电后不主动发 UDP，收到主机的第一个 DATA 包时**学习源 IP:端口**，
   之后所有 CAN→UDP 的回包都发往该地址。这样主机 cannelloni 用 `-l 4000` 起在哪个端口都能对上，
   也不怕主机重启换端口（见 `app_can_gw.c` 的 `gw_udp_recv()`）。
2. **CAN1 是网关**，不是 DUT。DUT（UDS 服务端、CANopen 从站等）占 CAN0；两者在同一段总线上，
   所以主机看到的 vcan0 就是"CAN0 所看到的总线"。
3. **vcan 必须限速**：vcan 是内存回环、内核不做速率限制，`cangen` 全速灌包会把 480 MHz 的 MCU 网关打爆。

### 28.4.3 Bench C：以太网应用直连

SOME/IP、XRCE-DDS、PTP、DLT 这类协议本身就跑在 UDP/TCP 上，不需要 CAN 网关：
主机 `enp6s0(192.168.2.1)` ↔ 千兆车载以太网盒 ↔ 板 `192.168.2.100`。
这一路全部走 `components/app_lwip`，本章只做**底座验证**（ping + UDP 数据通路），
协议本身在第 35~39 章展开。
### 28.4.4 cannelloni v2 协议逐字段解析

cannelloni 是 GPL-2.0 的 "SocketCAN over Ethernet tunnel"（作者 Maximilian Güntner，v2.1.2）。
板端 `components/app_can_gw` 与它**逐字节兼容**，因此可以直接用发行版/源码编译的 stock cannelloni 对接。

**UDP 载荷结构**（图 28-2）：5 字节包头 + count 个序列化后的 Linux `struct canfd_frame`。
多字节字段**全部网络字节序（大端）**，这是 decoder.cpp 里 `htonl/htons` 决定的。

![图 28-2 cannelloni v2 UDP 包格式](figures/28/fig-28-2-cannelloni-format.png)

### 28.4.5 表 28-2 cannelloni v2 字段定义（与 ref2/cannelloni/parser.cpp 一一对应）

| 层级 | 偏移 | 长度 | 字段 | 编码 | 说明 |
|---|---|---|---|---|---|
| 包头 | +0 | 1 | `version` | u8 | 固定 2；不等于 2 直接丢弃 |
| 包头 | +1 | 1 | `op_code` | u8 | 0=DATA；UDP 路径只有 DATA（ACK/NACK 未实现） |
| 包头 | +2 | 1 | `seq_no` | u8 | 发送方自增；接收方**不校验**（乱序不报错） |
| 包头 | +3 | 2 | `count` | u16 BE | 本包帧数；0 表示空包，静默忽略 |
| 帧 | +5 | 4 | `can_id` | u32 BE | bit31=CAN_EFF_FLAG(扩展帧)，bit30=CAN_RTR_FLAG(远程帧) |
| 帧 | +9 | 1 | `len` | u8 | **bit7=0x80 = CAN FD 帧**，低 7 位 = 真实字节数(0..64) |
| 帧 | +10 | 1 | `flags` | u8 | 仅当 len.bit7=1：Linux `canfd_frame.flags`（bit0=BRS，bit1=ESI） |
| 帧 | +11 | len | `data` | u8[] | RTR 帧没有数据段（len 仍表示 DLC） |

`canfd_len()` 是理解这个格式的钥匙（ref2/cannelloni/cannelloni.h）：

```cpp
/* Helper function to get the real length of a frame */
inline uint8_t canfd_len(const struct canfd_frame *f) {
  return f->len & ~(CANFD_FRAME);      /* CANFD_FRAME = 0x80 */
}
```

也就是说 cannelloni 把"是不是 FD 帧"塞进了 `len` 的最高位：`len=0x88` 表示"8 字节的 CAN FD 帧"，
`len=0x08` 表示"8 字节的经典帧"。**漏掉 `& 0x7F` 就会把 0x88 当成 136 字节读越界**——
这是自己实现网关时第一个会遇到的问题。板端 `gw_encode_frame()/gw_decode_frame()` 与主机
`host/can_bridge/udp2vcan.py` 都按这个语义实现。

**单包容量**：经典帧每帧 5 B 头 + data，FD 帧 6 B 头 + data。板端默认单包最多 16 帧，
最大 5 + 16×(4+1+1+64) = 1125 B，小于 1400 B，不碰 MTU；主机 cannelloni 侧按 `linkMtuSize`
动态算容量，通常一包能装几十个 8 字节帧。

### 28.4.6 CAN↔UDP 时延预算

一次 `cansend` 到 `candump` 看到回显，时间花在 6 段上（图 28-3）：

| 段 | 典型量级 | 影响因素 | 可调项 |
|---|---|---|---|
| ① 主机 cannelloni 攒包 | **1~100 ms** | `-t` 缓冲超时，默认 100000 µs | 必须改小：`-t 1000`（1 ms） |
| ② 主机 UDP → 板 ENET | 0.05~0.2 ms | 千兆以太网 + lwIP 收包 | 无（已近线速） |
| ③ 板网关 UDP→CAN1→总线→CAN0 | 0.1~0.4 ms | CAN 500k 一帧 ~0.26 ms（含位填充） | 提高波特率（FD 数据段 2 Mbit/s） |
| ④ DUT 处理 + 回帧 | 0.1~1 ms | 应用层调度（本板 16 帧/轮询上限） | 主循环顺序、去掉逐帧 printf |
| ⑤ 板 CAN1→UDP | 0.05~0.2 ms | lwIP 发送 + 1 次 pbuf 拷贝 | 批量打包（本组件收到即发，不额外攒包） |
| ⑥ 主机 UDP→vcan0→candump | 0.1~0.5 ms | 主机调度 + vcan tbf 排队 | vcan `latency` 别设太大 |

**结论**：默认配置下时延是"百毫秒级"，99% 来自 ① 的 100 ms 攒包。把 `-t` 改成 1000 µs 后，
端到端往返进入**个位数毫秒**（§28.6 有实测分布）。这也是为什么 `run_tunnel.sh` 里
`BUF_US` 默认 1000 而不是 cannelloni 的默认值。
### 28.4.7 表 28-3 寄存器与驱动 API 对照（应用层视角）

| # | 功能 | MCAN/ENET 寄存器或位域 | 驱动 API | 本仓库代码位置 |
|---|---|---|---|---|
| 1 | CAN 位时序（仲裁段） | `NBTP.NBRP/NTSEG1/NTSEG2/NSJW` | `mcan_set_can_nominal_bit_timing()` | `components/app_can/src/app_can.c` `node_init()` |
| 2 | CAN FD 数据段 + TDC | `DBTP.DBRP/DTSEG1/DTSEG2/TDC`、`TDCR` | `mcan_set_can_data_bit_timing()` / `mcan_apply_tdc_config()` | 同上（FD 档位 SSP=15） |
| 3 | 发送 FIFO | `TXBC/TXFQS/TXFQS.TFQPI` | `mcan_transmit_via_txfifo_nonblocking()` | `app_can_send()` |
| 4 | 接收 FIFO0/1 | `RXF0C/RXF0S/RXF0A`、`RXF1C/RXF1S/RXF1A` | `mcan_read_rxfifo()` | `app_can_recv()` |
| 5 | 验收过滤器 | `SIDFC/XIDFC/GFC/XIDAM` | `mcan_config_all_filters()` | `app_can_set_filter()` |
| 6 | 消息 RAM 基址（AHB SRAM） | `MCAN_MSG_BUF_BASE_VALID_START=0xF0200000` | `mcan_set_msg_buf_attr()` | `app_can.c` 顶部 `#if MCAN_SOC_MSG_BUF_IN_AHB_RAM` |
| 7 | 协议状态 / 错误计数 | `PSR/LEC/BO/EP`、`ECR.TEC/REC` | `mcan_get_protocol_status()` / `mcan_get_error_counter()` | `app_can_get_stats()` |
| 8 | ENET DMA 描述符环 | `ENET DMA TX/RX 描述符（OWN/IC/FS/LS）` | `enet_lwip_output()` / `ethernetif_input()` | SDK `samples/lwip/ports/baremetal/ethernetif.c` |
| 9 | ENET MAC 地址 / 端口速率 | `ENET_MAC_ADDR0_H/L`、`ENET_CTRL2.PHY_INF_SEL` | `enet_init()`、`enet_set_port_speed()` | SDK `samples/lwip/common/single/common.c` |
| 10 | RGMII 时钟延迟 | `ENET_CTRL0.TXCLK_DLY_SEL/RXCLK_DLY_SEL` | `board_init_enet_rgmii_clock_delay()` | `boards/hpm5e31_1/board.c` |
| 11 | GPIO（收发器 STB / 状态灯） | `GPIO_DO/DI`（PE03 控制 4 颗 TCAN1044AV） | `gpio_write_pin()` | `boards/hpm5e31_1/ext/board_can.h` |
| 12 | DMAMUX（CAN/UART 等） | `DMAMUX_MUXCFG` | `dmamux_config()` | 本组件不用 DMA，纯轮询即可 |

> 应用层章为什么也要寄存器表：应用层协议出问题时，90% 的定位路径是"协议字段 → 驱动 API → 寄存器位"。
> 例如 ISO-TP 收不到首帧，先看 RXFIFO0 是否满（`RXF0S.F0FL`）、再看过滤器是否把 0x7E0 拒了（`GFC`）。

## 28.5 例程

本章的例程分两半：**板端固件** `samples/app/can_gateway/` 与**上位机链路** `host/can_bridge/`。
板端固件 = cannelloni v2 网关 + CAN0 监视/回显节点；上位机链路 = vcan + cannelloni + 验收脚本。

### 28.5.1 表 28-4 例程清单

| 路径 | 覆盖特性 | 关键配置 | 实测现象 | 日志 |
|---|---|---|---|---|
| `samples/app/can_gateway/` | CAN1↔UDP4000 网关；CAN0 心跳/回显/监视；shell 命令 | CAN 500k/SP87.5%；UDP 4000；lwIP 192.168.2.100 | 见 §28.6 | `logs/*can_gateway*.log` |
| `components/app_can/` | MCAN 初始化/收发/过滤器/统计 | 经典 500k 或 FD 1M/2M | 内部环回 PASS | 同上 |
| `components/app_lwip/` | ENET0+YT8011+lwIP+UDP socket | 静态 IP、1 ms 时基 | ping 0% 丢包 | 同上 |
| `components/app_can_gw/` | cannelloni v2 编解码与转发 | 单包 ≤16 帧、peer 自动学习 | 心跳/回显/延迟/丢包 | 同上 |
| `host/can_bridge/acceptance.sh` | 主机侧一键验收 | ping+candump+cansend+bench | 五项 PASS/FAIL 汇总 | `/tmp/can_bridge_accept/` |
| `host/can_bridge/bench_gateway.py` | 心跳/往返延迟/丢包 | python-can 4.6.1 | avg/p50/p99 + 丢包率 | 同上 |
| `host/can_bridge/udp2vcan.py` | 纯 Python 备用桥（同协议） | 无 cannelloni 时用 | 与 cannelloni 等价 | 同上 |

### 28.5.2 板端主循环（关键代码，摘自 `samples/app/can_gateway/src/main.c`）

网关的实时性取决于主循环顺序：**先收 UDP（把上位机帧尽快写进 CAN），再发 UDP（把 CAN 帧尽快打包）**。

```c
while (1) {
    bsp_shell_poll();       /* letter-shell 命令，非阻塞 */
    app_lwip_poll();        /* ENET 收包 → lwIP → app_can_gw 的 udp_recv 回调 → 写 CAN1 */
    app_can_gw_poll();      /* CAN1 收到的帧 → 打包 → UDP 发回主机 */
    can0_poll();            /* DUT 节点：收帧打印 + 0x123→0x321 回显 */
    hb_tick();              /* 100 ms 心跳 0x321 */
}
```

单次 `app_can_gw_poll()` 最多从 CAN1 取 `APP_CAN_GW_MAX_FRAMES_PER_PKT`（16）帧打成一个 UDP 包；
没有帧就立即返回，不空转、不阻塞。UDP→CAN 方向在 lwIP 回调里完成（回调只做"解析 + 写 MCAN TX FIFO"，
用 `timeout_us=0` 的非阻塞发送，FIFO 满就计 `drop_can` 而不是在回调里等）。

### 28.5.3 上位机三步启动

```bash
cd host/can_bridge
sudo ./vcan_setup.sh            # 1) 建 vcan0 + tbf 500kbit 限速
./build_cannelloni.sh           # 2) 只做一次：构建 stock cannelloni（-DSCTP_SUPPORT=OFF）
./run_tunnel.sh &               # 3) vcan0 <-> 192.168.2.100:4000（-t 1000 低延迟）
candump vcan0                   # 应看到 10 Hz 的 321#...CA FE BA BE
```

没有 cannelloni 时：`./udp2vcan.py --iface vcan0 --board 192.168.2.100`（同一协议，纯 python-can）。
## 28.6 上板实测

测试台：Bench B（主机 `enp6s0 192.168.2.1/24` ↔ 千兆车载以太网盒 ↔ 板 `192.168.2.100/24`，
板端 CAN0/CAN1 板内互连）。一条命令完成"烧写 + 串口 + 上位机联调"：

```bash
cd hpm5e31_1
bash scripts/hwrun.sh samples/app/can_gateway --secs 12 --send "quiet;gw on;pass" \
     --post "timeout 90 bash host/can_bridge/acceptance.sh" --lock-wait 900
```

### 28.6.1 表 28-5 实测结果（2026-10-30 17:43，`logs/20261030-174321-can_gateway.log`）

| # | 项目 | 条件 / 方法 | 实测值 | 判定 |
|---|---|---|---|---|
| 1 | ping（Bench C 底座） | `ping -c 5 -i 0.2 192.168.2.100` | 5/5 收到，0% 丢包；rtt min/avg/max/mdev = 0.129/0.231/0.458/0.116 ms | PASS |
| 2 | 板端 PASS 结论行 | 串口 `pass` 命令 | `[pass] CAN0=OK CAN3-loop=PASS lwip=OK link=UP gw=CAN1/UDP4000 rc=0` | PASS |
| 3 | CAN3 内部环回自检 | `app_can_loopback_test()` | 发 8 B 经典帧收回，数据逐字节一致 | PASS |
| 4 | 心跳（CAN0→vcan0） | `candump vcan0` 抓 2 s，0x321 载荷 CA FE BA BE | 20 帧 / 2 s = **10.0 Hz**（配置 100 ms 周期） | PASS |
| 5 | 回显（vcan0→CAN0→vcan0） | `cansend vcan0 123#1122334455667788` + `candump` | 看到 `321#1122334455667788`，载荷一致 | PASS |
| 6 | 往返延迟 | python-can 100 次（间隔 20 ms），`-t 1000` | avg **2.24 ms**，p50 2.27，p95 2.40，p99 **2.48**，max 2.48，min 1.72 ms；100/100 收到 | PASS |
| 7 | 300 帧/s 丢包 | python-can 连续 5 s = 1500 帧 | 回显 1500 帧，丢失 0 = **0.00%**（阈值 1%） | PASS |
| 8 | 资源占用 | 链接报告 | Flash 138 440 B(13.2%)，DLM 95 168 B(**72.6%**)，AHB_SRAM 10 KB(31.3%)，AXI_SRAM 30 944 B | — |

主机侧输出已归档到 `logs/host-acceptance-174321.log`（含 ping/心跳/回显/延迟全部原始输出），
本条表中的 ping/延迟/丢包数字均可在该文件里逐行复核。

补充说明：

- 同一固件做了两次独立验收：17:34 那次延迟 avg 2.26 ms / p99 2.49 ms、300 帧/s 丢失 2/1500 = 0.13%；
  17:43 这次 avg 2.24 ms / p99 2.48 ms、丢失 0/1500。**延迟稳定在 2.2~2.5 ms**，丢包在 0~0.13% 之间抖动，
  属于主机调度噪声，不是链路问题。
- 心跳 10 Hz 与固件 `HB_PERIOD_US=100000` 一致；主机 `candump` 时间戳间隔实测稳定在 100 ms 量级。
- 串口 5 s 一行的 `[alive]` 计数可直接判断板子是否还在跑（`hb=50/100` 说明 5 s / 10 s 内发出 50/100 帧心跳）。

### 28.6.2 图 28-3 往返延迟分布与统计（100 次，`-t 1000`）

![图 28-3 往返延迟分布与统计](figures/28/fig-28-3-latency.png)

### 28.6.3 图 28-5 `-t 1000` 与 `-t 100000` 的完整延迟分布（各 100 次）

表 28-6 只给了 avg/p99 两个点，这里把两次运行的 100 个往返样本画成直方图与 CDF（图 28-5）：

![图 28-5 两种攒包超时下的延迟分布](figures/28/fig-28-5-latency-hist.png)

**图 28-5**：左图是延迟直方图，右图是 CDF。`-t 1000`（蓝）avg 2.24 ms、p99 2.48 ms、min 1.72 ms，
样本集中在 2.0~2.5 ms；`-t 100000`（橙）avg 79.89 ms、p99 101.00 ms、min 77.87 ms，
整条分布几乎平移了 100 ms，正好是 cannelloni 默认的攒包时间。样本与作图脚本：
`tools/figs/app29_data/bench_t1000.json`、`bench_t100000.json`、`tools/figs/app29_latency_hist.py`。


### 28.6.4 攒包超时（`-t`）的影响：定量对比

cannelloni 的 `-t` 参数是"帧在主机侧最多攒多久再合成一个 UDP 包"，默认 100 000 µs = 100 ms。
同一套板端固件、同一台主机，只改 `-t` 的实测对比见 表 28-6 与 图 28-4
（两次运行的板端日志：`logs/20261030-174321-can_gateway.log` 与 `logs/20261030-174200-can_gateway.log`）。

### 28.6.5 表 28-6 `-t` 对往返延迟的影响（同板同主机）

| cannelloni `-t` | 延迟 avg | p99 | 300 帧/s 丢包 | 说明 |
|---|---|---|---|---|
| 1000 µs（`run_tunnel.sh` 默认） | **2.24 ms** | 2.48 ms | 0.00% | 推荐：应用层联调用这个 |
| 100000 µs（cannelloni 默认） | **79.89 ms** | 101.00 ms | 46.27%* | 只在做"回放/抓包"时用，联调会被 100 ms 拖死 |

* 46.27% 不是 CAN/UDP 丢包：100 ms 攒包 + `bench_gateway.py` 200 ms 超时窗口，上一批还压在主机缓冲里、
下一批已判超时，表现为"丢帧"。它恰好说明攒包超时为什么必须调小。

**结论**：默认参数下 99% 的时延来自主机侧攒包，不是 MCU、不是 lwIP、不是 CAN。
这是应用层联调第一个要改的参数，也是本章"易错点 1"。

### 28.6.6 图 28-4 CAN↔UDP 时延预算与 `-t` 对比

![图 28-4 CAN-UDP 时延预算](figures/28/fig-28-4-budget.png)

## 28.7 性能与调优

### 28.7.1 时延的六段分解

| 段 | 实测/估算 | 优化手段 | 本章实测贡献 |
|---|---|---|---|
| ① 主机 cannelloni 攒包 | 1000 µs（`-t 1000`） | 改小 `-t`；用 `udp2vcan.py --flush-ms 1` | 主要项，决定 2 ms 量级 |
| ② 主机 UDP→板 ENET | <0.2 ms | 千兆已近线速（第 13 章 935 Mbit/s） | 小 |
| ③ 网关 UDP→CAN1（500k） | ~0.26 ms/帧（含位填充） | 上 CAN FD 2 Mbit/s 数据段 | 中等 |
| ④ DUT（CAN0）处理+回显 | <0.3 ms | 主循环不 printf、按 16 帧/轮批量取 | 小 |
| ⑤ 板 CAN1→UDP | <0.2 ms | 收到即发（不等攒包） | 小 |
| ⑥ 主机 UDP→vcan→python-can | <0.5 ms | vcan tbf `latency` 10 ms、别叠加别的 qdisc | 小 |

### 28.7.2 吞吐上限与"4 路网关"

- **单通道**：300 帧/s（8 B 帧）时丢包 0.13%；理论上 500 kbit/s 经典总线跑满约 **4300 帧/s**（第 08 章实测 4305），
  网关侧瓶颈不在 MCU（UDP 回调 + 16 帧/轮询），而在 vcan 限速与主机调度。
- **4 路网关**：`components/app_can_gw` 支持 4 个通道（CAN0/1/2/3），每通道一个 UDP pcb + 一个 `app_can_t`；

CAN 层的 4 路并发能力本轮已实测（第 09 章表 9-10、图 9-5）：两组总线双向、4 个 MCAN 全忙时
每方向 1425/3166 fps、聚合 5700/12664 fps、0 丢帧，主循环 CPU 忙闲比 81~82%。也就是说网关侧要打满 4 路，
真正的约束在主机侧 3 个 cannelloni + vcan 的调度，不在 MCU 的 CAN 搬移能力上。

  `gw full` 同时开 4000/4002/4003 三路。四路全开时 AHB_SRAM 消息 RAM 4×2.5 KB = 10 KB，DLM 不变
  （每通道 pcb/统计很小），因此**内存不是限制**；限制是主循环每轮最多搬 `16×N` 帧，需要按实际帧率调
  `max_frames_per_pkt` 或减少逐帧打印。
- **调优点**：① 去掉串口逐帧打印（`quiet`）；② 上位机侧工具优先 python-can 批量 `recv`；
  ③ CAN FD 场景把总线换到 1M/2M（`app_can_cfg_t.speed = APP_CAN_SPEED_FD_1M_2M`）。

### 28.7.3 与"以太网直连"的性能边界

Bench B 的价值是**工具链**，不是极限性能：一次往返多花 1~2 ms。对 UDS（P2 50 ms）、CANopen SDO、
XCP DAQ（10~100 ms 周期）都绰绰有余；对 1 ms 级硬实时环路（电机 FOC），必须用 Bench A（板内双节点）
或把控制环放在 MCU 内部，只把监控数据经网关送出。
## 28.8 易错点（现象 → 原因 → 解决）

### 28.8.1 易错点 1：cannelloni 默认攒包 100 ms，应用层联调"怎么都慢"

**现象**：`cansend` 后 `candump` 要等 ~100 ms 才看到回显；UDS 用默认 P2 超时直接超时。
**原因**：cannelloni 的 `-t`（buffer timeout）默认 100 000 µs，为"减少 UDP 包数"而设计；
板端收到包会立刻转发，慢在主机侧。
**解决**：`run_tunnel.sh` 默认 `-t 1000`；需要更低可到 200~500 µs（代价是 UDP 包数变多）。
**实测**：`-t 1000` 时 p99 = 2.49 ms；默认 `-t 100000` 时延迟被抬到 100 ms 量级（表 28-6）。

### 28.8.2 易错点 2：HPM5E31 的 MCAN 消息 RAM 在 AHB SRAM，不是 MCAN IP 内部

**现象**：`mcan_init()` 返回 2（`status_invalid_argument`），CAN 完全起不来；
`mcan_get_protocol_status()` 读出来全是 0；串口没有任何 CAN 报错。
**原因**：`hpm_soc_feature.h` 里 `MCAN_SOC_MSG_BUF_IN_AHB_RAM = 1`，消息 RAM 是共享的
AHB SRAM（`0xF0200000`，32 KB），驱动必须知道基地址；默认没登记时 RAM 配置校验失败。
**解决**：缓冲区加 `ATTR_PLACE_AT(".ahb_sram")`，并在 `mcan_init()` **之前**调用
`mcan_set_msg_buf_attr()`（见 `app_can.c` 顶部；4 实例各 640 words = 10 KB）。
**验证**：修好后 `AHB_SRAM` 段从 0 变 10 KB，`[pass] CAN0=OK`。

```c
#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
ATTR_PLACE_AT(".ahb_sram") static uint32_t s_msg_buf[APP_CAN_INST_COUNT][MCAN_MSG_BUF_SIZE_IN_WORDS];
...
mcan_msg_buf_attr_t attr = { (uint32_t) s_msg_buf[can_inst_index(base)], MCAN_MSG_BUF_SIZE_IN_WORDS * 4U };
mcan_set_msg_buf_attr(base, &attr);   /* 必须在 mcan_get_default_config/mcan_init 之前 */
#endif
```

### 28.8.3 易错点 3：`ipaddr_ntoa()` 返回静态缓冲区，一行 printf 打印三个地址会全错

**现象**：`printf("IP=%s MASK=%s GW=%s", ipaddr_ntoa(ip), ipaddr_ntoa(mask), ipaddr_ntoa(gw))`
打印出三个一样的地址（看起来像 lwIP 配错了，实际是打印 bug）。
**原因**：`ipaddr_ntoa()` 内部用 `static char str[]`，三次调用共享同一块内存。
**解决**：拆成三行 printf 或用 `ipaddr_ntoa_r()` 各自给缓冲区（组件 `app_lwip_print()` 已拆开）。
**教训**：调试时先怀疑"打印/工具"，再怀疑协议栈——这一条在本项目里骗过一次人。

### 28.8.4 易错点 4：vcan 不限速会淹掉 MCU 网关

**现象**：`cangen vcan0 -g 0` 全速灌包后，板端 `gw stat` 的 `drop_can` 飙升，主机侧大量丢帧。
**原因**：vcan 是内存回环，内核不做位定时/速率限制；CAN 帧瞬间灌入 → UDP 突发 → MCU 的 CAN TX FIFO（8 个元素）溢出。
**解决**：`sudo tc qdisc add dev vcan0 root tbf rate 500kbit latency 10ms burst 4000`（`vcan_setup.sh` 已做）。
注意 `latency` 参数不要设 100 ms（会让排队帧等 100 ms），10 ms 足够。

### 28.8.5 易错点 5：板锁 flock 死锁（多 agent 并行上板）

**现象**：17 个 `hwrun.sh` 全部卡住，日志不再更新，`/proc/locks` 显示的持锁 PID 已经不存在。
**原因**：旧实现把 flock 绑在会被子进程继承的 fd 上，持锁进程被 kill 后 fd 被孤儿进程继承 → 锁永不释放。
**解决**：`scripts/hwlock.sh` 改为 `mkdir` 原子锁 + PID 存活检测 + 退出 trap 自动释放；
另外用 `hwrun.sh --post` 在持锁期间跑上位机测试，避免"烧完就放锁、上位机还没测"。
**规则**：不要自己 `flock`；单次持锁 ≤90 s；`--post` 内命令必须带 `timeout`。

### 28.8.6 易错点 6：UDP 模式没有 ACK/重传，丢了就是丢了

**现象**：长时间跑 CANopen SDO 或 UDS 多帧，偶发"对端没收到"，总线上看帧数不对。
**原因**：cannelloni 的 UDP 路径只实现 DATA 包，enum 里的 ACK/NACK 只在 TCP/SCTP 模式用；
UDP 是 fire-and-forget，丢了不会重传。
**解决**：靠上层协议自己的重传（ISO-TP 的 N_Ar/N_As、J1939 TP 的 RTS/CTS、CANopen SDO 重传、
XCP 的 CMD 重发）；需要"绝不丢"的场合改用 TCP 模式或提高 `-x` 丢弃阈值并降速。

### 28.8.7 易错点 7：CAN FD 帧的 len 最高位 / flags 字节漏解析

**现象**：主机能收到 FD 帧但长度变成 136、解析越界；或 BRS 位丢失导致数据段速率不对。
**原因**：cannelloni 把"FD 帧"编码在 `len` 的 bit7，真实长度是 `len & 0x7F`，
FD 帧还要多读 1 字节 `flags`（bit0=BRS）。
**解决**：按 §28.4 的字段表实现；本仓库 `components/app_can_gw` 与 `host/can_bridge/udp2vcan.py`
都已处理，可直接对照。

### 28.8.8 易错点 8：串口逐帧打印拖慢网关

**现象**：`verbose` 开着时，300 帧/s 的测试开始丢帧，延迟 p99 变大。
**原因**：`printf` 走 921600 串口，一行 ~60 B ≈ 0.65 ms，逐帧打印会把主循环占满。
**解决**：跑性能/丢包测试前先发 `quiet`；日志里保留 `[alive]` 5 s 一行即可。

## 28.9 对比：上位机 CAN 链路方案

### 28.9.1 表 28-7 常见上位机 CAN 链路方案对比

| 方案 | 传输层 | 上位机接口 | 需要板端资源 | 延迟量级 | 带宽上限 | 适用场景 | 本板可行性 |
|---|---|---|---|---|---|---|---|
| **cannelloni v2**（本章） | UDP/TCP | SocketCAN `vcan0` | 1 路 CAN + ENET/lwIP | 1~5 ms（`-t 1000`） | 千兆以太网 | 多路 CAN 汇聚、远程调试、日志回放 | **已实测 PASS** |
| socketcand | TCP | SocketCAN `vcan0` | 1 路 CAN + ENET/lwIP | 1~3 ms | 百兆/千兆 | 与 cannelloni 类似，TCP 可靠但更重 | 需移植协议（结构简单，~500 行） |
| slcan（串口/USB-CDC） | 串口/UART | `slcand` → `can0` | UART 或 USB-CDC | 5~20 ms | 921600 baud ≈ 90 KB/s（约 800 帧/s 8B） | 单路、低成本、无以太网 | 可行（第 20 章 USB CDC 可复用），P2 例程 |
| candleLight gs_usb | USB | SocketCAN `can0`（内核 gs_usb） | USB device（需 USB 控制器） | <1 ms | USB 全速 12 Mbit/s | 高实时、单路/双路、桌面级分析仪 | 需 USB device 栈（HPM5E31 有 USB，但工作量大） |
| PCAN / Kvaser 等商用 | USB/PCIe | 厂商驱动 | 无（独立硬件） | <1 ms | 1 Mbit/s × 2~8 路 | 量产测试、HIL | 需采购硬件 |
| 自制 UDP 工具（`udp2vcan.py`） | UDP | python-can / SocketCAN | 同 cannelloni | 1~5 ms | 千兆 | 无 cannelloni 环境、二次开发 | **已随本章交付** |

**选型建议**：

- 需要"上位机直接用 candump/python-can/udsoncan" → **cannelloni**（最省事，社区成熟）；
- 需要可靠传输（不能丢帧）→ socketcand 或 cannelloni TCP 模式；
- 只有串口、成本敏感 → slcan；
- 需要 µs 级时间戳/硬触发 → 商用分析仪或 candleLight（USB 直连，不经以太网）。

## 28.10 待确认项与风险

**表 28-8 待确认项与风险（已逐条复核，2026-11-02）**

| 编号 | 项 | 现状 | 验证方法 / 说明 |
|---|---|---|---|
| U-29-1 | `-t 100000` 默认参数的完整延迟分布 | **已实测（2026-11-02 补画分布）** | 两次运行各 100 个往返样本：`-t 1000` avg 2.24 ms / p99 2.48 ms；`-t 100000` avg 79.89 ms / p99 101.00 ms（图 28-5，原始样本 `tools/figs/app29_data/bench_t1000.json`、`bench_t100000.json`） |
| U-29-2 | 4 路网关同时满载 | **已知限制** | 板端 `gw full` 可同时开 CAN1/2/3（UDP 4000/4002/4003），主机要 3 个 cannelloni 各绑一个 vcan；本轮共享板卡排队没取到窗口。验证：`gw full` + 3 个 `run_tunnel.sh`（改 IFACE/PORT）+ 四路 `cangen -g 1`，与 `gw stat` 的 fwd/drop 对照 |
| U-29-3 | CAN FD 帧经网关的端到端 | **已知限制** | 板端 FD 自测 PASS，网关的 FD 透传只做了代码路径审查（len 最高位/flags 解析，见易错点 7）。验证：`cansend vcan0 123##1AABBCC...` 后 `candump vcan0` 抓 0x321 回显并核对 FDF/BRS |
| U-29-4 | UDP 乱序/重复的统计 | **已知限制** | cannelloni 的 5 B 头带 1 字节 seq，本组件透传不校验；0.13% 丢包已测，但没有乱序/重复统计。验证：`tcpdump -i enp6s0 -w x.pcap udp port 4000` + python 解析 seq 单调性 |
| U-29-5 | 长时间（>1 h）稳定性 | **已知限制** | 已跑通的窗口是 10~90 s（心跳 10 Hz、300 帧/s 持续 5 s）。验证：循环跑 `acceptance.sh` 60 轮，每轮看 `[alive]` 计数与 `gw stat` 是否漂移；板锁一次 ≤90 s，必须分段 |
| U-29-6 | 主机防火墙/多网卡路由 | **已知限制** | 本机防火墙关闭，`enp6s0` 同时有 192.168.0.1/24 与 192.168.2.1/24。验证：开 `ufw` 后只放行 UDP 4000，再跑 `acceptance.sh` |
| U-29-7 | 单次持锁上限与长测冲突 | **已定位（已定案）** | 板锁规则 ≤90 s，1 h 长稳拆成多轮 `--secs 60`；设备持续在线类测试用 `scripts/board_lease.sh 120 <名字>` |
| U-29-8 | 参考来源里的本地路径 | **已定位（已清理）** | ref2 下载物改为上游仓库/官网链接（与第 27 章表 27-10/27-11 一致） |

7. 上游工具与源码（与第 27 章表 27-10/27-11 一致，链接核对日期 2026-11-02）：
   [can-utils](https://github.com/linux-can/can-utils)（candump/cansend/cangen/canplayer）、
   [cannelloni](https://github.com/mguentner/cannelloni)（CAN over IP 隧道，v2.1.2，本章网关协议的对端）、
   [cannelloni_ports](https://github.com/mguentner/cannelloni_ports)（移植补丁集）、
   [linux-can 文档](https://github.com/linux-can/can-doc)、
   [Linux SocketCAN](https://www.kernel.org/doc/html/latest/networking/can.html)、
   [python-can](https://github.com/hardbyte/python-can)（4.6.1，`bench_gateway.py` 用它发帧）、
   [iperf2](https://sourceforge.net/projects/iperf2/)（第 13 章吞吐测试）、
   [HPM SDK](https://github.com/hpmicro/hpm_sdk)（v1.13.0）。
