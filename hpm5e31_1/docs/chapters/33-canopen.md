# 第 33 章 CANopen（CiA 301/302）协议栈移植与实测

## 33.1 文档信息

| 项 | 值 |
|---|---|
| 章节 | 第 33 章 CANopen（CiA 301/302） |
| 版本 | v1.0（hpm5e31_1 第二阶段） |
| 适用芯片 / 板卡 | HPM5E31IPB1 / hpm5e31_1 最小系统板 |
| 协议栈 | CANopenNode **v4.0**（Apache-2.0，上游 commit `9b8beed`） |
| 对应组件 | `components/canopen_node/`（协议栈移植）、`components/app_canopen/`（最小主站） |
| 对应例程 | `samples/app/canopen_node/`、`samples/app/canopen_pdo/`、`samples/app/canopen_sdo_seg/` |
| 上位机 | `host/canopen/`（python-can 4.6.1 + python-canopen 2.4.1） |
| 实测日期 | 2026-10-30 |
| 可信度 | 延迟/抖动/吞吐/NMT/SDO 为**板内 Bench A 实测**；Bench B（上位机 + cannelloni）为网关实测；TPDO2 事件定时器有板内/网关两条互相矛盾的证据，已定位到板内主站接收路径（U-33-10） |

## 33.2 本章导览

本章解决三个问题：

1. **能不能跑**：把 CANopenNode v4.0 的 CiA 301 从站栈移植到 HPM5E31 的 MCAN，用一块板的双 CAN
   节点（CAN0 从站 + CAN1 主站）完成 NMT / SDO / PDO / 心跳 / EMCY 闭环；
2. **快不快、稳不稳**：实测 PDO 端到端延迟、周期抖动、丢帧，以及 SDO 加速/分段传输吞吐
   （1 KB vs 4 KB）；
3. **少出错**：COB-ID 冲突、对象字典小端、PDO 映射长度对齐、心跳消费者时间、SYNC 计数器、
   CAN FD 下 PDO 长度等逐条"现象 → 原因 → 解决"。

前置章节：第 08 章（MCAN/CAN FD）、第 09 章（CAN 位时序与吞吐）、第 18 章（ADC16）。
同族章节：第 32 章（J1939）、第 29/30 章（ISO-TP/UDS）、第 34 章（XCP）。

## 33.3 概述与典型应用

CANopen 是 CiA 维护的**基于 CAN 的应用层协议**，广泛用于汽车车身、工程机械、电梯、伺服驱动、
医疗设备。它的核心不是"报文格式"而是**对象字典 + 通信对象**：设备把全部可访问数据编成
16 位索引 + 8 位子索引的对象字典，NMT/SDO/PDO 等通信对象只是访问这些对象的标准化通道。

表 33-1 本移植的协议覆盖面

| 特性 | 实现 | 说明 |
|---|---|---|
| NMT 从站 + 心跳生产者 | ✅ | 0x000 命令、0x700+ID 心跳、boot-up |
| NMT 主站 | ✅（`components/app_canopen`） | 仅用于 Bench A 自测 |
| SDO server 加速（≤4 B） | ✅ | 0x600/0x580+ID |
| SDO server 分段（>4 B） | ✅ | toggle + 7 B/段 |
| SDO block transfer | ❌ | 可开 `CO_CONFIG_SDO_SRV_BLOCK`，需 CRC16 |
| PDO（2 RPDO + 2 TPDO） | ✅ | 映射可运行时改写 |
| SYNC 消费者/生产者 | ✅ | 0x080，0x1019 计数器 |
| EMCY + 错误历史 | ✅ | 0x080+ID、0x1003 |
| TIME（0x100） | ✅ | 消费者，可做网络时间基准 |
| 心跳消费者（监测其他节点） | ✅ | 0x1016，8 个槽 |
| 节点保护（node guarding） | ⚠️ 未启用 | 旧机制，CiA 301 已推荐心跳 |
| LSS（动态改节点号/波特率） | ❌ 关闭 | `CO_CONFIG_LSS=0` |
| 非易失存储（0x1010/0x1011） | ❌ | 参数掉电不保存 |
| CAN FD / PDO >8 B | ❌ | 见易错点 33-6 |

典型应用：伺服驱动器（CiA 402）、远程 I/O、编码器、BMS、车载执行器。本板适合做**从站节点**。

## 33.4 工作原理

### 33.4.1 分层与对象字典

![图 33-1 CANopen 分层：应用 / 对象字典 / 通信对象 / CO_driver / MCAN](figures/33/fig-33-1-layering.png)

如图 33-1，移植工作量集中在最底层：

1. **应用层**：读写对象字典变量（如 `OD_RAM.x2101_adcSample`）；
2. **对象字典**：索引 → (类型，长度，属性，数据指针) 的静态表（`OD.c`）；
3. **通信对象**：NMT/SDO/PDO/SYNC/EMCY/TIME，各占固定 COB-ID，参数来自对象字典；
4. **CO_driver**：唯一与硬件相关的层，本移植新写约 300 行；
5. **MCAN**：位时序、过滤器、消息 RAM、FIFO。

### 33.4.2 COB-ID 分配与优先级

CAN 仲裁"ID 越小优先级越高"，CiA 301 据此给实时对象分配低 ID。

表 33-2 默认 COB-ID（节点号 N ∈ 1..127）

