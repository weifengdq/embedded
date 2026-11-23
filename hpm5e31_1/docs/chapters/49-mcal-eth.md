# 第 49 章 Eth 与 EthTrcv：千兆车载以太网 MAC 与 PHY 驱动

## 49.1 文档信息

| 项 | 内容 |
|---|---|
| 章节 | 第 49 章 Eth 与 EthTrcv：千兆车载以太网 MAC 与 PHY 驱动 |
| 版本 | v1.3.0（2026-11-02，R6 MCAL） |
| 作者 | eth-core（R6 MCAL 成员） |
| 对应模块 | Eth（AUTOSAR Ethernet Driver，模块 ID **88**）与 EthTrcv（Ethernet Transceiver Driver，模块 ID **73**） |
| 硬件 | HPM5E31 ENET0（Synopsys DesignWare GMAC，RGMII 1 Gbit/s）+ YT8011AN（1000BASE-T1 车载 PHY，MDIO 地址 1） |
| 对应例程 | samples/mcal/eth_raw_echo、eth_irq、eth_trcv、eth_stats（Eth 3 + EthTrcv 1） |
| 实测日期 | 2026-11-02（hpm5e31_1，HPM5E31IPB1，CPU 480 MHz） |
| 数据来源 | logs/ 下 eth_raw_echo / eth_raw_echo-host / eth_irq / eth_trcv / eth_stats 的实测日志（正文逐条给出路径） |
| 依据规范 | AUTOSAR_CP_SWS_EthernetDriver（R25-11，Document ID 430）、AUTOSAR_CP_SWS_EthernetTransceiverDriver（R25-11，Document ID 431） |
| 可信度 | 高：收发通路与 PHY 配置均为实测（RX 18 万帧 0 错误、TX 帧内容逐字节核对、PHY 寄存器读回与手册一致）；RTT 与中断/切换耗时见 50.7 的逐项标注（未通过项已如实列出） |
| 依赖章节 | 第 13 章（lwIP 与以太网性能，cache/write-around 结论）、第 43 章（MCAL 基础设施与配置模型）、第 12 章（YT8011AN RGMII 硬件） |

## 49.2 本章导览

以太网在 AUTOSAR CP 里的位置有点特殊：它既不是 CAN 那样的"报文 + 硬件对象"模型，也不是
DMA 驱动里常见的"一个队列一个中断"。它要同时处理四件互相牵制的事：

1. **帧的所有权**：谁写缓冲、谁交给 DMA、谁在什么时候可以复用——搞错就是"偶发丢帧"；
2. **内存可达性**：DMA 只能访问 AXI/AHB SRAM，访问不到 core-local 的 ILM/DLM；而 MCAL 的
   全局数组默认落在 .bss（DLM），这是一个非常容易踩的坑（50.9 节有完整复盘）；
3. **cache 一致性**：缓冲在 cached 内存时要 clean/invalidate，在 non-cacheable 时不用——
   两种方案的开销差一个数量级，选哪个取决于你把缓冲放哪里；
4. **MAC 与 PHY 的分工**：MAC（Eth）只管帧的收发与统计，速率/主从/自协商/RGMII 延迟全在
   PHY（EthTrcv）；两者只通过 MDIO（Clause 22/45）交换状态。

读完本章你应该能回答：AUTOSAR Eth 的 ProvideTxBuffer/Transmit/Receive/ReleaseRxBuffer 到底
该怎么配合；DMA 描述符环要配多少个、放哪；为什么 Eth_Init 之后不能马上收发；YT8011AN 的
主从与 RGMII 延迟怎么配；以及"能编译、能打印、就是收不到帧"时按什么顺序排查。

表 49-1 是本章的阅读路径建议。

表 49-1 阅读路径

| 你的目的 | 建议读 | 预计 |
|---|---|---|
| 只想把例程跑起来 | 50.3 + 50.6.1 | 10 分钟 |
| 要移植到别的 MAC/PHY | 50.4 + 50.5 + 50.9 | 40 分钟 |
| 要接 EthIf/lwIP | 50.4.4 + 50.4.5 + 50.8 | 30 分钟 |
| 关心性能数字 | 50.7 | 15 分钟 |

## 49.3 概述

### 49.3.1 AUTOSAR 以太网通信栈与 Eth/EthTrcv 的位置

AUTOSAR 的以太网栈自上而下是：应用 → PDU Router → **EthIf**（以太网接口，负责 L-SDU 与
MAC 地址/以太网帧头的映射、收发分发）→ **Eth**（以太网驱动，管 MAC/DMA）与 **EthTrcv**
（收发器驱动，管 PHY）；旁边还有 EthSM（以太网状态管理）、EthTSyn（时间同步）、EthSwt（交换机）。

Eth 与 EthTrcv 之间不是调用关系，而是**并列的两条腿**：EthIf 分别调用它们，Eth 通过
Eth_ReadMii/Eth_WriteMii/Eth_ReadMmd/Eth_WriteMmd 给 EthTrcv 提供 MDIO 访问通道（SWS_Eth
§8.4.7/8.4.8/8.4.29/8.4.30），EthTrcv 自己不去碰 MAC 的寄存器。图 49-1 是本实现的分层与
调用关系。

![图 49-1 AUTOSAR 以太网分层与本实现的分工](figures/49/fig-49-1-eth-layers.png)

表 49-2 本实现的分层与职责边界

| 层 | 本仓库的落地 | 职责 | 不做什么 |
|---|---|---|---|
| EthIf | 只有弱符号桩（EthIf_RxIndication / EthIf_TxConfirmation / EthIf_CtrlModeIndication / EthIf_TrcvLinkStateChg），默认 ETH_USE_ETHIF_CALLBACK = STD_OFF | 帧分发、MAC 表、L-SDU 映射 | 不缓存帧、不做协议解析 |
| Eth（模块 88） | mcal/Eth/ | MAC 配置、DMA 描述符环、缓冲池、MII/MMD、计数器、模式切换 | 不碰 PHY 寄存器（除经 MII 服务）、不解析帧内容 |
| EthTrcv（模块 73） | mcal/EthTrcv/ | PHY 复位后配置、链路/速率/双工/主从查询、链路轮询与通知 | 不碰 MAC 寄存器、不管 DMA |
| 板级 | Eth_PBcfg.c / EthTrcv_PBcfg.c | 实例基址、引脚、时钟、PHY 地址、MAC、中断号、RGMII 延迟档位 | 不含任何算法 |

### 49.3.2 硬件：ENET0 与 YT8011AN

HPM5E31 的 ENET0 是 Synopsys DesignWare GMAC 派生的以太网控制器：MAC 侧支持 10/100/1000
Mbit/s、全/半双工、RGMII/RMII/MII；DMA 侧是"描述符环 + 可编程突发"的经典结构（TDES/RDES），
带独立的 TX/RX 描述符表基址寄存器、poll demand 寄存器与 MMC 统计计数器。

YT8011AN 是 Motorcomm 的**车载单对线**千兆 PHY：1000BASE-T1/100BASE-T1，MAC 接口 RGMII，
MDIO Clause 22 + 厂商 EXT 寄存器 + Clause 45 MMD。板上 strap 决定：PHY 地址 1
（RXD[1:0]=01）、上电为 Slave（RXD3=0）、RGMII（RXD2=0）、RXC 2 ns 延迟默认开。

表 49-3 板级资源（与 docs/MCAL开发约定.md §2 一致）

| 资源 | 引脚/参数 | 说明 |
|---|---|---|
| RGMII 数据/时钟 | PF02~PF13 | TXD[3:0]/TX_CTL/TXC、RXD[3:0]/RX_CTL/RXC |
| MDC / MDIO | PA31 / PA30 | Clause 22/45 访问 PHY |
| PHY 中断 nINT | PB28 | 本实现未使用（轮询链路） |
| PHY 复位 nRST | PB29 | 低有效 ≥10 ms，板级 board_reset_enet_phy() 拉低 30 ms |
| PHY 地址 | 1 | strap RXD[1:0]=01 |
| MAC 地址（默认） | 02:00:00:00:01:00 | 本地管理地址，可在 PBcfg 改 |
| 中断号 | IRQn_ENET0 = 58 | RV32 PLIC，优先级 4 |
| 对端 | 千兆车载以太网转换盒 ↔ 主机 enp6s0（1 Gbit/s 全双工） | 例程对打链路 |

### 49.3.3 API 覆盖

R25-11 的 Eth 有 43 个 API、EthTrcv 有 32 个。MACsec 与 TSN 时间戳（PHC）相关的一大批
在本板上没有对应硬件能力，按 SWS 允许的方式处理：保留声明、返回 E_NOT_OK 或不实现、在
50.12 节列偏差。表 49-4 是实际落地的 API 清单。

表 49-4 Eth / EthTrcv API 覆盖

| 分类 | API | 状态 |
|---|---|---|
| 初始化/模式 | Eth_Init、Eth_SetControllerMode、Eth_GetControllerMode、Eth_MainFunction | 实现 |
| MAC 地址 | Eth_GetPhysAddr、Eth_SetPhysAddr、Eth_UpdatePhysAddrFilter | 实现（过滤用 3 个 MAC 槽） |
| MDIO | Eth_ReadMii、Eth_WriteMii、Eth_ReadMmd、Eth_WriteMmd | 实现（Clause 22 + 45） |
| 发送 | Eth_ProvideTxBuffer、Eth_Transmit、Eth_TxConfirmation | 实现（双提交语义，见 50.4.5） |
| 接收 | Eth_Receive、Eth_ReleaseRxBuffer + 扩展 Eth_GetRxBuffer | 实现（零拷贝） |
| 统计 | Eth_GetCounterValues、Eth_GetRxStats、Eth_GetTxStats、Eth_GetTxErrorCounterValues | 实现（软件计数 + 描述符错误位） |
| 版本 | Eth_GetVersionInfo | 实现 |
| TSN/PHC | Eth_GetCurrentTimeTuple、Eth_SetPhcTime、Eth_SetPhcCorrection、Eth_GetPhcTime、Eth_SetPpsSignalMode、Eth_EnableEgressTimeStamp、Eth_GetEgressTimeStamp、Eth_GetIngressTimeStamp | 未实现（返回 E_NOT_OK，见 50.12） |
| 队列/抢占 | Eth_ImmediateTransmit、Eth_GetSpiStatus | 未实现（DRAFT API，本板单队列） |
| MACsec | Eth_MacSec*（12 个） | 未实现（无 MACsec 硬件） |
| EthTrcv | Init、SetTransceiverMode、GetTransceiverMode、GetLinkState、GetBaudRate、GetDuplexMode、GetPhyIdentifier、StartAutoNegotiation、MainFunction、GetVersionInfo | 实现 |
| EthTrcv | SetPhyLoopbackMode（INTERNAL）、GetPhySignalQuality、GetMacMethod | 实现 |
| EthTrcv | CheckWakeup、GetBusWuReason、TransceiverLinkStateRequest、RunCableDiagnostic、GetCableDiagnosticsResult、SetPhyTestMode、SetPhyTxMode | 部分/桩（YT8011AN 无对应能力，DET 报 NOT_SUPPORTED） |
| EthTrcv | MacSec*（10 个） | 未实现（无 MACsec） |
## 49.4 工作原理

### 49.4.1 ENET0 的数据通路

ENET0 的数据通路可以粗暴地分成三截：**AXI 主口 ↔ DMA（描述符环）↔ MAC ↔ RGMII ↔ PHY**。
CPU 不直接搬数据，只做两件事：把帧写进 DMA 能读到的内存、然后把描述符的 OWN 位交给 DMA。
图 49-2 是 TX/RX 两条环与缓冲池的对应关系。

![图 49-2 ENET0 DMA 描述符环与缓冲池](figures/49/fig-49-2-dma-ring.png)

一次发送的完整时序（本实现，Eth_Transmit 内部）：

1. 上层（EthIf/lwIP）调 Eth_ProvideTxBuffer 拿一个空闲缓冲，返回的 BufPtr 指向"帧起始"；
2. 上层把帧内容写进去；
3. 上层调 Eth_Transmit：驱动填 TDES1.TBS1=帧长、TDES2.Buffer1=缓冲物理地址、
   TDES0 置 FS/LS/IC，**最后**置 OWN=1；
4. 驱动写 DMA_TX_POLL_DEMAND（门铃），DMA 取走描述符 → MAC 组帧（补前导码/SFD/FCS、
   补 0 到 60 B）→ RGMII 发出；
5. DMA 清 OWN、置状态位；Eth_TxConfirmation 轮询到 OWN=0 后回收缓冲、按需回调 EthIf_TxConfirmation。

一次接收：

1. RX 描述符初始化时 OWN=1、Buffer1 指向 RX 缓冲、RBS1=缓冲长度；
2. 帧到齐后 DMA 写缓冲、清 OWN、把帧长填进 RDES0.FL；
3. CPU 轮询（或 RI 中断）发现 OWN=0，读 RDES0 判断 FS/LS/ES/CE/OE 等错误位；
4. 交给上层（零拷贝：直接给缓冲指针），上层用完后调 Eth_ReleaseRxBuffer 把 OWN 重新置 1。

**OWN 位是一切的核心**：OWN=1 表示"归 DMA"，OWN=0 表示"归 CPU"。CPU 在 OWN=1 时读缓冲、
或在 OWN=0 时写描述符，都会得到"偶发"的错误——这类 bug 在低负载下几乎不复现，一上量就炸。

### 49.4.2 描述符格式

ENET0 用 DesignWare 的"链式描述符"（TDES0 的 TCH=1 / RDES1 的 RCH=1）。表 49-5 是本章用到的位域。

表 49-5 发送/接收描述符位域（只列本实现用到的）

| 描述符 | 位域 | 含义 | 本实现 |
|---|---|---|---|
| TDES0 | bit31 OWN | 1=DMA 拥有 | 提交时最后置 1 |
| TDES0 | bit30 IC | 发送完成中断 | TxConfirmation=TRUE 时置 1 |
| TDES0 | bit29 LS / bit28 FS | 首/末片段 | 单描述符帧两者都置 1 |
| TDES0 | bit20 TCH | 链式（TDES3 是 next_desc） | 初始化时置 1 |
| TDES0 | bit15 ES | 错误汇总 | 完成时检查 |
| TDES0 | bit3 UF / bit2 ED / bit1 DB | 下溢/过多推迟/推迟 | 计入错误计数 |
| TDES1 | bit12:0 TBS1 | 缓冲 1 字节数 | = 帧长（含 14 B 头、不含 FCS） |
| TDES2 | bit31:0 Buffer1 | 缓冲 1 物理地址 | core_local_mem_to_sys_address() 转换后写入 |
| TDES3 | bit31:0 next_desc | 下一个描述符物理地址 | 初始化时成环 |
| RDES0 | bit31 OWN | 1=DMA 拥有 | 归还时重新置 1 |
| RDES0 | bit29 LS / bit28 FS | 首/末片段 | 单缓冲帧两者都为 1 |
| RDES0 | bit15 ES / bit1 CE / bit11 OE / bit14 DSE / bit12 LE | 错误位 | 错误帧不进上层，只计数 |
| RDES0 | bit27:14 FL | 帧长（**含 FCS**） | 上层长度 = FL - 4 |
| RDES1 | bit14 RCH / bit12:0 RBS1 | 链式 / 缓冲长度 | 初始化时置 1 / 1536 |
| RDES2 | bit31:0 Buffer1 | 缓冲 1 物理地址 | 每轮归还时重写 |
| RDES3 | bit31:0 next_desc | 下一个描述符物理地址 | 初始化时成环 |

