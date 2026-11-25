# 第 8 章 MCAN / CAN FD（4 路）

## 8.1 文档信息


如表 8-1 所示：

**表 8-1 本章文档信息**

| 项 | 内容 |
|---|---|
|   文档版本   | v1.2.0（2026-11-02，R5 复核） |
| 适用芯片 / 板卡 | HPM5E31IPB1（HPM5E00 系列）/ hpm5e31_1 最小系统板 |
| 外设实例 | MCAN0 ~ MCAN3，基地址 0xF0300000 / 0xF0304000 / 0xF0308000 / 0xF030C000，IRQ 46 ~ 49 |
| 对应例程 | <BTK>samples/can/mcan_loopback_internal/<BTK>、<BTK>mcan_two_node/<BTK>、<BTK>mcan_filter/<BTK>、<BTK>mcan_error_recovery/<BTK>、<BTK>mcan_fd_timing/<BTK>、<BTK>mcan_throughput/<BTK>、<BTK>mcan_quad_load/<BTK>（本轮新增） |
| 本章图 | 图 8-1 MCAN 架构、图 8-2 消息 RAM、图 8-3 板级接线、图 8-4 4 路资源与消息 RAM 分配 |
| 实测日期 | 2026-10-30（串口日志见 `logs/`，文件名在各例程小节给出） |
| 可信度 | **高**：实例/基地址/IRQ、CAN 时钟 80 MHz、位时序、双节点收发、过滤器、bus-off 恢复均为本板实测；**中**：TSU/PTPC 时间戳、DMA 通路未实测（见第 12 节）；**低**：其它 HPM5E00 型号的引脚复用需按封装修正 |

## 8.2 本章导览

读完本章你应当能够：

1. 说清 HPM5E31 的 4 个 MCAN（Bosch M_CAN 兼容）在芯片里的资源边界：基地址、IRQ、消息 RAM 放在哪、容量多大。
2. 独立完成 MCAN 的最小可用初始化（时钟 -> 消息 RAM -> 默认配置 -> 位时序 -> 过滤器）。
3. 看懂并修改 CAN FD 位时序：`prescaler/seg1/seg2/sjw`、采样点、TDC/SSP，知道 80 MHz CAN 时钟下常用波特率怎么算。
4. 用两个"片内节点"（CAN0 与 CAN1）搭出真实两节点总线，完成收发、环回、过滤器、错误与恢复测试。
5. 避开本板实测出来的高频易错点（时钟门控、消息 RAM 位置、引脚复用冲突、bus-off 后重新初始化等）。

前置章节：第 01 章（芯片与板卡）、第 02 章（开发环境与烧写）、第 03 章（letter-shell 调试框架）。
后续章节：第 09 章（CAN FD 性能、压力与错误注入）会直接复用本章的例程与测量方法。

## 8.3 概述与典型应用

HPM5E31 集成 **4 个独立的 Bosch M_CAN 兼容控制器（MCAN）**，每个控制器都可以独立配置成
CAN 2.0B 或 CAN FD 节点。官方数据手册给出的能力边界是：

- 4 个 CAN 控制器，支持 CAN-FD（DS p.1、DS p.12）；
- CAN 2.0B 数据段速率 **1 Mbit/s**，CAN FD 数据段速率 **8 Mbit/s**（DS p.12）；
- 每个控制器最多 8 字节（经典帧）/ 64 字节（FD 帧）数据载荷（RM §51.1, p.865）；
- 4 个实例共享一块 **32 KB 的 AHB SRAM（HRAM，0xF0200000 起）** 作为消息存储器，按低 16 位地址划分（RM §51.1, p.865）。


如表 8-2 所示：

**表 8-2 MCAN 关键特性**

| 关键特性 | 规格 | 出处 |
|---|---|---|
| 控制器数量 / 类型 | 4 x MCAN（Bosch M_CAN，ISO 11898-1:2015） | DS p.12；RM §51.1 (p.865) |
| 协议 | CAN 2.0B、CAN FD（ISO 与非 ISO 模式可切） | RM §51.1 (p.865)、§51.4.4 (p.868) |
| 波特率预分频 | 1 ~ 1/16（配合系统 CAN 时钟分频器），驱动层允许 prescaler 1~512 | RM §51.1 (p.865)；`hpm_mcan_drv.h:315` |
| 消息存储 | 4 实例共享 32 KB AHB SRAM；单实例驱动默认 640 words（2560 B） | RM §51.1 (p.865)；`hpm_mcan_soc.h:42` |
| 过滤器 | 标准 ID ≤128 元素、扩展 ID ≤64 元素；范围/双 ID/经典/禁用四种类型 | RM §51.4.13~§51.4.14 (p.874~875)；`hpm_mcan_drv.h:772` |
| 接收缓冲 | RX FIFO0 / RX FIFO1 各 ≤64 元素 + 专用 RX Buffer ≤64 | RM §51.4.15 (p.876) |
| 发送缓冲 | 专用 TX Buffer ≤32 + TX FIFO/Queue ≤32 + TX Event FIFO ≤32 | RM §51.5.3 (p.879) |
| 时间戳 | 16 位内部计数器 / TSU 32 位 / TSU 64 位，可接 PTPC 外部时基 | RM §51.4.9 (p.870)、§51.6.48~§51.6.52 (p.917~919) |
| 中断 | IR/IE/ILS/ILE，可映射到两条中断线 | RM §51.4.16 (p.877)、§51.6.16~§51.6.19 (p.893~900) |
| DMA | 支持（RM 特性列表），本仓库暂未提供实测例程 | RM §51.1 (p.865)；第 12 节待确认 |

典型应用：

1. **伺服/变频器 CANopen（CiA 301）**：4 路可分别接不同轴或不同网段；SDK 自带 `samples/canopen/301` 主从例程。
2. **车载/工程机械 J1939**：64 字节 FD 帧适合多包状态上报，TSU 可做帧级时间戳。
3. **EtherCAT <-> CAN 网关**：HPM5E00 同芯片带 EtherCAT 从站，SDK 的 `hpm_apps-main/apps/gateway_ecat2can` 即为该形态。
4. **多总线数据采集**：4 路 MCAN 独立 FIFO，适合做 4 通道总线记录仪（配 TSU + DMA 更佳）。

## 8.4 工作原理

### 8.4.1 控制器架构与数据通路

MCAN 是"**寄存器 + 消息 RAM**"两层结构：CPU 只配置寄存器（CCCR/NBTP/DBTP/GFC/SIDFC/…），
真正的报文收发由 CAN 内核完成，报文本身存放在消息 RAM 里，CPU 通过消息存储接口读写。

![图 8-1 MCAN 架构与数据通路](figures/08/fig-08-1-mcan-arch.png)

- **配置/状态寄存器**：模式（CCCR）、位时序（NBTP/DBTP）、TDC（TDCR）、过滤器（GFC/SIDFC/XIDFC）、
  中断（IR/IE）、错误（ECR/PSR）等，见 RM §51.6（p.879 起）。