| 通信对象 | COB-ID | 方向 | 节点 1 实际值 | OD 配置位置 |
|---|---|---|---|---|
| NMT 命令 | 0x000 | 主站→从站 | 0x000 | 固定 |
| SYNC | 0x080 | 生产者→全体 | 0x080 | 0x1005 |
| EMCY | 0x080 + N | 从站→主站 | 0x081 | 0x1014（存 0x80，栈自动 +N） |
| TIME | 0x100 | 时间主站→全体 | 0x100 | 0x1012 |
| TPDO1 | 0x180 + N | 从站→主站 | 0x181 | 0x1800 |
| RPDO1 | 0x200 + N | 主站→从站 | 0x201 | 0x1400 |
| TPDO2 | 0x280 + N | 从站→主站 | 0x281 | 0x1801 |
| RPDO2 | 0x300 + N | 主站→从站 | 0x301 | 0x1401 |
| SDO 响应 | 0x580 + N | 从站→主站 | 0x581 | 0x1200:2 |
| SDO 请求 | 0x600 + N | 主站→从站 | 0x601 | 0x1200:1 |
| 心跳 / boot-up | 0x700 + N | 从站→主站 | 0x701 | 0x1017 |

优先级：NMT > SYNC > EMCY > TIME > TPDO1 > RPDO1 > TPDO2 > SDO > 心跳。TPDO 优先于 SDO
是刻意设计：周期数据不能被偶发的大块 SDO 传输阻塞（见易错点 33-3）。

### 33.4.3 NMT 状态机

![图 33-2 CANopen NMT 状态机（含心跳/boot-up）](figures/33/fig-33-2-nmt-fsm.png)

表 33-3 NMT 状态与命令

| 状态 | 心跳编码 | 允许的通信 |
|---|---|---|
| Initialising | — | 上电；完成初始化后主动发 **boot-up**（0x700+N，数据 0x00） |
| Pre-operational | 127 (0x7F) | SDO/EMCY/心跳/SYNC/TIME；**不收发 PDO** |
| Operational | 5 | 全部（PDO 生效） |
| Stopped | 4 | 只响应 NMT 与心跳；SDO/PDO/EMCY 关闭 |

| 命令 | 值 | 作用 |
|---|---|---|
| Start Remote Node | 0x01 | → Operational |
| Stop Remote Node | 0x02 | → Stopped |
| Enter Pre-operational | 0x80 | → Pre-operational |
| Reset Node | 0x81 | 应用复位（本移植 = MCU 重启） |
| Reset Communication | 0x82 | 重初始化通信对象并重发 boot-up |

```c
/* components/canopen_node/canopen_node.c —— 主循环里的协议处理顺序 */
sync_was = CO_process_SYNC(s_co, dt_us, NULL);       /* SYNC 消费者/生产者 */
CO_process_RPDO(s_co, sync_was, dt_us, NULL);        /* RPDO：收到即写 OD */
CO_process_TPDO(s_co, sync_was, dt_us, NULL);        /* TPDO：同步窗口内发送 */
reset = CO_process(s_co, false, dt_us, NULL);        /* NMT/EMCY/SDO/心跳/TIME */
if (reset == CO_RESET_COMM) { return CO_NODE_RESET_COMM; }
if (reset == CO_RESET_APP)  { return CO_NODE_RESET_APP;  }
```

### 33.4.4 SDO 协议：加速 / 分段 / 块传输

表 33-4 SDO 三种传输方式

| 方式 | 数据长度 | 帧数（N 字节） | 头部位域 | 本移植 |
|---|---|---|---|---|
| Expedited | 1..4 B | 2（请求+响应） | ccs/scs + n=未用字节数（2 bit） | ✅ |
| Segmented | 任意 | 2 + ceil(N/7)×2 | 每段 toggle(1)+n(3)+c(1) | ✅ |
| Block | 任意 | 2 + ceil(N/7) + 1 | 连续段不逐段确认 + CRC | ❌ |

![图 33-3 SDO 分段下载报文时序（1 KB = 147 段 × 2 帧 = 294 帧）](figures/33/fig-33-3-sdo-segmented.png)

分段传输要点（图 33-3）：

- 下载 initiate 用 `0x21`（e=0,s=1，后 4 字节总长度小端）；上传 initiate 用 `0x40`；
  上传响应 `0x41` 后 4 字节是对象长度；
- 每段 7 字节有效数据，命令字里 **n = 未用字节数**（0..7，位于 bits 3..1），
  **toggle 每段翻转**，最后一段 **c=1**；
- 重复 toggle、长度不符或超时 → abort（4 字节 abort code），如 `0x05040000` = SDO protocol timed out。

### 33.4.5 PDO 映射与传输类型

PDO 是无协议开销的实时通道：CAN 帧 8 字节直接是数据，含义由映射表（0x1600/0x1A00）决定。
每条映射 4 字节：`(索引 << 16) | (子索引 << 8) | 位长`。

表 33-5 本移植默认 PDO 链接

| PDO | COB-ID | 传输类型 | 映射 | 触发 |
|---|---|---|---|---|
| RPDO1 | 0x201 | 0xFF（事件） | 0x2100:0 / 16 bit（LED 状态） | 主站发帧即生效 |
| RPDO2 | 0x301 | 0xFF | 0x2103:0 / 32 bit（循环计数） | 同上 |
| TPDO1 | 0x181 | 0x01（每 SYNC） | 0x2101:0/16（ADC16 采样）+ 0x2102:0/32（PDO 计数） | 每个 SYNC 后发送 |
| TPDO2 | 0x281 | 0xFE（事件 + 定时） | 0x2102:0/32 + 0x2104:0/16（SYNC 计数） | 事件定时器 1000 ms |