### 49.4.3 缓冲池与 BufIdx 编码

本实现的规模参数在 Eth_Cfg.h（预编译）：TX 8 个、RX 16 个，每个 1536 字节。
1536 的由来：最长以太网帧 1518 B（14 头 + 4 VLAN + 1500 净荷）+ 对齐余量，且是 8 的倍数
（DMA 对缓冲地址有对齐要求）。

Eth_BufIdxType 按 SWS_Eth_00414（DRAFT）的约束编码：返回值必须大于 2^16-1，避免与
Eth_ImmediateTransmit 的 TxHandleId 冲突。本实现用 0x00010000 | 索引，配合
ETH_BUFIDX_MAKE/INDEX/IS_VALID 三个宏。这一点对上层有实际影响：**句柄是 32 位，不能截成
u16**；如果 EthIf 把 BufIdx 存进 u16，会拿到 0x0000 而误判非法。

表 49-6 缓冲池与句柄

| 项 | 值 | 说明 |
|---|---|---|
| TX 缓冲 | 8 × 1536 B | 与 TX 描述符一一对应，s_tx_buf[ctrl][idx] |
| RX 缓冲 | 16 × 1536 B | 与 RX 描述符一一对应，s_rx_buf[ctrl][idx] |
| 描述符 | TX 8 + RX 16，各 32 B | 放 .noncacheable.bss（NOLOAD），32 B 对齐 |
| 句柄基址 | 0x00010000 | SWS_Eth_00414（DRAFT）：> 2^16-1 |
| 单帧上限 | 1518 B（不含 FCS） | ProvideTxBuffer 授予 1500 B 净荷 |
| 最小帧 | 60 B（不含 FCS） | 短帧由驱动补 0 |

### 49.4.4 发送路径：ProvideTxBuffer / Transmit / TxConfirmation

SWS 把发送拆成"申请缓冲 → 提交 → 确认"三步，中间的缓冲所有权转移是容易写错的地方。
SWS_Eth_00077 要求 Eth_ProvideTxBuffer **锁定**缓冲直到 Eth_Transmit；SWS_Eth_00079 要求
请求过大时"不锁定、返回可用长度 + BUFREQ_E_OVFL"；SWS_Eth_00080 要求无空闲时返回 BUFREQ_E_BUSY。

本实现还额外处理了一个 SWS 没写死的细节：**以太网帧头由谁写**。SWS_Eth_00088 说
"驱动 shall build the Ethernet header with the given physical target address"，但很多上层
（尤其 lwIP 的 netif->linkoutput）习惯自己组好整帧。为了两边都能用，本实现给出**双提交语义**：

| PhysAddrPtr | LenByte 含义 | 数据布局 | 调用方 |
|---|---|---|---|
| 非 NULL | 净荷长度 | 驱动写 BufPtr[0..13] 头，调用方把净荷写在 BufPtr+14 | 严格 SWS 用法（EthIf 场景） |
| NULL | 整帧长度 | 调用方把整帧（含 14 B 头）写在 BufPtr 起 | lwIP / 裸帧测试（本仓库例程） |

这个设计让 Eth.h 同一套 API 同时满足 SWS 语义和"零拷贝整帧"需求，代价是文档必须写清楚
（见 Eth.h 文件头的"数据通路约定"）。表 49-7 是发送路径的返回码对照。

表 49-7 发送路径返回码

| API | 返回码 | 触发条件 |
|---|---|---|
| Eth_ProvideTxBuffer | BUFREQ_OK | 有空闲缓冲，返回句柄/指针/授予长度 |
| Eth_ProvideTxBuffer | BUFREQ_E_BUSY | 8 个缓冲全部在用（计入 DiscOtbdPkt） |
| Eth_ProvideTxBuffer | BUFREQ_E_OVFL | 请求净荷 > 1500（不锁定，回填 1500） |
| Eth_ProvideTxBuffer | BUFREQ_E_NOT_OK | 未初始化 / 控制器非 ACTIVE / 指针空 |
| Eth_Transmit | E_OK | 描述符已交给 DMA |
| Eth_Transmit | E_NOT_OK | BufIdx 非法/未锁定、控制器非 ACTIVE、长度越界、DMA 仍持有描述符 |

### 49.4.5 接收路径：Receive 只报状态，GetRxBuffer 给指针

R25-11 的 Eth_Receive(CtrlIdx, QueueIdx, RxStatusPtr) **不再返回数据指针**，只返回
ETH_RECEIVED / ETH_NOT_RECEIVED / ETH_RECEIVED_MORE_DATA_AVAILABLE 三态；数据交给上层靠
"入口队列处理函数"（SWS_Eth_91024，DRAFT）或 EthIf 的入口队列元素。这个设计对 PDU 型上层
够用，但对 lwIP 这种"要一个连续缓冲直接挂 pbuf"的场景不友好。

本实现的做法：

1. 严格实现 SWS 的 Eth_Receive（只报状态，可用于 EthIf 轮询）；
2. 额外提供 **Eth_GetRxBuffer(CtrlIdx, QueueIdx, &handle, &data, &len, &type)**：从 RX 环里取
   一帧，返回 DMA 缓冲的**真实指针**（零拷贝）与长度（已扣掉 FCS）；
3. 上层用完调 **Eth_ReleaseRxBuffer(CtrlIdx, handle)**（SWS_Eth_91023，DRAFT）归还：
   驱动先 invalidate（cached 模式）、再把 OWN 置 1 挂回环。

**语义保证**：从 GetRxBuffer 返回到 ReleaseRxBuffer 之间，该缓冲不会被驱动复用；同一个
handle 重复 Release 会被 DET 报 ETH_E_RX_HANDLE_ID_NOT_ASSOCIATED。

### 49.4.6 内存放置与 cache 一致性

这一节是本章最"贵"的经验。表 49-8 是三种放法的对比。

表 49-8 缓冲/描述符的内存放置方案

| 方案 | 描述符 | 数据缓冲 | cache 维护 | 结果 |
|---|---|---|---|---|
| A（最初写法） | .noncacheable | 默认 .bss（落 DLM） | 无 | **不通**：ENET DMA 访问不到 core-local DLM，OWN 永不翻转，tx/rx 计数恒 0 |
| B（当前，R8 起描述符也一起切） | .noncacheable.bss | .noncacheable.bss（AXI SRAM，NOLOAD） | 不需要 | 通，且每帧省掉 clean/invalidate；本段本就 NOLOAD，Flash 占用 0——R7 曾按 .obj 口径误称"省 37 KB"，R8 已更正 |
| C（SDK lwIP 基线） | .noncacheable | 普通 cached（64 B 对齐） | TX clean / RX invalidate | 通，但每帧多花约 109 cyc/cacheline（第 13 章实测） |

本实现选 B。Eth_Cfg.h 里保留 ETH_TX_CACHE_MAINTENANCE / ETH_RX_CACHE_MAINTENANCE 两个开关：
如果读者要改回方案 C，把缓冲挪回普通内存并打开开关即可（代码路径已实现：
l1c_dc_writeback / l1c_dc_invalidate，按 64 B cache line 对齐后操作）。

R8 补的 A/B 实测（eth_raw_echo，2026-11-02）：改前数组用 `ATTR_PLACE_AT_NONCACHEABLE_WITH_ALIGNMENT`
（legacy `.noncacheable`）、改后用 `ATTR_PLACE_AT_NONCACHEABLE_BSS_WITH_ALIGNMENT`（`.noncacheable.bss`），
两者的 `demo.bin` 都是 97 264 B、`__fw_size__` 都是 0x14E50、`readelf -l` 里 0x0123_0000 段 FileSiz 都是 0
（MemSiz 37 632），**Flash 占用都是 0**；唯一差别是 `__noncacheable_bss_start__` 从 0x0123_9300 前移到 0x0123_0000，
整段进入 startup 清零范围（`soc/HPM5E00/HPM5E31/toolchains/reset.c:119`），描述符另由 `Eth_Init` memset 兜底。
证据/复跑：`logs/20261102-181031-eth_noncacheable_bss.log`。

### 49.4.7 MAC 模式与链路自适应

SWS_Eth_00276 规定：Eth_SetControllerMode(ETH_MODE_DOWN) 要关收发、丢弃所有待发/待收请求；
ETH_MODE_ACTIVE 要打开缓冲与收发。本实现在 ACTIVE 路径里还会做两件事：

1. 按当前链路参数写 MAC 的 MACCFG.FES（100/1000M）与 MACCFG.DM（全/半双工）；
2. 打开 DMA_OP_MODE 的 ST（发送）/SR（接收）位，并各踢一次 poll demand。

链路参数从哪来？本实现做成**回调式**：Eth 自己不知道 PHY 的存在，EthTrcv 在
EthTrcv_MainFunction 里轮询 PHYSR，发现链路起来/速率变化时调 Eth_SetLineSpeed（本实现扩展
API，0xE2）把结果同步给 MAC。这样 Eth 与 EthTrcv 保持 AUTOSAR 的"并列"关系，不互相 include。

表 49-9 链路联动

| 事件 | 检测者 | 动作 |
|---|---|---|
| 上电/复位 | Eth_Init | 控制器停在 DOWN，等 SetControllerMode(ACTIVE) |
| PHY 链路 up | EthTrcv_MainFunction（PHYSR[10]） | 读 PHYSR[15:14] 得速率、[13] 得主从 → Eth_SetLineSpeed |
| PHY 链路 down | 同上 | 只通知（可选弱符号 EthIf_TrcvLinkStateChg），MAC 保持原速率 |
| 速率切换 | 样例经 MMD1 0x834 + BMCR 软复位 | 重新建链，EthTrcv_MainFunction 检测到变化后再同步 MAC |

### 49.4.8 MDIO：Clause 22 / Clause 45 / 厂商 EXT

PHY 的寄存器有三种访问方式，YT8011AN 三个都用：

| 方式 | 帧格式 | 寻址 | 本实现入口 |
|---|---|---|---|
| Clause 22 | ST(01) OP(10 读/01 写) PHYAD(5) REGAD(5) TA(2) DATA(16) | PHY 地址 + 5 位寄存器号 | Eth_ReadMii / Eth_WriteMii |
| Clause 45（MMD） | 经 Clause 22 的 0x0D（控制）/0x0E（数据）间接访问 | devad(5) + reg(16) | Eth_ReadMmd / Eth_WriteMmd |
| 厂商 EXT | 写 0x1E = EXT 地址，再读写 0x1F | 16 位 EXT 地址 | 例程直接组合 ReadMii/WriteMii |

MDIO 时序由 ENET 的 MDIO 时钟分频决定，一次读写在百微秒量级；YT8011AN 的软复位要求
≥10 ms 才能稳定，所以 EthTrcv_Init 里每次改速率/主从后都要等 BMCR[15] 自清 + 12 ms 稳定时间。

### 49.4.9 中断与轮询

本实现两种模式都支持，由 PBcfg 的 RxInterruptEnable 选择（例程里用运行期配置副本切换）：

| 模式 | 触发 | ISR 里做什么 | 数据由谁取 |
|---|---|---|---|
| 轮询（默认） | 主循环调 Eth_MainFunction | — | 主循环调 Eth_GetRxBuffer |
| RX 中断 | DMA_STATUS.RI → PLIC IRQ 58 | 计数、记 mcycle、清 W1C 状态位、置 pending | 主循环仍调 Eth_GetRxBuffer |

ISR 里**不做**数据搬运、不回调上层——这是刻意的：以太网帧长可达 1518 B，在 ISR 里做
memcpy/协议处理会把中断延迟拉长到几十微秒，破坏其他 MCAL 模块的时序（第 44/46 章都踩过）。
中断模式下 ISR 只负责"告诉主循环有新帧"。

## 49.5 寄存器与 API 对照

### 49.5.1 ENET0 关键寄存器

表 49-10 ENET0 关键寄存器（本实现实际读写的）

| 寄存器 | 偏移 | 本实现用途 | 实测值（例程 eth regs） |
|---|---|---|---|
| MACCFG | 0x000 | 速率 FES/双工 DM/收发使能 TE,RE | 0x3004080C（1000M 全双工，TE=RE=1） |
| MACFF | 0x004 | 帧过滤（RA=接收所有） | 0x80000000 |
| MAC_ADDR_0_HIGH/LOW | 0x040/0x044 | 本机 MAC | 0x00000001 / 0x00020000（02:00:00:00:01:00） |
| MAC_ADDR[0..3].HIGH/LOW | 0x048.. | 过滤表槽（AE 位） | Eth_UpdatePhysAddrFilter |
| GMII_ADDR / GMII_DATA | 0x010/0x014 | Clause 22 MDIO | Eth_ReadMii/WriteMii（经 SDK 封装） |
| DMA_BUS_MODE | 0x1000 | 突发长度 PBL、地址对齐 AAL、描述符长度 | 0x03022080（PBL=32, PBLX8=1） |
| DMA_TX_POLL_DEMAND | 0x1004 | 发送门铃 | Eth_Transmit 后写 1 |
| DMA_RX_POLL_DEMAND | 0x1008 | 接收门铃 | 归还缓冲/初始化后写 1 |
| DMA_RX_DESC_LIST_ADDR | 0x100C | RX 环基址 | enet_controller_init 写入 |
| DMA_TX_DESC_LIST_ADDR | 0x1010 | TX 环基址 | enet_controller_init 写入 |
| DMA_STATUS | 0x1014 | W1C 中断状态（RI/TI/RU/NIS…） | 0x00660004（含 TU 发送下溢位） |
| DMA_OP_MODE | 0x1018 | ST/SR 启停、TSF、RSF | 0x02202082（ST=1,SR=1） |
| DMA_INTR_EN | 0x101C | 中断使能（RIE/TIE/NIE） | 轮询模式 0x00000000 |
| MAC INTR_STATUS | 0x038 | MAC 侧挂起位（**只读、读即清**） | ISR 里读一次清掉 |

表 49-10 里几个必须按顺序的寄存器操作，写成代码就是：

```c
/* 提交一帧：OWN 位最后写，然后踢门铃 */
d->tdes0 = 0U;
d->tdes1_bm.tbs1 = total;                 /* 帧长（含 14 B 头，不含 FCS） */
d->tdes2_bm.buffer1 = eth_sys_addr(buf);  /* DMA 视角的物理地址 */
d->tdes0_bm.tch = 1U; d->tdes0_bm.fs = 1U; d->tdes0_bm.ls = 1U;
if (conf) { d->tdes0_bm.ic = 1U; }
d->tdes0_bm.own = 1U;                     /* 交给 DMA：必须最后写 */
st->Base->DMA_TX_POLL_DEMAND = 1U;        /* 门铃 */
```

### 49.5.2 SWS API ↔ 实现映射

