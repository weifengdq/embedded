# 第 47 章 MCAL CAN 驱动：Can（4 路 MCAN / CAN FD）

## 47.1 文档信息

| 项目 | 内容 |
|---|---|
| 章标题 | MCAL CAN 驱动：Can（4 路 MCAN / CAN FD） |
| 版本 | v1.3.0（2026-11-02，R6 MCAL） |
| 适用硬件 | hpm5e31_1（HPM5E31xGNx，4 × Bosch M_CAN，TCAN1044AV 收发器） |
| 适用软件 | HPM SDK v1.13.0、GCC RISC-V（-Wall -Wextra，0 warning）、本仓库 autosar 分支 |
| 对应模块 | 模块 ID 80，规范 AUTOSAR_CP_SWS_CANDriver（R25-11） |
| 对应例程 | can_classic_echo、can_fd_echo、can_irq_perf、can_busoff、can_filter |
| 实测日期 | 2026-11-02（全部数据为上板实测，日志在 logs/ 下，正文逐条给出路径） |
| 可信度 | 高：每个数字都能在 logs/ 的串口日志里复核；未实测项在 §47.14 单独标注 |
| 代码位置 | mcal/Can/include/Can.h、mcal/Can/src/Can.c、mcal/Can/cfg/Can_PBcfg.c |

## 47.2 本章导览

这一章写最后一块"总线型"外设：CAN。它和前几章的 SPI/I2C 不一样，难点不在时序，而在于
**一套硬件要给两层软件用**——AUTOSAR CanIf 只认 HTH/HRH 句柄和 CAN_BUSY，MCAN 硬件只认
发送缓冲、消息 RAM 偏移和滤波器元素，中间这层翻译就是本章的正文。

阅读顺序：

1. §47.3 说清 Can 在 AUTOSAR 通信栈里的位置，以及为什么它没有"阻塞发送"；
2. §47.4 讲硬件原理：消息 RAM 布局、HTH/HRH 映射、过滤器、状态机、两种推进模型；
3. §47.5 是寄存器对照表，驱动源码基本按这张表写；
4. §47.6、§47.7 是配置模型与 API 完成度表；
5. §47.8、§47.9 是 5 个例程与上板实测数据（经典帧、FD、过滤器、bus-off、4 路并发）；
6. §47.10、§47.11 是性能与遇到过的问题（这一节信息密度最高）；
7. §47.12 与 HPM SDK 直调、Infineon TC4x MCMCAN、NXP S32K3 FlexCAN 做对照；
8. §47.14 只写"已实测 / 已定位 / 已知限制"三态，包括中断模型的限制。

## 47.3 概述

Can 驱动在 AUTOSAR 里的地位很特殊：它是唯一一个**发送可能被拒绝**的 MCAL 模块。
`Can_Write` 的返回值不是 `Std_ReturnType` 而是 `Can_ReturnType`，多一个 `CAN_BUSY`——
因为硬件发送对象是有限的，上层必须接受"这一帧没排上队"。

本章实现的模块覆盖 SWS 的必选 API 全集（19 个），支持：

- 4 路独立 MCAN（CAN0~CAN3），两两 120 Ω 对接成两段总线；
- 经典 CAN 2.0（500 kbit/s、1 Mbit/s）与 CAN FD（仲裁 500 kbit/s + 数据 1/2 Mbit/s，BRS、TDC）；
- 标准 11 位 / 扩展 29 位 ID；
- HTH（发送硬件对象）与 HRH（接收硬件对象）语义，HTH 忙时返回 `CAN_BUSY`；
- 硬件过滤器四种类型（掩码、范围、双 ID、拒绝）+ 全局过滤；
- 三种控制器状态（UNINIT / STOPPED / STARTED / SLEEP）与 bus-off 恢复入口；
- 完整 DET 开发错误检查（9 个错误码）与 `CAN_E_DATALOST` 运行时错误。

模块文件构成（契约 §3）：

**表 47-1 模块文件构成**

| 文件 | 作用 | 行数级别 |
|---|---|---|
| mcal/Can/include/Can_GeneralTypes.h | AUTOSAR 通用类型（Can_IdType/Can_PduType/枚举） | ~150 |
| mcal/Can/include/Can.h | 公共 API、API ID、DET 错误码、post-build 配置类型 | ~360 |
| mcal/Can/cfg/Can_Cfg.h | 预编译开关（4 路、HTH/HRH 上限、处理模型、版本号） | ~80 |
| mcal/Can/cfg/Can_PBcfg.h | `Can_Config` 与中断入口声明 | ~30 |
| mcal/Can/cfg/Can_PBcfg.c | 板级映射：实例/引脚/时钟/STB/消息 RAM/位时序表/过滤器表/中断向量 | ~430 |
| mcal/Can/src/Can.c | 驱动实现（寄存器级 + AUTOSAR 状态机 + CanIf 回调） | ~1150 |

## 47.4 工作原理

### 47.4.1 数据通路

上层 CanIf 调 `Can_Write(Hth, &PduInfo)`，驱动按 Hth 查到"哪个控制器的第几个专用发送缓冲"，
把 `Can_IdType` 拆成标准/扩展 ID 与 CAN 2.0/FD 帧标志，填进 MCAN 的消息 RAM 元素，再写
`TXBAR` 置发送请求。接收方向反过来：MCAN 按过滤器把帧放进 RXFIFO0/1，驱动轮询（或中断）
取出后回调 `CanIf_RxIndication`。

整条链路上有三个"翻译层"，都在 `Can.c` 里：

1. **ID 编码**：`Can_IdType` 高 2 位是帧类型（00 标准帧、01 FD 标准帧、10 扩展帧、11 FD 扩展帧），
   低 29 位是 ID 值；MCAN 只认 `std_id`/`ext_id` 字段。