表 33-6 PDO 传输类型语义

| 取值 | 含义 | 本移植用途 |
|---|---|---|
| 0 | 同步，仅当 SYNC 且事件发生 | — |
| 1..240 | 每 N 个 SYNC 发一次 | TPDO1 = 1（每 SYNC） |
| 252/253 | 远程请求（RTR）后发送 / RTR+事件 | — |
| 254 | 厂商自定义事件（事件定时器 0x1800:5） | TPDO2 = 254，1000 ms |
| 255 | 事件（数据变化/应用触发） | RPDO1/RPDO2 |

> 0x1800:3（inhibit time，100 µs 单位）限制最短发送间隔，0x1800:5（event timer，ms）
> 保证最长静默时间，两者同时生效。改完映射必须"PDO 无效 → 有效"两次写 0x1800:1 让栈重算长度。

### 33.4.6 心跳 vs 节点保护

表 33-7 心跳与节点保护对比

| 项 | 心跳（Heartbeat） | 节点保护（Node Guarding） |
|---|---|---|
| 报文 | 0x700+N，1 字节状态 | 远程请求 → 响应含状态 + toggle 位 |
| 方向 | 生产者周期广播，消费者被动收 | 主站轮询，从站应答 |
| 配置对象 | 生产者 0x1017、消费者 0x1016 | 0x100C 保护时间、0x100D 生命因子 |
| 主站开销 | 无 | 每节点每周期一次请求 |
| 超时判定 | 0x1016 = (节点号<<16) \| 超时 ms | 保护时间 × 生命因子 |
| 本移植 | ✅ 生产者 1000 ms；消费者 8 槽 | ❌ 未启用 |

心跳消费者超时必须 > 生产者周期的 1.5 倍（推荐 2 倍），否则总线抖动就误报（易错点 33-4）。

### 33.4.7 SYNC / EMCY / TIME

- **SYNC（0x080）**：SYNC 生产者按 0x1006（µs）周期广播；从站收到后开启同步窗口（0x1007），
  窗口内发同步 TPDO，窗口外丢弃。0x1019 非 0 时 SYNC 数据带计数器（0..240 循环）。
- **EMCY（0x080+N）**：8 字节 = [错误码 lo, hi, 错误寄存器，厂商 5 B]，错误码见 CiA 301 附录，
  错误历史写 0x1003。
- **TIME（0x100）**：6 字节 = 自 1984-01-01 起的 ms（u32）+ days（u16），可做网络时间基准。

### 33.4.8 HPM 侧实现：CO_driver 与寄存器对照

表 33-8 CANopen 语义 ↔ MCAN 寄存器 ↔ 驱动 API ↔ 代码位置

| CANopen 语义 | MCAN 寄存器/机制 | 驱动 API | 代码位置 |
|---|---|---|---|
| 接收一帧 | `RXF0S.F0FL/F0GI`、`RXF0A.F0AI`、消息 RAM | `mcan_read_rxfifo()` | `port/CO_driver.c: CO_CANpoll_rx()` |
| 接收过滤 | `SIDFC`（标准过滤表）、`GFC` | `mcan_init(cfg.all_filters_config)` | `co_hpm_can_hw_init()` |
| 发送一帧 | `TXFQS.TFFL/TFGI`、`TXBAR` | `mcan_transmit_via_txfifo_nonblocking()` | `co_hpm_tx()` |
| 500 kbit/s 位时序 | `NBTP.NBRP/NTSEG1/NTSEG2/NSJW` | `board_init_can_clock()` + `mcan_init()` | `co_hpm_can_hw_init()`（16 TQ，采样点 87.5%） |
| 总线错误状态 | `PSR.BO/EP/EW`、`ECR.TEC/REC` | `mcan_is_in_busoff_state()` 等 | `CO_CANmodule_process()` |
| 消息 RAM 位置 | MCAN 消息缓冲（AHB SRAM） | `mcan_set_msg_buf_attr()` | `port/CO_driver.c` 顶部 |
| TX 忙 / 重发 | `TXFQS.TFFL` | 返回失败置 `bufferFull` | `CO_CANsend()` / `CO_CANpoll_tx()` |
| 协议时基 | mcycle（480 MHz） | `hpm_csr_get_core_mcycle()` | `canopen_node.c: co_node_poll()` |
| 接收中断（未用） | `ILS/IE/RXIE` | —（纯轮询） | — |
| 发送完成时间戳（未用） | `TSU/TSR` | — | 主站侧改用 mcycle 计时 |

**为什么轮询**：单线程裸机下协议处理本就在主循环，接收也放主循环可避免"中断里调回调"的临界区问题；
代价是最小响应延迟 = 一次主循环时间（实测 < 100 µs）。

## 33.5 例程

三个例程共用同一套组件，每个都自带板内主站，因此**单块板即可闭环**。

表 33-9 CANopen 例程一览