- **CAN 协议内核**：负责位时序、位填充、CRC、ACK、错误帧与总线集成（bus integration）状态机。
  发送路径 TXD -> 收发器 -> 总线；接收路径 RXD <- 收发器 <- 总线。
- **过滤器单元**：标准 ID 列表 + 扩展 ID 列表 + 全局过滤配置（GFC）决定一帧是进 RX FIFO0、
  RX FIFO1、专用 RX Buffer 还是被丢弃，见 RM §51.4.13~§51.4.15（p.874~877）。
- **消息缓冲接口**：把 CAN 内核收发的报文与消息 RAM 中的 TX/RX 元素对应起来；消息 RAM 不在 IP 内，
  而在 AHB SRAM 中，见 2.2 节。
- **TSU**：时间戳单元，可选用内部 16 位计数器、TSU 32/64 位时间戳，或由 PTPC 提供外部时基
  （RM §51.4.9, p.870）。TSU 的 16 组 32 位 / 8 组 64 位时间戳寄存器见 RM §51.6.48~§51.6.52（p.917~919）。

### 8.4.2 消息 RAM：为什么必须放 .ahb_sram

HPM5E31 的 `MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1`（`soc/HPM5E00/HPM5E31/hpm_soc_feature.h:219`），
即消息 RAM **不在 MCAN IP 内部**，而是 4 个实例共享一块 32 KB AHB SRAM：


如表 8-3 所示：

**表 8-3 消息 RAM 资源**

| 项 | 值 | 出处 |
|---|---|---|
| AHB SRAM 基地址 | 0xF0200000 | `hpm_mcan_soc.h:53` |
| 总容量 | 32 KB（`MCAN_MSG_BUF_SIZE_MAX`） | `hpm_mcan_soc.h:54` |
| 单实例驱动默认大小 | 640 words = 2560 B | `hpm_mcan_soc.h:42` |
| 有效地址窗口 | 0xF0200000 ~ 0xF0208000，4 字节对齐 | `hpm_mcan_soc.h:53~55` |
| 链接段 | `.ahb_sram (NOLOAD)` -> AHB_SRAM ORIGIN=0xF0200000 LENGTH=32k | `soc/HPM5E00/HPM5E31/toolchains/gcc/flash_xip.ld:18,276` |

应用层必须做两件事：

1. 用 `ATTR_PLACE_AT(".ahb_sram")` 声明缓冲区；
2. 在 `mcan_init()` 之前调用 `mcan_set_msg_buf_attr()` 注册基址/长度。

`mcan_init()` 内部会按 `ram_config` 计算 SID/XID 过滤器、RX FIFO0/1、RX Buffer、TX Buffer、
TX Event FIFO 各段的起始偏移并写 SIDFC/XIDFC/RXF0C/RXF1C/RXBC/TXBC/TXEFC（RM §51.4.17 消息存储器示意图,
p.877）。驱动默认布局见 图 8-2：经典帧每元素 16 B，FD 帧每元素 72 B（2 words 头 + 64 B 数据）。

![图 8-2 消息 RAM 默认布局（经典 CAN / CAN FD）](figures/08/fig-08-2-msg-ram.png)

> 关键点：**消息 RAM 不在 D-Cache 覆盖范围内**（AHB SRAM 属性为 non-cacheable），所以"放普通 SRAM"
> 不会像 Cortex-M 那样偶尔出错，而是**直接 init 失败或完全收不到帧**。本板 4 个实例各 2560 B，
> 共 10 KB，链接器报告 AHB_SRAM 使用 10 KB / 32 KB。

### 8.4.3 位时序、采样点与 TDC

M_CAN 的位时间由 1 个同步段 + 段 1（传播段 + 相位段 1）+ 段 2（相位段 2）组成：

```text
TQ        = prescaler / f_can_clk
bit_time  = (1 + seg1 + seg2) * TQ
baudrate  = f_can_clk / (prescaler * (1 + seg1 + seg2))
sample_pt = (1 + seg1) / (1 + seg1 + seg2)
```

**最容易照抄错的一点**：驱动结构体 `mcan_bit_timing_param_t` 的 `num_seg1` **不含同步段**
（`hpm_mcan_drv.h:315~332`；驱动写寄存器时做 -1，即 NBTP.NTSEG1 = num_seg1 - 1）。
经典 CAN 驱动 `hpm_can_drv.h` 的 `num_seg1` 语义不同（含同步段），两者不能混用；
本板只有 MCAN，不涉及经典 CAN 驱动。

80 MHz CAN 时钟下常用配置（本章实测所用；实测时钟 80 000 000 Hz 见
`logs/20261030-110650-mcan_loopback_internal.log` 的 `src_clk=80000000 Hz`）：


如表 8-4 所示：

**表 8-4 80 MHz 下常用位时序配置**

| 目标速率 | prescaler | seg1 | seg2 | 总 TQ | 采样点 | 说明 |
|---|---|---|---|---|---|---|
| 500 kbit/s | 1 | 119 / 127 / 139 | 40 / 32 / 20 | 160 | 75.0 / 80.0 / 87.5% | 双节点主用例用 87.5% |
| 1 Mbit/s | 1 | 59 / 63 / 69 | 20 / 16 / 10 | 80 | 75.0 / 80.0 / 87.5% | FD 仲裁段常用 |
| 2 Mbit/s | 1 | 29 / 31 / 34 | 10 / 8 / 5 | 40 | 75.0 / 80.0 / 87.5% | FD 数据段 |
| 4 Mbit/s | 1 | 14 / 15 / 16 | 5 / 4 / 3 | 20 | 75.0 / 80.0 / 85.0% | 20 TQ 凑不出 87.5% |
| 5 Mbit/s | 1 | 11 / 12 / 13 | 4 / 3 / 2 | 16 | 75.0 / 81.25 / 87.5% | FD 数据段 |
| 8 Mbit/s | 1 | 6 / 7 / 8 | 3 / 2 / 1 | 10 | 70.0 / 80.0 / 90.0% | 10 TQ 凑不出 87.5% |

表中"凑不出"是工程事实而非笔误：采样点 = (1+seg1)/N，要得到 87.5% 需要 N 是 8 的倍数，
而 4 Mbit/s 在 80 MHz 下只能取 20 TQ、8 Mbit/s 只能取 10 TQ。第 09 章给出这些组合下的实测丢帧率。

**TDC（发送延迟补偿）**：数据段速率 ≥2 Mbit/s 时，收发器环回延迟（TXD 到 RXD）可接近甚至超过 1 个位时间，
必须开 TDC，让控制器在"实测延迟 + SSP 偏移"处做二次采样。HPM5E31 的 TDCR 在 RM §51.6.15（p.893），
驱动通过 `cfg.enable_tdc` + `cfg.tdc_config.ssp_offset / filter_window_length` 配置
（`hpm_mcan_drv.h:1127~1133`）。

- `ssp_offset` 的常用取值是"数据段 seg1 + 1"（SDK `mcan_txrx_demo.c` 同款写法），
  `filter_window_length` 取同值；本仓库双节点例程实测 `PSR.TDCV = 25`（见第 09 章）。