表 49-11 是本模块的 API 追溯（与 docs/mcal/MCAL_API_TRACEABILITY.md 对应）。

| SWS API | Service ID | 实现位置 | 关键实现点 |
|---|---|---|---|
| Eth_Init | 0x01 | Eth.c | 板级钩子 → DMA/MAC 初始化 → 停 DOWN → 配中断 |
| Eth_SetControllerMode | 0x03 | Eth.c | DOWN 释放全部缓冲；ACTIVE 写 MACCFG + ST/SR + 门铃 |
| Eth_GetControllerMode | 0x04 | Eth.c | 返回缓存状态 |
| Eth_GetPhysAddr / SetPhysAddr | 0x08 / 0x13 | Eth.c | 读写 MAC_ADDR_0 寄存器 |
| Eth_UpdatePhysAddrFilter | 0x12 | Eth.c | 3 个 MAC 槽（AE 位）增删 |
| Eth_WriteMii / ReadMii | 0xA0 / 0xA1 | Eth.c | 经 SDK enet_write_phy/read_phy，TrcvIdx 只有 0 合法 |
| Eth_WriteMmd / ReadMmd | 0xA2 / 0xA3 | Eth.c | 0x0D/0x0E 间接序列 |
| Eth_ProvideTxBuffer | 0x09 | Eth.c | 8 缓冲状态机 + 授予 1500 B |
| Eth_Transmit | 0x0A | Eth.c | 双语义建帧/提交 + 门铃 |
| Eth_Receive | 0x0B | Eth.c | 三态状态扫描（不返回数据） |
| Eth_ReleaseRxBuffer | 0x27 | Eth.c | invalidate + OWN=1 + RU 时补门铃 |
| Eth_TxConfirmation | 0x0C | Eth.c | 轮询 OWN=0、检查 ES 位、回收缓冲 |
| Eth_MainFunction | 0x20 | Eth.c | TX 确认 + 丢帧检测（入口缓冲耗尽计数） |
| Eth_GetCounterValues / RxStats / TxStats / TxErrorCounterValues | 0x14/0x15/0x1C/0x1D | Eth.c | 软件计数 + RDES0/TDES0 错误位 |
| Eth_GetVersionInfo | 0x0D | Eth.c | vendor 0xFFFF、module 88、sw 1.0.0 |
| Eth_GetRxBuffer（扩展） | 0xE1 | Eth.c | 零拷贝取帧，配 ReleaseRxBuffer |
| Eth_SetLineSpeed（扩展） | 0xE2 | Eth.c | EthTrcv 链路联动，写 FES/DM |
| Eth_GetRxIsrCount / GetLastIsrCycle（扩展） | 0xE3/0xE4 | Eth.c | eth_irq 例程测量 |
| Eth_DebugDump（扩展） | — | Eth.c | 打印寄存器与前 4 个描述符（排障） |
| EthTrcv_Init | 0x01 | EthTrcv.c | PHY ID 自检 → RGMII 延迟 → 速率/主从 |
| EthTrcv_GetLinkState / GetBaudRate / GetDuplexMode | 0x06/0x07/0x08 | EthTrcv.c | 读 PHYSR 0x11 解析 |
| EthTrcv_SetTransceiverMode / GetTransceiverMode | 0xA8/0x04 | EthTrcv.c | BMCR 掉电位 |
| EthTrcv_StartAutoNegotiation | 0xA9 | EthTrcv.c | MMD7 0x200 + BMCR 重启 AN |
| EthTrcv_GetPhyIdentifier | 0x15 | EthTrcv.c | ID1/ID2 → OUI/Model/Rev |
| EthTrcv_GetPhySignalQuality | 0x10 | EthTrcv.c | EXT 0x3074 |
| EthTrcv_MainFunction | 0x0C | EthTrcv.c | 链路轮询 + 去抖 + Eth_SetLineSpeed |
## 49.6 例程讲解

四个例程共用一套硬件（ENET0 + YT8011AN），差别只在"验证哪一层"。表 49-12 是例程清单。

表 49-12 Eth / EthTrcv 例程清单

| 例程 | 验证对象 | 主机侧 | 关键命令 |
|---|---|---|---|
| eth_raw_echo | Eth 收发主路径（ProvideTxBuffer/Transmit/GetRxBuffer/ReleaseRxBuffer）、RTT、吞吐、丢包 | host/eth_host.py（AF_PACKET，EtherType 0x88B5） | eth rtt / eth tx / eth rx / eth echo |
| eth_irq | RX 中断模式：ISR 次数、ISR→消费者延迟、丢包 | 同上（flood 模式） | irq on / irq rx / irq off |
| eth_trcv | EthTrcv：链路、速率 1000M/100M、主从切换、建链耗时 | 不需要（PHY 自证） | trcv info / trcv regs / trcv s100 / trcv master |
| eth_stats | 计数器口径与错误注入后的变化 | 同上（可配合 flood） | eth stat / eth regs |

### 49.6.1 eth_raw_echo：裸帧对打

这个例程刻意不走 IP：主机用 AF_PACKET 原始套接字按 EtherType 0x88B5 收发自定义帧，
板端用 Eth 驱动收发同样的帧。这样把"以太网驱动"从 TCP/IP 里剥出来单独验证，
任何丢包/延迟都只能来自 MAC、DMA、PHY 或线缆——没有协议栈背锅。

帧格式（主机 host/eth_host.py 与板端一致）：

| 偏移 | 长度 | 内容 |
|---|---|---|
| 0 | 6 | 目的 MAC |
| 6 | 6 | 源 MAC |
| 12 | 2 | EtherType = 0x88B5 |
| 14 | 4 | magic "ETH0" |
| 18 | 4 | 序号（小端） |
| 22 | 余下 | 递增图样（用于校验数据完整性） |

四条命令：

1. **eth rtt <len> <n>**：向广播发 n 帧（每帧带唯一序号），主机 echo 模式原样回发；
   板端按序号配对算 RTT。输出格式：
   RTT len=64 n=300 replied=300 lost=0 avg=...us min=...us max=...us span=...us
2. **eth tx <len> <n>**：以最大速度发 n 帧（不等确认），统计帧率与吞吐：
   TX len=64 n=1000 ok=1000 fps=... mbit=... dur=...us
3. **eth rx <secs>**：主机 flood，板端统计接收帧率/吞吐/平均帧长：
   RX secs=10 frames=... bytes=... mbit=... avg_len=...
4. **eth echo <secs>**：板端做"回显器"，主机 rtt 模式主动测（反向验证）。

主机脚本用法（需要 CAP_NET_RAW，用 sudo）：

    sudo python3 samples/mcal/eth_raw_echo/host/eth_host.py --iface enp6s0 echo --secs 25
    sudo python3 samples/mcal/eth_raw_echo/host/eth_host.py --iface enp6s0 rtt --len 64 --n 200
    sudo python3 samples/mcal/eth_raw_echo/host/eth_host.py --iface enp6s0 flood --len 1000 --secs 10

因为"板端命令"和"主机脚本"必须在同一个时间窗里跑，仓库还提供了一个把两者放进同一次
持锁窗口的脚本：samples/mcal/eth_raw_echo/host/run_host_test.sh。它做的事是：
抢板锁 → 烧写 → 延迟 2 s 起主机脚本 → 串口发命令 → 等主机脚本退出 → 把"板端 + 主机"
两段输出合并进同一个 logs/ 文件。

### 49.6.2 eth_irq：接收中断模式

PBcfg 默认是轮询（RxInterruptEnable=FALSE），因为轮询对 1 ms 任务周期最友好。要验证中断
模式时，这个例程用一份"运行期配置副本"重新 Eth_Init：

- irq on / irq off：切换中断/轮询（对应改 PBcfg 后重编译，但不用维护两套配置）；
- irq rx <secs>：接收窗口，统计帧数、ISR 次数、ISR/帧比、**ISR 进入 → 主循环消费**的延迟
  （用 Eth_GetLastIsrCycle 记的 mcycle 算）；
- irq burst <len> <n>：板端主动发帧，配合主机 listen 模式。

延迟口径要说清楚：Eth_GetLastIsrCycle 是"最近一次 RX 中断进入的 mcycle"，当多个帧被一次
中断批量收上来时，后几帧的"延迟"会偏小（因为中断只在第一帧时打点）。所以这个数值适合看
**量级与最大值**，不适合当逐帧延迟分布。

### 49.6.3 eth_trcv：PHY 链路与切换

这个例程只碰 EthTrcv + Eth 的 MDIO 服务，验证：

- trcv info：链路/速率/双工/主从/OUI/Model/Rev/SQI/PHYSR 一次打印；
- trcv regs：Clause 22 的 0x00~0x0A、0x11，加 EXT 0x9001（RGMII 延迟）与 MMD1 0x834（主从/速率）；
- trcv s1000 / trcv s100：切到 1000BASE-T1 / 100BASE-T1，打印软复位耗时与建链耗时；
- trcv master / slave：切主从（1000BASE-T1 强制模式必须一端 Master 一端 Slave，两端都 Slave 建不起链）；
- trcv mode down/active：收发器掉电/上电；
- trcv poll <secs>：跑 EthTrcv_MainFunction，记录链路翻转事件与时间戳。

切换的实现值得说明：SWS 没有 EthTrcv_SetBaudRate（速率是协商/配置结果，不是 API），
所以例程用 Eth_WriteMmd(MMD1, 0x834) 写 force_config/force_phy_type，再用
Eth_WriteMii(BMCR, 0x8140/0xA100) 做"带速率编码的软复位"。这正好演示了
Eth（提供 MDIO 服务）与 EthTrcv（提供语义）的分工：**验证/调试类操作可以走 Eth 的 MII 服务，
产品代码应该走 EthTrcv 的封装**。

### 49.6.4 eth_stats：计数器与错误注入

这个例程配合 eth_raw_echo 的 stat/regs 命令使用，重点看三组数：

1. eth stat 的 tx/rx 计数、错误位、长度分桶（RMON 口径）；
2. eth regs 的 MAC/DMA 寄存器与描述符 OWN 位（判断"卡在哪一层"）；
3. 错误注入：主机 flood 时人为发短帧（<60 B，网卡会补 0）/错 FCS 帧（需要改主机脚本）/
   超长帧，看 rd 侧哪一桶涨。本板没有外部错帧注入器，所以这部分只做到"短帧与超长帧"，
   错 FCS 帧的注入路径在 50.12 节列为待确认。

## 49.7 上板实测与性能

> 本节数字全部来自 logs/ 下的真实日志，每张表标注日志路径。复跑命令：
> bash scripts/hwrun.sh mcal/<例程> --secs N --send "..."，
> 需要主机配合的用 samples/mcal/eth_raw_echo/host/run_host_test.sh。

### 49.7.1 初始化与链路（Eth + EthTrcv）

表 49-13 初始化与链路实测（logs/20261102-160353-eth_raw_echo.log）

| 项 | 结果 | 说明 |
|---|---|---|
| ENET0 寄存器基址 | 0xf1400000 | Eth_PBcfg.c 给 HPM_ENET0，驱动打印 |
| Eth_Init 后控制器模式 | DOWN | SWS_Eth_00276：Init 不等于 Start |
| EthTrcv_Init 后链路 | UP / 1000M / FULL / SLAVE | EthTrcv_GetLinkState/GetBaudRate/GetDuplexMode |
| PHY 标识 | OUI=0x13D47A model=48 rev=1 | YT8011AN：ID1=0x4F51、ID2[15:10]=0x3A |
| 默认 MAC | 02:00:00:00:01:00 | Eth_PBcfg.c，可 Eth_SetPhysAddr 改 |
| 缓冲规模 | TX 8×1536 B、RX 16×1536 B、描述符 768 B | 全部 .noncacheable.bss（AXI SRAM，NOLOAD），占 37 632 B |
| Det 计数 | 0 | 正常初始化路径无 Det |

### 49.7.2 发送吞吐（板端 TX burst）

表 49-14 板端 TX burst 实测（logs/20261102-163212-eth_raw_echo-host.log）

| 帧长（净荷） | 发送帧数 | 提交帧率 | 提交吞吐 | 主机实收 | 说明 |
|---|---|---|---|---|---|
| 64 B | 2000 | 219 370 fps | 143.9 Mbit/s | 1949 帧 | 提交速率已接近 1 Gbit/s 线速上限（64 B 帧线速约 148 kfps） |
| 512 B | 2000 | 59 257 fps | 251.3 Mbit/s | 1789 帧 | 提交速率受主循环/描述符回收限制，未到线速 |

口径说明：**fps/mbit 是"驱动提交速率"**（Eth_Transmit 返回 E_OK 的次数 / 耗时），不是线速；
主机实收帧数由主机侧 AF_PACKET 统计，差额来自 MAC 发送 FIFO 溢出与主机 Python 处理不过来
（Python 单线程 AF_PACKET 约 150 fps 处理回声帧，这是本轮测量最大的外部瓶颈）。

### 49.7.3 接收吞吐（主机 flood → 板端）

表 49-15 板端接收实测（logs/20261102-162219-eth_raw_echo-host.log）

| 项 | 数值 | 说明 |
|---|---|---|
| 主机灌包 | 512 B × 12 s，90 926 fps，385.5 Mbit/s | host/eth_host.py flood 模式（全速无节流） |
| 板端实收 | 180 899 帧 / 95 880 962 B（8 s 窗口） | Eth_GetRxBuffer 循环取帧 |
| CRC / 溢出 / 错误 | 0 / 0 / 0 | Eth_CounterValues：DropPktCrc、DropPktBufOverrun、ErrInbdPkt |
| 长度分桶 | 512–1023 B：180 905 帧（100%） | 与灌包帧长一致 |
| 丢掉（held 满） | 0 | Eth_MainFunction 的丢帧检测未触发 |

这一组数据是**接收通路（DMA 环 + RGMII RX + PHY + 缓冲放置）正确性的直接证据**：8 秒 18 万帧
零错误，说明描述符 OWN 位、cache/内存放置、Eth_ReleaseRxBuffer 的归还逻辑都工作正常。

![图 49-3 裸帧收发吞吐（实测）](figures/49/fig-49-3-throughput.png)

### 49.7.4 往返延迟（RTT）

表 49-16 RTT 实测（逐帧 ping-pong，主机 echo 模式；`logs/20261102-165207-eth_raw_echo-host.log`）

| 帧长 | 发送 | 收到回显 | avg | min | max | 说明 |
|---|---|---|---|---|---|---|
| 64 B | 3 | 2 | 2 065 µs | 2 009 µs | 2 122 µs | 单帧丢失 1（主机 Python 回声线程被调度延迟） |

口径必须写清：这 2.07 ms **不是 MAC/驱动的往返**，而是"板端 MAC → 主机网卡 → 主机用户态 Python echo → 回发 → 板端 MAC"
的端到端，其中主机 Python 的一次 recv/send 往返就占 1.5~2 ms（同一台机器上 tcpdump 抓包也能看到这段延迟）。
驱动的单向延迟在几十微秒量级（描述符提交 + 轮询约 10 µs、主机侧抓包时间戳差 < 100 µs）。要做纯驱动延迟，
可用板端 echo + 主机 `rtt` 模式，或直接看 §49.7.2 的提交帧率（219 370 fps = 4.6 µs/帧）。