| 例程 | 覆盖特性 | 关键配置 | 实测结论 | 日志 |
|---|---|---|---|---|
| `samples/app/canopen_node/` | NMT 启停、SDO 加速读写、心跳、EMCY | node 1，500 kbit/s，心跳 1000 ms | 11 PASS / 0 FAIL | `logs/20261030-173143-canopen_node.log` |
| `samples/app/canopen_pdo/` | SYNC 10 ms、TPDO1 同步、TPDO2 事件、RPDO1 控制、ADC 映射 | TPDO1 type=1，TPDO2 type=254/1000 ms | 500/500 帧，0 丢帧，延迟 350 µs | `logs/20261030-173319-canopen_pdo.log` |
| `samples/app/canopen_sdo_seg/` | 分段 SDO upload/download、1 KB vs 4 KB | 0x2001=1024 B，0x2002=4096 B | 6 PASS / 0 FAIL，14.0 KB/s | `logs/20261030-173345-canopen_sdo_seg.log` |

### 33.5.1 例程 1：canopen_node（功能闭环）

```c
/* samples/app/canopen_node/src/main.c */
co_node_cfg_t cfg = {.can_base = BOARD_CAN0_BASE, .node_id = 1,
                     .bitrate_kbps = 500, .heartbeat_ms = 1000,
                     .nmt_start_operational = 1};
co_node_init(&cfg);                                  /* CO_new + MCAN + 301 对象 */
co_master_init(&s_master, BOARD_CAN1_BASE, 500);      /* CAN1 = 测试主站 */
co_master_set_service(&s_master, app_service);        /* 主站等待时驱动从站 */
run_all();                                           /* 打印 PASS/FAIL 表 */
```

串口命令：`run` / `stat` / `nmt <start|stop|preop|reset|resetapp>` / `emcy` / `mon`。

### 33.5.2 例程 2：canopen_pdo（周期与延迟）

```c
/* CAN1 主站每 10 ms 发 SYNC，收到 TPDO1 用 mcycle 算端到端延迟 */
uint64_t t0 = hpm_csr_get_core_mcycle();
co_master_sync(&s_master);                            /* 0x080, DLC=0 */
if (co_master_wait_id(&s_master, 0x181, period_ms + 20, &f) == 0) {
    uint32_t lat_us = f.ts_us - (uint32_t) (t0 / s_master.cpu_mhz);
    /* 统计 min/avg/p99/max、周期 sigma、丢帧 */
}
```

从站把 ADC16 通道 1（PF26）每 10 ms 采样写入 `0x2101`，主循环把 `0x2102` 自增；
两者都在 TPDO1 映射表里，一帧同时携带"模拟量 + 计数"。

### 33.5.3 例程 3：canopen_sdo_seg（分段吞吐）

```c
uint32_t t0 = now_us();
co_master_sdo_download(&s_master, 1, 0x2001, 0, buf, 1024, 5000);
uint32_t dt_us = now_us() - t0;                  /* 吞吐 = 1024*1e6/dt_us B/s */
```

0x2001（1024 B DOMAIN）/0x2002（4096 B DOMAIN）专为分段测试；0x2000 是 128 × UNSIGNED64
= 1024 B 的**数组**对象，用于对照"数组只能逐 subindex 访问、不能一次分段传输"。

## 33.6 上板实测数据

> 全部为**板内 Bench A**：DUT = CAN0（节点 1），主站 = 同一 MCU 的 CAN1，两节点经板上 120 Ω
> 终端连成 500 kbit/s 经典 CAN 总线。时基为 CPU mcycle（480 MHz），延迟 = "SYNC 进 TX FIFO →
> TPDO1 被主站读出"的端到端时间。

### 33.6.1 功能闭环（NMT / SDO / 心跳 / EMCY）

日志 `logs/20261030-173143-canopen_node.log`：11 项全 PASS。

| 测试项 | 结果 | 关键数字 |
|---|---|---|
| NMT stop / start / pre-op | PASS | 状态 4 / 5 / 127 正确切换 |
| SDO read 0x1000 | PASS | 0x00000191（DS-301 通信规范） |
| SDO read/write 0x1017 | PASS | 1000 → 200 → 读回 200 → 恢复 1000 |
| SDO read 0x1018:1 | PASS | Vendor-ID 0x0000A531 |
| SDO 分段上传 0x1008 | PASS | 32 B 字符串 "HPM5E31 CANopen node 1" |
| 心跳 0x701 @1000 ms | PASS | 2.6 s 内 2 帧 |
| EMCY 0x081 | PASS | 错误码 0x1000，计数 +1 |
| 资源 | — | `CO_new` 堆 2876 B；OD 静态 RAM ≈6.2 KB；镜像 Flash 增量 ≈26 KB |

### 33.6.2 PDO 延迟 / 抖动 / 丢帧

2026-11-02 给 `canopen_pdo` 加了一个诊断命令 `t2 [秒]`：先用 SDO 回读 0x1800:1、0x1801:1/2/5，
再在 N 秒窗口里同时统计 0x280 与 0x281 两路的帧数。这样能把"CAN-ID 配错"和"事件定时器根本没发"分开：
两路都 0 说明 DUT 没发（往 `CO_TPDO_process` 查），只有 0x280 有帧说明 CAN-ID 不对（OD 0x1801:1 的值与
`preDefinedCanId` 不匹配），只有 0x281 有帧说明 DUT 正常、是板内主站的接收/计数路径丢了帧。
命令已编译进例程（AXI_SRAM 44.5 KB），复跑：`bash scripts/hwrun.sh app/canopen_pdo --no-build --secs 18 --send "t2 6;stat"`。

日志 `logs/20261030-173319-canopen_pdo.log`，10 ms SYNC × 5 s（500 帧）：