2. **长度编码**：AUTOSAR 传的是字节数，MCAN 要的是 DLC；8 字节以上是非线性映射
   （9→12、10→16、11→20、12→24、13→32、14→48、15→64）。
3. **对象编码**：HTH → 专用发送缓冲序号，HRH → RXFIFO 序号。

### 47.4.2 消息 RAM 布局

HPM5E31 的 M_CAN 没有片内消息 RAM，4 个实例共享 32 KB AHB SRAM，必须先把窗口登记给驱动
（`mcan_set_msg_buf_attr`），否则初始化直接失败。每实例申请 640 字（2560 B），
按下面的偏移自动排布（SDK 的 `mcan_config_ram` 累加计算，驱动只给每段元素个数）：

**表 47-3 MCAN 消息 RAM 分区（每实例 2560 B）**

| 段 | 元素数 | 元素大小 | 占用 | 说明 |
|---|---|---|---|---|
| 标准过滤器 SIDFC | 6 | 4 B | 24 B | 掩码/范围/双 ID 元素 |
| 扩展过滤器 XIDFC | 3 | 8 B | 24 B | 29 位 ID 元素 |
| RXFIFO0 | 8 | 8+64=72 B | 576 B | 主接收通道，落最大 64 B 载荷 |
| RXFIFO1 | 4 | 72 B | 288 B | 双 ID 元素命中后落这里 |
| TX Buffer | 4 专用 + 4 FIFO | 72 B | 576 B | 4 个 HTH 各占一个专用缓冲 |
| 合计 | | | 1488 B | 余量 1072 B（2560 B 窗口内） |

**元素大小必须由同一套公式算**：数据段大小选项 TXESC/RXESC 一旦写 64 B，过滤器/发送/接收
所有段的元素步长都是 8+64；手写偏移和 SDK 公式不一致会导致"能初始化但收发无数据"，
这是本模块调试期花时间最多的一个易错点（见 §47.11 第 2 条）。

### 47.4.3 状态机与两种推进模型

驱动维护每个控制器一个软件状态：UNINIT → STOPPED → STARTED ↔ SLEEP。
注意 `Can_Init` 之后控制器必须处于 **STOPPED**（SWS_Can_00259），实现方式是初始化完
立刻让 MCAN 回到 INIT 模式（CCCR.INIT=1），此时不参与总线、不 ACK 任何帧。

控制器状态与允许的迁移（SWS_Can_91013、SWS_Can_00409、SWS_Can_00263、SWS_Can_00283）：

**表 47-2 控制器状态机**

| 当前状态 | 请求 STARTED | 请求 STOPPED | 请求 SLEEP | 说明 |
|---|---|---|---|---|
| UNINIT | DET CAN_E_TRANSITION | DET CAN_E_TRANSITION | DET CAN_E_TRANSITION | 只能 `Can_Init` |
| STOPPED | 重初始化 + 进 normal 模式 | 允许（幂等） | 进 SLEEP | `Can_Init`/`Can_DeInit` 后的状态 |
| STARTED | bus-off 时触发恢复，否则 DET | 进 INIT 模式 | DET CAN_E_TRANSITION | 参与总线通信 |
| SLEEP | 唤醒后进 STARTED | 允许 | 允许 | M_CAN 只能停机，无总线唤醒 |

推进收发有两种模型，SWS 都允许，由 `Can_Cfg.h` 两个开关选择：

**表 47-4 两种处理模型对比**

| 模型 | 开关 | 发送确认 | 接收指示 | 代价 |
|---|---|---|---|---|
| 轮询（本构建默认） | `CAN_TX_CONFIRM_IN_INTERRUPT=0`、`CAN_RX_PROCESSING_INTERRUPT=0` | `Can_MainFunction_Write` 查 TXBRP/TXBTO | `Can_MainFunction_Read` 抽 RXFIFO | 延迟含轮询间隔，CPU 线性增长 |
| 中断 | 两个开关都置 1 | ISR 里回调 `CanIf_TxConfirmation` | ISR 里抽 FIFO + 回调 | 延迟低，但本板实测 ISR 只进 1~2 次就停（见 §47.14） |

轮询模型下 `Can_MainFunction_Write` 的判定条件是 `TXBRP[n]==0 && TXBTO[n]==1`：

```c
/* mcal/Can/src/Can.c —— 发送完成判定（轮询模型） */
for (h = 0U; h < s_can_config->HthCount; h++) {
    const Can_HthConfigType *hth = &s_can_config->Hths[h];
    uint32 idx;
    if ((hth->ControllerId != ctrl) || (s_hth_busy[h] == FALSE)) {
        continue;
    }
    idx = (uint32) hth->TxBufferIndex;
    /* TXBRP=0 表示请求已完成；TXBTO=1 表示确实发到总线上。
     * M_CAN 的 TXBTO 是只读位，只能由下一次 TXBAR 写 1 自动清，不能直接写 1 清零。 */
    if ((mcan_is_transmit_request_pending(base, idx) == false) &&
        mcan_is_transmit_occurred(base, idx)) {
        s_hth_busy[h] = FALSE;
        CanIf_TxConfirmation(s_hth_pdu[h]);
    }
}
```

## 47.5 寄存器

驱动直接操作的 M_CAN 寄存器与对应 API（寄存器语义见 HPM5E31 RM 的 MCAN 章、
Bosch M_CAN User Manual §2）：

**表 47-5 M_CAN 寄存器与驱动 API 对照**