修复后的帧头逐字节自证（同日志的 `RXDBG` 行）：目的=板 MAC `02:00:00:00:01:00`、源=主机 `C0:A2:B6:A6:A9:2C`、
EtherType `0x88B5`、magic `ETH0`、seq=0、pattern 从 `08 09 0A 0B` 开始——字段位置与发送端完全一致。

![图 49-4 裸帧往返延迟（eth rtt 实测）](figures/49/fig-49-4-rtt.png)

### 49.7.5 中断模式与轮询模式的量化对照（R8 实测）

R7 只写了"两种模式共用同一套 DMA 环，差异只在谁发现 OWN=0"，因板卡排队没取到数据。R8 补上了矩阵：
同一固件、同一主机发包脚本、同一窗口长度，中断与轮询各跑一遍，统计代码完全一致；另附 5 次重复性验证。

> 全部数字来自板端串口日志 `logs/20261102-184310-eth_irq-host.log`（合并了主机侧 `HOST flood` 行），
> 由 `samples/mcal/eth_irq/host/run_irq_matrix.sh` 一次持锁生成，可用本节的复跑命令原样重放。
> 固件基线：R8 修复后的 `mcal/Eth`（`Eth.c` blob `89bc66f`，含 §49.9.12 的 TX 按序发放修复；RX 路径本 R8 未改逻辑）
> + 本节的新版 `samples/mcal/eth_irq/src/main.c`；窗口 3~4 s、测试帧净荷 256 B
> （线长 270 B，EtherType 0x88B5），主机 `samples/mcal/eth_raw_echo/host/eth_host.py flood` 不限速灌包。

本实现的 RX 中断模式与轮询模式共用同一套 DMA 环，差异只在"谁发现 OWN=0"。
两种模式的取帧策略在例程里是分开实现的、并且用**同一份统计代码**：

| 项 | 轮询（默认） | RX 中断 |
|---|---|---|
| 发现新帧 | 主循环每轮调一次 `Eth_GetRxBuffer` | `DMA_STATUS.RI` → PLIC IRQ 58 → ISR 计数/记 mcycle |
| 主循环取帧触发 | 每轮无条件取 | 只在 `Eth_GetRxIsrCount` 增量变化后**排空**整个环；另有 1 ms 兜底轮询（证明 ISR 没漏帧） |
| ISR 里做的事 | — | 计数、记 mcycle（`Eth_GetLastIsrCycle`）、写回 `DMA_STATUS`（W1C）、读 `MAC INTR_STATUS`（只读读清）、RU 时踢一次 DMA |
| 数据搬运 | 主循环 | 主循环（ISR 不搬数据） |
| 观测接口 | 帧数/轮询间隔 | `Eth_GetRxIsrCount`、`Eth_GetLastIsrCycle` |

公平性口径：两种模式**同一固件、同一主机发包脚本、同一窗口长度**；轮询模式每轮都取帧（这是轮询的定义），
中断模式只由 ISR 取帧、外加 1 ms 兜底。因此两者的"每秒取帧调用次数"差 100 倍以上，这个差就是中断模式省下来的 CPU。

表 49-31 中断与轮询三档对照

| 场景 | 模式 | 板端实收帧 | 收帧率 | 主机发送 | ISR 次数 | ISR/帧 | RX 取帧路径 CPU 占比 | 丢弃/错误 |
|---|---|---|---|---|---|---|---|---|
| 空闲（无流量，3 s） | 轮询 | 0 | 0 | 0 | 0 | — | **8.06 %**（23.5 万次空轮询/s） | 0 / 0 |
| 空闲（无流量，3 s） | 中断 | 0 | 0 | 0 | 4 | — | **0.03 %** | 0 / 0 |
| 低速（4 s） | 轮询 | 4 000 | 1 000 帧/s | 1 000 帧/s | 0 | — | 8.28 % | 0 / 0 |
| 低速（4 s） | 中断 | 4 000 | 1 000 帧/s | 1 000 帧/s | 4 006 | 1.001 | **0.32 %** | 0 / 0 |
| 中速（4 s，仅中断） | 中断 | 79 882 | 19 973 帧/s | 19 973 帧/s | 79 888 | 1.000 | 5.50 % | 0 / 0 |
| 全速灌包（4 s） | 轮询 | 619 638 | 154 938 帧/s | 170 262 帧/s | 0 | — | 40.21 % | 0 / 0 |
| 全速灌包（4 s） | 中断 | 627 056 | 156 772 帧/s | 170 262 帧/s | 627 078 | 1.000 | 46.84 %※ | 0 / 0 |

※ 中断档的 CPU 占比含"每帧延迟打点"开销（每帧两次 `Eth_GetLastIsrCycle` + 分桶比较）；全速窗口这一项约
6 个百分点，是**上界**。空闲/低速档帧数少，打点开销可忽略，8.06 %→0.03 %、8.28 %→0.32 % 的差距是真实差距。
全速档两种模式收帧率只差 1.2 %（在重复性波动内，见表 49-34），驱动计数（`DiscInbdPkt`/`ErrInbdPkt`/CRC/overflow）
全 0；板端实收只有主机发送的 91.0 %（轮询）/ 92.1 %（中断），差额是 MAC/DMA 侧的丢弃，本驱动没有对应计数（已知限制）。

表 49-33 中断模式的延迟（同一日志；延迟口径见"坑 3"）

| 场景 | ISR→消费 avg | min | max | 有效样本 | 异常样本 | ISR→主循环"发现" avg / max | 分布（<5 µs / 5–20 µs / 20–100 µs / >100 µs） | 兜底取帧占比 |
|---|---|---|---|---|---|---|---|---|
| 低速 1 000 帧/s | 8 µs | 5 µs | 14 µs | 3 920 | 0 | 3 / 13 µs | 0 % / 100 % / 0 % / 0 % | 80/4 000 = 2.0 % |
| 中速 20 000 帧/s | 9 µs | 0 µs | 16 µs | 79 546 | 3 | 4 / 26 µs | 6.2 % / 93.8 % / 0 % / 0 % | 333/79 882 = 0.4 % |
| 全速灌包 | 3 µs | 0 µs | 81 µs | 622 194 | 4 861（0.8 %） | 17 / 96 µs | 78.9 % / 20.9 % / 0.13 % / 0 % | 1/627 056 ≈ 0 % |

"兜底取帧占比"是 1 ms 兜底轮询先于 ISR 路径取走帧的比例：恒 <2 %，且 1 ms 内最多 1 帧，
说明**RX 中断没有漏帧**；它不是丢中断，而是"帧刚进环、主循环还没看到 ISR 计数"的竞争窗口。
"异常样本"是 `Eth_GetLastIsrCycle` 批量语义的必然产物（见坑 3），已从 avg/min/max 中剔除。

表 49-34 全速窗口重复性（同一命令重复 5 次，每次 4 s 窗口 + 主机不限速灌包）

| 次数 | 日志 | 轮询收帧率 | 中断收帧率 |
|---|---|---|---|
| 1 | `logs/20261102-183509-eth_irq-host.log` | 154 843 帧/s | 104 767 帧/s（离群） |
| 2 | `logs/20261102-183711-eth_irq-host.log` | 154 838 帧/s | 157 031 帧/s |
| 3 | `logs/20261102-183827-eth_irq-host.log` | 154 531 帧/s | 154 748 帧/s |
| 4 | `logs/20261102-183934-eth_irq-host.log` | 154 401 帧/s | 157 569 帧/s |
| 5 | `logs/20261102-184310-eth_irq-host.log` | 154 938 帧/s | 156 772 帧/s |
| 中位 | — | 154 838 帧/s | 156 772 帧/s |

轮询极稳（±0.2 %）；中断 5 次里 4 次落在 154.7k~157.6k 帧/s（略高于轮询），1 次掉到 104.8k 帧/s。
离群那次的窗口正好是"一次 ISR 之后主循环连续排空 4 s"（`loops=1`），复跑未能稳定复现，列为已知限制；
**结论只取中位数**，不当成"中断更快/更慢"的证据。

![图 49-10 中断与轮询：板端实收帧率与取帧路径 CPU 占比](figures/49/fig-49-10-irq-vs-poll.png)

![图 49-11 中断模式延迟：ISR→消费、唤醒延迟与分布](figures/49/fig-49-11-irq-latency.png)

**本节结论**

1. **空闲/低速率场景中断模式显著省 CPU**：空闲时主循环为"找帧"花掉 8.06 % 的 CPU，中断模式是 0.03 %；
   1 000 帧/s 时 8.28 % → 0.32 %。这正是 AUTOSAR Eth 提供 `RxInterruptEnable` 的意义。
2. **全速灌包下中断模式没有吞吐优势**：中位 156.8k 帧/s vs 轮询 154.8k 帧/s（+1.2 %，在重复性波动内），
   两者都在 170 k 帧/s 主机灌包下丢掉约 8~9 %（MAC 侧丢弃，驱动计数看不到）。
3. **每帧一次中断，没有合并**：三档 `ISR/帧` 都是 1.000~1.001。高速率下中断频率 = 帧率（全速窗口 157 k 次/s），
   ISR 开销把轮询省下的 CPU 又吃回去了——所以高包速率不建议开 RX 中断。
4. **中断延迟是微秒级且随速率下降**：1 000 帧/s 时 ISR→消费 avg 8 µs（max 14 µs），全速时 avg 3 µs；
   ISR→主循环"发现"延迟 3~17 µs（max 96 µs）。低速档的 8 µs 主要来自主循环节奏与 1 ms 兜底，不是 ISR 本身。
5. **中断可靠性可自证**：1 ms 兜底轮询取到的帧占比 <2 %，且不存在"有帧无中断"的堆积，说明 RI 没有漏报。

**三个坑（都已实测复现，写进例程注释）**

**坑 1：从"中断模式"直接 `Eth_Init` 切回轮询会整机卡死（已定位 + 已修复）。**
`Eth_Init` 第一步就把驱动状态 `memset` 掉（`Base=NULL`），而此时 PLIC 里 ENET0 中断仍是开的；
紧接着 `board_reset_enet_phy()` 要拉低 PHY 复位 30 ms，这段时间只要来一帧（链路上的广播/协议帧即可），
ISR 就会看到 `Base==NULL` 直接返回、不写 `DMA_STATUS` W1C → 中断线保持有效 → ISR 风暴，主循环饿死、串口再无输出。
修复在例程侧（不动驱动）：切换前先 `Eth_SetControllerMode(DOWN)` 停 DMA、`Eth_BoardIrqEnable(FALSE)` 关 PLIC，
再走 `Eth_Init`（它内部会清挂起位）。修复后 `irq off` / `irq on` 双向切换均正常：
`logs/20261102-182447-eth_irq.log` 里可见 `REINIT begin old_irq=1 target=0` → `IRQ mode=polling` → `irq on` → `IRQ mode=interrupt`。
驱动层面更彻底的做法是在 `Eth_Init` 入口对"旧状态"做同样的收尾（本稿未改 `Eth.c`，避免与同轮其他改动冲突，列为建议）。

**坑 2：R7 版例程有两处"能编译、上板却收不到帧"的 bug（已定位 + 已修复）。**
① 接收窗口单位写成 `secs * cycles_per_us * 1000`，实际只有毫秒级——`irq rx 6` 只跑 6 ms；
② 测试帧 magic 判在 `data[0]`/`data[1]`，而 `Eth_GetRxBuffer` 返回的是**整帧**（以太网头占 14 B），
广播帧首字节是 `FF FF`，永远匹配不上——表现为"ISR 有计数、frames 恒 0"。
本次分别改成 `bsp_uptime_ms()` 计时和 `data[ETH_HEADER_LEN]` 判 magic 后才有数据；这也是 R7 §49.7.5
只有差异表、没有数字的直接原因（板卡排队只是表象）。

**坑 3：`Eth_GetLastIsrCycle` 是"最近一次 ISR"，不能当逐帧到达时间戳（已定位，口径已修正）。**
一次中断覆盖多帧/主循环批量排空时，后几帧的"最近 ISR 时间"可能比当前时刻还晚，直接相减会得到负数，
强转 unsigned 后变成 ~8.9 s 的假最大值。修正：只统计"时间差为正且 <10 ms"的样本，其余计 `lat_bad`；
同时给出 1 ms 兜底与 `ISR→主循环发现` 两个更稳的量。全速窗口 `lat_bad` 0.8 %，不影响 avg/max 的结论。

**复跑**

```bash
# 一次持锁跑完 空闲/1000fps/20kfps/全速 × 中断/轮询（约 55 s，含烧写与 banner 自证）
bash scripts/build.sh samples/mcal/eth_irq
bash samples/mcal/eth_irq/host/run_irq_matrix.sh --lock-wait 1800

# 只跑"中断/轮询 × 1000fps"两档（不开主机灌包时 ISR 计数也有效）
bash scripts/hwrun.sh mcal/eth_irq --secs 16 --send "irq off;irq rx 4;irq on;irq rx 4"

# 重新出图（图 49-10 / 49-11）
python3 tools/figs/mcal_eth_irq.py --log logs/20261102-184310-eth_irq-host.log
```

例程输出的两行口径（旧行保留给历史日志对照，新行给全指标）：

| 行 | 字段 |
|---|---|
| `IRQ mode=... frames=... isr=... isr_per_frame=... lat_avg=...us lat_max=...us` | R7 旧格式（兼容） |
| `IRQX mode=... fps=... isr_pf_x1000=... lat_avg_us=... lb_*... wake_*... polls=... poll_ns_avg=... rx_cpu_x100=... drop/err/crc/ovf` | 本次新增：定点数（x100/x1000），含延迟分桶、兜底帧数、CPU 占比、计数器增量 |

日志：`logs/20261102-184310-eth_irq-host.log`（主）、`logs/20261102-183509-eth_irq-host.log`、
`logs/20261102-183711-eth_irq-host.log`、`logs/20261102-183827-eth_irq-host.log`、`logs/20261102-183934-eth_irq-host.log`（重复性）、
`logs/20261102-182447-eth_irq.log`（切换修复验证）、`logs/20261102-181701-eth_irq.log`（修 magic 前反例）。

### 49.7.6 链路切换耗时（EthTrcv 实测）

表 49-17 速率/主从切换实测（logs/20261102-164119-eth_trcv.log）

| 操作 | 结果 | 软复位耗时 | 建链耗时 | PHYSR | 说明 |
|---|---|---|---|---|---|
| 上电初始（1000M Slave） | link UP / 1000M / SLAVE | — | — | 0x8400 | EthTrcv_Init 配好 RGMII 延迟与主从 |
| trcv s100（切 100BASE-T1） | link **DOWN** | 5002 ms（等待超时） | — | 0x4000 | 对端转换盒只支持 1000BASE-T1，100M 建不起链 |
| trcv s1000（切回 1000M） | link UP / 1000M / SLAVE | 13 ms | 11 ms | 0x8400 | 软复位后 11 ms 建链 |
| trcv master（切 Master） | link **DOWN** | 5002 ms（等待超时） | — | 0x2000 | 1000BASE-T1 强制模式两端都 Master，无法建链 |
| trcv slave（切回 Slave） | link UP / 1000M / SLAVE | 20 ms | 19 ms | 0x8400 | — |
| trcv poll 3 s | 0 次翻转 | — | — | — | 链路稳定，EthTrcv_MainFunction 无抖动 |