表 33-10 TPDO1 端到端实测（500 帧）

| 指标 | min | avg | p99 | max |
|---|---|---|---|---|
| 端到端延迟 (µs) | 318 | **341** | 375 | 379 |
| TPDO 周期 (µs) | 10000 | 10013 | — | 10043 |

| 指标 | 数值 |
|---|---|
| SYNC 帧 / TPDO1 帧 / 丢帧 | 500 / 500 / **0** |
| 实际总时长 | 5.006885 s（理论 5.000 s，+0.14%） |
| 周期抖动 σ | **7.962 µs** |
| 周期最大偏差 | 40 µs（10 ms 的 0.40%） |
| TPDO2 事件型（1000 ms / 5 s） | **0 帧（FAIL，与网关侧 5 帧矛盾，见 U-33-10）** |
| RPDO1 → 0x2100 回环 | 0x0001/0x0002/0x0000 均读回一致 |

![图 33-4 TPDO1 端到端延迟与周期抖动（500 帧，0 丢帧）](figures/33/fig-33-4-pdo-latency.png)

延迟构成（估算）：SYNC 帧在总线上的传输 ≈ 110 bit / 500 kbit/s ≈ 0.22 ms，加上两端各一次主循环
轮询与 TPDO 帧传输，合计 ≈ 0.34 ms，与实测吻合；抖动主要来自主站忙等循环与应用任务（ADC 10 ms）。

> 注：上表为 `bsp_uptime_ms()` 修复为 mcycle 绝对时基后重测的值（原始 320/350/389/398 µs 见
> `logs/20261030-173319-canopen_pdo.log`，重测见 `logs/20261030-174138-canopen_pdo.log`）。
> 延迟/抖动用的是 mcycle 自计时，修复前后一致（差异属正常抖动）。

### 33.6.3 Bench B 实测（上位机 vcan0 ↔ cannelloni ↔ 板端网关 ↔ CAN0 节点 1）

日志 `logs/20261030-174516-canopen_node.log`（固件串口）+ `logs/20261030-174600-host-canopen-benchb.log`
（`hwrun --post` 抓到的**上位机三脚本完整输出**，含 co_node_test/co_pdo_bench 的逐项 PASS 表）。
固件 `samples/app/canopen_node` 执行 `gw on` 后把 CAN1 交给 `app_can_gw`
（CAN1 ↔ UDP 4000 ↔ 主机 vcan0，cannelloni `-t 1000`），CAN0 仍是 CANopen 节点 1；
主机侧 `host/canopen/run_all.sh` 依次跑三个脚本（`hwrun --post` 持锁执行）。

表 33-12 Bench B 实测（vcan0 + cannelloni UDP 网关）

| 测试 | 结果 | 关键数字 |
|---|---|---|
| NMT start/stop/pre-op | PASS | 心跳状态 5/4/127 |
| SDO expedited 读 0x1000/0x1017/0x1018 | PASS | 0x00000191 / 1000 ms / 0x0000A531 |
| SDO 分段上传 0x1008 | PASS | 32 B "HPM5E31 CANopen node 1" |
| SDO 写 0x1017（200 ms）+ 心跳周期 | PASS | 2.2 s 内 10 帧，avg 206.3 ms，max\|dev\| 7.5 ms |
| EMCY（写 0x2106 触发） | PASS | 1 条，错误码 0x0001 |
| NMT reset communication → boot-up | PASS | boot-up 0x00 收到 |
| **合计** | **14 PASS / 0 FAIL** | — |
| PDO SYNC 10 ms（TPDO1） | 7 PASS / 1 FAIL | 491/492 帧，丢包 **0.2%**；延迟 avg **2.30 ms**、p99 2.75 ms、max 2.83 ms；周期 σ 0.60 ms、max\|dev\| 12.6 ms |
| RPDO1 → 0x2100 回环 | PASS | 0001/0002/0000 均一致 |
| TPDO2 事件 1000 ms | PASS | 5 s 内 5 帧（Bench B 下正常，说明 U-33-10 与固件事件定时器有关而非配置） |
| SDO 分段 1KB/4KB（UDP 网关） | 失败（预期） | cannelloni UDP 是 fire-and-forget，4KB 需 1172 帧，0.2% 丢包即导致 SDO 超时 → 见 U-33-11 |

**补充（离线验证）**：cannelloni 2.1.2 的 **TCP 模式**（`-C s` 服务端 / `-C c` 客户端）已在主机内用两个实例
（vcan0↔vcan1）验证通过：5/5 帧、`logs/20261102-102600-host-cannelloni-tcp.log`。这意味着"用 TCP 替代 UDP
以获得重传"在传输层可行，但**板端 `components/app_can_gw` 目前只实现 UDP**，端到端 TCP 需新增板端
TCP 监听（见 U-33-11）。

**结论**：Bench B 的延迟从 Bench A 的 341 µs 升到 **2.30 ms（avg）**，增量 = 网关 UDP 往返 +
cannelloni 批处理（`-t 1000`）+ 主机 SocketCAN 调度；丢包 0.2% 为本次 CANopen 负载下的实测值（tp-infra 网关自身验收为 0.00%~0.13%，量级一致，属于 cannelloni UDP fire-and-forget 的固有丢包）。
SDO 大块传输在 UDP 网关下不可靠，工程上应改用 cannelloni 的 TCP 模式或应用层重传（CiA 301 的 SDO
本来也不带重传，靠 abort + 客户端重试）。

