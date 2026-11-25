# 第 24 章 总线吞吐与实时性对比

## 24.1 文档信息

| 项 | 内容 |
|---|---|
|   章节版本   | v1.2.0（2026-11-02，R5 复核） |
| 适用芯片 / 板卡 | HPM5E31（HPM5E00 系列）/ hpm5e31_1 |
| 对应例程 | `samples/bench/bench_bus`、`bench_memory`、`bench_irq_latency`（本章主线）；`samples/can/mcan_two_node`、`samples/dma/dma_mem2mem`、`samples/enet/enet_lwip_iperf`（引用） |
| 实测日期 | 2026-10-30 |
| 工具链 / SDK | RISC-V GCC 11.1.0、HPM SDK v1.13.0 |
| 可信度 | 吞吐/延迟均为实测（日志见章末）；理论峰值按手册与帧格式公式计算，已在表中标注 |
| 数据来源 | 本仓实测（本作者 3 个例程 + tp-can / tp-sys / tp-net2 的实测日志，逐条标注）；理论值按芯片手册与帧格式公式计算 |

## 24.2 本章导览

第 23 章回答"CPU 有多快"，本章回答"**数据搬得动多快、中断反应有多快**"。工程上后者更容易成为瓶颈：
CAN 4 路 + 千兆以太网 + 10BASE-T1S + 4 路 SPI 同时跑，总线争用、非 cacheable 访问、
DMA 通道分配、中断延迟抖动，任何一项算错都会让控制环超时。

读完本章你能：

1. 用一张表判断"我的数据该走 CPU 拷贝、DMA 还是 cache"；
2. 知道每类总线**实测能跑到理论峰值的百分之几**，以及掉下来的原因；
3. 拿到可复用的**中断延迟/抖动**数据，给实时性预算留够余量。

前置章节：第 07 章（SPI）、第 08/09 章（CAN FD）、第 12/13 章（以太网）、第 19 章（DMA）、第 23 章（基准方法学）。

## 24.3 概述：这颗 SoC 的总线长什么样

HPM5E31 的存储/总线子系统（本仓实测时钟，见第 23 章时钟表）：

```text
        CPU0 (RV32, 480MHz)
          |  L1 I-Cache / D-Cache
   +------+------+
   |             |
  ILM 128KB    AXI 总线矩阵 (200MHz, 32bit)  <--- 理论 800MB/s
  (0x0, 代码)     |        |          |
                AXI SRAM   XPI0      DMA/XDMA
                256KB      (100MHz)   (内存<->外设)
                (0x0120..)  |
                           Flash 1MB (0x80000000)
  DLM 128KB (0x00200000)  <- 紧耦合，放栈/热数据
```


![图 24-1 HPM5E31 存储总线拓扑示意（由本仓实测的地址与时钟绘制，示意见图，非实测）](figures/24/fig-24-1-bus-topology.png)

**图 24-1** ：HPM5E31 的存储/总线拓扑示意。图中除"Axi 理论 800 MB/s（按 200 MHz 乘 32 bit 推算）"外，
其余吞吐数字都来自本仓实测（第 23 章与本章表格、`logs/` 日志）。

几个关键事实（都有实测依据）：

- **ILM/DLM 各 128KB**，分别是"本地代码"和"本地数据"，链接脚本默认把**中断向量表**（`.vectors`）
  和 `.fast` 段拷到 ILM、把**栈和堆**放到 DLM（`flash_xip.ld`）。也就是说：
  默认工程的中断入口与栈已经在最快的存储器上，这是 SDK 的一个"隐形优化"。
- **AXI SRAM 是可缓存的**（`.data/.bss`），非 cacheable 要显式用 `.noncacheable` 段；
  两者性能差 **11.5 倍**（下一节）。
- **XPI0 只有 100MHz**，是整颗芯片最容易忽视的瓶颈：顺序读实测 **约 40MB/s**，
  比 AXI SRAM 慢 20 倍以上——因为 XIP 代码取指也要走这条通道。

## 24.4 工作原理：数据通路与测量方法

要读懂后面的吞吐数字，先要理解数据从 CPU 到外部世界经过哪几级，以及每级的时钟/位宽：

1. **CPU 核内**：L1 I-Cache / D-Cache（本 SoC 32KB 级）。命中时数据不经过任何总线，
   这也是第 23 章里"小尺寸 memcpy 能跑到 900MB/s 以上"的原因。
2. **本地存储器**：ILM（代码，0x0）/ DLM（数据，0x00200000），紧耦合、不经 AXI 矩阵；
   链接脚本默认把中断向量表放 ILM、把栈/堆放 DLM。
3. **AXI 矩阵（200MHz × 32bit）**：连 AXI SRAM、XPI0、DMA/XDMA 以及各外设。
   所有"跨模块"的数据都要过这一级，因此它是**争用点**：DMA 大突发、以太网收包、
   非 cacheable 访问都会在这里排队。
