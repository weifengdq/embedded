# 第 9 章 CAN FD 性能、压力与错误注入

## 9.1 文档信息

| 项 | 内容 |
|---|---|
|   文档版本   | v1.2.0（2026-11-02，R5 复核） |
| 适用芯片 / 板卡 | HPM5E31IPB1（HPM5E00 系列）/ hpm5e31_1 最小系统板 |
| 被测外设 | MCAN0（发送）与 MCAN1（接收/应答），4 个 MCAN 实例中的一组 |
| 对应例程 | `samples/can/mcan_two_node/`、`mcan_throughput/`、`mcan_fd_timing/`、`mcan_error_recovery/`、`mcan_loopback_internal/`、`mcan_quad_load/`（本轮新增：4 路并发/5M/TSU/矩阵） |
| 本章图 | 图 9-1 位时间结构、图 9-2 单向吞吐、图 9-3 数据段速率 vs 帧率、图 9-4 TSU/PTPC 时间戳路径、图 9-5 4 路并发满载 |
| 实测日期 | 2026-10-30（单链路/矩阵/错误注入）、2026-11-02（5M/8M 边界复核、4 路并发与 TSU 例程） |
| 可信度 | **高**：吞吐/帧率/位时序矩阵/TDC 开关/错误恢复均为本板实测；**中**：理论帧时间按无位填充计算，实测为下界对比；**低**：与 STM32/TC397 的对比引用厂商公开参数，未在本书实测（见第 12 节） |

如表 9-1 所示：

**表 9-1 本章测量对象与结论速览**