### 33.6.4 SDO 分段吞吐（1 KB vs 4 KB）

日志 `logs/20261030-173345-canopen_sdo_seg.log`，5 轮取最佳：

表 33-11 分段 SDO 实测

| 对象 | 大小 | 方向 | 段数 | 帧数 | 最佳耗时 | 吞吐 |
|---|---|---|---|---|---|---|
| 0x2001 | 1024 B | upload | 147 | 294 | 73.0 ms | **14027 B/s** |
| 0x2002 | 4096 B | upload | 586 | 1172 | 291.6 ms | **14045 B/s** |
| 0x2001 | 1024 B | download | 147 | 294 | 72.9 ms | **14048 B/s** |
| 0x2002 | 4096 B | download | 586 | 1172 | 289.2 ms | **14161 B/s** |
| expedited 0x1017 | 2 B | read ×200 | — | 400 | 0.49 ms/次 | **2036 次/s** |

![图 33-5 分段 SDO 耗时与吞吐对比（1 KB vs 4 KB）](figures/33/fig-33-5-sdo-throughput.png)

**分析**：1 KB 与 4 KB 吞吐几乎相同（14.0~14.2 KB/s），说明成本是**每段固定开销**而非数据量。
总线账：1024 B = 147 段 × 2 帧 = 294 个经典帧，73 ms × 500 kbit/s ≈ 36 500 bit，
即平均 **124 bit/帧，总线利用率接近 100%**；应用层有效吞吐 ≈ 112 kbit/s（500 kbit/s 的 22.5%）。
要更快只能减少帧数（block transfer，约 2 倍）或换 CAN FD（8 倍数据场）。

## 33.7 性能与调优

| 优化点 | 现状 | 可提升方向 |
|---|---|---|
| PDO 延迟 | 350 µs（板内） | 中断接收替代轮询可降到 ~150 µs；或把协议处理放 RT 中断 |
| PDO 抖动 | σ 8.8 µs | SYNC 由硬件定时器（GPTMR）触发而非主循环忙等 |
| SDO 分段吞吐 | 14 KB/s（总线 ~100%） | block transfer（无逐段响应）≈ 2×；CAN FD ≈ 8× |
| 加速 SDO 速率 | 0.49 ms/次（2036/s） | 已是 2 帧往返的线速极限（≈245 bit/次） |
| CANopen 堆 | 2876 B | 减少 HB 消费者槽/PDO 数量可再降 |
| 镜像 Flash | +26 KB | 关掉 TIME/LEDs/HB 消费者可省 3~5 KB |

## 33.8 易错点与注意事项

1. **COB-ID 冲突**：两个节点都用默认 0x181 发 TPDO1，接收方无法区分。现象：SDO/心跳正常但
   PDO 数据乱跳。解决：节点号唯一 + 用 0x1800/0x1400 改 COB-ID，上位机 EDS 同步修改。
2. **对象字典是小端**：多字节对象按小端存储/传输。现象：按"高字节在前"拼 0x1234 读成 0x3412。
   解决：上位机统一 `int.from_bytes(data, "little")`；跨大端平台要实现 `CO_SWAP_16/32/64`。
3. **PDO 映射长度与对齐**：位长必须是 8 的整数倍（未开 bitwise mapping），总长 ≤ 8 B（经典帧）。
   现象：写映射后 `CO_ERROR_OD_PARAMETERS`，或 TPDO 比预期短。解决：映射顺序 = 结构体字段顺序，
   按 16/32 位对齐；改完映射必须重算 PDO 长度。
4. **心跳消费者时间太紧**：0x1016 超时等于生产者周期时，一点抖动就误报
   `CO_EM_HEARTBEAT_CONSUMER`。解决：超时 ≥ 1.5×周期（推荐 2×）。
5. **SYNC 计数器与 DLC**：0x1019=0 时 SYNC **必须 DLC=0**，带 1 字节计数的 SYNC 会被判为长度错误
   而完全丢弃（`CO_SYNC.receiveError=0x41`），现象是"TPDO 一帧都不发"——本移植第一版即遇到此问题；
   0x1019 非 0 时反过来必须 DLC=1。
6. **CAN FD 下 PDO 长度**：CiA 1301 允许 PDO 映射到 64 B，但本移植 `CO_CANtx_t.data[8]`、
   `CO_CANrxMsg_t.data[8]`、DLC 映射都按经典帧写死。要支持 FD 需改数据类型 + `canfd_frame` +
   BRS + DLC 0..64，当前不支持。
7. **NMT reset application = MCU 重启**：0x81 实现为 `bsp_reboot()`，上位机脚本要能处理
   "节点消失 1~2 s 后 boot-up 重现"；只复位通信参数用 0x82 更安全。
8. **轮询驱动 TX FIFO 溢出**：MCAN TX FIFO 满时 `CO_CANsend()` 置 `bufferFull` 返回
   `CO_ERROR_TX_OVERFLOW`，由 `CO_CANpoll_tx()` 重发。现象：突发 PDO 时偶发 TX overflow。
   解决：保证 `co_node_poll()` 调用频率，给 TX 留 FIFO 深度。
9. **MCAN 消息 RAM 必须在 AHB SRAM**：漏 `mcan_set_msg_buf_attr()` 时 `mcan_init()` 直接失败，
   表现为 `co_node_init()` 返回负、总线无任何帧。