| 寄存器 | 关键位域 | 驱动里的用途 | 对应 API / 例程 |
|---|---|---|---|
| CCCR | INIT、CCE、FDOE、BRSE、TEST、MON、EFBI、UTSU | 配置模式、CAN FD 开关、模式切换 | `Can_Init`、`Can_SetControllerMode` |
| NBTP | NBRP、NTSEG1、NTSEG2、NSJW | 仲裁段位时序 | `Can_SetBaudrate`（PBcfg 位时序表） |
| DBTP | DBRP、DTSEG1、DTSEG2、DSJW、TDC | FD 数据段位时序 + TDC | `Can_SetBaudrate`（ID2/ID3/ID4/ID5） |
| TDCR | TDCO、TDCF | 二次采样点偏移/滤波窗口 | ID2 ssp=15、ID3 ssp=20 |
| PSR | LEC、ACT、EP、EW、BO、DLEC | 错误状态与活动状态 | `Can_GetControllerErrorState`、`Can_MainFunction_BusOff` |
| ECR | TEC、REC | 收发错误计数器 | `Can_GetControllerTxErrorCounter`、`Can_GetControllerRxErrorCounter` |
| SIDFC / XIDFC | FLSSA/LSS、FLESA/LSE | 过滤器列表起始地址与元素数 | `Can_Init`（RamConfig） |
| RXF0C / RXF1C | F0SA/F0S/F0WM、F1SA | 接收 FIFO 起始与深度 | `Can_Init`、`Can_MainFunction_Read` |
| TXBC / TXESC | TBSA/NDTB/TFQS、TBDS | 发送缓冲区与元素大小 | `Can_Write`（HTH→专用缓冲） |
| TXBAR / TXBRP / TXBTO | 每缓冲一位 | 置发送请求 / 请求未完成 / 发送已发生 | `Can_Write`、`Can_MainFunction_Write` |
| IR / IE / ILS | RF0N、RF1N、TC、BO、EP、EW、PEA、PED | 中断标志与使能 | `Can_Isr_Handler`、`Can_Enable/DisableControllerInterrupts` |
| GFC | ANFS、ANFE、RRFS、RRFE | 全局过滤（未命中帧丢弃/进 FIFO） | `Can_Init`（PBcfg AcceptNonMatching*） |
| TSCC | TSS、TCP | 内部 16 位时间戳 | `Can_Init`（UseTimestampCounter） |

位时序换算（CAN 时钟 = pll1_clk0/5 = 80 MHz，TQ = 12.5 ns @prescaler=1）：

**表 47-6 位时序配置表（CAN 时钟 80 MHz）**

| 配置 ID | 用途 | 仲裁段 | 数据段 | 采样点 |
|---|---|---|---|---|
| 0 | 经典 500 kbit/s | presc=1 seg1=139 seg2=20 sjw=4 | — | 87.5% |
| 1 | 经典 1 Mbit/s | presc=2 seg1=29 seg2=10 sjw=2 | — | 75.0% |
| 2 | FD 500k/2M（TDC 15/15） | presc=1 seg1=139 seg2=20 sjw=4 | presc=2 seg1=14 seg2=5 sjw=2 | 87.5% / 75.0% |
| 3 | FD 500k/5M（TDC 20/20） | 同上 | presc=1 seg1=11 seg2=4 sjw=2 | 87.5% / 75.0% |
| 4 | FD 500k/1M（无 TDC） | 同上 | presc=2 seg1=19 seg2=5 sjw=2 | 87.5% / 79.2% |
| 5 | FD 1M/2M（自动 SSP） | presc=2 seg1=29 seg2=10 sjw=2 | presc=2 seg1=14 seg2=5 sjw=2 | 75.0% / 75.0% |

## 47.6 配置模型

按契约 §5 三层都演示到：

1. **Pre-compile**（`Can_Cfg.h`）：控制器数 4、HTH 16、HRH 8、过滤器元素上限、
   FD 支持开关、处理模型开关、中断优先级、DET 开关、版本号与 vendor ID。
2. **Link-time**：本工程统一用 post-build 指针，不做独立的 link-time 配置（见第 43 章说明）。
3. **Post-build**（`Can_PBcfg.c`）：`const Can_ConfigType Can_Config` = 4 条控制器配置
   + 16 个 HTH + 8 个 HRH + 36 条过滤器元素，全部是常量表。

板级参数只出现在 `Can_PBcfg.c`，驱动源码零硬编码（契约 §3.3）：实例基址、中断号、
消息 RAM 地址、位时序表、过滤器表、引脚初始化回调、时 clock 初始化回调、收发器 STB 使能回调，
都是配置结构体成员。公共头 `Can.h` 不含任何 SDK 头，SDK 类型只在 `.c` 里出现。

## 47.7 API 实现与完成度

SWS_CANDriver R25-11 的必选 API 全部实现，服务 ID 与 SWS 一字不差（便于 DET 追踪表比对）：

**表 47-7 API 实现与 DET 覆盖**