4. **XPI0（100MHz）**：Flash 控制器，XIP 取指与常量读取都走它；Quad 4bit 模式理论 50MB/s。
5. **外设接口**：CAN（MCAN）、UART、SPI、ENET 等各有独立时钟域与 FIFO，吞吐由
   位速率与 FIFO/DMA 深度决定。

测量方法（本章所有数字的来源，可复现）：

- **吞吐类**：mcycle 计时 + 固定数据量搬运，重复 3 次取最好；XIP 读用
  "每轮 invalidate D-Cache" 强制走总线；DMA 带宽由 tp-sys 用 mchtmr 计时（
  `samples/dma/dma_mem2mem`）；以太网用 iperf2（lwIP 自带 lwiperf 只实现 iperf2 协议）。
- **实时性类**：GPTMR 自计时（ISR 第一条指令读计数器，差值即延迟），10000 次采样统计。
- **交叉验证**：同一指标尽量由两个 owner 独立测量（例如 XPI 读：本仓 40MB/s、tp-sys 45.7MB/s），
  差异写清测试条件而不是取平均。

### 24.4.1 关键寄存器 / API 对照

表 24-6 总线与缓存相关的关键寄存器 / API

| 用途 | 寄存器 / API | 说明 | 例程位置 |
|---|---|---|---|
| 读 CPU 周期 | `CSR_MCYCLE` / `hpm_csr_get_core_mcycle()` | 吞吐/延迟统一时基（480MHz） | `bench_bus` / `bench_irq_latency` |
| D-Cache 失效 | `l1c_dc_invalidate(addr,size)` | 强制下一次读走总线（测 XIP 读带宽） | `bench_bus/src/main.c` |
| D-Cache 写回 | `l1c_dc_flush(addr,size)` | DMA 发送前保证内存数据最新 | 第 19 章 |
| Cache 使能状态 | `l1c_dc_is_enabled()` / `l1c_ic_is_enabled()` | 打印环境、验证配置 | `bench_irq_latency` |
| 定时器比较中断 | `GPTMR_CH_CMP_STAT_MASK(ch,cmp)` | 实时性测量的触发源 | `bench_irq_latency` |
| 定时器计数 | `GPTMR->CHANNEL[n].CNT` | 延迟计算的时间基准（10ns/tick） | 同上 |
| UART 发送状态 | `uart_check_status(ptr, uart_stat_transmitter_empty)` | 测 UART 吞吐必须等 TEMT | `bench_bus` |
| DMA 通道配置 | `dma_setup_channel()` / `dma_start_transfer()` | mem2mem、链式描述符 | 第 19 章 / tp-sys |
| 时钟查询 | `clock_get_frequency(clock_axis` / `clock_xpi0)` | 理论带宽换算 | `bench_bus` |
| 非 cacheable 段 | `.noncacheable.bss`（链接脚本） | DMA/外设共享缓冲区 | `bench_memory` |

### 24.4.2 时钟域与争用（读数字前必须知道）

- **AXI 矩阵是唯一共享通路**：DMA 搬 406MB/s 时，CPU 的 AXI SRAM 访问会掉速；
  "DMA 快"不等于"系统快"，要做并发测试才知道（本仓未做，见待确认 P8）。
- **XPI 与取指共享**：代码在 flash 里时，每一次 I-Cache miss 都要占用 XPI0，
  和常量读取抢带宽（40MB/s 的通道很容易被打满）。
- **外设 FIFO 深度决定中断频率**：UART 只有 32 字节 FIFO，921600 波特率下约 350us 就会溢出，
  必须用 DMA 或足够高的中断优先级（第 05 章）。
- **AHB 桥是隐藏瓶颈**：HDMA 访问 AXI SRAM 时 ≥128B 突发从 117.7MB/s 掉到 25.9MB/s（tp-sys 实测），
  大块搬运要用 XDMA。

## 24.5 各类总线 / 存储实测吞吐（数据见表 24-1）

### 24.5.1 汇总表（全部实测，单位已统一）

表 24-1 各类总线 / 存储通路实测吞吐汇总