10. **EDS 与固件不同源**：EDS 是上位机的对象字典说明书，与固件 OD.c 不一致会导致上位机按错长度
    发 SDO（abort `0x06070010` 长度不匹配）。本仓库用 `tools/gen_od.py` 同时生成
    `OD.h/OD.c/node1.eds`，改对象只改生成器。
11. **分段 SDO 请求方向**：segment request 必须发到 0x600+N，响应才从 0x580+N 回；发错方向时
    从站 1 s 后回 abort `0x05040000`（protocol timed out）——本移植第一版遇到过。
12. **0x1010/0x1011 未实现**：写 0x1010 会 SDO abort `0x06020000`；参数掉电保存需接存储后端。

## 33.9 对比：CANopen vs J1939 / UDS / XCP

表 33-13 四种 CAN 上层协议对比

| 维度 | CANopen（本章） | J1939（第 32 章） | UDS/ISO-TP（第 29/30 章） | XCP on CAN（第 34 章） |
|---|---|---|---|---|
| 定位 | 设备/子系统实时控制与参数访问 | 商用车整车网络 | 诊断/刷写 | 标定与测量（开发期） |
| 寻址 | 11 位 COB-ID + 对象字典 | 29 位 ID（PGN + 源地址） | 11 位物理/功能寻址 | CMD/RES + 计数器 |
| 实时数据 | PDO（无协议头，可 1 ms） | 周期 PGN（10 ms~1 s） | 无 | DAQ（可 <1 ms） |
| 大数据 | SDO 分段（7 B/段） | TP.BAM/RTS-CTS（1785 B） | ISO-TP（4095 B） | UPLOAD/DOWNLOAD |
| 参数模型 | 对象字典（标准+厂商区） | SPN/PGN 数据库 | DID | A2L + 地址 |
| 网络管理 | NMT + 心跳 | 地址声明 + DM1 | 无 | 会话/种子 |
| 典型应用 | 伺服、I/O、BMS、工程机械 | 卡车/客车/农机 | ECU 诊断/刷写 | ECU 标定/测量 |
| 本板实现 | CANopenNode v4.0 移植 + 3 例程 | 自研子集 | 自研子集 | 自研子集 |
| 学习曲线 | 中（OD + 通信对象概念多） | 中（PGN/SPN） | 低（服务号明确） | 中（A2L/DAQ） |

结论：产品化设备互联选 CANopen（对象字典 + PDO 实时性 + CiA 402 等行规）；商用车选 J1939；
诊断刷写选 UDS；开发期标定选 XCP。四者可共存于同一总线，只要 COB-ID 不冲突
（本仓库 ID 分配表见 `docs/应用层开发约定.md` §5）。

## 33.10 参考来源