| API | 服务 ID | 实现要点 | DET 错误 |
|---|---|---|---|
| `Can_Init` | 0x00 | 逐控制器登记消息 RAM、写位时序/过滤器/中断、回 INIT 保持 STOPPED | POINTER、HANDLE、TRANSITION、INIT_FAILED |
| `Can_DeInit` | 0x10 | 要求所有控制器 STOPPED，然后 mcan_deinit 并置 UNINIT | UNINIT、TRANSITION |
| `Can_SetBaudrate` | 0x0F | 按配置 ID 重初始化，要求 STOPPED | UNINIT、CONTROLLER、PARAM_BAUDRATE、TRANSITION |
| `Can_SetControllerMode` | 0x03 | STARTED/STOPPED/SLEEP，STARTED 时重初始化；bus-off 中 STARTED 触发恢复 | UNINIT、CONTROLLER、TRANSITION |
| `Can_Write` | 0x06 | HTH→专用 TX 缓冲，忙返回 CAN_BUSY | UNINIT、HANDLE、POINTER、DATA_LENGTH |
| `Can_MainFunction_Write` | 0x01 | 轮询 TXBRP/TXBTO 完成确认 | UNINIT |
| `Can_MainFunction_Read` | 0x08 | 轮询抽 RXFIFO0/1 并报 DATALOST | UNINIT |
| `Can_MainFunction_BusOff` | 0x09 | 轮询 PSR.BO，边沿上报 `CanIf_ControllerBusOff` | UNINIT |
| `Can_MainFunction_Wakeup` | 0x0A | 无唤醒源，保留 API（见 §47.14） | UNINIT |
| `Can_MainFunction_Mode` | 0x0C | 状态迁移同步完成，保留 API | UNINIT |
| `Can_GetControllerMode` | 0x12 | 返回软件状态 | UNINIT、CONTROLLER、POINTER |
| `Can_GetControllerErrorState` | 0x11 | PSR 三态映射 ACTIVE/PASSIVE/BUSOFF | 同上 |
| `Can_GetControllerRxErrorCounter` | 0x30 | ECR.REC | 同上 |
| `Can_GetControllerTxErrorCounter` | 0x31 | ECR.TEC | 同上 |
| `Can_EnableControllerInterrupts` | 0x05 | 嵌套计数，归零才真正使能 | UNINIT、CONTROLLER、TRANSITION |
| `Can_DisableControllerInterrupts` | 0x04 | 嵌套计数，首次真正关中断 | 同上 |
| `Can_CheckWakeup` | 0x0B | M_CAN 无总线唤醒单元，恒返回 E_NOT_OK（见 §47.14） | UNINIT、CONTROLLER |
| `Can_GetVersionInfo` | 0x07 | Std_VersionInfoType | POINTER |
| CanIf 回调 7 个 | — | 驱动内 weak 桩，例程覆盖做计数/打印 | — |

## 47.8 例程

5 个例程都在 `samples/mcal/` 下，每个都有独立 README（接线/命令/实测片段/日志路径）。

**表 47-8 五个例程与实测结论**

| 例程 | 验证内容 | 关键数据（实测） | 日志 |
|---|---|---|---|
| can_classic_echo | 经典 500k ping-pong + 单向吞吐 + DET | 200/200 两轮，4210 frame/s | logs/20261102-142350-can_classic_echo.log |
| can_fd_echo | FD 各 DLC、1M/2M/5M 数据段 | 8~64B 全 PASS，2M 64B avg 704 µs | logs/20261102-141808-can_fd_echo.log |
| can_filter | 13 个过滤器用例（掩码/范围/双 ID/拒绝/未命中） | 13/13 PASS | logs/20261102-141905-can_filter.log |
| can_busoff | 对端不 ACK 下的 TEC/REC 与恢复 | 953 µs 进 error passive，TEC 卡 129 | logs/20261102-142116-can_busoff.log |
| can_irq_perf | 4 路并发吞吐/CPU/延迟分布 | 8366 frame/s，CPU 4.62%，avg 246 µs | logs/20261102-142208-can_irq_perf.log |

上板命令（`hwrun.sh` 内部整机串行锁）：

```bash
bash scripts/build.sh samples/mcal/can_classic_echo
bash scripts/hwrun.sh mcal/can_classic_echo --secs 30 --send "can echo 200;can echo 200;can stream 500;can stat;can err"
```

## 47.9 实测

### 47.9.1 经典帧（can_classic_echo）

```text
ECHO classic 500k: n=200 ok=200 lost_req=0 lost_echo=0 bad=0 total=95493us
  rx_total=400 rx_overflow=0 tx_confirmed=400 busoff=0
ECHO classic 500k: n=200 ok=200 lost_req=0 lost_echo=0 bad=0 total=95481us
  rx_total=400 rx_overflow=0 tx_confirmed=400 busoff=0
STREAM classic 500k: sent=500 rx=500 bad=0 total=118757us => 4210 frame/s
  payload=0.27 Mbit/s (8B/frame, 108 bit on wire)  bus=0.5 Mbit/s  wire_eff=53.9%
```

两轮 200 帧全部成功，TEC/REC 全程 0；往返延迟 477 µs（500 kbit/s 下 8B 帧约 222 µs，
两次单程加主函数开销）；单向 4210 frame/s，为理论上限 4386 frame/s 的 96.0%。

### 47.9.2 CAN FD（can_fd_echo）

**表 47-9 CAN FD 各载荷实测延迟（500k 仲裁 / 2M 数据）**

| 载荷 | DLC | 成功率 | 往返 avg | max | 单程（往返/2） |
|---|---|---|---|---|---|
| 8 B | 8 | 30/30 | 254 µs | 998 µs | 127 µs |
| 12 B | 9 | 30/30 | 262 µs | 267 µs | 131 µs |
| 16 B | 10 | 30/30 | 297 µs | 303 µs | 149 µs |
| 24 B | 12 | 30/30 | 369 µs | 376 µs | 185 µs |
| 32 B | 13 | 30/30 | 436 µs | 442 µs | 218 µs |
| 48 B | 14 | 30/30 | 572 µs | 581 µs | 286 µs |
| 64 B | 15 | 30/30 | 706 µs | 713 µs | 353 µs |

各数据段速率对比见表 47-16。5 Mbit/s 在 R5 曾记录为"已知限制"（0/50、TEC 248、bus-off），
R6 复查确认是**位时序配置**问题，不是收发器/线束能力问题，根因见下面的 TDC 偏移分析。