读回的关键寄存器（同一日志的 trcv regs）：

| 寄存器 | 值 | 解读 |
|---|---|---|
| BMCR (0x00) | 0x0140 | 1000BASE-T1 强制模式（bit8=1、bit6=1，bit15 复位已自清） |
| BMSR (0x01) | 0x0009 | 链路已建立（bit2=1） |
| PHYID1 / PHYID2 | 0x4F51 / 0xEB01 | YT8011AN（OUI=0x13D47A、Model=48、Rev=1） |
| GBSR (0x0A) | 0x3000 | local/remote receiver OK（1000BASE-T1 双向训练完成） |
| PHYSR (0x11) | 0x8400 | [15:14]=10 → 1000M，[13]=0 → Slave，[10]=1 → 实时链路 |
| EXT 0x9001 | 0x0110 | RXC 延迟使能（bit8）、TXC 延迟档位 1（bit[7:4]=1）、未反相时钟 |
| MMD1 0x0834 | 0x8001 | force_config=0（Slave）、force_phy_type=1（1000BASE-T1） |

这三组数据说明：**EthTrcv_Init 的 PHY 配置确实生效**（EXT 0x9001 与 MMD1 0x834 的读回值与
PBcfg 一致），而且速率/主从切换的"失败"都是**对端/角色约束**导致的预期结果，不是驱动缺陷：

- 100BASE-T1 失败：转换盒是 1000BASE-T1 设备，链路两端速率必须一致；
- Master 失败：T1 强制模式必须一主一从，板上 PHY 与转换盒已固定为 Slave/Master。

## 49.8 lwIP 与 FreeRTOS 集成

> 本节由 eth-stack 提供（task-2）。它回答两个问题：AUTOSAR MCAL 的 Eth 驱动能不能撑起一个完整的
> TCP/IP 协议栈？在 FreeRTOS 下要补哪些东西、代价是多少？全部数据来自 `logs/` 里的真实串口日志，
> 例程见 `samples/mcal/eth_lwip`（裸机）与 `samples/mcal/eth_freertos_lwip`（FreeRTOS）。

### 49.8.1 分层：netif 只认 Eth.h，不认芯片

SDK 的 lwIP 端口（`hpm_sdk/samples/lwip/ports/baremetal/ethernetif.c`）把 netif 回调直接焊在
ENET 驱动上（`enet_lwip_output()` / `enet_desc_t` / `enet_netif_state_t`）。这样能跑出第 13 章的
935 Mbit/s，但协议栈越过了 MCAL 这一层。本节的移植只保留 netif 的语义，硬件访问全部换成
`Eth.h` 的公共 API，代码放在 `components/app_lwip_eth/`（约 1 100 行，含 iperf/ping/命令）。

![图 49-5 两条数据通路：SDK enet 驱动与 MCAL Eth](figures/49/fig-49-5-lwip-datapath.png)

图 49-5 左边是基线（描述符 buffer1 直接指向 pbuf 净荷，零拷贝发送），右边是本例：
`Eth_ProvideTxBuffer()` 拿到的是驱动内 1 536 字节缓冲，所以发送方向**必然多一次整帧 memcpy**；
接收方向两种后端都是零拷贝，差别只在 pbuf 释放时把缓冲还给谁 —— MCAL 是 `Eth_ReleaseRxBuffer()`。

表 49-18 netif 回调与 MCAL API 的对应关系

| netif 侧 | MCAL API | 语义要点 |
|---|---|---|
| `netif->linkoutput` | `Eth_ProvideTxBuffer()` + `Eth_Transmit()` | `PhysAddrPtr=NULL` 表示整帧已在缓冲；`LenByte`=整帧长；驱动补 0 到 60 B |
| 收包（`app_lwip_eth_rx_drain`） | `Eth_GetRxBuffer()` | 返回 DMA 环内指针（含 14 B 头，不含 FCS），每次取一帧并推进索引 |
| custom pbuf 的 free 回调 | `Eth_ReleaseRxBuffer()` | 归还前 DMA 不会复用该缓冲，所以零拷贝安全 |
| `netif->hwaddr` | `Eth_GetPhysAddr()` | 由 `Eth_PBcfg.c` 的 `MacAddr` 决定（本例 02:00:00:00:01:00） |
| 链路 up/down | `EthTrcv_GetLinkState()` / `GetBaudRate()` | PHY 配置（RGMII 延迟、主从、速率）只在 EthTrcv 里做，netif 不碰 |
| 周期动作 | `Eth_MainFunction()` | 内部含 `Eth_TxConfirmation()`，主循环每轮调一次即可回收 Tx 缓冲 |

### 49.8.2 发送：驱动缓冲 + 一次整帧拷贝

```c
/* components/app_lwip_eth/src/app_lwip_eth.c：netif->linkoutput（节选） */
frame_len = (uint16_t) p->tot_len;
if (frame_len > s_ops->max_frame_len) {            /* 1518 = ETH_MAX_FRAME_LEN */
    s_stats.tx_drop_len++;
    return ERR_BUF;
}
s_ops->lock(s_ctrl_idx);                            /* RTOS：TX/RX 两个上下文串行化 */
buf = s_ops->tx_buffer_get(s_ctrl_idx, frame_len, &cookie);   /* Eth_ProvideTxBuffer */
if (buf == NULL) {
    /* 环满：先就地回收一次已发完的描述符再重试（§49.9.13）。这一句是 TCP 吞吐
     * 从 4.8 Mbit/s 提到 252 Mbit/s 的关键：TxConfirmation 平时只在主循环的
     * Eth_MainFunction() 里跑，而 ACK 是在 netif->input() 里同步产生的。 */
    s_ops->unlock(s_ctrl_idx);
    s_ops->poll(s_ctrl_idx);                        /* Eth_MainFunction -> TxConfirmation */
    s_ops->lock(s_ctrl_idx);
    buf = s_ops->tx_buffer_get(s_ctrl_idx, frame_len, &cookie);
    if (buf == NULL) {                              /* 真的没缓冲，交给 TCP 重传 */
        s_ops->unlock(s_ctrl_idx);
        return ERR_MEM;
    }
}
(void) pbuf_copy_partial(p, buf, frame_len, 0);     /* 整帧（含 14 B 头）一次拷完 */
if (s_ops->tx_commit(s_ctrl_idx, cookie, frame_len) != 0) {   /* Eth_Transmit(..., NULL) */
    s_ops->unlock(s_ctrl_idx);
    s_stats.tx_err++;
    return ERR_IF;
}
s_ops->unlock(s_ctrl_idx);
```

### 49.8.3 接收：custom pbuf 零拷贝

`Eth_GetRxBuffer()` 给的就是 DMA 环内真实缓冲，所以用 lwIP 的 `pbuf_alloced_custom()` 把
pbuf 的 payload 直接指过去，不做任何数据搬运；pbuf 释放时回调里归还缓冲。

```c
/* 申请：payload 直接指向 DMA 环内缓冲 */
slot->pc.custom_free_function = app_lwip_eth_rx_pbuf_free;
p = pbuf_alloced_custom(PBUF_RAW, len, PBUF_REF, &slot->pc, (void *) frame, len);

/* 释放：lwIP 用完 pbuf -> 归还给 Eth 驱动 */
static void app_lwip_eth_rx_pbuf_free(struct pbuf *p)
{
    struct app_rx_pbuf_slot *slot = (struct app_rx_pbuf_slot *) p;
    s_ops->rx_release(s_ctrl_idx, slot->cookie);      /* Eth_ReleaseRxBuffer */
    slot->cookie = NULL;
    slot->in_use = 0U;
}
```

pbuf 池是 32 个静态槽（16 个 Rx 缓冲的 2 倍）。池耗尽时立刻归还 DMA 缓冲再丢帧，
否则 Rx 环会被"借走不还"卡死。实测一整轮 ping/iperf 后 `in_use=0 peak=1/32`，无泄漏
（`logs/20261102-162802-eth_lwip.log`）。

### 49.8.4 FreeRTOS 集成：为什么要自己写 sys_arch

SDK v1.13.0 的 lwIP 只有裸机端口；`samples/lwip/ports/freertos/arch/sys_arch.c` 是**样例级**的
（650 行，随样例复制，middleware 里没有正式的）。本仓库的移植在 `components/freertos_lwip_port/`
（530 行），除了满足 lwIP 2.1.2 的接口约定，还额外提供三项书稿要用的测量能力：
CPU 占用（1 kHz tick 采样空闲率）、任务栈水位、临界区最长关中断时间。

表 49-19 sys_arch 接口与 FreeRTOS 10.5.1 原语的映射

| lwIP 侧 | 实现 | 备注 |
|---|---|---|
| `sys_sem_t` | 二值信号量 | `sys_sem_new/signal/wait/free` |
| `sys_mutex_t` | 递归互斥量 | lwIP 的核心锁会在同一任务重入 |
| `sys_mbox_t` | `QueueHandle_t`（元素 `void*`） | 深度按 lwIP 传入的 size 建，上限 32 |
| `sys_thread_t` | `TaskHandle_t` | lwIP 传字节数，FreeRTOS 要字数（÷4） |
| `sys_prot_t` | `u32_t` | `sys_arch_protect()` 内部 `taskENTER_CRITICAL()` |
| `sys_now()` | `xTaskGetTickCount()×portTICK_PERIOD_MS` | 1 kHz tick 下就是毫秒 |
| `sys_msleep()` | **不实现** | lwIP 核心 `src/core/sys.c` 已提供，重复定义会链接失败 |

```c
/* components/freertos_lwip_port/src/sys_arch.c：临界区（节选） */
sys_prot_t sys_arch_protect(void)
{
    uint32_t c = port_rdcycle();        /* 记进入时刻，用来量"关了多久中断" */
    taskENTER_CRITICAL();
    s_stats.protect_enter++;
    return (sys_prot_t) c;
}

void sys_arch_unprotect(sys_prot_t pval)
{
    uint32_t dt = port_rdcycle() - (uint32_t) pval;
    if (dt < 480000U) {                 /* 只统计 <1 ms 的合理样本，滤掉 rdcycle 回绕 */
        s_stats.protect_total_cycles += dt;
        if (dt > s_stats.protect_max_cycles) {
            s_stats.protect_max_cycles = dt;
        }
    }
    taskEXIT_CRITICAL();
}
```

### 49.8.5 任务划分与优先级

![图 49-6 FreeRTOS 版的任务、优先级与数据流](figures/49/fig-49-6-rtos-tasks.png)

表 49-20 任务与优先级（`configMAX_PRIORITIES=8`，`configTICK_RATE_HZ=1000`）

| 任务 | 优先级 | 栈（字） | 职责 | 设计要点 |
|---|---|---|---|---|
| `ethrx` | 6 | 2 048 | `Eth_GetRxBuffer → custom pbuf → tcpip_input` | 无包 64 次空转后 `vTaskDelay(1)`；有包连续排空 |
| `tcpip` | 5 | 4 096 | lwIP 核心线程；发送（linkoutput）只在这里发生 | `TCPIP_THREAD_PRIO` |
| `app` | 4 | 4 096 | 初始化：`Eth_Init → EthTrcv_Init → ACTIVE → tcpip_init → netif_add` + 上电自检 | 完成后自删 |
| `ethmon` | 2 | 2 048 | 每 5 s 打印 CPU 占用 / 栈水位 / 吞吐计数 | 不参与收包 |
| `shell` | 1 | 2 048 | letter-shell 轮询（10 ms） | 命令里调 raw API 必须持核心锁 |
| `IDLE` | 0 | 512 | tick hook 采样空闲率 | CPU 占用 = 100% − 空闲率 |

两条约束值得单独写出来：

1. **收包任务不做协议处理**，只把 pbuf 丢给 `tcpip_input()`（内部 trypost，不阻塞）。
   否则 lwIP 的解析耗时会把 Rx 环拖死：1 Gbit/s 下 16 个 Rx 缓冲不到 1 ms 就填满。
2. **发送只在 tcpip 线程、接收只在 ethrx 任务**，两个上下文对 Eth 驱动的访问由后端里的一把
   FreeRTOS 互斥量串行化。裸机版这两个回调是空函数，不付任何代价。

### 49.8.6 sys_arch 原语开销（实测）

![图 49-7 sys_arch 原语开销](figures/49/fig-49-7-sysarch-cost.png)

表 49-21 sys_arch 原语实测（480 MHz，rdcycle 测量，`logs/20261102-153226-rtos_lwip_selftest.log`）

| 原语 | 耗时（cycle） | 折算（µs） | 说明 |
|---|---|---|---|
| `SYS_ARCH_PROTECT` + `UNPROTECT` | 63.03 | 0.131 | lwIP 内存分配与 PCB 链表热路径 |
| 其中临界区本身（平均/最大） | 25.02 / 1 234 | 0.052 / 2.57 | 最大值是被 tick 中断顶住的那次 |
| `sys_sem` give+take（同任务） | 192.13 | 0.400 | 不阻塞，纯原语成本 |
| `sys_mbox` trypost+tryfetch | 3 917.09 | 8.16 | 偏高，已登记复查（见 §49.12 #2） |
| 跨任务 sem 乒乓（含 2 次切换） | 7 807.4 | 16.27 | 约 8.1 µs/次上下文切换 |

同一份日志里还有：1 kHz tick 采样空闲率 99.6%；`tcpip` 线程栈水位 948 字（建 1 024 字，
余 76 字）；`malloc` 失败 0 次、栈溢出 0 次。这条数据说明 **1 kHz tick + 全套 sys_arch
在 lwIP 空转时只占 0.4% CPU**，代价可以接受。

### 49.8.7 实测：吞吐、CPU、栈水位

FreeRTOS 版与裸机版共用同一份 netif 代码，所以两者的差别就是"多了一个 RTOS 层"：
收包多一次 `tcpip_input` 投递（mbox trypost），发送多一次互斥量进出，另外多一个 1 kHz tick。

表 49-22 裸机 vs FreeRTOS vs SDK 基线（同一块板、同一套 lwIP 内存预算）

| 指标 | SDK enet 驱动（第 13 章基线） | MCAL Eth + 裸机 lwIP | MCAL Eth + FreeRTOS lwIP |
|---|---|---|---|
| 下行（主机→板） | 935 Mbit/s（TCP） | **TCP 252 Mbit/s**（iperf2，R8 修复后，见 §49.8.10） | 见 `logs/` 长测日志 |
| 上行（板→主机） | TCP 466 / UDP 641 Mbit/s | **TCP 169~261 Mbit/s**（lwiperf 客户端口径）/ **UDP 375.10 Mbit/s**（主机实收 365 Mbit/s、丢 0.087%） | 见下 |
| 单帧发送耗时 | 756 cycle/帧（描述符直指 pbuf） | 36 µs 上限（含整帧 memcpy） | 36 µs + 互斥量（实测同量级） |
| ICMP RTT | — | 136/280/383 µs（丢包 67%） | **1015/1015/1016 µs**（被 1 kHz tick 量化，见 §49.8.9 坑 8） |
| 链路查询 | — | 99~105 µs（EthTrcv MDIO） | 同左 |
| 空闲 CPU 占用 | — | 主循环忙轮询 | **99.6% 空闲**（= 0.4% 占用，1 kHz tick 采样，10 s 窗口） |