- **HPM5E00 的勘误表中没有 CAN/MCAN 条目**（HPM5E00_Errata_V0_1 只有 E00038 PWMv2 与 E00042 系统总线时钟，
  p.2~5），因此 HPM6700/6400 的 **E00016（TDC 异常，要求 SSPOFF=0）不适用于本芯片**，HPM5E31 可以正常使用 SSP 偏移补偿。
- 数据段 >5 Mbit/s 时建议实测扫描 `ssp_offset`；第 09 章给出 8 Mbit/s 的 TDC 开关对比数据。

### 8.4.4 过滤器、FIFO 与发送通道

**过滤器**（RM §51.4.13~§51.4.15, p.874~877）：


如表 8-5 所示：

**表 8-5 MCAN 过滤器类型**

| 过滤器类型 | 匹配规则 | 驱动常量 |
|---|---|---|
| 范围过滤 | start_id ≤ ID ≤ end_id | `MCAN_FILTER_TYPE_RANGE_FILTER` |
| 双 ID 过滤 | ID == id1 或 ID == id2 | `MCAN_FILTER_TYPE_SPECIFIED_ID_FILTER` |
| 经典（掩码）过滤 | (rx_id & mask) == (filter_id & mask) | `MCAN_FILTER_TYPE_CLASSIC_FILTER` |
| 禁用 | 不参与匹配 | `MCAN_FILTER_TYPE_FILTER_DISABLED` |

每个元素还要指定匹配后的动作：存 RX FIFO0、存 RX FIFO1、拒收、提升优先级、存专用 RX Buffer
（`hpm_mcan_drv.h:781~790`）。**未命中帧的最终去向由 GFC 决定**，驱动默认是"未命中帧进 RX FIFO0"，
所以"过滤器配了却没生效"十有八九是 GFC 没设成 `MCAN_ACCEPT_NON_MATCHING_FRAME_OPTION_REJECT`。

**接收缓冲**：RX FIFO0/RX FIFO1（阻塞/覆盖两种模式，RM §51.4.15.1~2, p.877），以及专用 RX Buffer。
FIFO 深度在 `ram_config` 中分配，FD 默认每 FIFO 8 个 64 B 元素（图 8-2）。

**发送通道**（RM §51.5.3, p.879）：


如表 8-6 所示：

**表 8-6 MCAN 发送通道对比**

| 通道 | 特点 | 驱动 API |
|---|---|---|
| TX FIFO | 先提交先发送；提交后不可按帧取消 | `mcan_transmit_via_txfifo_nonblocking()` |
| TX Queue | 按 ID 优先级发送，低 ID 先走；实时性最好 | `mcan_transmit_via_txqueue_nonblocking()` |
| 专用 TX Buffer | 固定槽位、可按槽取消 | `mcan_transmit_via_txbuf_nonblocking()` |
| TX Event FIFO | 发送完成事件 + message marker 关联 | `mcan_read_tx_evt_fifo()` |

### 8.4.5 中断与错误处理

MCAN 的中断分两级：`IR/IE` 是事件标志与使能，`ILS` 把事件映射到中断线 0/1，`ILE` 使能线
（RM §51.4.16 p.877，§51.6.16~§51.6.19 p.893~900）。最常用的三类事件：

- `MCAN_INT_RXFIFO0_NEW_MSG`：收到新帧（配合 `mcan_read_rxfifo()`）；
- `MCAN_EVENT_TRANSMIT` / TX buffer 完成中断：发送完成（配合 TX Event FIFO 的 marker）；
- 错误类：错误警告、错误被动、bus-off、协议异常（配 `mcan_get_protocol_status()` / `mcan_get_error_state()`）。

错误状态机与 CAN 2.0B 一致：TEC/REC 计数、错误主动（TEC/REC<128）-> 错误被动（≥128）-> bus-off（TEC>255）。
驱动提供 `mcan_get_error_counter()`（`hpm_mcan_drv.h:1632`）、`mcan_get_protocol_status()`（:2656）、
`mcan_recover_from_busoff()`（:2698）。第 6.4 节给出错误注入与恢复的完整实测。

## 8.5 关键寄存器与驱动 API 对照表

### 8.5.1 寄存器

寄存器偏移与位域详见 RM §51.6（p.879 起）。下表只列本章例程实际用到的寄存器：


如表 8-7 所示：

**表 8-7 MCAN 关键寄存器**

| 功能 | 寄存器（偏移） | 关键位域 | 驱动 API / 例程用法 | 出处 |
|---|---|---|---|---|
| 模式与使能 | CCCR (0x18) | INIT / CCE / TEST / MON / FDOE / BRSE | `mcan_init()`、`mcan_begin_reconfig()` | RM §51.6.7 (p.884) |
| 标称位时序 | NBTP (0x1C) | NBRP / NTSEG1 / NTSEG2 / NSJW | `cfg.can_timing` | RM §51.6.8 (p.886) |
| 数据段位时序 | DBTP (0x0C) | DBRP / DTSEG1 / DTSEG2 / DSJW / TDC | `cfg.canfd_timing` | RM §51.6.4 (p.882) |
| 发送延迟补偿 | TDCR (0x48) | TDCO / TDCF | `cfg.tdc_config` | RM §51.6.15 (p.893) |
| 测试/回环 | TEST (0x10) | LBCK / TX / RX / TDCV | `mcan_mode_loopback_internal` | RM §51.6.5 (p.883) |
| 全局过滤 | GFC (0x80) | ANFS / ANFE / RRFS / RRFES | `all_filters_config.global_filter_config` | RM §51.6.20 (p.900) |
| 标准 ID 过滤器 | SIDFC (0x84) + RAM | FLSSA / LSS / FLESA | `std_id_filter_list` | RM §51.6.21 (p.901) |
| 扩展 ID 过滤器 | XIDFC (0x88) + RAM | FLESA / LSE / FLESA | `ext_id_filter_list` | RM §51.6.22 (p.902) |
| 扩展 ID 掩码 | XIDAM (0x90) | EIDM | `all_filters_config.ext_id_mask` | RM §51.6.23 (p.902) |
| RX FIFO0 配置/状态/应答 | RXF0C (0xA0) / RXF0S (0xA4) / RXF0A (0xA8) | F0SA / F0S / F0GI | `mcan_read_rxfifo(ptr,0,..)` | RM §51.6.27~29 (p.904~905) |
| RX FIFO1 | RXF1C (0xB0) / RXF1S (0xB4) / RXF1A (0xB8) | F1SA / F1S / F1GI | `mcan_read_rxfifo(ptr,1,..)` | RM §51.6.31~33 (p.906~908) |
| TX 缓冲配置 | TXBC (0xC0) / TXFQS (0xC4) / TXESC (0xC8) | TBSA / TFQS / NDTP / TBDS | `cfg.ram_config` | RM §51.6.35~37 (p.909~911) |
| TX 请求/状态 | TXBAR (0xD0) / TXBRP (0xCC) / TXBTO (0xD8) / TXBCF (0xDC) | AR / TRP / TO / CF | `mcan_transmit_via_txfifo_nonblocking()` | RM §51.6.38~42 (p.911~914) |
| TX Event FIFO | TXEFC (0xF0) / TXEFS (0xF4) / TXEFA (0xF8) | EFS A / EFGI | `mcan_read_tx_evt_fifo()` | RM §51.6.45~47 (p.915~916) |
| 中断 | IR (0x50) / IE (0x54) / ILS (0x58) / ILE (0x5C) | RF0N / TC / BO / EW / EP ... | `mcan_get_interrupt_flags()` | RM §51.6.16~19 (p.893~900) |
| 错误计数/状态 | ECR (0x40) / PSR (0x44) | TEC / REC / LEC / DLEC / ACT / BO / EP / EW | `mcan_get_error_counter()` | RM §51.6.13~14 (p.889) |
| 时间戳 | TSCC (0x20) / TSCV (0x24) / TSS1 (0x248) / TSS2 (0x24C) | TSS / TSP / TCP | `mcan_get_timestamp_from_tx_event()` | RM §51.6.9~10 (p.887)、§51.6.51~52 (p.919) |
| TSU 配置 | TSCFG (0x244) / GLB_CTL (0x400) | TBP / TBIN_SEL | `tsu_config` | RM §51.6.50 (p.918)、§51.6.55 (p.920) |