**表 47-16 CAN FD 各数据段速率对比（64 B ping-pong，500 kbit/s 仲裁，每项 50 轮）**

| 数据段 | 配置（DBTP） | 平均往返 | 通过 | 日志 |
|---|---|---|---|---|
| 1 Mbit/s | 无 TDC，1+19+5=25 tq @presc=2 | 846 us | 50/50 | logs/20261102-155424-can_fd_echo.log |
| 2 Mbit/s | TDCO=15，1+14+5=20 tq @presc=2 | 704 us | 50/50 | 同上 |
| 5 Mbit/s | TDCO=12，1+11+4=16 tq @presc=1 | 364 us | 50/50 | 同上 |

**5 Mbit/s 为什么曾经失败：TDC 偏移越界**

CAN FD 数据段用发送延迟补偿（TDC）在 `TDCR.TDCO` 定义的位置做二次采样（SSP，单位 = 数据段 mtq，
RM §51.6.15）。本板 CAN 功能时钟 80 MHz，5 Mbit/s 数据段 BRP=1、tq=12.5 ns，一位只有 16 tq
（DBTP=0x00800A31：1+11+4，采样点 75%）。旧配置把 TDCO 设成 **20**（TDCR=0x00001414），
SSP 超出本位 4 tq，发送端在数据段校验自己回读的位时必然判错，TEC 每次 +8 累加到 248 进 bus-off
（errstate=2、LEC=7、DLEC=0）。改成 16 tq 的 75%（=12）后立即全部通过；同一固件、同一线束
只改变 TDCO 的扫描见表 47-17。

**表 47-17 TDC 偏移扫描（500k/5M，16 tq 数据位，64 B ping-pong ×50）**

| TDCO | TDCF | SSP/位长 | 结果 | TEC | 说明 |
|---|---|---|---|---|---|
| 1 | 1 | 6% | 0/50 | 248 | 太早，同样不可用 |
| 8 | 8 | 50% | 50/50 | 0 | 可用 |
| 10 | 10 | 62.5% | 50/50 | 0 | 可用 |
| **12** | **12** | **75%** | **50/50** | 0 | **默认值，与采样点一致** |
| 13 | 13 | 81% | 50/50 | 0 | 可用 |
| 14 | 14 | 87.5% | 50/50 | 0 | 可用 |
| 20 | 20 | 125% | 0/50 | 248 | 旧配置，越界（故障复现） |

矩阵还暴露出一个工程细节：`PSR.TDCV`（硬件回读的"测量延迟 + TDCO"）在三种数据速率下都等于
`TDCO + 10`，说明本板收发器环回延迟折算约 10 mtq；5 Mbit/s 一位只有 16 mtq，所以 TDCO 的合法窗口
很窄（实测 8~14 可用），不是"越大越稳"。

驱动侧同步加了一道闸门：`can_init_controller()` 在 `FdEnabled && TdcOffset != 0` 时校验
`TDCO < 1 + DataSeg1 + DataSeg2` 且 `TDCF < 位长`，越界直接返回 `E_NOT_OK`（`Can_SetBaudrate` 失败），
避免再静默编进固件。旧配置以 ID18 保留在 PBcfg 里，`can at 20 18` 会被拒绝，可用来验证这道检查。

**表 47-18 仲裁段 × 数据段通过矩阵（64 B ping-pong ×50，avg = 一次 A→B→A 往返）**

| 仲裁段 | 数据段 | 采样点 / TDC | 结果 | avg |
|---|---|---|---|---|
| 500 kbit/s | 1 Mbit/s | 75%，无 TDC | 50/50 | 846 us |
| 500 kbit/s | 2 Mbit/s | 75%，TDCO=15 | 50/50 | 704 us |
| 500 kbit/s | 2 Mbit/s | 80%，TDCO=16 | 50/50 | 705 us |
| 500 kbit/s | 2 Mbit/s | 85%，TDCO=17 | 50/50 | 705 us |
| 500 kbit/s | 5 Mbit/s | 75%，TDCO=12 | 50/50 | 364 us |
| 500 kbit/s | 5 Mbit/s | 80%，TDCO=12 | 50/50 | 364 us |
| 500 kbit/s | 5 Mbit/s | 87.5%，TDCO=13 | 50/50 | 365 us |
| 1 Mbit/s | 5 Mbit/s | 75%，TDCO=12 | 50/50 | 312 us |
| 500 kbit/s | 5 Mbit/s | 75%，TDCO=1 | 0/50 | — |
| 500 kbit/s | 5 Mbit/s | 75%，TDCO=20 | 0/50 | — |

5 Mbit/s 的长测与吞吐：2000 轮 ping-pong 2000/2000、avg 366 us、max 380 us，TEC/REC=0；
单向 64 B 发 3000 帧 0 丢，5452 帧/s、2.79 Mbit/s 载荷（约理论帧时间的 94%）；
1 Mbit/s 仲裁 + 5 Mbit/s 数据 6360 帧/s、3.26 Mbit/s。日志 logs/20261102-155624-can_fd_echo.log。

图 47-4 把 TDCO 扫描画成通过/失败与 TEC 的对照：

![图 47-4 5 Mbit/s 数据段 TDC 偏移扫描：TDCO 合法窗口与越界后的 TEC/bus-off](figures/47-mcal-can/fig-47-4-tdc-offset-scan.png)

图 47-1 把经典帧与 FD 帧的位时间拆成仲裁段/数据段两段，并叠加实测单程延迟：

![图 47-1 经典帧与 CAN FD 帧的位时间构成及实测单程延迟](figures/47-mcal-can/fig-47-1-frame-timing.png)