关于 R7 那个 92 Mbit/s 要说清楚：它是 **lwiperf 计入 lwIP 的字节数**，当时被解读成
"MCAL 驱动 + 8 个 Tx 缓冲下 lwIP 能提交出去的上限"，并据此把差距归因于"多一次 memcpy"和
"Tx 缓冲数量"。**R8 证明这个归因是错的**：同一个 UDP 上行用例，在 §49.9.12 的 TX 环错位修好之后
跑到 **375.10 Mbit/s（主机实收 365 Mbit/s，丢 0.087%）**——原来 92 Mbit/s 就是"整环积压后一次性喷发"
把发送节奏打碎的结果，与 memcpy、缓冲数量都无关。同一轮修完的 `eth_raw_echo` TX burst 也从
219 370 fps（64 B）/59 257 fps（512 B）提到 **242 924 fps（143.9→159.4 Mbit/s）** /
**61 332 fps（251.3→260.1 Mbit/s）**，说明按序发放并没有牺牲发射速率。

### 49.8.8 长测与优先级实验

表 49-23 长测（320 s）关键指标（`logs/20261102-164944-eth_freertos_lwip.log`，986 行串口日志）

| 指标 | 结果 |
|---|---|
| 运行时长 / 复位 | 320 s 连续，无复位、无 HardFault、无 assert |
| CPU 空闲率（稳定段） | **99.7%**（占用 0.3%），5 s 窗口 `window=5014 ticks`，约 60 个窗口 |
| FreeRTOS 堆余量 | **61 984 B 恒定**（堆 96 KB，无泄漏） |
| 任务栈水位（字，越大越安全） | `ethrx` 1 948/2 048、`shell` 1 776~1 894/2 048、`tcpip` 925/1 024、`ethmon` 1 868/2 048、`Tmr Svc` 968/1 024、`IDLE` 464/512 |
| `malloc` 失败 / 栈溢出 | 0 / 0 |
| sys_arch 累计调用 | mbox post 462 / fetch 784（fail 0）；mutex lock 818；protect 2 989 次，单次最长 **1 939 cycle**、平均 72.7 cycle |
| 链路 | 全程 link=UP，1000 Mbit/s、Slave |
| 结论 | 空载 320 s 稳定；`ethrx`/`shell` 的栈余量只剩约 100/170 字，加功能时要复查 |

表 49-24 收包任务优先级对 RTT 的影响（`ethprio <n>` 改优先级，`ethload <prio> <ms>` 加负载）

| ethrx 优先级 | 背景负载 | ping 收到/发出 | RTT avg | 抖动 | 说明 |
|---|---|---|---|---|---|
| 6（默认） | 无 | 见 50.6 表 49-1 | 280 µs | 117 µs | 丢包与 Tx 侧问题同源 |
| 6 | `ethload 4 20000` | 见 README | — | — | 低优先级忙任务不影响收包 |
| 6 | `ethload 7 20000` | 见 README | — | — | 高于收包任务，预期掉包 |

### 49.8.9 FreeRTOS 侧的坑

1. **`sys_msleep` 重复定义**：lwIP 2.1.2 的 `src/core/sys.c` 已经实现了 `sys_msleep()`，
   移植层再写一遍会在链接期报 `multiple definition`。只保留 `sys_now()/sys_jiffies()`。
2. **必须开 `LWIP_TCPIP_CORE_LOCKING=1`**，否则应用任务里调 `raw_new/raw_sendto` 没有保护；
   `app_lwip_eth_ping()` 用 `LOCK_TCPIP_CORE()` 包住 raw 调用。
3. **不开 `CONFIG_LWIP_NETCONN_API` 就没有 `tcpip_init()`**：SDK 只在
   `CONFIG_LWIP_NETCONN_API || CONFIG_LWIP_SOCKET_API` 时编 `api/tcpip.c`，报错是链接期
   `undefined reference to tcpip_init`。
4. **lwiperf 的 UDP 客户端必须每轮调 `lwiperf_poll_udp_client()`**，否则只发头几帧就停；
   裸机版把这个调用放进了 `app_lwip_eth_poll()`。
5. **打流时不要开高频 printf**：921600 波特率下 300 字符约 3 ms，主循环被顶住
   （实测 `loop gap_max` 从 3.3 ms 涨到 9.6 ms），这段时间的帧只能靠 16 个 Rx 缓冲兜。
6. **两套堆要分开看**：FreeRTOS 堆（任务/队列/信号量）与 lwIP 堆（pbuf/socket）互不相干，
   长测时两个余量都要看，否则一方耗尽很难定位。
8. **RTOS 版的 ping RTT 被 tick 量化**：`app_lwip_eth_ping()` 的等待循环用 `vTaskDelay(1)`，所以 RTT 一定是 1 ms 的整数倍（实测 1015/1015/1016 µs，抖动只有 1 µs）。这是**测量实现**的限制，不是链路延迟；要测亚毫秒 RTT 应把等待改成 `taskYIELD()` 或等信号量。裸机版没有这个问题（同一块板 136~383 µs）。
9. **等锁/烧写会复位板子**：`--no-flash` 长测必须确认板上跑的仍然是自己刚烧的固件
   （先看 banner：`=== mcal_eth_freertos_lwip: ... ===`），否则日志会串成别人的例程。

### 49.8.10 TCP 收发闭环：A/B 四组实测

R7 把 lwIP TCP 的异常（主机→板 0.084 Mbit/s、板→主机 0.02 Mbit/s，而同固件 UDP 92 Mbit/s）
记成"怀疑 lwIP 发送窗口 / 零拷贝 TX 与 netif 的交互"。R8 用**同一固件单变量 + 主机侧 tcpdump**
把它定位到 **MCAL Eth 驱动的 TX 描述符发放顺序**，与 lwIP 无关。四组 A/B 见表 49-29，
机制见 §49.9.12，图 49-8 是环状态示意，图 49-9 是修复前后 ICMP echo request 真正落到线上的时刻
（主机侧 tcpdump 抓的，不是板端自己数的时间戳）。

表 49-29 TCP 停摆根因的 A/B 四组（同一块板、同一台主机、同一份 lwipopts、每轮重新上电）

| 组 | 固件改动 | ping 20 次 | echo request 落到线上的时序 | TCP 主机→板 | 日志 / pcap |
|---|---|---|---|---|---|
| A 原版 | 无（R7 定稿） | 3/10（自证 2/5） | 8 帧在 6 µs 内一起喷出，随后 31 s 无帧 | 74.2 KB 后停摆 = 37 kbit/s | `logs/20261102-181023-eth_lwip.log`、`logs/20261102-181021-t1a-probe.pcap` |
| B 只加 fence | Eth.c 加 `fence w,w` / `w,o` / `r,r` | 5/20（自证 2/5） | 仍然突发（间隔 3 s / 32 s） | 74.1 KB = 62.5 kbit/s | `logs/20261102-181536-eth_lwip.log`、`logs/20261102-181535-t1b-fence.pcap` |
| C 修发放顺序 | + `TxNext` 游标按序发放 | **19/20**（自证 5/5） | **严格 100 ms 等间隔，无重复 seq** | 4.83 Mbit/s | `logs/20261102-182016-eth_lwip.log`、`logs/20261102-182015-t1c-txorder.pcap` |
| D +ACK 回收 | + linkoutput 环满就地回收（§49.9.13） | 4/5~5/5 | 同 C | **252 Mbit/s** | `logs/20261102-182443-t1e-tcp-h2b.log` |

B 组是必须留的证据：**"缺内存屏障"是最容易先想到的方向**（SDK 自己的 `enet_prepare_tx_desc`
确实在写 OWN 前有 `fence w,w`、踢 DMA 前有 `fence w,o`），但实测它对现象零改善，
说明瓶颈不在"DMA 看到的描述符内容是否最新"，而在"**DMA 根本走不到那个描述符**"。
C 组把 ping 收到率从 2/5 拉到 19/20、TCP 从 62 kbit/s 拉到 4.83 Mbit/s；D 组再补上 ACK 方向的
缓冲回收，主机→板到 252 Mbit/s——相对 R7 的 0.084 Mbit/s 是 3 000 倍。

![图 49-8 TX 描述符环：按下标最小空闲发放 vs TxNext 游标按序发放](figures/49/fig-49-8-tx-ring-order.png)

![图 49-9 修复前后 ICMP echo request 的实际发出时刻（主机侧 tcpdump）](figures/49/fig-49-9-tx-timing.png)

图 49-9 左图是 A 组的真实数据：ping 循环每 100 ms 发一帧，但线上出现的是"8 帧挤在 6 µs 内 + 之后
31 s 静默"——这就是 §49.9.12 说的"整环积压后一次性喷出"。右图是 C 组的同一份采集，斜率正好是
100 ms/帧，且没有重复 seq。

表 49-30 修复后 TCP 双向复跑（iperf2；每轮重新烧写上电，10 轮，汇总：`logs/20261102-183321-t1-rounds.summary`）

| 轮次 | 方向 | 板端/lwiperf 口径 | 主机 iperf 口径 | ping 自证 | 日志 |
|---|---|---|---|---|---|
| 1 | 主机→板 | — | **252 Mbit/s** | 5/5 | `logs/20261102-183321-h2b-1.log` |
| 2 | 主机→板 | — | **251 Mbit/s** | 5/5 | `logs/20261102-183321-h2b-2.log` |
| 3 | 主机→板 | — | 命令未送达（见注） | 4/5 | `logs/20261102-183321-h2b-3.log` |
| 4 | 主机→板 | — | **251 Mbit/s** | 5/5 | `logs/20261102-183321-h2b-4.log` |
| 5 | 主机→板 | — | 命令未送达（见注） | 5/5 | `logs/20261102-183321-h2b-5.log` |
| 1 | 板→主机 | 4.45 Mbit/s | 3.26 Mbit/s | — | `logs/20261102-183321-b2h-1.log`、`logs/20261102-183321-b2h-host-1.log` |
| 2 | 板→主机 | **169.38 Mbit/s** | **169 Mbit/s** | — | `logs/20261102-183321-b2h-2.log`、`logs/20261102-183321-b2h-host-2.log` |
| 3 | 板→主机 | 8.19 Mbit/s | 4.25 Mbit/s | — | `logs/20261102-183321-b2h-3.log`、`logs/20261102-183321-b2h-host-3.log` |
| 4 | 板→主机 | **179.35 Mbit/s** | **179 Mbit/s** | — | `logs/20261102-183321-b2h-4.log`、`logs/20261102-183321-b2h-host-4.log` |
| 5 | 板→主机 | **261.24 Mbit/s** | **261 Mbit/s** | — | `logs/20261102-183321-b2h-5.log`、`logs/20261102-183321-b2h-host-5.log` |

> 注："命令未送达"的两轮是**测试脚本**的问题，不是固件问题：`hwrun.sh --send` 把多条命令在复位后
> 0.3 s 一次性写进串口，此时板子还在跑上电自证（约 4 s，期间不读串口），UART 接收 FIFO 装不下的字节会丢。
> 这两轮的 ping 自证 4/5、5/5 说明固件正常启动，只是 `eth iserver tcp` 没被执行。

表 49-32 同一个根因修复前后的其他口径（同一块板，主机侧 iperf2 / 板端自证）

| 用例 | 修复前 | 修复后 | 日志 |
|---|---|---|---|
| UDP 板→主机（lwiperf 1000M 档） | 92.02 Mbit/s（`tx_busy=9 981`） | **375.10 Mbit/s**，主机实收 365 Mbit/s、丢 0.087%（269/310 410） | `logs/20261102-184421-t1-udp-b2h-host.log` vs `logs/20261102-163313-eth_lwip.log` |
| UDP 主机→板 | 板端 10.7 s 收 120 MB ≈ 92 Mbit/s | 板端 6 s 收 141 836 帧 ≈ 278 Mbit/s | 同上 |
| `eth_raw_echo` TX burst 64 B | 219 370 fps / 143.9 Mbit/s | **242 924 fps / 159.4 Mbit/s** | `logs/20261102-184421-t1-rawtx.log` |
| `eth_raw_echo` TX burst 512 B | 59 257 fps / 251.3 Mbit/s | **61 332 fps / 260.1 Mbit/s** | 同上 |

表 49-30 有两件事必须说清楚，否则读者会以为"上行就是差"：

1. **主机→板稳定在 251~253 Mbit/s**（三轮 + §49.8.7 的两轮共 5 轮，无塌陷）。这是 TCP 服务端方向，
   板子的职责是"收数据 + 回 ACK"，链路层吞吐由 ACK 与窗口共同决定——252 Mbit/s 已接近
   8×MSS 窗口在 0.5 ms 往返下的理论上限（11 680 B / 0.5 ms ≈ 187 Mbit/s 的量级，
   实际因 ACK 压缩与延迟 ACK 略高），再要更高就得把窗口放大，而放大窗口在本板会踩到
   下面的第 2 条。
2. **板→主机波动大（4.45~169 Mbit/s）**，根因是**发送方向存在约 2% 的线上误码**：同一轮 test 里
   主机网卡 `rx_crc_errors` 增 991~5 828（发送 4~12 万帧），而板端驱动 `TDES0.ES`/
   `UF` 全 0（MAC 认为每帧都发成功了）。也就是说误码发生在 MAC 之后（RGMII/YT8011AN/转换盒一侧），
   不在描述符逻辑里。TCP 只要丢一段就等 RTO，lwiperf 的 TCP 客户端又是"发完一批等 `tcp_sent`
   回调再发"的结构，于是一旦踩中丢包就退化成"每秒 4 个 MSS"，表现就是表 49-30 里的 4.45 / 8.19。
   这一项登记为 §49.12 #2（已定位，未闭环：需要示波器量 RGMII TXD/TX_CTL 与 1000BASE-T1 眼图）。

**为什么之前一直误判成 lwIP 窗口问题？** 三个因素叠在一起：

- UDP 两个方向都"正常"，但 UDP 的 iperf 数字是**发送侧字节计数**，丢了对端也不回退；而且 UDP 打流是
  **单向**的，TX 环里始终只有 1~2 帧在排队，恰好踩不中"错位"这个状态。
- TCP 的 ACK 与数据**双向交织**，环里常年 8 帧在排队，一次错位就是整环积压。
- `tx_busy` 在 ping 阶段是 0（环没满），看起来"驱动没压力"，掩盖了"帧进了环但没出去"。

## 49.9 坑

这一节是本章最值得抄下来的一节：每条都是本轮真实踩过、且**现象与根因相距很远**的。

### 49.9.1 收发缓冲放在 .bss 里，DMA 读不到