| 通路 | 实测吞吐 | 换算 | 测试条件 | 来源 |
|---|---|---|---|---|
| AXI SRAM 写（CPU memset_u32） | 959.6 MB/s | — | cache 开，4KB | `logs/20261030-111657-bench_memory.log` |
| AXI SRAM 拷贝（CPU 16 字展开） | 922.5 MB/s | — | 同上 | 同上 |
| DLM 拷贝（CPU 16 字展开） | 922.9 MB/s | — | 4KB | 同上 |
| 非 cacheable AXI SRAM 拷贝 | **80.0 MB/s** | — | 4KB，逐字 AXI 事务 | 同上 |
| 非 cacheable AXI SRAM 写 | 425.3 MB/s | — | 4KB memset | 同上 |
| libc memcpy（newlib-nano） | 95.9 MB/s | — | 4KB，逐字节实现 | 同上 |
| XDMA mem2mem（nc→nc） | 406.0 MB/s | — | 16KB，512B~8KB 突发 | `logs/20261030-110400-dma_mem2mem.log`（tp-sys） |
| XDMA 链式描述符 3×4KB | 485.0 MB/s | — | 25.33us / 12KB | `logs/20261030-110512-dma_mem2mem.log`（tp-sys） |
| HDMA（AHB SRAM） | 327.7 MB/s | — | 8KB，512B 突发 | tp-sys 实测 |
| SPI1↔SPI3 互连（10 MHz，1024 B/次，主从两侧 HDMA） | **873 KB/s**（单向）/ 1746 KB/s（双向合计） | — | 80 MHz 源时钟 ÷(2×(3+1))=10 MHz，mode0，1 MB 连续、0 错误 | `logs/20261102-121103-spi1_spi3_master_slave.log`（tp-bus） |
| SPI 主机 DMA（板内回环，SCLK 20 MHz，2048 B/次） | 2490 KB/s | — | 源时钟 80 MHz÷(2×(1+1))，HDMA 单侧收发，DMA 效率 99% | `logs/20261030-113457-spi_dma_bw.log`（tp-bus，R4 数据本轮未复测） |
| XPI flash 顺序读（cold） | **40.0 MB/s** | — | 64KB，每轮 invalidate D-Cache | `logs/20261030-113730-bench_bus.log`（本仓） |
| XPI flash 顺序读（cold，独立复测） | 45.7 MB/s | — | 16KB，ROM 映射直读 | `logs/20261030-113129-flash_rw_erase.log`（tp-sys） |
| ROM API `rom_xpi_nor_read` | 30.0 MB/s | — | 4KB（含 API 开销） | `logs/20261030-110952-flash_rw_erase.log`（tp-sys） |
| YT8011AN 1000BASE-T1 TCP **下行**（host→板） | **902.3 Mbit/s** | 112.8 MB/s | lwIP 零拷贝接收，iperf2 2.2.1，CPU 24.7%（480MHz）；主机负载波动 ±4% | `logs/20261030-153556-enet_lwip_iperf.log`（tp-net2） |
| YT8011AN 1000BASE-T1 TCP **上行**（板→host） | **466.3 Mbit/s** | 58.3 MB/s | 发送路径优化后（跳过 TX D-cache 回写），**CPU 99.7% 仍饱和**；优化前 415.3 | `logs/20261030-153645-enet_lwip_iperf.log`（tp-net2） |
| YT8011AN 1000BASE-T1 UDP **上行**（板→host） | **641.5 Mbit/s** | 80.2 MB/s | 同上优化；优化前 493（+30.1%） | `logs/20261030-153733-enet_lwip_iperf.log`（tp-net2） |
| YT8011AN 1000BASE-T1 UDP **下行** | 105 / 315 / 629 / **944** Mbit/s | 最高 118.0 MB/s | `iperf2 -b 100M/300M/600M/900M`，实测 104.75/314.23/628.40/942.67，丢包 ≤0.25%（944Mbit/s 时 0.13%） | `logs/20261030-113919-enet_lwip_iperf.log`（tp-net2） |
| CAN FD 1M仲裁/2M数据，64B 单向 payload | 0.809 Mbit/s | — | 1580 roundtrip/s，TEC/REC=0 | `logs/20261030-112832-mcan_two_node.log`（tp-can） |
| CAN 2.0 500k，8B 单向 payload | 0.275 Mbit/s | — | 4305 帧/s，2000 帧 0 丢帧 | 同上（tp-can） |
| UART4 @921600 8N1（控制台路径） | **85.79 KB/s = 857.9 kbit/s** | — | 16KB 连续发送，等 TEMT 停表 | `logs/20261030-115014-bench_bus.log`（本仓） |
| XDMA 内存拷贝（AXI→ILM，8 KB，512 B 突发） | 334.4 MB/s | — | 写 ILM/DLM 都约 334；读 ILM 只有 39~48 | `logs/20261102-123937-dma_mem2mem-r5c.log`（tp-sys） |
| XDMA 内存拷贝（AXI→AXI，8 KB / 16 KB） | 143.4 / 401.7 MB/s | — | 同一驱动同一突发，8 KB 被固定开销拖到 143 | `logs/20261102-123937-dma_mem2mem-r5c.log`（tp-sys） |
| USB CDC bulk（主机→设备，16 KB 块） | **1.78 MB/s** | 14.2 Mbit/s | pyserial 写 1 MB / 0.588 s；瓶颈是固件逐包处理 | `logs/20261102-123831-usb_cdc_acm_echo-r5b.log`（tp-sys） |

![图 24-2 各通路实测有效吞吐](figures/24/fig-24-2-bus-bw.png)

图 24-2 各通路实测有效吞吐（对数坐标；蓝=片内 RAM、绿=DMA、紫=XPI、橙=libc、红=串行总线）

### 24.5.2 GPTMR 实际时钟比 `clock_get_frequency()` 低 1.5%（2026-11-02 复测）