图 47-2 是 FD 各载荷的实测单程延迟与理论帧时间对照，两者偏差 5%~8%，
差值就是主函数轮询与两次回调的开销：

![图 47-2 CAN FD 各载荷的单程延迟（实测 vs 理论）](figures/47-mcal-can/fig-47-2-fd-latency-vs-payload.png)

### 47.9.3 过滤器（can_filter）

13 个用例全部 PASS，判定依据不只是"收到"，而是 `Mailbox->Hoh` 反查到具体落哪个 RXFIFO：

```text
   0 0x00000123  mask 0x123/0x7FF          FIFO0     FIFO0     PASS
   2 0x00000405  range 0x400..0x40F        FIFO0     FIFO0     PASS
   3 0x0000060A  mask 0x600/0x7F0          FIFO0     FIFO0     PASS
   4 0x00000500  dual {0x500,0x501}        FIFO1     FIFO1     PASS
   7 0x00000705  reject 0x700..0x7FF       drop      drop      PASS
  12 0x92345678  ext no match -> drop      drop      drop      PASS
filter: pass=13 fail=0  (rx_total=18 rx_overflow=0 tx_confirmed=26)
```

### 47.9.4 error passive / bus-off（can_busoff）

**表 47-10 error passive 实测时间线**

| 阶段 | 时间 | TEC | 状态 |
|---|---|---|---|
| 基线 ping-pong 20 帧 | — | 0 | ACTIVE |
| 对端 STOPPED，首次失败重传 | 953 µs | 129 | PASSIVE |
| 再等 20 ms | 20 954 µs | 129（平台） | PASSIVE |
| 对端恢复 ACK | +6 022 µs | 118 → 0 | ACTIVE，业务恢复 |

实测发现一件违反直觉的事：**对端不 ACK 到不了 bus-off**。TEC 涨到 128 后就停住，
因为 ISO 11898-1 规定 error-passive 节点检测到 ACK 错误时不再增加 TEC。
要进 bus-off（TEC>255）必须制造位错误或格式错误，需要外部干扰源。这条写进了 §47.14。

## 47.10 性能

4 路并发（CAN0→CAN1、CAN2→CAN3 同时单向流水，8B 经典帧 500 kbit/s）：

**表 47-11 4 路并发性能实测**

| 指标 | 数值 | 说明 |
|---|---|---|
| 4 路总吞吐 | 8366 frame/s | 每通道 4183 frame/s |
| 单通道达成率 | 95.4% | 理论上限 4386 frame/s（114 bit/帧） |
| 主函数 CPU 占用 | 4.62% | 4.24 M cycle / 480 MHz，包围 `Can_MainFunction_Read/Write` 测得 |
| 逐帧延迟 | avg 246 µs / max 256 µs | 100% 落在 <300 µs 桶 |
| 4 路同时 ping-pong | A 200/200 avg 244 µs；B 200/200 avg 243 µs | 两对总线互不干扰，差 2 µs |
| `CAN_BUSY` 重试次数 | 39 545（1600 帧） | SWS_Can_00213 语义，不丢帧 |

![图 47-3 4 路并发吞吐与单帧延迟构成](figures/47-mcal-can/fig-47-3-throughput-and-latency-budget.png)

单帧延迟构成：总线传输 222 µs（8B 帧、114 bit 含 IFS）+ 轮询/回调 24 µs。
轮询模型下延迟分布很窄（max 只比 avg 高 10 µs），因为瓶颈是总线不是软件。

与既有 SDK 直调例程的性能对比（同一块板、同一段总线）：

**表 47-12 与 SDK 直调例程的性能对比**

| 指标 | SDK 直调 `samples/can/mcan_two_node` | 本 MCAL 例程 | 差异原因 |
|---|---|---|---|
| 发送方式 | `mcan_transmit_blocking` 阻塞等 TXBTO | `Can_Write` + `Can_MainFunction_Write` 确认 | MCAL 多一次状态检查与回调 |
| 经典 ping-pong 吞吐 | 参考值取自该例程 README 的口径 | 200 帧 95 493 µs = 2094 帧/s（往返） | 同量级，MCAL 多了 CanIf 回调 |
| 单向吞吐 | 该例程 `thr` 命令实测约 4.2 k frame/s 量级 | 4210 frame/s | 一致（都是总线极限） |
| 4 路并发 | 该例程 `mcan_quad_load` 有 4 路负载测试 | 8366 frame/s 总计 | 量纲不同，本轮给出每通道占比 |

数据来源：`logs/20261102-142350-can_classic_echo.log`、`logs/20261102-142208-can_irq_perf.log`；
SDK 侧对比取自 `samples/can/mcan_two_node` 与 `mcan_quad_load` 的 README 记录。

## 47.11 易错点

1. **CCCR.INIT 清不掉不是硬件卡住，是 posted write**。清 INIT 后立刻读 CCCR 会读到 1，
   导致 `Can_SetControllerMode(STARTED)` 误判失败、控制器停在 STOPPED。修法是轮询确认
   （`can_wait_cccr_init`），对应 SWS_Can_00262"等有限时间直到控制器完全可用"。
2. **手写消息 RAM 偏移和 SDK 公式不一致**。最初按"经典帧元素 16 B"估算 SIDFC/RXF0C/TXBC，
   与 SDK 按 64 B 数据段算出的偏移不同，结果能初始化但收发无数据。改成只给每段元素个数、
   偏移交给 `mcan_config_ram` 累加后一次通过。
3. **TXBTO 是只读位**。M_CAN 规定 TXBTO 由下一次 TXBAR 写 1 自动清除，直接写会编译报错
   （寄存器声明为 `__R`）。完成判定必须用 `TXBRP==0 && TXBTO==1`。