### 8.5.2 驱动 API

驱动头文件 `drivers/inc/hpm_mcan_drv.h`，源文件 `drivers/src/hpm_mcan_drv.c`（HPM SDK v1.13.0）：


如表 8-8 所示：

**表 8-8 MCAN 驱动 API 对照**

| 用途 | API | 行号 | 备注 |
|---|---|---|---|
| 取默认配置 | `mcan_get_default_config()` | hpm_mcan_drv.h:2134 | 波特率默认 500 kbit/s / FD 2 Mbit/s，采样点窗口 75~87.5% |
| 初始化 | `mcan_init()` | :2296 | 内部完成时钟使能、RAM 分配、过滤器、模式切换 |
| 反初始化 | `mcan_deinit()` | :2304 | 会关闭控制器；**必须在 CAN 时钟已使能时调用**（见第 9 节易错点 3） |
| 静态重配置 | `mcan_begin_reconfig()` / `mcan_end_reconfig()` | :2144 / :2154 | 运行中改位时序/过滤器时使用 |
| 阻塞发送 | `mcan_transmit_blocking()` | :2493 | 内部用 TX FIFO，等 TXBTO |
| 非阻塞 TX FIFO | `mcan_transmit_via_txfifo_nonblocking()` | :2521 | 批量发送首选 |
| 非阻塞 TX Queue | `mcan_transmit_via_txqueue_nonblocking()` | :2536 | 需先使能 TX Queue 模式 |
| 指定 TX Buffer | `mcan_transmit_via_txbuf_nonblocking()` | :2549 | 可按槽取消 |
| 读 RX FIFO | `mcan_read_rxfifo()` | :2472 | 非阻塞，返回 empty 时重试 |
| 阻塞收帧 | `mcan_receive_from_fifo_blocking()` | :2573 | 内部重试计数到 `MCAN_RX_RETRY_COUNT_MAX` |
| TX 事件 | `mcan_read_tx_evt_fifo()` | :2482 | 与 message marker 关联发送完成 |
| 协议状态 | `mcan_get_protocol_status()` | :2656 | LEC/DLEC/ACT/BO/EP/EW/TDCV |
| 错误计数 | `mcan_get_error_counter()` | :1632 | TEC / REC / 错误日志计数 |
| 错误状态 | `mcan_get_error_state()` | :2667 | 错误主动/被动/bus-off 解码 |
| bus-off 恢复 | `mcan_recover_from_busoff()` | :2698 | 清 INIT 重新集成总线 |
| 消息 RAM 注册 | `mcan_set_msg_buf_attr()` | hpm_mcan_soc.h:137 | 基址必须落在 0xF0200000~0xF0208000 |
| 中断控制 | `mcan_enable_interrupts()` 等 | :1795 | 配合 `SDK_DECLARE_EXT_ISR_M()` |

## 8.6 四路资源与板级接线

![图 8-3 四路 CAN FD 接线拓扑](figures/08/fig-08-3-board-wiring.png)

### 8.6.1 引脚与实例映射（hpm5e31_1）


如表 8-9 所示：

**表 8-9 hpm5e31_1 引脚与实例映射**

| 通道 | 控制器 | TXD | RXD | 收发器 | 终端电阻 | 状态灯（红/白） |
|---|---|---|---|---|---|---|
| CAN0 | MCAN0 (0xF0300000, IRQ46) | PD00 | PD01 | TCAN1044AV | 120 Ω（与 CAN1 同段总线两端） | PF26 / PF18 |
| CAN1 | MCAN1 (0xF0304000, IRQ47) | PD05 | PD04 | TCAN1044AV | 120 Ω（与 CAN0 同段总线两端） | PF20 / PF16 |
| CAN2 | MCAN2 (0xF0308000, IRQ48) | PC08 | PA09 | TCAN1044AV | 120 Ω（与 CAN3 同段总线两端） | PF22 / PF21 |
| CAN3 | MCAN3 (0xF030C000, IRQ49) | PC15 | PC14 | TCAN1044AV | 120 Ω（与 CAN2 同段总线两端） | PF23 / PF19 |

引脚复用由板级 `boards/hpm5e31_1/pinmux.c` 的 `init_mcan0_pins()` ~ `init_mcan3_pins()` 完成，
本章不新增 pinmux 文件（避免与板级同名符号冲突）。8 颗状态灯由 `init_canled_pins()` 配置。
板级宏集中在 `boards/hpm5e31_1/ext/board_can.h`（由 tp-can 维护）。

> **引脚冲突（重要）**：CAN2 的 TXD 是 **PC08**，而板级 `init_i2c0_pins()` 把 PC08 配成 I2C0_SCL；
> CAN3 的 PC15/PC14 又与 PPI0_DQ_19/DQ_18 复用。**同一工程里不要同时初始化这些外设**，
> 否则后初始化的那个会抢走引脚。本章例程只初始化自己用到的 `init_mcan*_pins()`。

### 8.6.2 收发器与终端电阻

- 4 颗 **TCAN1044AV**（SOIC-8：1=TXD 2=GND 3=VCC 4=RXD 5=STB 6=CANL 7=CANH 8=VCC），
  STB 低 = 正常工作，高 = standby。本板 4 颗 STB **共用 PE03**，代码里由
  `board_can_transceivers_enable()` 拉低（`boards/hpm5e31_1/ext/board_can.h`）。
- 终端电阻两两成对：CAN0-CAN1 一段总线、CAN2-CAN3 一段总线，**每段两端各 120 Ω**（板上固定）。
  这意味着 MCU 内部两个 MCAN 就能组成一个合法的两节点总线 —— 不需要外部 CAN 分析仪。
- 8 颗状态灯高电平点亮，可分别做 TX / RX / ERR 指示；本章例程在收发时点亮对应灯。