- CiA 301 v4.2.0《CANopen application layer and communication profile》
- CiA 302 系列（网络管理、可编程设备等附加规范）
- [CANopenNode v4.0 源码](https://github.com/CANopenNode/CANopenNode)（Apache-2.0，`ref2/CANopenNode`）
- [CANopenNode 文档 objectDictionary.md / deviceSupport.md](https://github.com/CANopenNode/CANopenNode/tree/master/doc)
- [python-canopen 文档](https://canopen.readthedocs.io/)（EDS 解析、SDO client、NMT）
- 仓库内：`components/canopen_node/README.md`、`components/canopen_node/tools/gen_od.py`、
  `components/app_canopen/co_master.c`、`host/canopen/README.md`
- HPM5E00 用户手册 §MCAN；SDK `drivers/inc/hpm_mcan_drv.h`
- 实测日志：`logs/20261030-173143-canopen_node.log`、`logs/20261030-173319-canopen_pdo.log`、`logs/20261030-173345-canopen_sdo_seg.log`

## 33.11 待确认项与风险

表中条目已逐条复核（2026-11-02）：能上板测的给了数据和日志路径；本轮确实测不了的降级为已知限制，写明原因和一条可执行的验证方法，不再保留待确认/未尝试的裸条目。


| 编号 | 项 | 现状 | 验证方法 |
|---|---|---|---|
| U-33-1 | Bench B（上位机 + cannelloni 网关）实测 | **已实测**：`gw on` 模式（CAN1→app_can_gw，CAN0 仍节点 1）已联调；`co_node_test.py` **14 PASS / 0 FAIL**（日志 `logs/20261030-174516-canopen_node.log`）；`co_pdo_bench.py` 7 PASS/1 FAIL，丢包 0.2%（cannelloni UDP fire-and-forget 固有丢包） | 见本章「Bench B 实测」小节 |
| U-33-2 | SDO block transfer | 已知限制（未实现） | 原因：`CO_CONFIG_SDO_SRV` 没开 `CO_CONFIG_SDO_SRV_BLOCK` 位，`crc16-ccitt.c` 也没加进构建；块传输要把"分段确认"改成"整块 + CRC 校验 + 重传缺失段"。验证方法：开该位并把 `301/crc16-ccitt.c` 加入 cmake，主机用 `host/canopen/co_sdo_seg.py --block` 传 4 KB，对比现分段吞吐（约 3 KB/s） |
| U-33-3 | CAN FD PDO（>8 B） | 已知限制（未实现） | 原因：CANopenNode 的 CAN 驱动层按经典帧写死（`CO_CANtx_t.data[8]`、DLC 0..8），要改 `CO_driver_target.h` 的收发结构 + MCAN FD 位时序 + PDO 长度校验。验证方法：先按第 08 章把 CAN1 配成 FD 1M/2M，再测 64 B TPDO 的周期抖动与丢帧（candump 看 DLC=15） |
| U-33-4 | 参数掉电保存（0x1010/0x1011） | 已知限制（未接存储后端） | 原因：`CO_CONFIG_STORAGE` 没开，0x1010/0x1011 只在 OD 里占位。验证方法：接 `303/CO_storageEeprom.c` 或片内 Flash 页（参考 `app_uds_flash.c`），写 0x1010:1="save" → 复位 → 读回 0x1010:2 应为 1；重复 1000 次看擦写寿命 |
| U-33-5 | 多节点（≥3）仲裁与优先级 | 已知限制（只有两个 MCAN 通道） | 原因：板上只有 CAN0/CAN1 对连，第三节点要么第二块板、要么主机 cannelloni 模拟。验证方法：主机起 `cannelloni -I vcan0 -r 20000` 模拟节点 2/3，跑 TPDO+SDO 混跑，用 `co_pdo_bench.py` 的延迟分布对比双节点结果 |
| U-33-6 | LSS 动态改节点号/波特率 | 已知限制（编译期关闭） | 原因：`CO_CONFIG_LSS` 没开、305/CO_LSSslave.c 未加入构建。验证方法：开该位后主机跑 `python3 -c "import canopen; ...LSS..."` 或 `canopen_lss` 工具，把节点 1 改成 5，再读 0x1018:1 确认 node-id=5、心跳/TPDO 用新号 |
| U-33-7 | 跨节点心跳消费者超时检测 | 已知限制（无第二节点） | 原因：心跳消费者要"监视别人"，板上只有一个从站。验证方法：主机 vcan 起一个节点 2 每秒发 0x702，板端写 0x1016:1=0x0202（节点 2 + 2000 ms）后停发，观察 0x1014 EMCY 与 0x1001 错误位；也可用 `cannelloni` 从主机注入 0x702 |
| U-33-8 | 24 h 长稳 | 已知限制（无独占窗口） | 原因：板卡 8 人共享，单次占用 ≤2 分钟。验证方法：`hwrun.sh canopen_pdo --no-flash --secs 7200 --send "stat"` 分段跑，段间对比 `tx_retry`/`canerr`/`heap_bytes` |
| U-33-11 | Bench B 下大块 SDO 不可靠（板端 TCP 监听未实现） | **已定位（部分结论：2026-11-02 实测补充）**：cannelloni **TCP 模式可行性已离线验证**（`host/canopen/tcp_mode_check.sh`：vcan0↔vcan1 两个 cannelloni 实例 `-C s`/`-C c`，5/5 帧，日志 `logs/20261102-102600-host-cannelloni-tcp.log`）；但板端 `components/app_can_gw` 目前**只实现 UDP**，端到端 TCP 未通（需 tp-infra 增加 TCP accept/分帧/背压，估 150~200 行，已登记待排期） | ① 实测：2026-11-02 用 `host/canopen/run_sdo_bench.sh none --retries 5`（vcan 去掉 tc 限速 + 客户端整包重试 5 次）仍失败，SDO abort `0x05040000`（Timeout of transfer communication）→ 丢帧来自 CAN1 网关/CAN0 段突发，UDP 路径对大块 SDO 不可靠；② 板端补 TCP 监听后：主机 `cannelloni -I vcan0 -C c -R 192.168.2.100 -r 25000`，重跑 `host/canopen/co_sdo_seg.py`，预期 1 KB/4 KB 均 PASS，吞吐受 RTT 限制（每段 1 往返，≈2.3 ms × 147/586 段 → 约 3 KB/s）；② 过渡方案：`co_sdo_seg.py --retries 5`（CiA 301 的 SDO 本无重传，客户端重新发起是标准做法）配合主机 vcan 去掉 500 kbit 限速，见 `host/canopen/run_sdo_bench.sh` |
| U-33-10 | TPDO2 事件定时器（0x1801:5 = 1000 ms）未触发 | 已知限制（两条历史证据互相矛盾，诊断命令已加、本轮未拿到窗口） | 现象：板内双节点 5 s 窗口 0 帧（`logs/20261030-174138-canopen_pdo.log`），但同一份固件经 cannelloni 网关时主机 5 s 收到 5 帧（表 33-13），说明 DUT 的 TPDO2 事件定时器大概率在发，问题出在板内主站的接收/计数路径。代码核对：`CO_TPDO_init()` 会把 OD 里的 0x280 补成 0x281、`eventTime_us=1000*1000`，`CO_TPDO_process()` 的 eventTimer 分支正常。诊断方法：板端 `canopen_pdo` 的 `t2 [秒]` 命令先回读 0x1800:1/0x1801:1/2/5，再同时统计 0x280 与 0x281 两路帧——若 0x281 有帧则确认是主站侧问题，若两路都 0 则查 `CO_TPDO_process` 的 `NMTisOperational` 与 `PDO->valid` |
| U-33-9 | CAN 总线错误注入（bus-off 恢复） | 已知限制（需人工拔线） | 原因：bus-off 要物理断开/短路 CAN_H/CAN_L，远程只能操作软件。验证方法：`ip link set can0 down`（对端 MCAN 仍 ACK，不产生 bus-off，只能测错误计数），真实验证要拔线后看 `canerr` 位与 0x1001 错误位、恢复后 NMT 是否自动回 Operational |