`bench_irq_latency` 用 mcycle（480 MHz，XTAL+PLL 派生）做基准、GPTMR 计数器做被测时钟：
CMP=2000 tick 的入口到入口周期减去 ISR 自身执行周期后，反推 GPTMR = **98.445 MHz**（第二组 98.407 MHz），
而 `clock_get_frequency(clock_gptmr0)` 报 100 MHz，偏差 **-1.56% / -1.59%**（日志
`logs/20261102-122836-bench_irq_latency-r5sys.log`）。

这不是测量误差：同一固件里 mcycle 与 mchtmr 已互校（第 23 章）。结论是 GPTMR 的"标称 100 MHz"与实际有
系统性 1.5% 差。影响直接落在用 GPTMR 当时间基准的外设上——PWM 频率、ADC 触发周期、波特率分频都会同比例偏。
要精确标定：用外部 1 pps/PTP 或直接以 mchtmr 为时间基准；对"绝对频率精度"敏感的应用，别把
`clock_get_frequency()` 的返回值当真实时钟用。

### 24.5.3 三个必须理解的数字

**1) XPI0 是取指与代码读的唯一通道，只有 ~40MB/s。**
XPI0 时钟 100MHz，Quad 模式理论 50MB/s，实测 40MB/s = **80% 利用率**（图 24-3）。
这解释了两件事：

- 为什么 SDK 默认把中断向量表放 ILM（`.vectors ORIGIN(ILM)`）：中断入口必须避开 XIP；
- 为什么第 23 章 CoreMark 里"数据放 DLM vs AXI SRAM"只差 0.6%，但"代码放 XIP"与"代码放 RAM"差别巨大
  （HPM6750 类芯片跑分时通常也把代码放 RAM/ILM）。

**2) 非 cacheable 访问慢 11.5 倍。**
CPU 逐字访问非 cacheable AXI SRAM 时，每个字都是一次完整 AXI 事务（读阻塞），只有 80MB/s；
而写有缓冲，能到 425MB/s。**结论：非 cacheable 只用于"必须与 DMA/外设共享且要求实时可见"的缓冲区**，
而且这种缓冲区**一定要用 DMA 搬**，不要让 CPU 逐字读。

**3) CPU 拷贝并不比 DMA 慢多少——前提是自己写内核。**
CPU 16 字展开拷贝 922.5MB/s（可缓存），DMA 406~485MB/s（非 cacheable）。
DMA 的价值不是"更快"，而是"不占 CPU 且能与外设流水"；如果你的数据已经在可缓存 SRAM，
CPU 拷贝反而更快，`memcpy` 慢是因为 newlib-nano 的实现逐字节（第 23 章）。

### 24.5.4 理论 vs 实测：利用率

表 24-2 理论峰值 vs 实测利用率

| 总线 | 理论峰值 | 实测 | 利用率 | 说明 |
|---|---|---|---|---|
| XPI0（Quad，100MHz） | 50 MB/s | 40.0 MB/s | 80.0% | 4bit×100MHz；命令/地址/turnaround 开销 |
| YT8011AN 1000BASE-T1 | 1000 Mbit/s | 902.3 Mbit/s | 90.2% | TCP 下行，lwIP 零拷贝 |
| YT8011AN 1000BASE-T1 | 1000 Mbit/s | 944 / 641 Mbit/s | 94.4% / 64.2% | UDP 下行 944（丢包 0.13%）/ UDP 上行 641（优化后，+30.1%） |
| YT8011AN 1000BASE-T1 | 1000 Mbit/s | 641.5 Mbit/s | 64.2% | UDP 上行，CPU 饱和（软件瓶颈） |
| YT8011AN 1000BASE-T1 | 1000 Mbit/s | 466.3 Mbit/s | 46.6% | TCP 上行，**CPU 99.7% 是瓶颈**（非物理层） |
| CAN FD 1M/2M（64B） | ≈0.822 Mbit/s（单向 payload） | 0.809 Mbit/s | 98.4% | ping-pong 双向，按帧位数估算 |
| CAN 2.0 500k（8B） | ≈0.288 Mbit/s（单向 payload） | 0.275 Mbit/s | 95.6% | 4305 帧/s，按 111 位/帧估算 |
| UART4 921600 8N1 | 0.9216 Mbit/s（92.16 KB/s） | 857.9 kbit/s（85.79 KB/s） | 93.1% | 控制台 printf 路径，16KB 连续发 |

![图 24-3 各总线实测吞吐相对理论峰值的利用率](figures/24/fig-24-3-utilization.png)

图 24-3 各类总线实测吞吐相对理论峰值的利用率

CAN 的帧格式验算（用于核对"实测 95.6% / 98.4%"是否可信）：

- 经典 CAN 标准帧 8B 载荷：帧长约 44 个固定位 + 64 个数据位 + 位填充（约 3）+ IFS 3 ≈ **111 位**；
  500kbit/s ÷ 111 = 4504 帧/s 理论上限，实测 4305 帧/s = **95.6%**，合理（含仲裁、抖动、软件开销）。