## 8.7 初始化流程与最小代码

正确顺序（任何一步顺序错都可能挂死或收不到帧）：

1. 打开板级引脚（`init_mcan*_pins()`）与收发器 STB；
2. **打开 CAN 外设时钟**（`board_init_can_clock()` -> `clock_add_to_group(clock_canX,0)`）；
3. 注册消息 RAM（`mcan_set_msg_buf_attr()`，缓冲在 `.ahb_sram`）；
4. 取默认配置，改 mode / 波特率 / 位时序 / 过滤器；
5. `mcan_init()`；
6. 收发；需要重配时用 `mcan_begin_reconfig()/mcan_end_reconfig()` 或 `mcan_deinit()+mcan_init()`。

下面这段代码来自 `samples/can/mcan_two_node/src/main.c`（双节点初始化，经典帧 + FD 两套）：

```c
static hpm_stat_t node_init(MCAN_Type *base, uint32_t *msg_buf, bool fd)
{
    mcan_config_t cfg;
    mcan_msg_buf_attr_t attr = {(uint32_t) msg_buf, MCAN_MSG_BUF_SIZE_IN_WORDS * 4U};
    mcan_set_msg_buf_attr(base, &attr);              /* 消息 RAM 必须在 AHB_SRAM */
    mcan_get_default_config(base, &cfg);
    cfg.mode = mcan_mode_normal;
    cfg.enable_canfd = fd;
    cfg.use_lowlevel_timing_setting = true;
    cfg.can_timing.prescaler = 1U;                   /* 80MHz/(1*(1+139+20)) = 500k */
    cfg.can_timing.num_seg1 = 139U;
    cfg.can_timing.num_seg2 = 20U;
    cfg.can_timing.num_sjw  = 4U;
    if (fd) {
        cfg.canfd_timing.prescaler = 1U;             /* 80MHz/(1*(1+29+10)) = 2M */
        cfg.canfd_timing.num_seg1 = 29U;
        cfg.canfd_timing.num_seg2 = 10U;
        cfg.canfd_timing.enable_tdc = true;          /* 数据段高速必须开 TDC */
        cfg.enable_tdc = true;
        cfg.tdc_config.ssp_offset = 15U;
        cfg.tdc_config.filter_window_length = 15U;
        mcan_get_default_ram_config(base, &cfg.ram_config, true);
    }
    board_init_can(base);
    uint32_t clk = board_init_can_clock(base);       /* 先开时钟！ */
    mcan_deinit(base);
    return mcan_init(base, &cfg, clk);
}
```

## 8.8 例程逐条讲解

本仓库 `samples/can/` 下共有 6 个例程，覆盖从"不接线的内部环回自检"到"双节点真实总线性能/错误注入"的完整梯度：

如表 8-10 所示：

**表 8-10 CAN 例程总览**

| 例程 | 覆盖特性 | 上板条件 | 关键配置 | 实测结论 | 日志 |
|---|---|---|---|---|---|
| `mcan_loopback_internal` | 4 实例 x 经典/FD x 标准/扩展 ID，内部环回 | 只需串口 | 内部环回、500 kbit/s + FD 2 Mbit/s | 16/16 项 PASS | `logs/20261030-110650-mcan_loopback_internal.log` |
| `mcan_two_node` | CAN0<->CAN1 真实总线、ACK、ping-pong、单向吞吐 | 板内两节点 + 120 Ω | 500 kbit/s SP87.5%；FD 1M/2M | 经典 4305 帧/s；FD ping-pong 1580 次/s，0 错 | `logs/20261030-113541-mcan_two_node.log` |
| `mcan_filter` | 经典/范围/双 ID 过滤、FIFO0/1、标准/扩展、GFC 拒收 | 内部环回 | GFC=reject，5 个场景 14 个探针 | 14/14 项 PASS | `logs/20261030-114549-mcan_filter.log` |
| `mcan_error_recovery` | 错误帧、TEC/REC、错误被动、bus-off、恢复 | 板内两节点 | 对端 listen-only 造成无 ACK | 见 7.3 节 | `logs/20261030-115202-mcan_error_recovery.log`（旧版）；修复后日志见 7.3 节 |
| `mcan_fd_timing` | 24 组位时序矩阵、采样点、TDC 开关 | 板内两节点 | 仲裁 500k/1M x 数据 2M/4M/5M/8M | 见 7.2 节 | `logs/20261030-114002-mcan_fd_timing.log`（初版） |
| `mcan_throughput` | 经典/FD 单向吞吐、CPU 周期 | 板内两节点 | 6 种速率组合，每次 2000 帧 | 见 7.1 节 | `logs/20261030-114225-mcan_throughput.log` |

### 8.8.1 mcan_loopback_internal：4 路内部环回自检

**路径**：`samples/can/mcan_loopback_internal/`；**特性**：MCAN 内部环回（TEST.LBCK），
不需要收发器/总线/终端电阻，用于确认"MCAN IP + 消息 RAM + 位时序"通路本身是好的。

- 4 个实例各跑 4 轮：经典标准帧 0x123/8B、经典扩展帧 0x12345678/8B、FD 标准帧 0x1AB/64B、FD 扩展帧 0x1ABCDEF0/64B；
- 每轮"发起送 -> 从 RXFIFO0 读回 -> 逐字节比对 ID/DLC/FDF/BRS/负载"，打印 PASS/FAIL 与 LEC/TEC/REC；
- 实测：**16/16 项 PASS，src_clk = 80 000 000 Hz**（`logs/20261030-110650-mcan_loopback_internal.log`）。
  命令 `info` 打印消息 RAM 地址，`stat` 打印 4 个实例的 PSR/ECR。

### 8.8.2 mcan_two_node：CAN0 <-> CAN1 真实两节点总线

**路径**：`samples/can/mcan_two_node/`；**特性**：板内 CAN0 与 CAN1 经各自 TCAN1044AV 接在同一段
带 120 Ω 终端电阻的真实总线上，MCU 内部两个 MCAN 构成两个节点，可做带 ACK 的真实收发。

- `run [n]`：经典 ping-pong（CAN0 发 0x123 -> CAN1 收并校验 -> CAN1 回 0x321 -> CAN0 收），
  实测 500 帧 **ok=500、bad_req=0、bad_echo=0**，echo 传播延迟 avg 1 µs、max 1 µs（`logs/20261030-113541-mcan_two_node.log`）；
- `thr [n]`：经典单向流水 2000 帧 **4305 frame/s、0 丢帧**；
- `fd [n]`：FD 1 Mbit/s 仲裁 / 2 Mbit/s 数据、64 B，ping-pong 500 次 **ok=500、err=0、1579 roundtrip/s**；
- `stat`：正常通信时 CAN0/CAN1 的 TEC/REC 均为 0、LEC=0，TDCV=25。

### 8.8.3 mcan_filter：过滤器路由

**路径**：`samples/can/mcan_filter/`；**特性**：MCAN0 内部环回 + GFC 设为"未命中即拒收"，
用探针 ID 验证"命中进哪个 FIFO / 是否被丢弃"。5 个场景、14 个探针全部 PASS
（`logs/20261030-114549-mcan_filter.log`）：