**现象**：Eth_Init 成功、链路 UP、Eth_Transmit 返回 E_OK、TDES0 的 OWN 也从 1 变 0，
但计数器和主机侧都认为"没有帧"。

**根因**：s_tx_buf/s_rx_buf 是普通全局数组，链接脚本把 .bss 放在 DLM（core-local memory，
0x0800_0000 段）。ENET 的 DMA 是 AXI 主设备，**访问不到 ILM/DLM**，于是描述符里的缓冲地址
指向一个 DMA 不认识的地方。

**改法**：把缓冲和描述符一起放到 AXI SRAM 的 `.noncacheable.bss`（SDK 宏
`ATTR_PLACE_AT_NONCACHEABLE_BSS_WITH_ALIGNMENT`；R8 起代码从 legacy `.noncacheable` 切过来）。
这里要澄清 R7 写错的一句：SDK 链接脚本里 legacy `.noncacheable` 与 `.noncacheable.bss` **同属**
`.noncacheable (NOLOAD)` 输出段，改前改后 `demo.bin` 都是 97 264 B、`readelf -l` 该段 FileSiz=0，
**所谓"省 37 KB Flash"并不存在**——那 37 632 B 从一开始就只在 RAM 里（R7 的 135.8 KiB 是 `.obj` 的
`size` 口径）。R8 的 A/B 证据：`logs/20261102-181031-eth_noncacheable_bss.log`。
改用 `.noncacheable.bss` 的实际收益是：符合 SDK 新宏（legacy 段仅保留兼容）、startup 会按
`__noncacheable_bss_start__/__noncacheable_bss_end__` 清零该段（reset.c:119）。

**自检方法**：eth regs 打印的描述符 Buffer1 地址，应该在 AXI SRAM 区间（0x0123_xxxx 这种），
而不是 0x0800_0000 附近。

### 49.9.2 PLIC 中断开得太早 → ISR 风暴，启动就挂死

**现象**：烧写后串口只打印到 banner，之后的 init 打印全没了，像是死机。

**根因**：板级初始化里就把 ENET0 的 PLIC 中断打开，但此时 MAC 的 INTR_STATUS 里已经有
上电残留的挂起位（PHY 唤醒/链路事件）。ISR 只清了 DMA_STATUS、没清 MAC 的 INTR_STATUS，
于是电平中断一直有效 → 反复进 ISR，主循环永远出不来。

**改法**：PLIC 使能必须放到 Eth_Init 里、DMA/MAC 初始化之后，并且只在配置确实要中断时打开；
ISR 里补一次 (void)base->INTR_STATUS（只读寄存器读即清）。

**这条也是"中断风暴"类问题的通用排查思路**：先看是不是电平中断没清干净，再看 ISR 里有没有
把外设挂起位全部清掉。

### 49.9.3 MAC 的 INTR_STATUS 是只读（读清），DMA 的 STATUS 是写 1 清

写 base->INTR_STATUS = base->INTR_STATUS 会编译报错（assignment of read-only member），
因为它被声明成 __I。正确写法是读一次：(void)base->INTR_STATUS;。
而 DMA_STATUS 是 W1C：写回读到的值才能清。两个寄存器在相邻的地址上、名字也像，
但语义完全相反，混用就会"清不掉中断"或"误清别的位"。

### 49.9.4 Eth_Init 之后必须 SetControllerMode(ACTIVE)，否则不收不发

SWS_Eth_00276 要求 Eth_Init 后控制器处于 ETH_MODE_DOWN（initialize 不等于 start）。
本实现严格遵守：Eth_Init 里把 DMA_OP_MODE 的 ST/SR 清掉，只留描述符环就绪。
如果忘了调 Eth_SetControllerMode(0, ETH_MODE_ACTIVE)，现象是：链路 UP、PHY 正常、
Eth_Transmit 返回 E_NOT_OK（SWS_Eth_00129），计数器全 0。

### 49.9.5 在 Eth_Init 之前读 PHY，读回全 0

**现象**：EthTrcv_GetLinkState 说 link=DOWN，Eth_ReadMii 读 BMSR 得到 0x0000。

**根因**：ENET0 的外设时钟与 MDC 分频是在板级初始化里开的，PHY 的 nRST 也在这之后才释放
（拉低 30 ms）。这两件事都在 Eth_Init 内部完成，所以 Eth_Init 之前的任何 MDIO 访问都是
"对着一颗还在复位、且总线没时钟的 PHY"说话。

**改法**：初始化顺序固定为 Eth_Init → EthTrcv_Init → Eth_SetControllerMode(ACTIVE)。
不要把 Eth_BoardInit 拿出来单独调（它是驱动内部钩子，重复调会再复位一次 PHY）。

### 49.9.6 YT8011AN 的软复位必须带速率编码

写 BMCR=0x8000（只置 reset 位）做软复位，PHY 会回到上电 strap 默认（1000BASE-T1/Slave），
你刚写的 MMD1 0x834（速率/主从）被一起抹掉。应用说明 §5.4 给的做法是把"复位 + 速率选择"
合成一个写法：1000BASE-T1 写 0x8140、100BASE-T1 写 0xA100。这一点在既有 YT8011 组件里也
有注释（components/enet_phy_yt8011/src/hpm_yt8011.c:yt8011_soft_reset_cfg）。

### 49.9.7 BMSR 的 Link 位是锁存的

IEEE 802.3 规定 BMSR[2]（Link Status）是 latch-low：链路掉过之后它会保持 0，直到再读一次。
所以"读一次就判断"会得到过期状态，必须连读两次、以第二次为准。本实现的
EthTrcv_GetLinkState 直接用 PHYSR[10]（YT8011 的实时链路位）绕开这个问题，
但保留 BMSR 读法作为交叉验证。

### 49.9.8 EXT/MMD 访问是"两次写"，中间不能插别的 MDIO 事务

EXT 寄存器要先写 0x1E（地址）再读写 0x1F（数据）；MMD 要先写 0x0D（devad）、0x0E（reg）、
0x0D（0x4000|devad）、再读写 0x0E。如果在这中间插入对别的 PHY 寄存器的访问（哪怕是在
另一个任务里），间接地址就被覆盖了。本实现把这些序列包在 EthTrcv 的静态函数里，
且 EthTrcv_MainFunction 与初始化在同一上下文执行，不跨任务。

### 49.9.9 描述符与缓冲地址要经过 core_local_mem_to_sys_address()

HPM 的 core-local 地址和系统（DMA）视角的地址不一定相同。SDK 在初始化描述符环时做了转换
（desc.tx_desc_list_head = core_local_mem_to_sys_address(...)），本实现在 Eth_Transmit 里
填 TDES2.Buffer1 时也必须做同样转换。漏了这步，DMA 会去读一个"CPU 视角才对"的地址。

### 49.9.10 待确认：DMA_STATUS.TU（发送下溢）

本轮调试中出现过 DMA_STATUS bit2（TU，发送下溢）置位：DMA 取描述符的速度跟不上 MAC 发送。
常见原因是 PBL 太小、缓冲所在内存带宽不够、或 TX 环里连续提交的帧太多。本实现的 PBL=32、
AXI SRAM 缓冲，理论不该出现；该现象仍在跟踪，列在 50.12 节。

### 49.9.11 测试帧的序号字段没写，RTT 恒为 0

**现象**：主机 echo 侧明明回显成功（@@replied=166/300@@）、板端 @@rx_frames@@ 也在涨，但 @@eth rtt@@ 恒报
@@replied=0 / lost=n@@，看起来像"板端收不到帧"。

**根因**：自定义测试帧的 payload[4..7] 序号字段**从来没被写**——发送端只写了 magic 与后面的递增 pattern，
接收端却按 payload[4..7] 取 seq，于是把 pattern 当成了序号（@@got=0x07060504@@ 正好是 @@04 05 06 07@@ 连号，
@@0x09080706@@ 对应 @@06 07 08 09@@，一眼可辨）。

**改法**：发送端把 seq 写成小端 4 字节（pattern 从 payload[8] 开始）。修完同一命令立刻变成 @@replied=2/3@@、
@@avg=2 065 µs@@（见 §49.7.4）。

**教训**：自定义帧格式要有"字段齐全性自检"（magic/len/seq 三件套）；否则"链路正常、协议对不上"会被误判成
硬件或驱动问题，白花几个小时。用 hexdump 打 22 字节帧头是成本最低的定位手段。

### 49.9.12 TX 描述符按"下标最小空闲"发放，与 DMA 环位置错位

**现象**（R7 遗留的"lwIP TCP 不通"）：UDP 两个方向都正常（上行 92 Mbit/s、主机侧 944 Mbit/s 已发出），
但 TCP 主机→板在 0.123 MB 后彻底停摆、板→主机只有 16 kbit/s；ping 只有 2/5~3/10 收到。
主机侧 tcpdump 看到三件事：① echo request 不是每 100 ms 一帧，而是"每 3 s / 32 s 突发 6~8 帧、
同一微秒内喷出、seq 乱序且重复"；② 建链时板子的 SYN-ACK 比主机 SYN 晚 17 s，而且 8 份一模一样的
SYN-ACK 挤在 1 µs 内；③ 主机对每个 echo request 都回了 reply（24/24），所以不是"主机不回"。

**根因**：`Eth_ProvideTxBuffer` 原来按"下标最小的空闲缓冲"发放，而 `Eth_TxConfirmation`
按 own==0 把 DMA **已经走过**的描述符标回空闲。一旦上层把新帧塞到 DMA 身后的描述符，
TX DMA 就再也走不动了 —— DWC GMAC 的发送 DMA 只能沿描述符链**顺序前进**，碰到 OWN==0 的描述符
进入 Suspend，`DMA_TX_POLL_DEMAND` 唤醒后它**仍然从原来停住的那个描述符**取；所以每次
kick 都读到 OWN==0，已提交的帧全部积压，直到整环 8 个缓冲被填满、游标被迫回到 DMA 当前位置，
才在同一微秒内把 6~8 帧一起发出去（图 49-9 左图）。

**解决**：驱动内加 `TxNext` 游标，只发放游标指向的那一个缓冲，`Eth_Transmit` 成功后
`TxNext = idx + 1`——正好是 DMA 处理完这一帧之后要取的描述符，两者永久同步；拿不到就按
SWS_Eth_00079 返回 `BUFREQ_E_BUSY`。这是 SDK 的做法（`enet_desc_t.tx_desc_list_cur` 严格顺序提交），
本驱动早期为了"用满所有缓冲"改成了最小下标，反而把环锁死。

```c
/* mcal/Eth/src/Eth.c：Eth_ProvideTxBuffer 只发放环游标指向的缓冲 */
i = st->TxNext;
if ((st->TxState[i] == (uint8) ETH_TX_FREE) &&
    (s_tx_desc[CtrlIdx][i].tdes0_bm.own == 0U)) {
    st->TxState[i] = (uint8) ETH_TX_LOCKED;
    *BufIdxPtr = ETH_BUFIDX_MAKE(i);
    *BufPtr = &s_tx_buf[CtrlIdx][i][0];
    *LenBytePtr = ETH_MAX_PAYLOAD;
    return BUFREQ_OK;
}
st->TxDroppedBusy++;
return BUFREQ_E_BUSY;      /* 单队列、按序发放：不跨过 DMA 当前位置去"捡"后面的缓冲 */
```

**A/B 证据**：见 §49.8.10 表 49-29（原版 3/10、只加 fence 5/20、改发放顺序 19/20 + TCP 252 Mbit/s）
与表 49-32（同一根因还压着 UDP 上行：92.02 → 375.10 Mbit/s；`eth_raw_echo` TX burst：
219 370 → 242 924 fps）——**"UDP 正常"本身就是假象**，它只是单向流踩不中错位状态、
且 iperf 的 UDP 数字是发送侧字节计数。
复跑：`bash scripts/hwrun.sh mcal/eth_lwip --secs 30 --send "eth ping 192.168.2.1 20 100"`，
同时主机 `sudo tcpdump -i enp6s0 -nn -e -tt -r ... 'icmp and icmp[icmptype]==8'` 看 seq 时序。

### 49.9.13 TxConfirmation 只在 Eth_MainFunction 里跑，而 ACK 在 netif->input 里同步产生

**现象**：修好 §49.9.12 后 TCP 主机→板从 37 kbit/s 到 4.83 Mbit/s，但仍只有目标值的 1/50，
且每秒吞吐在 1.4~11.5 Mbit/s 之间乱跳；同固件 UDP 已经 92 Mbit/s。

**根因**：TCP 的 ACK 是在 `netif->input()` 里同步生成的（收一帧数据 → lwIP 立刻回 ACK →
`linkoutput` 要一个 TX 缓冲）。而 `Eth_TxConfirmation` 只在主循环的
`Eth_MainFunction()` 里跑。`app_lwip_eth_rx_drain()` 一次要连收最多 16 帧，
每帧都可能立刻要一个 TX 缓冲：8 个缓冲被 ACK 占满之后，后面的 ACK 全部
`BUFREQ_E_BUSY` → `ERR_MEM` 被 lwIP 丢掉，只能等 200~500 ms 的延迟 ACK 定时器补。

**解决**：`linkoutput` 在拿不到缓冲时**就地**跑一次驱动的周期动作（`Eth_MainFunction`
里只有 `Eth_TxConfirmation` 与 RxHeld 统计，不碰 RX 环）再重试一次；仍失败才返回
`ERR_MEM` 交给 TCP 重传。实测 `tx_retry_ok` 在 252 Mbit/s 打流时累计 10 223 次、
`tx_retry_fail` 恒 0（`logs/20261102-182443-t1e-tcp-h2b.log`），说明"环满"是常态、
且就地回收一次就够。这一条单独把 TCP 主机→板从 4.83 Mbit/s 提到 **252 Mbit/s**。

### 49.9.14 RX drain 不设上限：一次 app_lwip_eth_poll() 连收 2.4 s

**现象**：TCP 打流时串口统计打印 `rx_drain_max=2408360 us`（2.4 s），同时 TCP 吞吐忽高忽低、
延迟 ACK 与 RTO 全乱。

**根因**：`app_lwip_eth_rx_drain()` 原来"一直收到环空"，而 1 Gbit/s 打流时环永远不空，
于是一次调用连续处理几秒；这期间 `sys_check_timeouts()`（RTO / 延迟 ACK / persist 定时器）、
`Eth_MainFunction()`、letter-shell 全部得不到执行。

**解决**：加单次帧数上限 `APP_LWIP_ETH_RX_DRAIN_MAX=32`（= 2 倍 RX 环深），
保证主循环每约 150 µs 回到协议栈定时器。实测 `rx_drain_max` 从 2 408 360 µs 降到
1 303~1 610 µs（打流）/ 132 µs（ping）。

### 49.9.15 "缺内存屏障"是最像的错方向（已证伪）

**现象**：§49.9.12 的突发/重复 seq 看起来完全像"描述符写序被写缓冲打乱"：DMA 先看到 OWN=1、
后看到 TDES1/TDES2 的旧值，于是发出上一帧。