- CAN FD 1M/2M 64B：仲裁段约 45 位 @1Mbit（45us）+ 数据段约 532 位 @2Mbit（266us）≈ 311us/帧
  = 3214 帧/s；ping-pong 实测 1580 帧/s/方向 ×2 = 3160 帧/s = **98.4%**。

这两个"接近 100%"的数字说明：**CAN 控制器 + TCAN1044AV 收发器的效率很高，瓶颈只在位速率**，
想提高 CAN 吞吐只能提高数据段波特率（5M/8M，tp-can 正在补测）或改用 CAN FD 64B 载荷（已做）。

## 24.6 外设吞吐横向对比

表 24-3 外设吞吐横向对比

| 外设 | 接口 | 实测/理论吞吐 | 瓶颈在哪 | 数据来源 |
|---|---|---|---|---|
| YT8011AN 千兆 PHY | RGMII（4bit×125MHz DDR） | 下行 TCP 902.3 / UDP 944；上行 TCP 466.3 / UDP 641.5 | 下行受协议栈+拷贝（CPU 24.7%）；**上行受 CPU 99.7% 限制（软件）** | tp-net2 实测（日志见章末；优化细节见第 13 章 §13.8.3） |
| LAN8651 10BASE-T1S | SPI2（主机侧 SPI） | 待确认（10Mbit/s 线速上限） | 10BASE-T1S 线速 + PLCA 调度 + SPI 主机带宽 | 见第 10/11 章（tp-net1） |
| CAN FD ×4 | MCAN | 0.809 Mbit/s 单向 payload（1M/2M，64B） | 位速率（2Mbit 数据段） | tp-can 实测 |
| CAN 2.0 ×4 | MCAN | 0.275 Mbit/s 单向 payload（500k，8B） | 位速率 | tp-can 实测 |
| UART ×8 | UART4 921600 | 理论 0.9216 Mbit/s | 波特率（UART 无流控时还可能溢出） | 本仓（理论） |
| SPI（主机） | SPI1/2/3 | **待确认**（需外部从机或回环飞线） | 取决于 SPI 时钟与 FIFO/DMA | 本仓无数据 |
| USB0（设备） | USB 2.0 FS/HS | 待确认（tp-sys 正在测 CDC ACM） | USB 控制器 + 端点缓冲 | tp-sys 待补 |
| XPI NOR | XPI0 Quad | 读 40MB/s；擦 2.13MB/s；写 0.28MB/s | XPI 时钟 + flash 器件特性 | 本仓 + tp-sys |

**读表要点**：以太网是唯一"百 MB/s 级"的对外通路，CAN/UART 都是"几 Mbit/s 级"，
相差 3 个数量级。做"多总线网关"时，瓶颈一定出现在把以太网数据分发到 CAN 的那一侧，
而不是反过来。

## 24.7 实时性：中断延迟与抖动

### 24.7.1 实测数据（第 23 章方法，`bench_irq_latency`，n=10000）

表 24-4 中断延迟与抖动实测

| 配置 | min | avg | max | p99 | sd | 日志 |
|---|---|---|---|---|---|---|
| ISR 主体在 XIP flash | **21 tick = 210 ns** | 210 ns | 220 ns | 220 ns | 0.10 tick | `logs/20261030-114009-bench_irq_latency.log` |
| ISR 主体在 ILM（`.fast`） | 22 tick = 220 ns | 220 ns | 250 ns | 220 ns | 0.03 tick | 同上 |

（GPTMR 配置 100MHz → 1 tick = 10ns；由周期反推实际约 98.5MHz，绝对值误差 ≤1.5%）

### 24.7.2 怎么读这组数

1. **绝对量级：约 210~220ns（≈100~106 个 CPU 周期）**。组成：PLIC 仲裁 + trap 入口 + 保存上下文 +
   跳到 ISR（本仓 ISR 还包含 SDK 的中断包装与一次计数清零）。
2. **抖动极小**：99.99% 的样本落在同一 tick（10ns 分辨率），说明裸机 + 不开 DMA 突发时
   中断确定性非常好。这是做"硬实时"（如 20us 控制环、CAN 时间戳）的好基础。
3. **ISR 放 ILM 并没有更快（反而 +1 tick）**：因为
   - 中断向量表本来就在 ILM（链接脚本 `.vectors ORIGIN(ILM)`）；
   - ISR 代码很小，第二次进中断时已在 L1 I-Cache 里，XIP 取指的代价被 cache 吸收；
   - ILM 版本多了一层函数调用（`isr_measure_body_ilm`），多出 1 tick。
   所以**"中断入口搬 ILM"这条经验在本芯片上收益很小**；要降低最坏延迟，应该关注的是
   关闭不必要的中断嵌套、避免在 ISR 里访问非 cacheable 内存、避免长 DMA 突发抢总线。
4. **什么时候抖动会变大**（值得实测的后续项）：DMA 大突发与 CPU 抢 AXI、以太网接收风暴、
   非 cacheable 缓冲区访问、RTOS 调度。本仓未做这些组合测试（见待确认 P4）。