| 场景 | 配置 | 探针与结果 |
|---|---|---|
| 经典精确 | filter_id=0x123, mask=0x7FF -> FIFO0 | 0x123 -> FIFO0；0x124 -> 丢弃 |
| 范围 | [0x200,0x20F] -> FIFO0 | 0x200/0x20F -> FIFO0；0x210 -> 丢弃 |
| 双 ID | 0x300 / 0x301 -> FIFO0 | 0x300/0x301 -> FIFO0；0x302 -> 丢弃 |
| FIFO 分流 | 0x400 -> FIFO0，0x401 -> FIFO1 | 0x400 -> FIFO0；0x401 -> FIFO1；0x402 -> 丢弃 |
| 标准 + 扩展 | 标准全通 -> FIFO0；扩展 [0x1ABCDE00,0x1ABCDEFF] -> FIFO1 | 标准 0x010 -> FIFO0；扩展 0x1ABCDE12 -> FIFO1；扩展 0x1ABCDD00 -> 丢弃 |

关键配置片段（过滤器元素 + GFC）：

```c
cfg.all_filters_config.global_filter_config.accept_non_matching_std_frame_option = MCAN_ACCEPT_NON_MATCHING_FRAME_OPTION_REJECT;
cfg.all_filters_config.global_filter_config.accept_non_matching_ext_frame_option = MCAN_ACCEPT_NON_MATCHING_FRAME_OPTION_REJECT;
static const mcan_filter_elem_t f[] = {
    {MCAN_FILTER_TYPE_CLASSIC_FILTER, MCAN_FILTER_ELEM_CFG_STORE_IN_RX_FIFO0_IF_MATCH, 0, 0,
     .filter_id = 0x123U, .filter_mask = 0x7FFU},
};
cfg.all_filters_config.std_id_filter_list.filter_elem_list = f;
cfg.all_filters_config.std_id_filter_list.mcan_filter_elem_count = 1;
```

### 8.8.4 mcan_error_recovery：错误注入与 bus-off 恢复

**路径**：`samples/can/mcan_error_recovery/`；**特性**：把对端 CAN1 设成 **listen-only**
（只听模式：不应答 ACK、不发错误帧），CAN0 正常发送 -> 每重传一次 TEC +8 -> 错误被动（TEC≥128）->
bus-off（TEC>255）；随后把 CAN1 恢复正常并调用 `mcan_recover_from_busoff()`，验证恢复后能正常收发。

- 定义与恢复流程见 RM §51.4.7~§51.4.8（p.869）、错误计数寄存器 ECR（RM §51.6.13, p.889）；
- 实测数据见 7.3 节；日志 `logs/20261030-120042-mcan_error_recovery.log`。

> 早期版本曾用"对端 250 kbit/s 错配"来制造错误，实测发现：错配节点会主动发错误帧，
> 双方 TEC/REC 此消彼长（CAN0 的 REC 涨到 127、TEC 只在 6~14 徘徊），**不一定能进入 bus-off**。
> 要稳定复现 bus-off，应让对端"完全不应答"（listen-only 或未初始化），这是本章实测得到的一个易错点。

### 8.8.5 mcan_fd_timing：位时序矩阵与 TDC

**路径**：`samples/can/mcan_fd_timing/`；**特性**：24 组"仲裁段 x 数据段 x 采样点"组合，
每组用 CAN0 -> CAN1 单向发 300 帧 64 B FD 帧（BRS=1），统计丢帧/错误码/吞吐；另有 TDC 开关对比。

- 覆盖：仲裁 500 kbit/s / 1 Mbit/s；数据 2/4/5/8 Mbit/s；每档 3 个采样点（75%、80%、最接近 87.5% 的可达值）；
- 驱动会对非法位时序返回 `status_mcan_invalid_bit_timing = 26014`（`hpm_mcan_drv.h:145`），
  例如"2 Mbit/s + 87.5%"需要 seg1=34，超过 FD 数据段 seg1 上限 32（`hpm_mcan_drv.c:73`，校验逻辑 :755~787），
  矩阵里用 77.5% 代替；"8 Mbit/s + 90%"需要 seg2=1，小于 seg2 下限 2，被驱动拒绝，用 60% 代替；
- 实测矩阵见 7.2 节；TDC 开关对比数据同节。

### 8.8.6 mcan_throughput：单向吞吐与 CPU 周期

**路径**：`samples/can/mcan_throughput/`；**特性**：6 种速率组合各发 2000 帧，统计 frame/s、
payload Mbit/s 与"每帧平均 CPU 周期"（轮询方式，CPU 全程忙等，数值可用于估算 CPU 占用）。

- 覆盖：经典 500 kbit/s / 1 Mbit/s（8 B）、FD 500k/2M、1M/2M、1M/5M、1M/8M（64 B）；
- 实测数据见 7.1 节；命令 `one <idx> [n]` 可单独重跑某一档。

## 8.9 上板实测数据

测量方法：CAN0 发送、CAN1 接收并 ACK，两节点在同一段真实总线（TCAN1044AV，两端 120 Ω），
串口 921600 打印，时间基准为 CPU 周期计数器（`hpm_csr_get_core_mcycle()`，480 MHz 实测）。
所有数据来自 `logs/` 下的日志文件，可直接复查。

### 8.9.1 单向吞吐（2000 帧/组，轮询方式）

如表 8-11 所示：

**表 8-11 CAN / CAN FD 单向吞吐实测（CAN0 -> CAN1，2000 帧，轮询，0 丢帧）**

| 用例 | 载荷 | frame/s | payload Mbit/s | CPU 周期/帧 | LEC/TEC/REC | 日志 |
|---|---|---|---|---|---|---|
| 经典 500 kbit/s | 8 B | 4313 | 0.28 | 111 290 | 0/0/0 | `logs/20261030-114225-mcan_throughput.log` |
| 经典 1 Mbit/s | 8 B | 8615 | 0.55 | 55 714 | 0/0/0 | 同上 |
| FD 500k/2M | 64 B | 2632 | 1.35 | 182 348 | 0/0/0 | 同上 |
| FD 1M/2M | 64 B | 2843 | 1.46 | 168 859 | 0/0/0 | 同上 |
| FD 1M/5M | 64 B | 6302 | 3.23 | 76 172 | 0/0/0 | 同上 |
| FD 1M/8M | 64 B | 9058 | 4.64 | 52 989 | 0/0/0 | 同上 |

要点：

1. 经典帧 500 kbit/s 的理论上限约 500 000/108 ≈ 4630 帧/s（108 bit 为 8 字节标准帧的无填充位数），
   实测 4313 帧/s 达到理论值的 **93%**，差值是位填充与软件轮询开销。
2. FD 64 B 在 8 Mbit/s 数据段下达到 **4.64 Mbit/s 有效载荷吞吐**、9058 帧/s；
   受 1 Mbit/s 仲裁段（每帧约 22 bit ≈ 22 µs）限制，继续提高数据段速率收益递减（5M -> 8M 只提升 44%）。