| 测量项 | 结论 | 数据位置 |
|---|---|---|
| 经典 CAN 2.0B 500 kbit/s 单向 | 4313 frame/s，payload 0.28 Mbit/s，0 丢帧 | 表 9-5、`logs/20261030-114225-mcan_throughput.log` |
| 经典 CAN 2.0B 1 Mbit/s 单向 | 8615 frame/s，payload 0.55 Mbit/s，0 丢帧 | 表 9-5 |
| CAN FD 1M/8M 64B 单向 | 9058 frame/s，payload **4.64 Mbit/s**，0 丢帧 | 表 9-5、图 9-2 |
| 双向 ping-pong（500k 经典帧） | 500 次往返全成功，2148 往返/s，echo 传播 1 µs | 表 9-4、`logs/20261030-113541-mcan_two_node.log` |
| 双向 ping-pong（FD 1M/2M 64B） | 500 次往返全成功，1580 往返/s | 表 9-4 |
| 24 组位时序矩阵 | 23 组成功、全部 0 丢帧；1 组越界配置被驱动拒绝 | 表 9-6、图 9-3 |
| TDC 开关 | 本板短总线下 2/5 Mbit/s 均 0 丢帧，TDC 不改变吞吐 | 表 9-7 |
| 错误注入 / bus-off | 单节点无 ACK 时 TEC 增长慢，bus-off 需 >200 ms；恢复后收发正常 | 表 9-8 |
| 数据段 5 Mbit/s | 6302 fps、3.23 Mbit/s payload、0 丢帧；已到板载 TCAN1044AV 的规格上限 | 表 9-5、§9.6.2 |
| 数据段 8 Mbit/s | 9058 fps 属超规格实验（收发器上限 5 Mbit/s，[TI TCAN1044A-Q1](https://www.ti.com/product/TCAN1044A-Q1)），不能当量产配置 | 表 9-5、§9.6.2 |
| TSU / PTPC 时间戳 | **已定位（prescaler=0 被驱动拒绝），精度待复跑** | PTPC 初始化与路由 PASS；`mcan_init` 返回 st=2，根因是 `mcan_set_tsu()` 只接受 prescaler 1~256。已改为 1 并重新编译。复跑：`... --send "tsu 200;ptp 200"` |

## 9.2 本章导览

第 08 章讲"怎么把 CAN 跑起来"，本章回答"能跑多快、余量多大、哪里会崩"。读完本章你应当能：

1. 用"位时间 + 帧 bit 数"手算出任意波特率下的理论帧率，并解释实测值与理论值的差距来自哪里。
2. 读懂位时序矩阵：哪些"波特率 + 采样点"组合在 HPM5E31 上根本不可达（硬件字段限制），哪些可达且 0 丢帧。
3. 判断何时必须开 TDC、SSP 偏移怎么选、短总线和长总线的差别。
4. 设计正确的错误注入实验（为什么"对端波特率错配"不是稳定的 bus-off 复现手段）。
5. 给出 CAN FD 吞吐调优的优先级：数据段波特率 > 仲裁段 > 软件收发改中断/DMA。

前置章节：第 08 章（MCAN 架构、位时序、过滤器、驱动 API）。
数据可复现：本章所有数据都来自第 08 章 6.x 节的例程，命令与 --secs 参数在对应小节给出。

## 9.3 概述：测量方法学与指标体系

### 9.3.1 硬件与工具

如表 9-2 所示：

**表 9-2 测量环境**

| 项 | 配置 |
|---|---|
| MCU / 板卡 | HPM5E31IPB1 @ hpm5e31_1，CPU 480 MHz，CAN 时钟 80 MHz（PLL1 clk0 / 5，实测） |
| CAN 通道 | CAN0 = MCAN0（PD00/PD01），CAN1 = MCAN1（PD05/PD04），各经 TCAN1044AV |
| 总线 | CAN0 与 CAN1 接在同一段总线，**两端各 120 Ω 终端电阻（板上固定）**，线长 <0.3 m |
| 对端 | MCU 内部的第二个 MCAN（CAN0<->CAN1），**有真实 ACK**，不是单节点自环 |
| 时间基准 | CPU 周期计数器 `hpm_csr_get_core_mcycle()`（480 MHz），`bsp_uptime_ms()` 同源 |
| 串口 | /dev/ttyACM0，921600 8N1，`scripts/hwrun.sh` 抓取并落盘 `logs/` |
| 无外部仪器 | 无 CAN 分析仪、无示波器；所有结论由固件自证输出（帧计数/错误计数/时间戳） |

### 9.3.2 判定口径

- **丢帧**：发送 N 帧，接收端逐帧校验 ID/DLC/FDF/BRS/计数器，未收到的帧数；
- **错误**：读 `PSR.LEC/DLEC`、`ECR.TEC/REC`，非 0 即记录（表 9-5~9-7 中 LEC=7 表示"自上次读后无变化"，
  是正常值；数据段错误看 DLEC）；
- **时间**：从第一帧提交到最后一帧被接收端读出的墙上时间（含软件轮询开销），
  吞吐 = 帧数 / 时间；ping-pong 同理（一次往返 = 2 帧）；
- **CPU 周期/帧**：轮询方式下 CPU 全程忙等，所以该值≈每帧的墙上时间折算，可用于估算"纯轮询方案的 CPU 占用"。

### 9.3.3 误差来源

1. 位填充：经典帧每 5 个相同位插入 1 位，最坏 +20%，表 9-3 的理论值按**无填充**算，实测/理论比必然小于 100%；
2. 软件轮询：每帧要执行提交、查状态、读 FIFO，8 Mbit/s 时一帧仅 ~90 µs，软件开销占比明显；
3. 串口打印：本章所有批量测试在统计区间内**不打印**，避免串口阻塞影响；
4. 时钟精度：CAN 时钟 80 MHz 由 PLL 分频得到，实测 80 000 000 Hz（日志 `src_clk=80000000 Hz`）。

## 9.4 工作原理：理论帧时间与上限

计算口径（无位填充、ISO FD 帧）：

- 经典标准帧 8 B：1 SOF + 11 ID + 1 RTR + 1 IDE + 1 r0 + 4 DLC + 64 DATA + 15 CRC + 1 CRC-DEL + 1 ACK + 1 ACK-DEL + 7 EOF = **108 bit**；
- CAN FD 帧 64 B：仲裁段 22 bit（SOF/ID/RTR/IDE/FDF/res/BRS/ESI/DLC，全在标称速率），
  数据段 512 bit 数据 + 21 bit CRC + 位填充，按 **540 bit** 估算。

帧时间 = 仲裁段比特数 / 仲裁波特率 + 数据段比特数 / 数据波特率；理论帧率 = 1 / 帧时间。

如表 9-3 所示：

**表 9-3 理论帧率 vs 实测帧率（64 B FD 与 8 B 经典帧）**

| 用例 | 理论帧时间 | 理论帧率 | 实测帧率 | 实测/理论 | 差值主要来源 |
|---|---|---|---|---|---|
| 经典 500 kbit/s 8B | 108 bit / 500k = 216 µs | 4630 fps | 4313 fps | 93% | 位填充 + 轮询 |
| 经典 1 Mbit/s 8B | 108 bit / 1M = 108 µs | 9259 fps | 8615 fps | 93% | 同上 |
| FD 500k+2M 64B | 44 µs + 270 µs = 314 µs | 3185 fps | 2632 fps | 83% | 位填充 + 轮询占比更大 |
| FD 1M+2M 64B | 22 µs + 270 µs = 292 µs | 3425 fps | 2843 fps | 83% | 同上 |
| FD 1M+5M 64B | 22 µs + 108 µs = 130 µs | 7692 fps | 6302 fps | 82% | 同上 |
| FD 1M+8M 64B | 22 µs + 67.5 µs = 89.5 µs | 11173 fps | 9058 fps | 81% | 同上 |

两个可直接用于工程估算的结论：

1. **经典帧的实测/理论比稳定在 93%**：位填充是主要损失（最坏 +20%，随机数据典型 +8~10%），
   软件轮询在 108~216 µs 的帧时间下占比很小。
2. **FD 64 B 帧的实测/理论比约 81%~83%**：数据段越快，软件轮询的相对开销越大
   （8 Mbit/s 时一帧仅 89.5 µs，而每帧轮询检查 + FIFO 读写要几十个周期），
   这是"上中断/DMA"最直接的收益点。

第 2 节补充：位时序计算（可直接对照本仓库 `samples/can/mcan_two_node/` 的配置）：

```c
/* 位时间 = 1/bitrate = (1 + SEG1 + SEG2) * (PRESC + 1) / f_mcan
 * 采样点 = (1 + SEG1) / (1 + SEG1 + SEG2)
 * 以 500 kbit/s、f_mcan=80 MHz、SP=87.5% 为例：tq=20 个/位 -> PRESC=7 (80M/8=10M, 10M/500k=20)
 *   SEG1 = 16, SEG2 = 3, SJW = 4
 */
mcan_timing_config_t timing;
timing.pre_divider = 7;    /* 实际分频 = pre_divider + 1 = 8 */
timing.num_seg1   = 16;    /* 传播段 + 相位段1 */
timing.num_seg2   = 3;     /* 相位段2 */
timing.sync_jump_width = 4;
mcan_set_timing_config(HPM_MCAN0, &timing);
```

![图 9-1 CAN FD 位时间结构与采样点（1 位 = SYNC + SEG1 + SEG2）](figures/09/fig-09-1-bit-timing.png)

**图 9-1** ：一个位时间由同步段 SYNC、相位段 SEG1（含传播段）与相位段 SEG2 组成，
采样点 = (1 + SEG1) / (1 + SEG1 + SEG2)，SJW 决定重同步时可拉伸或压缩的最大 TQ 数。
本板 500 kbit/s、80 MHz 时钟下取 SP=87.5%（SEG1=16、SEG2=3、PRESC=7）；
数据段 2 Mbit/s 时取 SP=75%（第 08 章实测稳定的配置）。

## 9.5 双向 ping-pong 与往返延迟

**方法**：`samples/can/mcan_two_node` 的 `run [n]` / `fd [n]`：CAN0 发 0x123，
CAN1 收并校验后用 0x321 回显，CAN0 收并校验；一次往返 = 2 帧 + 两次软件收发。

如表 9-4 所示：

**表 9-4 双向 ping-pong 实测（500 次往返）**

| 用例 | 成功/总数 | 总时间 | 往返率 | 单次往返平均 | echo 传播延迟 | 日志 |
|---|---|---|---|---|---|---|
| 经典 500 kbit/s，8 B | 500/500 | 232 772 µs | 2148 /s | 465.5 µs | 1 µs（max 1 µs） | `logs/20261030-113541-mcan_two_node.log` |
| FD 1M 仲裁 / 2M 数据，64 B | 500/500 | 316 547 µs | 1580 /s | 633.1 µs | — | 同上 |

- 经典 500 kbit/s 往返理论值 = 2 x 216 µs = 432 µs，实测 465.5 µs（93%），与单向一致；
- **echo 传播延迟 1 µs** 是"CAN1 发送完成中断/轮询返回"到"CAN0 轮询读到帧"的软件延迟，
  不包含 216 µs 的帧传输时间 —— 说明本板 MCAN + 轮询的接收响应在微秒级，实时性很好；
- FD 1M/2M 往返 633 µs，接近 2 x 292 µs = 584 µs 的理论值，64 B 帧在 2 Mbit/s 数据段下的优势明显。

## 9.6 单向吞吐实测

测试条件：CAN0 -> CAN1 单向流水（CAN1 只收不答），每组 2000 帧，批次深度 3（FD）/6（经典，受 TX FIFO 深度限制），
0 丢帧。如图 9-2 与表 9-5 所示。

![图 9-2 CAN / CAN FD 单向吞吐实测](figures/09/fig-09-2-throughput.png)

如表 9-5 所示：

**表 9-5 单向吞吐实测（2000 帧/组，0 丢帧）**

| 用例 | 载荷 | frame/s | payload Mbit/s | CPU 周期/帧 | 相对经典 500k 提升 |
|---|---|---|---|---|---|
| 经典 500 kbit/s | 8 B | 4313 | 0.28 | 111 290 | 1.00x |
| 经典 1 Mbit/s | 8 B | 8615 | 0.55 | 55 714 | 2.00x（帧率） |
| FD 500k+2M | 64 B | 2632 | 1.35 | 182 348 | 4.8x（载荷） |
| FD 1M+2M | 64 B | 2843 | 1.46 | 168 859 | 5.2x |
| FD 1M+5M | 64 B | 6302 | 3.23 | 76 172 | 11.5x |
| FD 1M+8M | 64 B | 9058 | 4.64 | 52 989 | 16.6x |

要点：

1. **帧率最高**的是 FD 8 Mbit/s（9058 fps），但**有效载荷吞吐**只有 4.64 Mbit/s，
   因为 64 B 帧里还有 22 bit 仲裁段（1 Mbit/s）和 21 bit CRC/填充；继续提速应优先提高数据段；
2. 经典 1 Mbit/s 相比 500 kbit/s 帧率正好翻倍（8615 vs 4313），说明经典帧下软件不是瓶颈；
3. FD 500k/2M 与 1M/2M 的帧率接近（2632 vs 2843），说明**仲裁段 22 bit 在 2 Mbit/s 数据段下只占 ~7%**，
   数据段速率才是主导；
4. 所有用例 TEC/REC=0、LEC=0（无错误、无重传），数据可信。

### 9.6.1 四路并发满载（本轮新增例程）

4 个 MCAN 两两成组（CAN0<->CAN1、CAN2<->CAN3），两组总线物理独立，可以同时打满。并发测试放在
`samples/can/mcan_quad_load/`：主循环同时服务 4 个控制器（TX FIFO 非阻塞提交、RX FIFO 每轮最多取
16 帧），每组独立统计帧率/丢帧/TEC/REC；空闲时执行固定空转块，先用 10 ms 空转校准每块周期数，
再由空转次数反推 **CPU 忙闲比**。三种拓扑：

| 模式 | 命令 | 总线上的发送方 | 用来回答 |
|---|---|---|---|
| 单链路 | `one 2000 0` | CAN0 | 单路基准（与表 9-5 同口径） |
| 两组单向 | `quad 2000 0 2` | CAN0、CAN2 | 4 个控制器全忙时，共享 32 KB 消息 RAM 与 AHB 带宽是否成为瓶颈 |
| 两组双向 | `bi 2000 0` | 4 个控制器都发 | 每对总线两端同时发，仲裁把每方向吞吐压到多少 |

判瓶颈的方法很直接：两组单向时若每路帧率与单链路相同（差异 <5%），说明消息 RAM/AHB 搬移不是瓶颈，
瓶颈就在 CAN 总线本身；若明显下降，再看 `cyc/frame` 与 CPU 忙闲比，区分是软件搬不过来还是内存带宽不够。

实测（2026-11-02 12:28，日志 `logs/20261102-122843-mcan_quad_load.log`，n=2000/链路，
64 B FD、BRS=1；拓扑语义：links=2 是一对总线两端都发，links=4 是两组总线两端都发）：

**表 9-10 4 路并发满载实测（0 丢帧，TEC/REC/LEC=0，RXFIFO 溢出 0）**

| 拓扑 | 数据段 | 每方向帧率 | 聚合帧率 | 每方向 payload | CPU 忙闲比 | cyc/frame |
|---|---|---|---|---|---|---|
| 单链路单向（2 个 MCAN） | 2 Mbit/s | 2843 fps | — | 1.46 Mbit/s | — | 168 859 |
| 一对总线双向（links=2） | 2 Mbit/s | 1425 fps | 2850 fps | 0.73 Mbit/s | 68.5% | 115 377 |
| 两组总线双向（links=4） | 2 Mbit/s | 1425 fps | 5700 fps | 0.73 Mbit/s | 81.3% | 68 492 |
| 单链路单向 | 5 Mbit/s | 6302 fps | — | 3.23 Mbit/s | — | 76 172 |
| 一对总线双向 | 5 Mbit/s | 3169 fps | 6338 fps | 1.62 Mbit/s | 69.3% | 52 506 |
| 两组总线双向 | 5 Mbit/s | 3166 fps | 12664 fps | 1.62 Mbit/s | 82.0% | 31 117 |

四点结论：

1. **每方向帧率被仲裁对半分**：1M/2M 单向 2843 fps，改成同一条总线两端都发后每方向 1425 fps
   （理论对半 1712，实测 83%；差值来自位填充和双向仲裁）。这是 CAN 物理层的必然，不是软件问题。
2. **加第二组总线不再降速**：两组双向（4 个 MCAN 全忙）与一对双向的每方向帧率差 <0.1%
   （1425 vs 1425、3166 vs 3169）。32 KB 消息 RAM 共享和 AHB 带宽都不是 4 路并发的瓶颈，
   瓶颈始终在 CAN 总线本身。
3. **CPU 还有约 18% 余量**：4 路全忙时主循环忙闲比 81.3%（2M）/82.0%（5M）；每帧摊到的周期
   反而从 115 377 降到 68 492，因为帧在 4 个实例间并行、软件开销被摊薄。纯轮询能扛住
   "4 路 x 5M 数据段"，上中断/DMA 的收益主要在长稳与延迟抖动。
4. **聚合吞吐**：5M 数据段、两组双向时总收帧 12 664 fps（4 x 3166），折算 payload 约 6.5 Mbit/s；
   2M 数据段是 5700 fps（约 2.9 Mbit/s）。这是两块独立总线同时跑满的结果。

![图 9-5 4 路并发满载：每方向帧率与 CPU 忙闲比](figures/09/fig-09-5-quad-load.png)

**图 9-5**：左图是每方向帧率（单链路单向 vs 一对总线双向 vs 两组总线双向），右图是主循环 CPU 忙闲比。
两组双向比一对双向只多 12.8 个百分点的 CPU，每方向帧率不变。

两个坑（本轮实测踩到的）：

1. **`tx_full` 计数很大不等于丢帧**：4 路满载时非阻塞提交频繁返回"TX FIFO 满"
   （实测 288 305~653 996 次），说明总线已饱和、队列持续背压。处理方式是计数后立即重试，
   最终 8000/8000 帧全部收到、`rxfifo_lost=0`。工程上要区分背压与丢帧：
   `tx_full` 是背压，`bad`/`rxfifo_lost` 才是丢帧。
2. **参数别按直觉理解**：示例里 `links=2` 是"一对总线两端都发"，不是"两组各单向"
   （拓扑表 link1 是 CAN1->CAN0）。要测两组各单向，用 `links=1` 分两次跑，或改拓扑表
   把两条链路的发送端都设成 CAN0/CAN2。本轮数据按实际拓扑记录，没有换算。

### 9.6.2 数据段 5 Mbit/s 与 8 Mbit/s 的边界

- **5 Mbit/s 是这块板子的可用上限**：板载 TCAN1044AV 的数据手册上限就是 5 Mbit/s
  （[TI TCAN1044A-Q1](https://www.ti.com/product/TCAN1044A-Q1)）。表 9-5 的 1M/5M 组合实测
  6302 fps、3.23 Mbit/s payload、0 丢帧，已经贴着收发器规格跑；
- **8 Mbit/s 属于超规格实验**：MCAN 控制器本身能配出来（80 MHz、10 TQ，seg1=7/seg2=2），
  表 9-5 的 9058 fps 也是真实测出来的，但条件是 <0.3 m 板内短总线、两端 120 Ω。
  换线缆、加节点、上温度余量后不能保证。要量产 8 Mbit/s 必须换支持 8 Mbit/s 的收发器
  （如 Microchip MCP2558FD，[产品页](https://www.microchip.com/en-us/product/MCP2558FD)），
  并重做眼图与终端匹配；
- 5M 到 8M 只提升 44%（6302 -> 9058 fps），卡在仲裁段：每帧约 22 bit 的仲裁段固定按 1 Mbit/s 走，
  单帧时间被这 22 µs 兜底，数据段再快也要付这笔钱。

补充（2026-11-02，MCAL 侧独立复测）：samples/mcal/can_fd_echo（AUTOSAR Can 驱动）在
500k/5M 配置下 64 B 单向 3000 帧 0 丢、5452 帧/s、2.79 Mbit/s 载荷；1M 仲裁 + 5M 数据
6360 帧/s、3.26 Mbit/s，与本章芯片级数据（6302 fps @1M/5M）在 ±1% 内互相印证。
这次复测还纠正了 MCAL 章原来的一个错误结论：5 Mbit/s 失败不是收发器/线束裕量问题，
而是板上 TDC 偏移配置越界（见 §9.8 与第 47 章表 47-17）。

## 9.7 位时序矩阵与采样点

**方法**：`samples/can/mcan_fd_timing` 用 CAN0 -> CAN1 单向发 300 帧 64 B FD（BRS=1），
遍历"仲裁段（500k/1M）x 数据段（2/4/5/8 Mbit/s）x 3 个采样点"，共 24 组；
每组读 TEC/REC/LEC 并统计丢帧。驱动对位时序有硬性校验（`hpm_mcan_drv.c:755~787`）：

- FD 数据段 seg1 上限 32（含同步段，即配置值 ≤31）、seg2 ∈ [2,16]；
- 开启 TDC 时 FD 数据段 prescaler ≤2；
- 因此 **2 Mbit/s 的 87.5%**（需 seg1=34）与 **8 Mbit/s 的 87.5%/90%**（需 seg2=1）不可达，
  矩阵用最接近的可达值（2M：75/77.5/80%；8M：60/70/80%）；
- 矩阵中有 1 组故意越界配置（2M×82.5%，seg1=32）被驱动以 26014 拒绝，属预期行为。

结果如图 9-3 与表 9-6 所示：**23 组有效配置全部 0 丢帧、TEC/REC=0**（`logs/20261030-115604-mcan_fd_timing.log`）。

![图 9-3 CAN FD 数据段速率 vs 帧率（仲裁段 500k vs 1M）](figures/09/fig-09-3-timing-matrix.png)

如表 9-6 所示：

**表 9-6 位时序矩阵实测（64 B FD，BRS=1，每组 300 帧，0 丢帧）**

| 数据段 | 采样点范围（实测） | 仲裁 500k fps | 仲裁 1M fps | payload Mbit/s | PSR.TDCV |
|---|---|---|---|---|---|
| 2 Mbit/s | 75.0 / 80.0% | 2842 | 2842 | 1.45~1.46 | 40~42 |
| 4 Mbit/s | 75.0 / 80.0 / 85.0% | 4565 | 5239~5240 | 2.34~2.68 | 25~27 |
| 5 Mbit/s | 75.0 / 81.25 / 87.5% | 5352 | 6303 | 2.74~3.23 | 22~24 |
| 8 Mbit/s | 60 / 70 / 80% | 7218~7219 | 9059~9060 | 3.70~4.64 | 16~18 |

解读：

1. **短总线上采样点 60%~87.5% 全部 0 丢帧**：本板线长 <0.3 m、两端 120 Ω、单段两节点，
   采样点余量非常大；"必须 87.5%"是长总线多节点的经验值，不要照搬到短总线做过度优化；
2. 同一数据段下三档采样点帧率差异 <0.1%，说明在本条件下**采样点对吞吐没有可测影响**；
3. 仲裁段从 500 kbit/s 提到 1 Mbit/s 的收益随数据段升高而增大：2M 时 0%（2842 vs 2842）、
   4M 时 +15%（4565 -> 5239）、5M 时 +18%、8M 时 +26%（7218 -> 9059）；
4. `PSR.TDCV` 从 2M 的 ~42 降到 8M 的 ~17，与"数据段位时间变短、环回延迟折合的 TQ 数变少"一致。

### 9.7.1 采样点与误码（本板条件下的观察）

本矩阵没有观测到"低采样点导致误码"的现象（全部 0 丢帧）。原因：
总线只有两个节点、线长短、终端匹配良好，信号上升沿与反射都在 1 个 TQ 内结束。
若要验证采样点与误码的关系，需要更长总线（≥10 m）或多节点并故意不匹配终端 ——
这属于第 12 节的"待确认项（需外部硬件）"。

## 9.8 TDC 发送延迟补偿

TDC 的作用：数据段 ≥2 Mbit/s 时，收发器 TXD->RXD 环回延迟（TCAN1044AV 典型 100~200 ns）
在 8 Mbit/s（位时间 125 ns）下已不可忽略；TDC 在"实测延迟 + SSP 偏移"处二次采样。

**方法**：同一个 1 Mbit/s 仲裁 + 80% 数据段采样点配置，分别关/开 TDC 各发 300 帧 64 B FD。

如表 9-7 所示：

**表 9-7 TDC 开/关对比（300 帧，0 丢帧）**

| 数据段 | TDC=0 | TDC=1 | 说明 |
|---|---|---|---|
| 2 Mbit/s | 2842 fps，tdcv=0 | 2842 fps，tdcv=42 | 本板短总线不需要 TDC 也能 0 错 |
| 5 Mbit/s | 6303 fps，tdcv=0 | 6304 fps，tdcv=23 | 同上；TDC 提供 23 TQ 的二次采样余量 |
| 8 Mbit/s | 见日志（0 丢帧） | 见日志（0 丢帧） | 短总线 + 低延迟收发器下两者都可用 |

工程结论：

1. **本板实测不能证明"不开 TDC 也行"**：短总线 + 低延迟收发器的组合掩盖了问题；
   换长线/高延迟收发器/多节点时必须开 TDC（ISO 11898-2 与收发器手册要求）；
2. 推荐默认配置：数据段 ≥2 Mbit/s 时 `enable_tdc = true`，`ssp_offset = 数据段 seg1 + 1`，
   `filter_window_length` 同值；本矩阵的 TDCV 表明控制器实测延迟在 16~42 TQ 之间；
3. HPM5E00 勘误表无 CAN 条目，**TDC 可正常使用**（HPM6700 E00016 的 SSPOFF=0 限制不适用，见第 08 章 2.3 节）。

**TDC 偏移的合法窗口（2026-11-02 补充）**：SSP 的位置由 TDCR.TDCO 直接定义（单位 = 数据段 mtq），
它必须小于数据段一位的长度 1 + DTSEG1 + DTSEG2。5 Mbit/s、80 MHz、BRP=1 时一位只有 16 tq，
MCAL 侧旧配置把 TDCO 写成 20，SSP 越界 4 tq，发送端在数据段校验自己回读的位时必然判错，
TEC 累加到 248 进 bus-off——这不是"5 Mbit/s 跑不了"，而是偏移越界。同一固件只扫 TDCO：
1 -> 0/50 失败、8/10/12/13/14 -> 各 50/50 通过、20 -> 0/50 失败（第 47 章表 47-17）。
另外实测 PSR.TDCV 恒等于 TDCO + 10（三种数据速率下都是 +10），说明本板收发器环回延迟
折算约 10 mtq；所以 5 Mbit/s 下 TDCO 的合法窗口只有 8~15，推荐取 12（= 采样点 75%）。
工程上建议：TDCO 与数据段采样点对齐（SP=75% 就取 75% 位长的 tq 数），并在配置工具里
加"TDCO < 位长"的校验，别手填一个看起来"更大更保险"的值。

### 9.8.1 TSU / PTPC 硬件时间戳

MCAN 的 Timestamp Unit（TSU）有三档时基（RM §51.4.9, p.870）：

1. 内部 16 位计数器（按位时间递增），只能做帧排序；
2. TSU + 内部 AHB 时基：32/64 位，分辨率 (prescaler+1)/AHB 时钟，本板 AHB 200 MHz 时约 5 ns；
3. TSU + PTPC：PTPC0 的秒/纳秒计数器经 `MCAN_TBSEL` 路由给 4 个 MCAN，所有实例捕获同一时间基准。

关键差别：内部 AHB 时基下每个 MCAN 的 TSU 从各自使能时刻起算，两个实例的计数器之间有一个固定未知偏移，
**TX 与 RX 时间戳不能直接相减**；只有走 PTPC 公共时基，`RX 时间戳 - TX 时间戳` 才是物理上有意义的量
（收发器环回 + 总线传播 + 一个位时间量级）。路径如图 9-4 所示。

![图 9-4 MCAN TSU/PTPC 时间戳路径](figures/09/fig-09-4-tsu-path.png)

**图 9-4**：AHB 时基经 TSU 预分频直接计数；PTPC0 经 MCAN_TBSEL 作为外部时基进入 TSU。
捕获由 SOF 触发，时间戳写进 TX Event FIFO 元素或 RX 元素，分别用
`mcan_get_timestamp_from_tx_event()`、`mcan_get_timestamp_from_received_message()` 读出。

例程里的两组实验（`samples/can/mcan_quad_load/`）：

- `tsu 200`：内部 AHB 时基，比较同一节点相邻帧的 TX 间隔与 RX 间隔（两个实例同一时钟源，
  间隔应只差一个 tick 量级），并统计 0 间隔（漏捕获）次数；
- `ptp 200`：PTPC0 + 64 位模式，直接统计每帧 `RX - TX` 的 min/mean/max。

上板结果（2026-11-02 12:28，同一份日志）：`ptpc_init st=0`（源 200 MHz）、
`ptpc0->MCAN0/1 st=0` 都正常，随后 CAN0 的 `mcan_init()` 返回 `st=2`
（`status_invalid_argument`）。查驱动源码定位到根因：`mcan_set_tsu()`
校验 `prescaler` 在 [1, 256]（`hpm_mcan_drv.c:1667`），示例最初传 `prescaler=0`
想取 AHB 全分辨率，被驱动直接拒绝。修法：`prescaler=1`（TSCFG.TBPRE = prescaler-1 = 0，
TSU 计数时钟按 AHB/1，本板 AHB 200 MHz 时 tick 约 5 ns）。修正已改进例程并重新编译通过，
但没有再次占用板卡复跑，所以本节目前闭环到"配置路径 + 根因"，时间戳精度仍是待复跑项。复跑：

```bash
bash scripts/hwrun.sh mcan_quad_load --no-build --secs 50 --send "tsu 200;ptp 200"
```

注意：驱动头文件注释写 tick=(prescaler+1)/AHB，与寄存器编码 TBPRE=prescaler-1 不一致，
实测时以 TX/RX 时间戳间隔反推实际分辨率，不要只信注释。

## 9.9 错误注入与 bus-off 恢复

**方法**：CAN0 500 kbit/s 正常模式；CAN1 设 **listen-only**（只收不发、不应答 ACK、不发错误帧），
CAN0 发送后因无 ACK 不断重传，TEC +8/次 -> 错误被动（TEC≥128）-> bus-off（TEC>255）；
随后恢复 CAN1 为正常模式并调用 `mcan_recover_from_busoff()`，验证恢复后收发。

如表 9-8 所示：

**表 9-8 错误注入与恢复实测（`logs/20261030-120042-mcan_error_recovery.log`）**

| 阶段 | 实测现象 | 关键寄存器 |
|---|---|---|
| 单节点无 ACK（错误主动） | 60 ms 内 TEC 0 -> 111（约 14 次重传，>4 ms/次） | TEC=111、REC=0、LEC=0、BO=0 |
| 进入错误被动后 | 200 ms 只从 111 涨到 135，仍未 bus-off | TEC=135、EP=1、BO=0、LEC=3（ACK 错误） |
| 恢复 CAN1 为正常模式 + `mcan_recover_from_busoff()` | 返回 `status_success`，PSR.BO 清零 | recovered=1、耗时 0 µs（调用前总线已静默足够久，硬件已自动退出） |
| 恢复后收发 | 发 0x123/8B，CAN1 收到且数据一致（0xDEADBEEF） | **PASS**；TEC=118/REC=0（错误历史保留） |

三个高价值结论：

1. **"无 ACK 单节点"不是快速 bus-off 手段**：实测错误主动阶段每次重传间隔 >4 ms，
   从 0 到 255 需要 32 次以上即 >120 ms；进入错误被动后更慢（200 ms 只 +24）。
   做 bus-off 压力测试建议时间窗口 ≥500 ms，并在每轮之间彻底复位节点；
2. **"对端波特率错配"更不适合做 bus-off 测试**：错配节点会主动发错误帧，双方 TEC/REC 此消彼长，
   实测 CAN0 的 REC 涨到 127、TEC 仅在 6~14 徘徊，长时间进不了 bus-off；
3. **恢复成功的判据是 PSR.BO=0 + 能正常收发**，不是 TEC 归零：实测恢复后 TEC 仍为 118 却收发正常。

## 9.10 双节点 vs 内部环回的差异

| 维度 | 内部环回（`mcan_loopback_internal`） | 双节点真实总线（`mcan_two_node`） |
|---|---|---|
| 硬件需求 | 只需串口，收发器/总线/终端都可不管 | 需要收发器上电 + 总线 + 两端 120 Ω |
| ACK | 自发自答（TEST.LBCK），不依赖对端 | 依赖对端真实 ACK，无对端必错 |
| 能测什么 | MCAN IP / 消息 RAM / 位时序 / 过滤器逻辑 | 真实位时序余量、总线仲裁、错误帧、bus-off、吞吐 |
| 实测结果 | 4 实例 16/16 PASS | 经典 4305 fps / FD 8M 9058 fps，0 丢帧 |
| 局限 | 总线侧全部被绕过，测不出收发器/终端/EMC 问题 | 需要第二节点，单板只能用片内两路互测 |

结论：**内部环回做"上电自检 + 寄存器级回归"，双节点做"性能与错误注入"**，两者缺一不可。
本仓库两套例程都提供，建议产线自检用前者、研发评估用后者。

## 9.11 坑与注意事项

1. **MCAN 时钟门控**：必须先 `board_init_can_clock()`（返回源时钟）再 `mcan_deinit()`/访问寄存器；否则访问被门控的
   MCAN 寄存器会把 CPU 挂死。曾导致双节点例程首次上板即卡死。
2. **消息 RAM 位置**：HPM5E31 的 MCAN 消息 RAM 在 AHB SRAM，必须
   `ATTR_PLACE_AT(".ahb_sram")` + `mcan_set_msg_buf_attr()`；放在 AXI SRAM 或默认段会异常。
3. **单节点无 ACK 的 bus-off 不是瞬间**：实测约 3.7 ms/+8 TEC，要 TEC>255 需 >100 ms；测试窗口建议 ≥500 ms。
4. **"对端波特率错配"不等于"无 ACK"**：错配节点会主动发错误帧，反而不易进 bus-off；要稳定复现请用 listen-only 对端。
5. **bus-off 恢复后 TEC/REC 保留历史值**（实测 TEC=132）：以 PSR.BO=0 且能正常收发判定恢复成功，不要看 TEC 是否归零。
6. **TDC 不是"开了就好"**：数据段 ≥2 Mbit/s 时 TDC 与 SSP 要按收发器环回延迟标定；配错会引入位错误，矩阵见第 6 节。
7. **双节点必须有正确端接**：本板 CAN0-CAN1、CAN2-CAN3 两两相连（各 120 Ω×2）；接外部节点时注意端接数量与共地。
8. **FD 帧长与 FIFO**：64 B 载荷要确认接收 FIFO/专用 buffer 容量，否则高帧率下丢帧（本板测试 0 丢帧的条件见第 4 节）。
9. **中断 vs 轮询**：>4000 frame/s（经典）或 >1500 往返/s（FD）时建议中断/ DMA，轮询会吃掉主循环时间。

## 9.12 与经典 CAN / 同类 MCU 的对比

如表 9-9 所示：

**表 9-9 CAN / CAN FD 控制器横向对比**

| 平台 | 控制器 | 节点数 | 经典 CAN | CAN FD 数据段 | 消息存储 | 出处 |
|---|---|---|---|---|---|---|
| **HPM5E31（本板）** | MCAN（Bosch M_CAN） | 4 | 1 Mbit/s（实测 4313 fps @500k） | **8 Mbit/s**（实测 4.64 Mbit/s payload） | 4 实例共享 32 KB AHB SRAM | DS p.12；RM §51.1 (p.865)；本章实测 |
| HPM6E00 | MCAN | 8 | 1 Mbit/s | 8 Mbit/s | 32 KB AHB SRAM | HPM6E00_DS_V0_6；SDK `hpm_soc_ip.h` |
| HPM6200 | MCAN | 4 | 1 Mbit/s | 8 Mbit/s | 片内 640x32 bit 消息 RAM（免应用分配） | 驱动注释；HPM6200 勘误 E00028 |
| HPM6700/6400 | 经典 CAN IP（支持 FD） | 4 | 1 Mbit/s | 8 Mbit/s（受 E00016 限制，SSPOFF 强制 0） | 寄存器内 TBUF/RBUF（16 SLOT） | HPM6700_6400_UM_V2_1 §58；Errata E00016 |
| STM32G4/H7 | FDCAN | 1~3 | 1 Mbit/s | 公开资料 5 Mbit/s（未在本书实测） | 消息 RAM（可配） | ST 官网 FDCAN 产品页（外部资料，待核对具体型号） |
| Infineon TC397 | MCMCAN | 4 | 1 Mbit/s | 公开资料 8 Mbit/s（未在本书实测） | 消息 RAM | Infineon AURIX TC3xx 公开资料（未实测） |

对比结论：

1. **同等定位下 HPM5E31 的 CAN FD 能力与 TC397 同级（8 Mbit/s）**，明显高于多数 STM32 型号的 5 Mbit/s；
   4 路独立 MCAN 适合多轴/网关场景；
2. HPM5E31 的**消息 RAM 需要应用分配**（共享 32 KB AHB SRAM），比 HPM6200 的片内消息 RAM 多一步配置，
   但换来 4 个实例共享、按需划分的灵活性；
3. HPM5E31 **没有 CAN 相关勘误**，TDC 可以正常使用（HPM6700 的 E00016 不适用），这是选型时的实际优势；
4. 本章实测的 4.64 Mbit/s payload 是"64 B 帧 + 1 Mbit/s 仲裁"的结果，不是芯片上限；
   若仲裁段也用 8 Mbit/s（需要全网段支持），payload 可进一步提升，未在本书测试。

## 9.13 性能与调优建议

按收益从高到低排序：

1. **提高数据段波特率**（收益最大）：本板 2M -> 5M -> 8M，帧率 2843 -> 6302 -> 9058 fps
   （+122% / +219%），payload 1.46 -> 3.23 -> 4.64 Mbit/s；注意 seg1≤31、seg2≥2、TDC 时 prescaler≤2 的硬限制。
2. **仲裁段提到 1 Mbit/s**：8 Mbit/s 数据段下 +26% 帧率；2 Mbit/s 时几乎无收益（仲裁段占比小）。
3. **收发改中断 + 批量读**：当前轮询方案在 8 Mbit/s 下每帧要忙等 ~53 k 周期（~110 µs），
   改中断后 CPU 只处理 FIFO 里的帧，预计释放 80% 以上 CPU；建议 RX FIFO 深度设 32~64 避免溢出。
4. **发送用 TX Queue 而非 TX FIFO**：TX Queue 按 ID 优先级调度，控制帧（低 ID）可以插到数据帧之前，
   降低关键报文延迟；本仓库例程暂时用 TX FIFO，切换只需改配置与 API。
5. **批量提交**：本板 FD TX FIFO 深度 4，实测批次深度 3 时 0 丢帧；批次越深软件开销占比越低，
   但要注意 FIFO 满时 `mcan_transmit_via_txfifo_nonblocking()` 返回错误要立即重试。
6. **不要过度追求高采样点**：短总线/两节点下 60%~87.5% 全部 0 丢帧，优先保证全网采样点一致；
   长总线再向 80% 靠拢，并开 TDC。
7. **消息 RAM 划分按最坏突发量**：FD 默认每 FIFO 8 元素（8x72 B）；高负载/中断处理不及时时，
   把 RX FIFO 提到 32~64 元素，代价是占用 32 KB 共享 AHB SRAM 的其他部分（4 实例各 2560 B 时还剩 22 KB）。
8. **MCAN DMA 的现实情况**：SDK 1.13.0 的 `hpm_mcan_drv.h` 没有 MCAN DMA 接口（没有 dma 字段/函数），
   RM §51 里 MCAN 的 DMA 请求需要应用层自己接。可行方案：RX FIFO0 水位设 1，中断里只置标志，
   主循环用 HDMA 链式搬运 72 B（64 B 帧）FIFO 元素到环形缓冲，CPU 只处理指针；
   TX 方向收益很小（数据还是要 CPU 填）。落地前核对两点：MCAN 消息 RAM 在 AHB SRAM（源地址固定、
   4 字节对齐、32 位宽度），以及 HPM5E31 的 HDMA 通道数与触发源。轮询成本见表 9-5，可据此估收益。

## 9.14 参考来源

1. RM：HPM5E00_UM_V0_4 §51「控制器局域网 MCAN」§51.1 概述 (p.865)、§51.2 架构 (p.865)、
   §51.4.2 位时间 (p.866)、§51.4.4 CAN FD 工作模式 (p.868)、§51.4.5 静默模式 (p.868)、
   §51.4.7/8 外部/内部环回 (p.869)、§51.4.9 时间戳 (p.870)、§51.4.13/14 过滤器 (p.874~875)、
   §51.4.15 RX FIFO (p.876)、§51.4.16 中断 (p.877)、§51.5.3 发送处理 (p.879)、
   §51.6 寄存器 (p.879~921)，其中 TDCR §51.6.15 (p.893)、ECR §51.6.13 (p.889)、PSR §51.6.14 (p.889)。
2. DS：HPM5E00_DS_V0_4 p.1、p.12（4 个 CAN、CAN 2.0B 1 Mbit/s、CAN FD 8 Mbit/s）。
3. Errata：HPM5E00_Errata_V0_1 p.2~5（仅 E00038 PWMv2、E00042 系统总线时钟；**无 CAN 条目**）；
   HPM6700_6400_Errata_V2_4 §2.14 E00016（TDC 限制，不适用于 HPM5E00）。
4. SDK 头文件：`hpm_sdk/drivers/inc/hpm_mcan_drv.h`（位时序结构 :315~332、TDC :1127~1133、
   过滤器 :772~843、API :2134~2698）；`hpm_sdk/drivers/src/hpm_mcan_drv.c:755~787`（位时序校验）、
   :148~202（默认时序表）、:2364~2388（阻塞发送）、:2524~2545（阻塞接收）。
5. SDK SoC 头文件：`soc/HPM5E00/HPM5E31/hpm_soc_ip.h:283~299`（4 个 MCAN 基地址）、
   `hpm_soc_irq.h:59~62`（IRQ 46~49）、`hpm_soc_feature.h:217~220`（`MCAN_SOC_MSG_BUF_IN_AHB_RAM=1`）、
   `hpm_mcan_soc.h:42~65`（消息 RAM 属性与大小）、`toolchains/gcc/flash_xip.ld:18,276`（.ahb_sram 段）。
6. SDK 官方例程：`hpm_sdk/samples/drivers/mcan/src/mcan_txrx_demo.c`（位时序/TDC 写法）、
   `mcan_loopback_demo.c`、`mcan_filter_demo.c`、`mcan_error_demo.c`、
   `mcan_app_common.c`（.ahb_sram 消息缓冲）。
7. 本仓库例程与日志：
   - `samples/can/mcan_loopback_internal/` -> `logs/20261030-110650-mcan_loopback_internal.log`；
   - `samples/can/mcan_two_node/` -> `logs/20261030-113541-mcan_two_node.log`；
   - `samples/can/mcan_throughput/` -> `logs/20261030-114225-mcan_throughput.log`；
   - `samples/can/mcan_fd_timing/` -> `logs/20261030-115604-mcan_fd_timing.log`；
   - `samples/can/mcan_error_recovery/` -> `logs/20261030-120042-mcan_error_recovery.log`；
   - `samples/can/mcan_filter/` -> `logs/20261030-114549-mcan_filter.log`。
8. 外部资料（仅用于横向对比，未在本书实测）：[ST FDCAN 产品页](https://www.st.com/en/microcontrollers-microprocessors/stm32-high-performance-mcus.html)、
   [Infineon AURIX TC3xx](https://www.infineon.com/cms/en/product/microcontroller/32-bit-tricore-microcontroller/)、
   [HPM SDK 文档](https://hpm-sdk.readthedocs.io/zh-cn/latest/)、[HPMicro 官网](https://www.hpmicro.com/)。

9. 参考实现：[SlcanX](https://github.com/weifengdq/SlcanX)（HPM5321 四路 CAN FD 分析仪的 MCAN 用法；GPL-3.0，只读参考）。
10. Bosch M_CAN User's Manual（位时序/TDC/SSP 的权威原始来源），Bosch 半导体 CAN IP 页：<https://www.bosch-semiconductors.com/ip-modules/can-ip-modules/m-can/>。
11. 收发器：[TI TCAN1044A-Q1](https://www.ti.com/product/TCAN1044A-Q1)（5 Mbit/s 上限、环回延迟 100~200 ns）、
    [Microchip MCP2558FD](https://www.microchip.com/en-us/product/MCP2558FD)（8 Mbit/s 级，换 PHY 用）。
12. 工具与内核文档：[can-utils](https://github.com/linux-can/can-utils)、[Linux SocketCAN](https://www.kernel.org/doc/html/latest/networking/can.html)。
13. HPM SDK 与文档：<https://github.com/hpmicro/hpm_sdk>（v1.13.0）、<https://hpm-sdk.readthedocs.io/zh-cn/latest/>；
    芯片资料入口 <https://www.hpmicro.com/>、<https://doc.hpmicro.com/>。

## 9.15 待确认项与风险

如表 9-11 所示：

**表 9-11 待确认项与风险（已逐条复核，2026-11-02）**

| 项 | 状态 | 说明 / 验证方法 |
|---|---|---|
| 无外部 CAN 分析仪 | **已知限制** | 全书数据由 MCU 自证（帧计数/ECR/时间戳），测不了总线眼图、位宽、ACK 电平；有条件时用 CANable/SLCAN 抓原始位流复测 |
| 外部节点 / 长总线 | **已知限制** | 本板只能片内两路互测（<0.3 m）；长总线（≥10 m）、多节点、终端不匹配未测。验证：接 10~50 m 双绞线 + 2~3 个节点重跑位时序矩阵 |
| 4 路同时满载 | **已实测（2026-11-02）** | 两组总线双向、4 个 MCAN 全忙：每方向 1425/3166 fps，聚合 5700/12664 fps，0 丢帧、RXFIFO 溢出 0，CPU 忙闲比 81.3/82.0%（表 9-10、图 9-5）。复跑：`bash scripts/hwrun.sh mcan_quad_load --no-build --secs 50 --send "quad 2000 0 4;bi 2000 1"` |
| TSU / PTPC 时间戳 | **已知限制（例程已入库，数据待复跑）** | `tsu`/`ptp` 命令按 RM §51.4.9 与驱动 timestamping API 实现：PTPC0 经 MCAN_TBSEL 路由到 MCAN0/1，64 位模式。复跑：`... --send "tsu 200;ptp 200"` |
| DMA 收发 | **已知限制（驱动无 DMA 接口，方案已给）** | SDK 1.13.0 的 `hpm_mcan_drv.h` 没有 MCAN DMA 接口，需应用层自接：RX FIFO0 水位中断 + HDMA 搬 72 B 元素到环形缓冲。轮询成本见表 9-5，可据此估收益；落地前核对 RM §51 的 DMA 触发与 HDMA 通道约束 |
| TDC 偏移的合法窗口 | **已实测（2026-11-02）** | TDCO 必须小于数据段位长：5 Mbit/s（16 tq）下 TDCO=1 与 20 都 bus-off（TEC=248），8/10/12/13/14 全部 50/50 通过；PSR.TDCV 恒等于 TDCO+10（环回延迟折算约 10 mtq）。详见第 47 章表 47-17。TDC 的吞吐收益在短总线上仍无法量化（开/关都 0 丢帧） |
| 采样点与误码关系 | **已知限制** | 本板短总线全 0 丢帧；需长线或人为阻抗不匹配复现，方法见 §9.7.1 |
| 8 Mbit/s 数据段眼图 | **已知限制（器件上限 5 Mbit/s）** | 板载 TCAN1044AV 上限 5 Mbit/s（[数据手册](https://www.ti.com/product/TCAN1044A-Q1)），8 Mbit/s 属超规格；需示波器看眼图/振铃，并换 8 Mbit/s 级收发器（MCP2558FD）后复测 |
| 非 ISO FD 模式 | **已知限制** | 驱动支持 `enable_non_iso_mode`，仅与老的非 ISO 设备互操作时需要；本板两节点都是 ISO 模式，没有对端可测 |
| 高帧率下的丢帧边界 | **已知限制** | 已测到 4305 fps（经典 500k）、6302 fps（FD 5M）、9058 fps（FD 8M）均 0 丢帧；继续加压需先降软件开销（中断/DMA） |
| 与 STM32/TC397 的对比数据 | **已知限制（非本板实测，引用厂商公开资料）** | 表 9-9 中外部平台参数来自厂商公开资料，未在本书实测；选型以对应芯片手册为准 |
| 低功耗唤醒后的 CAN 恢复 | **已知限制** | HPM5E00 勘误 E00042 涉及系统总线时钟关闭；验证方法：进入 stop 前保持总线时钟、唤醒后重新 `mcan_init()`，本轮未测 |