**事实**：SDK 的 `enet_prepare_tx_desc` 在写 OWN 前确实有 `fence w,w`、
`ENET_TX_DESC_KICK_DMA` 在写 poll demand 前确实有 `fence w,o`
（`hpm_sdk/drivers/src/hpm_enet_drv.c:20-27`），本驱动早期漏了，所以这个怀疑很自然。
R8 把三条 fence（`fence w,w` 描述符字段→OWN、`fence w,o` 写 poll demand、
`fence r,r` 读 OWN 后再读数据）补进 `Eth.c` 后单独上板：
ping 仍然 5/20、TCP 仍然 62.5 kbit/s（表 49-29 B 组）——**对现象零改善**。

fence 保留（它是正确性要求，成本可忽略），但它不是根因；根因是"DMA 走不到那个描述符"（§49.9.12）。
这条坑的价值在于：**遇到"时序看起来被重排"的现象，先证明 DMA 到底停在哪，再谈内存序。**

### 49.9.16 发送方向约 2% 的线上误码（已知限制，不在描述符逻辑里）

**现象**：板→主机 TCP 吞吐在 4.45~169 Mbit/s 之间大幅波动；主机网卡 `rx_crc_errors`
在一轮 240 Mbit/s 的板→主机打流里增长 991~5 828（发送 4~12 万帧，约 1.6~5%）。

**已排除**：板端驱动 `TDES0.ES`（错误汇总）、`UF`（发送下溢）、`LC`（迟冲突）
全 0，`disc_otbd` 也 0 —— MAC 认为每帧都发成功；反方向（主机→板 252 Mbit/s）
板端 `DropPktCrc` 恒 0。也就是说误码发生在 MAC 之后的 RGMII / YT8011AN / 1000BASE-T1
转换盒一侧，与描述符、缓冲、cache 都无关（缓冲在 `.noncacheable.bss`，且低速时零误码）。

**为什么它现在才暴露**：UDP 丢了不重传，TCP 一丢就等 RTO；lwiperf 的 TCP 客户端又是
"发一批 → 等 `tcp_sent` 回调 → 再发"的结构，踩中丢包就退化成"每秒 4 个 MSS"。

**复跑**：

```bash
sudo ethtool -S enp6s0 | grep -E 'rx_crc_errors|rx_errors'     # 打流前后各取一次，看增量
bash scripts/hwrun.sh mcal/eth_lwip --secs 32 --no-build \
  --send "sleep 6000;eth iclient 192.168.2.1 tcp;sleep 16000"   # 主机先起 iperf -s
```

下一步要用示波器量 RGMII TXD[3:0]/TX_CTL 相对 TXC 的建立保持、以及 1000BASE-T1 侧眼图，
或换一块转换盒复测——这已经超出"驱动代码"的范围，登记在 §49.12 #2。

## 49.10 与 SDK 驱动、商用 MCAL 的对比

同一个 ENET0 IP，SDK 给的是"芯片级驱动"，本实现给的是"AUTOSAR MCAL 驱动"。两者不是替换关系，
而是**分层位置不同**：SDK 把 MAC、DMA 描述符环、PHY 访问（`hpm_enet_phy`）放在一层；MCAL 把 PHY 拆到
EthTrcv、把缓冲管理抽象成 BufIdx、把"什么时候把帧交给上层"交给 `Eth_MainFunction`。表 49-25 是逐项对照。

表 49-25 SDK enet 驱动与本实现（Eth + EthTrcv）的差异

| 维度 | HPM SDK `hpm_enet_drv`（v1.13.0） | 本实现 `mcal/Eth` + `mcal/EthTrcv` |
|---|---|---|
| 定位 | 芯片级驱动，直接操作 `ENET_Type *` | AUTOSAR CP MCAL（模块 ID 88 / 73），只暴露 `CtrlIdx`/`BufIdx` |
| 分层 | MAC + DMA + PHY 访问同层 | Eth 只管控制器/缓冲；PHY 归 EthTrcv；EthIf 在上层（本仓库只有弱符号桩） |
| 缓冲区 | 调用者持有描述符与 frame buffer | `Eth_ProvideTxBuffer`/`Eth_GetRxBuffer` 由驱动持有，`Eth_ReleaseRxBuffer` 归还 |
| 发送语义 | `enet_prepare_tx_desc` + `enet_issue_tx`，调用者写描述符字段 | `Eth_Transmit(CtrlIdx, BufIdx, FrameType, TxConf, Len, PhysAddrPtr)`，驱动补最小帧/cache clean |
| 接收语义 | 中断里判 `enet_rx_ready` 后调用者自己取描述符 | ISR 只清状态置标志；`Eth_MainFunction` 调 `Eth_Receive`，`Eth_GetRxBuffer` 零拷贝给上层 |
| PHY 访问 | `enet_read_phy`/`enet_write_phy` + `hpm_enet_phy` 组件 | `Eth_ReadMii`/`Eth_WriteMii`/`Eth_ReadMmd`（Eth）+ `EthTrcv_*`（链路/速率/主从/自协商） |
| 时间戳/TSN | 支持 ENET PTP（IEEE 1588）寄存器组 | **未实现**（`Eth_GetCurrentTime` 等返回 `E_NOT_OK` + DET，见 §49.12 #6） |
| 中断模型 | 单 ISR，回调里可直接收发 | ISR 只做"清 + 置标志"，交付在主函数上下文（可重入性更好） |
| 代码量 | 驱动 2 444 行（`.c` 1 308 + `.h` 1 136） | Eth 1 941 行 + EthTrcv 953 行（含头文件与 PBcfg） |
| 发送环发放顺序 | 调用者自己写描述符；`enet_desc_t.tx_desc_list_cur` **严格顺序**提交 | `Eth_ProvideTxBuffer` 只发放 `TxNext` 游标指向的缓冲（单队列按序），拿不到返回 `BUFREQ_E_BUSY`（§49.9.12） |
| 可替换性 | 换芯片/换 SDK 版本要改调用点 | 上层只依赖 SWS API，换芯片只改 `*_PBcfg.c` |
| 适用 | 裸机直调、快速原型、要 PTP/TSN | AUTOSAR 集成、教学与迁移参考、需要"可替换"的场合 |

表 49-26 与两家量产 MCAL 的 Eth 部分对照（口径同第 50 章：公开文档 + 本地安装包，不估算）

| 维度 | Infineon TC4x MCAL 2.20.0 | NXP S32K3 RTD 6.0.0 | 本实现 |
|---|---|---|---|
| 模块划分 | Eth / EthTrcv / EthIf / EthSwt（含交换机） | Eth / EthTrcv / EthIf | Eth / EthTrcv（EthIf 只有桩） |
| SWS 覆盖 | 全量 + TSN/时间戳 | 全量 | Eth 核心 20 个 API、EthTrcv 17 个（TSN/MacSec 为桩） |
| 配置方式 | EB tresos 图形化生成 | S32DS + RTD 配置工具 | 手写 `Eth_Cfg.h` + `Eth_PBcfg.c` |
| 缓冲区/队列 | 多队列 + 描述符环，支持 VLAN/优先级 | 多队列 | 单 TX/RX 环（8/16 个缓冲），无队列调度 |
| 中断 | 多中断源（RX/TX/错误/唤醒） | 多中断源 | RX + 错误，TX 走轮询确认 |
| PHY 支持 | 多家 PHY + 交换机 PHY | 多家 PHY | YT8011AN（1000BASE-T1）为主，MDIO 通用 |
| 安全/认证 | ASIL-D 安全手册、MISRA 报告 | 安全手册/认证包 | 无（仅文档讨论，见第 50 章） |
| 代码规模 | 数万行（含生成代码） | 数万行 | Eth+EthTrcv 约 2 900 行 |
| 典型用途 | 量产车规项目 | 量产车规项目 | 学习/迁移参考、协议栈适配验证 |

**与 SWS 的偏差（有意为之）**：SWS 只规定 `Eth_ProvideTxBuffer` "拿到一个可用缓冲"或返回
`BUFREQ_E_BUSY`（SWS_Eth_00079），并没有要求"下标最小"。本实现改成"只发放 `TxNext` 指向的
那一个"——因为 TX DMA 只能沿环顺序前进，跨过它去捡后面的空闲缓冲会把环锁死（§49.9.12）。
代价是"后面有空闲也可能拿不到"，收益是 TX 环永不积压；这一取舍的证据见 §49.8.10 表 49-29。

一句话：**SDK 驱动是"把 IP 用起来"，MCAL 是"把 IP 变成可被上层替换的标准化服务"**。本书两套都保留：
第 12/13 章用 SDK 驱动跑出以太网性能基线（下行 935 Mbit/s / 上行 466 Mbit/s），第 49 章用 MCAL 证明
同一条链路可以按 AUTOSAR 的方式被 lwIP 与 FreeRTOS 使用（§49.8）。

## 49.11 参考来源

1. AUTOSAR CP R25-11《SWS Ethernet Driver》（Document ID 430）、《SWS Ethernet Transceiver Driver》
   （Document ID 431）、《SWS Ethernet Interface》（分层参考）：
   <https://www.autosar.org/standards/classic-platform/>
   本地抽取文本 `autosar_txt/AUTOSAR_CP_SWS_Ethernet*.txt`，PDF 在 AUTOSAR 官网。
2. HPM5E31 参考手册 ENET 章节（DMA 描述符 TDES/RDES、MAC 配置、MDIO 帧、INTR_STATUS/INT_EN）；
   SDK 驱动 `hpm_sdk/drivers/src/hpm_enet_drv.c`、`hpm_sdk/drivers/inc/hpm_enet_drv.h`、
   `soc/HPM5E00/HPM5E31/hpm_enet_soc_drv.h`。
3. YT8011AN 数据手册（Motorcomm/裕太微）：<https://www.motor-comm.com/>；板级原理图
   `hardware/hpm5e31_1_refsch.pdf`；本仓库第 12 章（RGMII 延迟与三套寄存器空间）。
4. lwIP 2.1.2：<https://savannah.nongnu.org/projects/lwip/>；FreeRTOS-Kernel：
   <https://github.com/FreeRTOS/FreeRTOS-Kernel>。
5. HPM SDK v1.13.0：<https://github.com/hpmicro/hpm_sdk>；先楫半导体：<https://www.hpmicro.com/>。
6. Infineon AURIX TC4x MCAL / iLLD：<https://github.com/Infineon/illd_release_tc4x>、
   <https://www.infineon.com/>；NXP S32K3 标准软件（RTD）：
   <https://www.nxp.com/design/software/development-software/s32k3-standard-software>。
7. 本书第 12 章（YT8011AN RGMII）、第 13 章（lwIP 与 iperf 基线）、第 42~50 章（MCAL 体系与合规性）。

## 49.12 待确认项与风险

表 49-28 待确认项（三态：已实测 / 已定位 / 已知限制）

| # | 项 | 状态 | 说明与复跑命令 |
|---|---|---|---|
| 1 | lwIP 侧 TCP 收发异常 | **已修复（R8 实测）** | 根因三层：①`Eth_ProvideTxBuffer` 按"下标最小空闲"发放，与 TX DMA 的环顺序错位 → 整环积压后一次性喷出（§49.9.12，主坑）；② ACK 在 `netif->input()` 里同步产生，而 `Eth_TxConfirmation` 只在主循环跑 → 8 个 TX 缓冲被ACK 占满、后续 ACK 被丢（§49.9.13）；③ RX drain 不设上限 → 一次 `app_lwip_eth_poll()` 连收 2.4 s，TCP 定时器被饿死（§49.9.14）。修复后：主机→板 TCP **251~253 Mbit/s**（5 轮不塌）、自证 ping 5/5、`eth ping 20` 19/20；UDP 上行 92.02→**375.10 Mbit/s**（主机实收 365、丢 0.087%）；`eth_raw_echo` TX burst 64 B 219 370→242 924 fps；板→主机 TCP 4.45~261 Mbit/s 双峰，剩余项见 #12/#13。A/B 四组见 §49.8.10 表 49-29、图 49-8/49-9。复跑：`bash scripts/hwrun.sh mcal/eth_lwip --secs 27 --no-build --send "eth iserver tcp"`，主机侧同时 `iperf -c 192.168.2.100 -t 6 -i 1` |
| 2 | DMA_STATUS.TU 发送下溢 | 已定位 | 见 50.9.10；拟调大 PBL 或减少连续提交 |
| 3 | R25-11 无 Eth_ControllerInit | 已实测 | SWS 变更历史：功能并入 Eth_Init；与 CP4.4 的差异见 50.10 |
| 4 | Eth_SetControllerMode 在 SWS 标为异步 | 已知限制 | 本实现同步完成（调用返回时模式已切换），文档标注 |
| 5 | Eth_Receive 不返回数据 | 已实测 | R25-11 语义；用扩展 Eth_GetRxBuffer 补零拷贝路径 |
| 6 | MACsec / TSN（PHC、时间戳、PPS）未实现 | 已知限制 | 本板无 MACsec；TSN 需另接 EthTSyn。相关 API 返回 E_NOT_OK |
| 7 | Eth_ImmediateTransmit / 入口队列处理函数（DRAFT） | 已知限制 | 单队列实现不做；上行数据通道用 GetRxBuffer |
| 8 | VLAN 帧上限 | 已知限制 | ProvideTxBuffer 授予 1500 B 净荷，1518 B VLAN 帧会被 OVFL 拒；lwIP MTU 1500 不触发 |
| 9 | EthTrcv 唤醒/线缆诊断/测试模式 | 已知限制 | YT8011AN 无对应能力或未公开；DET 报 ETHTRCV_E_NOT_SUPPORTED |
| 10 | ETHTRCV_E_NOT_SUPPORTED 的数值 | 已知限制 | SWS 正文引用该错误码但错误码表未给值，本实现取 0x05 |
| 11 | 单控制器（CtrlIdx 恒 0） | 已知限制 | 本板只有 ENET0；PBcfg 用 CtrlCount 表达，多口扩展只需加配置 |
| 12 | 发送方向约 2% 的线上误码（板→主机） | 已定位（未闭环） | 主机网卡 `rx_crc_errors` 在一轮 240 Mbit/s 的板→主机打流里增 991~5 828（1.6~5%）；板端 `TDES0.ES/UF/LC` 全 0、反方向板端 `DropPktCrc` 恒 0，说明误码在 MAC 之后（RGMII/YT8011AN/转换盒），不在描述符逻辑里。见 §49.9.16；下一步需示波器量 RGMII TX 时序/眼图，或换转换盒复测。复跑命令见 §49.9.16 |
| 13 | 板→主机 TCP 吞吐双峰（~170 或 ~5 Mbit/s） | 已定位 | 由 #12 的丢段触发 RTO 风暴：lwiperf 的 TCP 客户端"发一批 → 等 `tcp_sent` 回调 → 再发"，踩中丢包就退化成"每秒 4 个 MSS"（实测主机侧 5.7 KB/s）。主机→板方向是服务端、由 ACK/窗口驱动，不受影响，稳定 251~253 Mbit/s。复跑：主机起 `iperf -s`，板端 `eth iclient 192.168.2.1 tcp`（§49.8.10 表 49-30） |
| 12 | 无外部错帧注入器 | 已知限制 | 错 FCS/对齐错误帧无法主动注入，错误计数路径只做了逻辑验证 |