3. "CPU 周期/帧"是**轮询方式**下 CPU 全程忙等的周期数（480 MHz），不是硬件开销；
   若要释放 CPU，应改用 RX FIFO 中断 + 批量读（第 09 章讨论）。

### 8.9.2 位时序矩阵（24 组，每组 300 帧 64 B FD，BRS=1）

驱动对位时序有硬性校验（`hpm_mcan_drv.c:755~787`）：FD 数据段 seg1 上限 32（含同步段，
即 `num_seg1 ≤ 31`）、seg2 ∈ [2,16]、TDC 开启时数据段 prescaler ≤2。因此：

- 2 Mbit/s 在 80 MHz 下只能取 40 TQ，80% 已是 seg1≤31 的上限，**87.5% 不可达**；
- 8 Mbit/s 只能取 10 TQ，seg2≥2 时上限是 80%，**87.5%/90% 不可达**；
- 矩阵中 1 组故意越界的组合（2M×82.5%，seg1=32）被驱动以 `status_mcan_invalid_bit_timing = 26014` 拒绝，
  属预期行为，日志中可见 `TX init failed st=26014`。

如表 8-12 所示：

**表 8-12 位时序矩阵实测（64 B FD，BRS=1，每组 300 帧，全部 0 丢帧）**

| 数据段 | 仲裁 500k 采样点 | 500k 下 frame/s | 仲裁 1M 采样点 | 1M 下 frame/s | payload Mbit/s | TDCV |
|---|---|---|---|---|---|---|
| 2 Mbit/s | 75.0 / 80.0% | 2842 | 75.0 / 80.0% | 2842 | 1.45~1.46 | 40~42 |
| 4 Mbit/s | 75.0 / 80.0 / 85.0% | 4565 | 75.0 / 80.0 / 85.0% | 5239~5240 | 2.34~2.68 | 25~27 |
| 5 Mbit/s | 75.0 / 81.25 / 87.5% | 5352 | 75.0 / 81.25 / 87.5% | 6303 | 2.74~3.23 | 22~24 |
| 8 Mbit/s | 60 / 70 / 80% | 7218~7219 | 60 / 70 / 80% | 9059~9060 | 3.70~4.64 | 16~18 |

结论：

1. 在本板的短总线（<0.3 m）与两端 120 Ω 终端下，**采样点 60%~87.5% 全部 0 丢帧**，
   说明"采样点必须在 75%~80%"是长总线/多节点场景的经验值，短总线余量很大；
2. 同一数据段下，仲裁段从 500 kbit/s 提到 1 Mbit/s 对吞吐的影响在 8 Mbit/s 数据段时才明显
   （7219 -> 9059 帧/s，+25%），因为仲裁段 22 bit 的占比随数据段加快而变大；
3. `PSR.TDCV`（TDC 测量值）随数据段速率升高而降低（2M: 42 -> 8M: 18 TQ），
   与"位时间变短、环回延迟占的 TQ 数变少"一致（RM §51.6.14, p.889）。

4. **TDC 偏移必须小于数据段位长，不是越大越稳**。SSP 位置由 TDCR.TDCO 直接定义（单位 = 数据段 mtq），
   本板 5 Mbit/s 一位只有 16 tq；TDCO 取 20 时 SSP 越界，发送端在数据段判位错误、TEC 累加到 248 进 bus-off，
   现象很像"收发器跑不了 5M"。同一固件只扫 TDCO：1 失败、8/10/12/13/14 通过、20 失败；
   PSR.TDCV 恒等于 TDCO+10（环回延迟折算约 10 mtq）。MCAL 侧据此修复（第 47 章表 47-17），
   本章矩阵用的 ssp_offset = seg1+1 = 12 恰好是合法值，所以一直正常。

TDC 开关对比（仲裁 1 Mbit/s、采样点 80%）：

如表 8-13 所示：

**表 8-13 TDC 开/关对比（300 帧，全部 0 丢帧）**

| 数据段 | TDC=0 | TDC=1 | 结论 |
|---|---|---|---|
| 2 Mbit/s | 2842 frame/s, bad=0, tdcv=0 | 2842 frame/s, bad=0, tdcv=42 | 本板环回延迟 < 1 位时间，TDC 不改变吞吐 |
| 5 Mbit/s | 6303 frame/s, bad=0, tdcv=0 | 6304 frame/s, bad=0, tdcv=23 | 同上；但 TDC 提供 23 TQ 的二次采样点余量 |
| 8 Mbit/s | 0 丢帧（见日志） | 0 丢帧（见日志） | 短总线 + 低延迟收发器下两种配置都可用 |

> 注意：TDC=0 在**本板短总线**上没出错，不代表长总线/高延迟收发器也不会出错。按 ISO 11898-2 与
> TI TCAN1044AV 数据手册，收发器环回延迟典型 100~200 ns，8 Mbit/s 时位时间仅 125 ns，
> 因此工程上数据段 ≥2 Mbit/s 一律建议开 TDC（见第 09 章）。

### 8.9.3 错误注入与 bus-off 恢复

测量条件：CAN0 500 kbit/s 正常模式；CAN1 500 kbit/s **listen-only**（RM §51.4.5, p.868），
不应答 ACK、不发错误帧，CAN0 每次重传产生 ACK 错误。

实测现象（`logs/20261030-120042-mcan_error_recovery.log`）：

| 阶段 | 现象 | 数据 |
|---|---|---|
| 单节点无 ACK 发送 | 错误主动阶段 TEC 每 +8 约需数 ms（60 ms 内 0 -> 111） | TEC=111、REC=0、LEC=0 |
| 继续重传 | 进入错误被动后 TEC 增长明显变慢：200 ms 只从 111 涨到 135，未达 bus-off（TEC>255） | TEC=135、EP=1、BO=0、LEC=3（ACK 错误） |
| 修复对端为 500 kbit/s 正常模式 | `mcan_recover_from_busoff()` 返回 0 | recovered=1，耗时 0 µs（调用前总线已静默足够长时间，硬件已自动退出 bus-off） |
| 恢复后收发 | 发送 0x123/8B 且 CAN1 收到，数据 0xDEADBEEF 一致 | **PASS**，TEC=118/REC=0（错误历史保留，通信正常） |

结论与易错点：

1. **单节点无 ACK 时 bus-off 不是"瞬间"发生的**：每次重传间隔由硬件重传时序决定，实测约 3.7 ms/+8 TEC，
   要达到 TEC>255 需 30 次以上重试（>100 ms）。做 bus-off 测试要留足时间窗口（建议 ≥500 ms）；
2. **对端"波特率错配"不等于"无 ACK"**：错配节点会主动发错误帧，双方 TEC/REC 此消彼长，
   反而不容易进 bus-off（早期版本实测 CAN0 REC=127、TEC 仅 6~14）。要稳定复现请用 listen-only 对端；
3. bus-off 恢复后 **TEC/REC 会保留历史值**（实测 TEC=132），只要 PSR.BO=0 且能正常收发即视为恢复成功，
   不要用 TEC 是否归零判断。