### 24.7.3 实时性预算表（给控制环用）

表 24-5 20us 控制环实时性预算

| 项目 | 数值 | 说明 |
|---|---|---|
| 中断入口延迟（GPTMR） | 210~220 ns | 实测 |
| ISR 执行时间（本仓简单 ISR） | 496 ns（238 周期） | 实测（含计数器清零与统计） |
| 一次 cache miss 的额外代价（估） | 数十~数百 ns | 由 40MB/s XPI / 800MB/s AXI 推算，未单独实测 |
| 20us 控制环可用 CPU 时间 | 9600 周期 | 480MHz × 20us |
| 其中一次 sinf 占比 | 141.46 周期 = 1.47% | 第 23 章实测 |
| 其中一次 64 位除法占比 | 56.16 周期 = 0.59% | 同上 |

## 24.8 瓶颈分析与调优建议

1. **别用非 cacheable 缓冲区做 CPU 逐字处理**：慢 11.5 倍。正确做法：DMA 搬 + cache 维护
   （`l1c_dc_invalidate` / `l1c_dc_flush`），或把缓冲区设成可缓存的一次性快照。
2. **别用 `memcpy` 当性能路径**：newlib-nano 是逐字节实现（95.9MB/s）。
   需要 CPU 拷贝时自己写 32 位/64 字展开内核（922.9MB/s，快 9.6 倍）；否则用 DMA。
3. **XIP 代码是最大的隐形瓶颈**：顺序读只有 40MB/s，而 AXI SRAM 是 900MB/s 级。
   把**热循环**（控制环、编解码、协议栈收发）搬 ILM 或 RAM，比换编译器选项收益大得多。
   判断方法：用 `bench_compute` 的 ILM/XIP 对比法，或看 map 文件确认代码段位置。
4. **DMA 通道与优先级要规划**：XDMA 链式可达 485MB/s，但 HDMA 走 AHB 桥访问 AXI SRAM 时
   ≥128B 突降（tp-sys 实测 25.9MB/s），这是 AHB 桥的限制，**大块搬运优先用 XDMA**。
5. **CAN FD 用 64B 载荷 + 2Mbit 数据段**：实测 98.4% 总线利用率，已经没有优化空间；
   要更高吞吐只能上 5M/8M 数据段（tp-can 待补）或增加通道并行。
6. **以太网收发路径严重不对称（本章最重要的发现）**：TCP 下行 902~935Mbit/s 时 CPU 只用 24.7%，
   而 TCP 上行 466Mbit/s 时 CPU 已 99.7%。逐项排除后的结论（tp-net2 的 8 组实验，明细见第 13 章 §13.8.3）：

   | 假设 | 验证方法 | 结果 |
   |---|---|---|
   | 物理层/PHY 受限 | UDP 下行 944Mbit/s、UDP 上行 641Mbit/s | 排除：物理层有明显余量 |
   | ENET 驱动/DMA 受限 | 新增 `txbench` 绕过 lwIP 直发：1460B 帧 74,460 pkt/s = **869.7 Mbit/s**（6,446 cyc/帧） | 排除：驱动能到 870Mbit/s |
   | 校验和计算 | 关掉 lwIP 软件 TX 校验和、改 ENET 硬件插入 | 排除：415.3 → 415.3 Mbit/s 无变化 |
   | 数据源在 XIP flash | 发送缓冲从 flash 换到 RAM | 排除：390.4 Mbit/s 无变化 |
   | **TX 前 D-cache 回写** | 实测 `l1c_dc_writeback` 走 CCTL 命令约 **109 周期/cacheline**；1460B 帧 ≈ 46 行 ≈ **5,000 周期** | **命中**：本 SoC MCACHE_CTL=0x00007F03，write-around=0b11（写直达），TX 回写可安全跳过 → TCP +12.3%、UDP +30.1% |
   | 剩余部分 | 优化后 CPU 仍 99.7%，折算 TCP 每 1460B 段 ≈ **12,000 周期**（驱动仅约 750） | 剩余瓶颈 = **lwIP 每段处理**（tcp_write/tcp_output/pbuf 管理/ACK） |

   这条证据链的价值不在"结果多少"，而在**方法**：先分别测"物理层上限"和"驱动上限"，
   再逐个排除软件开销，最后用 cycle 计数定位到具体函数。读者遇到任何"吞吐上不去"都可以照这个顺序走。
   顺带一个反直觉结论：**在写直达（write-around）配置下，发送前不需要 flush D-cache**，
   而很多移植模板默认都会 flush——这就是白花的 5000 周期。
7. **DMA 缓冲区的 cache 维护要算成本**：一次 `l1c_dc_writeback` 约 109 周期/cacheline，
   1KB 数据 ≈ 16 行 ≈ 1700 周期；在 480MHz 下相当于 3.5us。高频小包场景（CAN 网关、
   T1S 转发）要按"包数 × 行数"估算这部分开销，而不是只看 memcpy 带宽。