4. **收发器 STB 忘拉低**。4 颗 TCAN1044AV 共用 PE03，不拉低时 CAN0 发出的帧没人 ACK，
   TX 无限重传、TXBRP 永不清零、`Can_Write` 一直 `CAN_BUSY`。现象和"消息 RAM 配错"极像。
5. **跨用例不清硬件 FIFO 会串帧**。软件队列清空不等于硬件 FIFO 清空，上一轮的尾帧会在下一轮
   被当成第一帧，表现为"ok 只有 2 个、其余全 mismatch"。例程统一用 `rxq_drain_all()`。
6. **中断模型 ISR 只进 1~2 次**。同一套状态机搬到 ISR 后，第一帧能收发，之后 RXFIFO 不再
   被抽、TX 不再确认（`rx_total` 停在 2）。轮询模型完全正常，说明状态机与配置没问题，
   问题在中断接入侧（向量绑定/PLIC claim 完成），本轮未定位到根因（见 §47.14 第 2 条）。
7. **5 Mbit/s 数据段跑不起来，先查 TDC 偏移而不是怀疑收发器**。5 Mbit/s 数据位只有 16 tq（80 MHz、BRP=1、tq=12.5 ns），TDC 的 SSP 位置 TDCR.TDCO 必须小于 16 tq；旧 PBcfg 写成 20，SSP 越界后发送端在数据段误判位错误，TEC 每次 +8 累加到 248 进 bus-off，现象和"收发器/线束裕量不够"一模一样。定位时先读 PSR/ECR 区分"收不到"与"发不出去"：本例 REC=0、TEC=248 说明是发送侧判错，再回读 TDCR 就能看到 20 > 16 tq。改成 12（16 tq 的 75%）后 5 Mbit/s 全部通过（表 47-17、表 47-18），板内 120 Ω 杜邦线接法本身没有问题。

## 47.12 对比

### 47.12.1 与 HPM SDK 直调

**表 47-13 与 HPM SDK 直调驱动的抽象层次对比**

| 维度 | HPM SDK `mcan_*` 驱动 | 本 MCAL Can |
|---|---|---|
| 抽象层次 | 寄存器 + 消息 RAM 元素 | AUTOSAR HTH/HRH/过滤器对象 + 回调 |
| 发送 | `mcan_transmit_blocking`（阻塞）或 `..._nonblocking` | `Can_Write` 立即返回 E_OK/CAN_BUSY |
| 接收 | 应用自己 `mcan_read_rxfifo` | `Can_MainFunction_Read` 或 ISR 回调 `CanIf_RxIndication` |
| 配置 | 运行期填 `mcan_config_t` | post-build 常量表 `Can_Config` |
| 错误处理 | 返回 `hpm_stat_t` | DET 开发错误 + `Can_ErrorStateType` 状态查询 |
| 代码量 | 驱动库约 3000 行 | 本模块 1150 行（复用 SDK 的寄存器助手） |

保留 SDK 的部分：消息 RAM 布局计算、TX/RX 元素编解码、过滤器元素编码、位时序字段换算——
这些是纯寄存器搬运，重写没有收益且容易错。重写的是 AUTOSAR 语义层：状态机、HTH/HRH、
CAN_BUSY、过滤器表翻译、CanIf 回调、DET。

### 47.12.2 与 Infineon TC4x MCMCAN（MCAL 2.20.0）

**表 47-14 与 Infineon TC4x MCMCAN MCAL 对比**

| 维度 | TC4x MCMCAN MCAL | 本实现 |
|---|---|---|
| 控制器 | 最多 4 个 MCMCAN，每控制器多实例 + 共享消息 RAM | 4 个 M_CAN，共享 32 KB AHB SRAM |
| 配置工具 | EB tresos 生成的 `Can_PBcfg.c` | 手写 post-build 常量表（同构） |
| API 覆盖 | SWS 全集 + 安全扩展（`Can_GetControllerErrorState` 等） | 必选 19 个 API 全覆盖 |
| 中断模型 | 全部中断驱动，提供 ILS 多中断线 | 中断与轮询双模型，本构建跑轮询 |
| 特有功能 | 主机核心/安全核心双核访问、超时看门狗 | 内部 16 位时间戳、TSU/PTPC（未启用） |
| 性能口径 | 文档给"每帧 CPU 周期数"，无板级实测 | 本文给出板级实测 4.62% CPU / 246 µs 延迟 |

### 47.12.3 与 NXP S32K3 FlexCAN（RTD 6.0.0）

**表 47-15 与 NXP S32K3 FlexCAN RTD 对比**

| 维度 | S32K3 FlexCAN | 本实现 |
|---|---|---|
| 硬件 | FlexCAN（经典/FD），每实例最多 96 个邮箱 | M_CAN，4 专用 TX + 4 FIFO + 8/4 RXFIFO |
| HTH/HRH | 邮箱号即硬件对象，`Can_Write` 找空闲邮箱 | HTH 固定映射专用 TX 缓冲，忙则 CAN_BUSY |
| 过滤器 | 全局掩码 + 每邮箱独立掩码 | 两种列表（标准/扩展）× 四种过滤类型 |
| FD 支持 | 完整（含 BRS/TDC） | 完整（含 BRS/TDC），实测到 2 Mbit/s 数据段 |
| 配置 | RTD 配置工具生成 `Can_Cfg.c` | 手写 `Can_PBcfg.c` |
| 中断 | 每实例一组 IRQ，回调 `CanIf_*` | 每实例一个 IRQ，回调 `CanIf_*`（同构） |