### 8.9.4 四路并发、5 Mbit/s 与消息 RAM

第 09 章把 4 路并发单独做成例程 `samples/can/mcan_quad_load/`（本轮新增），本章只用它说明板级结论：

- 4 个 MCAN 的消息缓冲各占 2560 B，合计 10 KB，放在 32 KB AHB SRAM 里，剩余约 22 KB 可用来加深 RX FIFO；
  这 10 KB 是固定的、不随帧率变化，所以并发时消息 RAM 不是吞吐的限制因素（分配见图 8-4）；
- 5 Mbit/s 数据段（1M 仲裁 + 5M 数据，BRS=1）是板载 TCAN1044AV 的规格上限，第 09 章表 9-5 的
  6302 fps、3.23 Mbit/s payload 就是这条配置实测出来的；
- 8 Mbit/s 数据段控制器可以配（80 MHz、10 TQ），但超了收发器规格——板载 TCAN1044AV 的数据手册
  上限是 5 Mbit/s（[TI TCAN1044A-Q1](https://www.ti.com/product/TCAN1044A-Q1)），8M 只能做短总线实验，
  不能当量产配置，详见第 09 章 §9.6.2。

- 4 路并发实测（2026-11-02，`logs/20261102-122843-mcan_quad_load.log`）：两组总线双向
  （4 个 MCAN 全忙）时每方向 1425 fps（2 Mbit/s 数据段）/3166 fps（5 Mbit/s 数据段），
  聚合 5700/12664 fps，0 丢帧、RXFIFO 溢出 0，主循环 CPU 忙闲比 81.3%/82.0%；
  与"一对总线双向"的每方向帧率差 <0.1%，说明消息 RAM 与 AHB 带宽不是 4 路并发的瓶颈。
  详细表与图见第 09 章表 9-11、图 9-5。

![图 8-4 4 路 MCAN 与两组双节点总线、32 KB 消息 RAM 分配](figures/08/fig-08-4-quad-topology.png)

**图 8-4**：左图是 hpm5e31_1 的 4 路 CAN 接线，CAN0<->CAN1、CAN2<->CAN3 两两接在同一段总线上，
每组两端各 120 Ω，两组可以独立同时满负荷；右图是 4 个 MCAN 在 32 KB AHB SRAM 中的消息 RAM 占用，
每实例 2560 B、合计 10 KB，余量 22 KB。引脚与端子定义见 `boards/hpm5e31_1/ext/board_can.h`。

## 8.10 参考来源

1. RM Rev0.4 §CAN-FD（M_CAN）章节（位时序、消息 RAM、过滤器、FIFO/队列、错误计数与 bus-off 状态机）
2. DS Rev0.4 §1.2.12（4 路 CAN FD 特性）、§8.4 引脚表（PD00/PD01、PD05/PD04、PC08/PA09、PC15/PC14）
3. `hpm_sdk/drivers/inc/hpm_mcan_drv.h`、`hpm_sdk/drivers/src/hpm_mcan_drv.c`
4. `hpm_sdk/soc/HPM5E00/HPM5E31/hpm_mcan_soc.h`（实例/消息 RAM 属性）
5. 芯片手册与 SDK 官方入口：<https://www.hpmicro.com/>、<https://doc.hpmicro.com/>（HPM5E00 手册/勘误）、
   <https://github.com/hpmicro/hpm_sdk>（v1.13.0）、<https://hpm-sdk.readthedocs.io/zh-cn/latest/>。
6. 收发器：[TI TCAN1044A-Q1](https://www.ti.com/product/TCAN1044A-Q1)（板载 TCAN1044AV，5 Mbit/s 上限）、
   [Microchip MCP2558FD](https://www.microchip.com/en-us/product/MCP2558FD)（8 Mbit/s 级，换 PHY 用）。
7. 工具：[can-utils](https://github.com/linux-can/can-utils)、[Linux SocketCAN](https://www.kernel.org/doc/html/latest/networking/can.html)。
5. 官方例程：`hpm_sdk/samples/drivers/mcan/`
6. 本仓库例程：`samples/can/mcan_loopback_internal/`、`mcan_two_node/`、`mcan_diag/`、`mcan_error_recovery/`
7. 实测日志：`logs/20261030-110650-mcan_loopback_internal.log`、`logs/20261030-112832-mcan_two_node.log`
8. 参考实现：[SlcanX](https://github.com/weifengdq/SlcanX)（HPM5321 四路 CAN FD 分析仪，MCAN 用法可借鉴；GPL-3.0，只读参考）。
9. Bosch M_CAN User's Manual（位时序/TDC/SSP 的权威原始来源），Bosch 半导体 CAN IP 页：<https://www.bosch-semiconductors.com/ip-modules/can-ip-modules/m-can/>。

## 8.11 待确认项与风险
**表 8-14 待确认项与风险（已逐条复核，2026-11-02）**

| 项 | 状态 | 说明/验证方法 |
|---|---|---|
| 外部第三方 CAN 节点 | **已知限制** | 本板没有外部 CAN 分析仪，也没有第二块开发板，双节点测试都是 MCU 内部两个 MCAN 互测。验证：接 CANable/SLCAN 后用 `candump -td` 对照 |
| 8 Mbit/s 数据段 | **已知限制（收发器上限 5 Mbit/s）** | 板载 TCAN1044AV 数据手册上限 5 Mbit/s（[TI TCAN1044A-Q1](https://www.ti.com/product/TCAN1044A-Q1)）；8M 属超规格使用，短总线能跑，量产要换 8M 级 PHY（MCP2558FD）并做眼图 |
| 4 路同时满载 | **已实测（2026-11-02）** | 两组总线双向、4 个 MCAN 全忙：每方向 1425/3166 fps、聚合 5700/12664 fps，0 丢帧、RXFIFO 溢出 0，CPU 81.3/82.0%；每方向帧率与一对总线双向完全相同，见第 09 章表 9-11、图 9-5 |
| TDC/SSP 实际效果 | **已实测（2026-11-02）** | 偏移必须小于数据段位长：5 Mbit/s（16 tq）下 TDCO=1/20 都 bus-off（TEC=248），8/10/12/13/14 全部 0 丢帧；PSR.TDCV 恒等于 TDCO+10。短总线上 TDC 开/关的吞吐差异仍不可测（都 0 丢帧），但偏移越界会直接 bus-off。详见第 47 章表 47-17 |
| 位时序矩阵完整性 | **已实测（2026-10-30）** | 24 组矩阵与 TDC 开/关见第 09 章表 9-6/9-7；SJW 维度并入了 `mcan_quad_load matrix`（48 组，本轮未取到窗口） |
| 终端电阻匹配 | 已知限制 | 本板 CAN0-CAN1、CAN2-CAN3 两两相连，各 120 Ω×2；接外部设备时注意端接数量，拨码是手动的、软件不可控 |
| 消息 RAM 摆放 | 已知限制 | 必须在 AHB SRAM（`.ahb_sram`）+ `mcan_set_msg_buf_attr()`，否则异常；4 实例每实例 2560 B |