8. **以太网侧 CPU 占用**（902~935Mbit/s 下行时 24.7%）：如果还要跑控制环，记得给 lwIP 与网络中断设优先级，
   或把协议栈线程化（第 13 章）。**上行方向 99.7% 的 CPU 占用意味着**：想同时做实时控制 + 高速上行，
   必须先把发送路径的软件开销降下来（Jumbo Frame 摊薄每包成本、TCP 发送批处理/零拷贝）。

## 24.9 易错点与注意事项

1. **`clock_get_frequency(clock_gptmr0)` 返回 100MHz，但由中断周期反推实际为 98.5MHz**（差 1.5%）。
   原因未完全定位（推测与 ISR 内计数器清零、状态清除的时钟域延迟有关）。**用 GPTMR 做精确计时前必须自校准**：
   测已知周期信号的实际 tick 数，或像本仓一样用 mcycle 交叉校验。
2. **AXI SRAM 实测 959.6MB/s > "200MHz×32bit=800MB/s"**：小尺寸测的是 L1 D-Cache 命中路径，
   或端口位宽/时钟与 `clock_get_frequency()` 报告不一致。做带宽预算时**以实测为准**，
   但要知道小尺寸数字不代表真实内存带宽（第 23 章 P1）。
3. **非 cacheable 的"写"比"读"快 5 倍**（425 vs 80 MB/s）：写是 posted（有缓冲），读是阻塞的。
   所以"CPU 写非 cacheable 共享缓冲区"尚可接受，"CPU 读"必须避免。
4. **XIP 读的两种测法差别大**：本仓 40MB/s（每轮 invalidate，64KB 超 cache）vs tp-sys 46MB/s（16KB 直读）。
   差异来自 cache 命中比例与是否计入 invalidate 开销，**报数时要写清测试方法**。
5. **UART 吞吐测量易错（已定位）**：直接 `while(!THRE) write(THR)` 逐字节写寄存器，
   实测 6.35MB/s——比 921600 波特率的理论上限 92.16KB/s 高 69 倍，是假值；而且在串口抓包里
   连一个字母都没出现，说明**字节被丢掉了**（写 THR 的节奏与 FIFO 语义不匹配）。
   改用 SDK 控制台路径（`printf("%s", 16KB字符串)` + `fflush` + 等 TEMT）测得 **85.79KB/s = 93.1%**，
   与理论值吻合，日志里也能看到完整 16KB 字母流。**结论：测 UART 吞吐要用 SDK 的发送路径或 DMA，
   不要自己裸写 THR 寄存器。**
6. **同一块板必须串行测试**：本仓所有上板操作走 `scripts/hwrun.sh` 的 `flock`，
   多人/多 agent 并发时会排队（本章数据采集期间实测排队等待 1~3 分钟）。

## 24.10 参考来源

**上游链接（2026-11-02 核对）**

- HPM SDK 源码（总线时钟/存储例程）：<https://github.com/hpmicro/hpm_sdk>
- HPM5E00 数据手册（总线拓扑/位宽/时钟上限）：<https://doc.hpmicro.com/Public/Uploads/uploadfile/files/20250505/HPM5E00FSV01.pdf>
- SPI 互连实测脚本与日志（tp-bus）：@@samples/spi/spi1_spi3_master_slave@@、@@logs/20261102-121103-spi1_spi3_master_slave.log@@