结论：三家的 AUTOSAR 语义层高度一致（HTH/HRH/CAN_BUSY/CanIf 回调），差异集中在
硬件对象数量、过滤器表达能力和消息 RAM 管理方式。本实现的 API 语义与 TC4x/S32K3 对齐，
可以作为移植参考。

## 47.13 参考来源

- AUTOSAR CP SWS CAN Driver（R25-11）：<https://www.autosar.org/fileadmin/standards/R25-11/CP/AUTOSAR_SWS_CANDriver.pdf>
  - 本地抽取文本：`autosar_txt/AUTOSAR_CP_SWS_CANDriver.txt`
- AUTOSAR CP SWS CAN Interface（`CanIf_RxIndication`/`CanIf_TxConfirmation` 签名）：
  <https://www.autosar.org/fileadmin/standards/R25-11/CP/AUTOSAR_SWS_CANInterface.pdf>
- Bosch M_CAN User Manual（M_CAN 控制器寄存器语义）：
  <https://www.bosch-semiconductors.com/ip-modules/can-ip-modules/m-can/>
- HPMicro HPM5E31 数据手册与参考手册：<https://www.hpmicro.com/>
- HPM SDK v1.13.0（`drivers/src/hpm_mcan_drv.c`、`soc/HPM5E00/HPM5E31/hpm_mcan_soc.h`）：
  <https://github.com/hpmicro/hpm_sdk>
- Infineon AURIX TC4x MCAL（MCMCAN）：<https://github.com/Infineon/illd_release_tc4x>
- NXP S32K3 RTD（FlexCAN）：<https://www.nxp.com/design/software/development-software/automotive-software-and-tools>
- ISO 11898-1:2015（CAN 数据链路层与 TEC/REC 规则）
- 本仓库既有直调例程：`samples/can/mcan_two_node`、`samples/can/mcan_quad_load`、`samples/can/mcan_filter`

## 47.14 待确认项与风险

| 项 | 状态 | 说明 |
|---|---|---|
| 经典帧 200 帧 ping-pong 无丢帧 | **已实测** | 两轮 ok=200/200，日志 `logs/20261102-142350-can_classic_echo.log` |
| CAN FD 2 Mbit/s 数据段（64B，BRS） | **已实测** | 50/50，avg 704 µs，TEC/REC 0，日志 `logs/20261102-141808-can_fd_echo.log` |
| 硬件过滤器四种类型 + 全局过滤 | **已实测** | 13/13 PASS，日志 `logs/20261102-141905-can_filter.log` |
| 过滤器元素落点（掩码/范围/双 ID，FIFO0/FIFO1） | **已实测** | 用 `Mailbox->Hoh` 反查，非仅"收到即通过" |
| error passive 进入时刻与 TEC 平台 | **已实测** | 953 µs 进 PASSIVE，TEC 卡在 129，日志 `logs/20261102-142116-can_busoff.log` |
| 4 路并发吞吐/CPU/延迟 | **已实测** | 8366 frame/s、4.62%、avg 246 µs，日志 `logs/20261102-142208-can_irq_perf.log` |
| 中断模型 ISR 只进 1~2 次 | **已定位（现象）** | 现象：置 `CAN_RX_PROCESSING_INTERRUPT=1` 后第一帧可收发，之后 `rx_total` 停在小值、`tx_confirmed=0`；轮询模式同配置完全正常，说明状态机/消息 RAM/过滤器无误，问题在中断接入侧（`.isr_vector` 绑定或 PLIC claim 完成路径）。复跑命令：`mcal/Can/cfg/Can_Cfg.h` 两个中断开关置 1，编译 `can_classic_echo` 后 `bash scripts/hwrun.sh mcal/can_classic_echo --secs 20 --send "can echo 50;can stat"` |
| bus-off 进入未实测 | **已知限制** | 原因：ISO 11898-1 规定 error-passive 节点遇 ACK 错误不再增加 TEC，对端不 ACK 只能到 TEC=128；复跑命令：`can run`（要真正进 bus-off 需外部干扰源或主动发错误帧的节点）。bus-off 恢复代码路径（STARTED 触发 `mcan_recover_from_busoff`）已实现但无实测日志 |
| CAN FD 5 Mbit/s 数据段 | **已实测（2026-11-02 修复并复测）**：根因是位时序配置——TDC 的 SSP 偏移 TDCR.TDCO=20 超过数据段位长 16 tq（80 MHz、BRP=1、tq=12.5 ns、200 ns/位），发送端数据段必然判位错误，TEC 累加到 248 进 bus-off（errstate=2、LEC=7）；TDCO 改 12（16 tq 的 75%）后 500k/5M 与 1M/5M 全部通过（表 47-17、表 47-18），2000 轮 ping-pong 与 3000 帧单向长测 TEC/REC=0。驱动已加 TDC 越界校验，非收发器/线束能力问题 | bash scripts/hwrun.sh mcal/can_fd_echo --secs 45 --lock-wait 1800 --send "can at 2000 3;can stream 3000 3;can stat" |
| `Can_CheckWakeup` 恒返回 E_NOT_OK | **已知限制** | 原因：M_CAN 控制器无 CAN 总线唤醒检测单元，TCAN1044AV 也无 INH 唤醒输出、板上未接唤醒线；复跑命令：`can stat`（观察 SLEEP 状态迁移可用，唤醒检测无硬件支持） |
| `Can_MainFunction_Wakeup` 为空实现 | **已知限制** | 同上（无唤醒源）；API 保留以符合 SWS 完整性要求 |
| PduIdType 依赖 `ComStack_Types.h` | **已定位** | `mcal/common` 暂未提供该头，`Can_GeneralTypes.h` 用 `__has_include` 兜底定义；Lead 补头后自动切换，无需改驱动 |