| 类别 | 出处 |
|---|---|
| 本仓实测（本人） | `logs/20261030-113730-bench_bus.log`（XPI/flash/时钟）、`logs/20261030-111657-bench_memory.log`（内存带宽）、`logs/20261030-114009-bench_irq_latency.log`（中断延迟） |
| 本仓实测（tp-sys） | `logs/20261030-110400-dma_mem2mem.log`、`logs/20261030-110512-dma_mem2mem.log`（DMA 带宽）、`logs/20261030-113129-flash_rw_erase.log`（XIP 直读 45.7MB/s）、`logs/20261030-110952-flash_rw_erase.log`（ROM API/擦写） |
| 本仓实测（tp-can） | `logs/20261030-112832-mcan_two_node.log`（CAN FD 1M/2M、CAN 2.0 500k）；例程 `samples/can/mcan_two_node` |
| 本仓实测（tp-net2） | 最终三组：`logs/20261030-153556-enet_lwip_iperf.log`（TCP 下行 902.3Mbit/s）、`logs/20261030-153645`（TCP 上行 466.3Mbit/s）、`logs/20261030-153733`（UDP 上行 641.5Mbit/s）；
优化前对照：`logs/20261030-114622`（TCP 上行 415.3）、`logs/20261030-113919`（UDP 下行 944、丢包 0.13%）、`logs/20261030-113501`（TCP 下行 935）；
`txbench` 直发上限：`logs/20261030-151018-enet_lwip_iperf.log`（869.7Mbit/s）；**优化方法/证据链见第 13 章 §13.8.3 与 `docs/figures/13/fig-13-5-tx-opt.png`**；例程 `samples/enet/enet_lwip_iperf` |
| SDK 源码 | `hpm_sdk/soc/HPM5E00/HPM5E31/toolchains/gcc/flash_xip.ld`（`.vectors → ILM`、`.stack → DLM`、`.noncacheable`）、`hpm_sdk/arch/riscv/l1c/hpm_l1c_drv.h` |
| 外设驱动 | `hpm_sdk/drivers/inc/hpm_gptmr_drv.h`、`hpm_sdk/drivers/inc/hpm_uart_drv.h`、`hpm_sdk/drivers/src/hpm_dma_drv.c` |
| 厂商公开数据 | [HPMicro HPM6700 系列页](https://www.hpmicro.com/en/product-center/microcontroller/hpm6700)（CoreMark/DMIPS 参考，见第 23 章） |
| 图表脚本 | `tools/figs/perf_bus.py`、`tools/figs/perf_utilization.py`、`tools/figs/app24_bus_topology.py`（图 24-1） |

## 24.11 待确认项与风险

> 已逐条复核（2026-11-02）：本节每条都有实测数据、定位结论，或"已知限制 + 原因 + 验证方法"；不再保留裸"待确认/未做"条目。

| 编号 | 待确认项 | 现状 | 验证方法 |
|---|---|---|---|
| P1 | SPI 主机吞吐 | **已实测（2026-11-02，tp-bus 实测）**：SPI1↔SPI3 互连 @10 MHz 单向 873 KB/s、双向 1746 KB/s，0 错误；板内回环 DMA @20 MHz 2490 KB/s（DMA 效率 99%）；SCLK 只能取 80/(2m) MHz 的点（40/20/13.3/10/8…），25/32/16 MHz 配置直接失败 | `logs/20261102-121103-spi1_spi3_master_slave.log`、`logs/20261030-113457-spi_dma_bw.log`（第 07 章 R5 复测） |
| P2 | UART 实测吞吐 | **已实测**：SDK 控制台路径 85.79 KB/s（93.1%）；裸写 THR 丢字节（表 24-1、易错点 5） | 需要更精确时用 UART TX DMA 路径复测（判据：与 92160 B/s 理论差 <1%） |
| P3 | GPTMR 实际时钟与 `clock_get_frequency()` 差 1.5% | **已实测（2026-11-02）**：mcycle 基准反推 98.445 / 98.407 MHz vs 报告 100 MHz = **-1.56% / -1.59%**，见上文小节 | 若要第三方验证：GPTMR 输出引脚接频率计；本轮用 mcycle（已与 mchtmr 互校）做基准，未用示波器 |
| P4 | 中断抖动在"DMA 突发/以太网风暴/RTOS"下的表现 | **已知限制（2026-11-02）**：只测到裸机空闲（sd 0.43 tick）；不过第 19 章已证明 CPU 并发不让 DMA 带宽下降，反过来 DMA 对中断的扰动仍未测 | 在 `bench_irq_latency` 里同时跑 `dmamem` 或 ENET 收包，比较 IRQ_STAT 的 p99/sd |
| P5 | LAN8651 10BASE-T1S 实际吞吐 | **已知限制（由 tp-net 负责，见第 10/11 章；表 24-1 只列 YT8011AN 千兆数据）** | 见第 10/11 章 iperf2 数据 |
| P6 | USB 设备吞吐 | **已实测（2026-11-02）**：主机→设备 1.78 MB/s（16 KB 块），与块大小无关；"先写满再读"回读只有 13312 B（例程 echo 设计限制） | 见第 20 章 5.1（`logs/20261102-123831-usb_cdc_acm_echo-r5b.log`） |
| P7 | 非 cacheable 读的 80MB/s 是否受访问模式影响（连续 vs 跨步） | **已知限制（2026-11-02）**：只测连续 4 KB；本轮 DMA 数据（403~407 MB/s）说明连续访问已到 DMA 上限，跨步没测 | 在 `bench_memory` 加 stride=4/8/16 的读测点，比较 MB/s 折线 |
| P9 | 以太网上行能否达到 700Mbit/s | **已知限制（由 tp-net 主攻，见第 12/13 章；本轮只给参考数据 466.3/641.5 Mbit/s）** | 三条路：Jumbo Frame（MTU 3000~9000，摊薄每包成本；128 KB DLM 装不下放大后的 pbuf pool，需改链接脚本）；TCP 发送批处理/零拷贝（替换逐 MSS `tcp_write`）；硬件 TSO（ENET 不支持）。与 tp-net 协调，避免重复劳动 |
| P8 | AXI SRAM 真实带宽（绕过 cache） | **已实测（2026-11-02）**：XDMA 在 non-cacheable AXI SRAM 上 8 KB 143 / 16 KB 402 / 2048 B 突发 407 MB/s，是"240 MHz × 32 bit = 960 MB/s"理论的 **42%**；缺口来自 DMA 单通道排队与 SCC 到 AXI 的桥 | 复测：`bash scripts/hwrun.sh dma/dma_mem2mem`（`dmamem`/`dmaburst` 上电自动跑） |
