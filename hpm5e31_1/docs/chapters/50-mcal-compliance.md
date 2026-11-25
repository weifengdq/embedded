# 第 50 章 AUTOSAR 合规性与商用 MCAL 对比

## 50.1 文档信息

| 项 | 内容 |
|---|---|
| 文档版本 | v1.3.0（2026-11-02，R6 MCAL） |
| 适用芯片/板卡 | HPM5E31IPB1 / hpm5e31_1 |
| 对应例程 | `samples/mcal/` 下 15 个模块共 30 个例程（见 表 50-3） |
| 实测日期 | 2026-11-02 |
| 可信度 | 中高。SWS 条款、商用 MCAL 文档数据逐条核对；本实现数据来自上板日志；商用产品的认证结论只引用其公开文档，未做独立复测 |
| 支撑材料 | `docs/mcal/MCAL_API_TRACEABILITY.md`（逐 API 追溯）、`docs/mcal/sws_api_data.json`（SWS 机器清单）、`docs/mcal/traceability_stats.json`（统计） |
| 对比对象 | Infineon AURIX TC4x MC-ISAR 2.20.0（本机 `C:\AURIX_TC4x_MC-ISAR_2.20.0`）、NXP S32K3 RTD 6.0.0（本机 `D:\NXP\SW32K3_S32M27x_RTD_R21-11_6.0.0`） |

## 50.2 本章导览

- 学到什么：
  1. AUTOSAR CP 的分层边界在哪里，MCAL 到底负责什么、不负责什么（图 50-1）；
  2. 怎么用 SWS 原文逐条核对一个 MCAL 实现，而不是靠"看起来像 AUTOSAR"；
  3. 本实现 15 个模块的 API 覆盖到了什么程度，缺的 API 缺在哪（图 50-2、`docs/mcal/MCAL_API_TRACEABILITY.md`）；
  4. 与 Infineon TC4x MCAL 2.20.0、NXP S32K3 RTD 6.0.0 比，差在哪些维度、哪些差距是钱能补的、哪些必须自己写（表 50-6、表 50-7）；
  5. 现有 HPM SDK 直调代码怎么迁到 MCAL API（表 50-8），以及什么时候不该迁；
  6. 从"能跑"到"能量产"还差哪 12 件事（表 50-9）。
- 前置章节：第 42 章（MCAL 总览）、第 43 章（基础设施与配置模型）；各模块实测见第 44~48 章。
- 阅读建议：先看 图 50-1 建立分层概念，再看 表 50-1 确认模块与 SWS 的对应；做技术选型直接跳到 第 7 节与 第 9 节；做代码迁移看 第 8 节。

## 50.3 概述与典型应用

### 50.3.1 一句话定位

本章不谈某个外设怎么配，只回答三个问题：**这套 15 模块 MCAL 合不合 AUTOSAR、跟能买到的商用 MCAL 差多少、离量产还差什么。**

"合规"（compliance）在 AUTOSAR 语境里不是"函数名一样"就算。一个 MCAL 模块要被称为合规，至少要同时满足：API 名称/参数/返回类型与 SWS 一致；每个 API 的 Service ID、同步/异步属性、重入性一致；开发错误码与检测条件一致；配置参数落在 SWS 规定的容器里且语义一致；行为满足 SWS 第 07 章的功能要求（初始化顺序、状态机、错误上报）。商用 MCAL 还会再加一层：MISRA/CERT C 静态分析记录、WCET 测量、安全手册、认证报告。

本实现只承诺前两项里"API 签名 + Service ID + 主要 DET 检查"这一档，其余在 第 9 节 逐条列为差距，不打包成"合规"。

### 50.3.2 关键数字

表 50-1 本实现的合规定位（截至 v1.4.0）

| 维度 | 数值 | 说明 |
|---|---|---|
| 模块数 | 15 | Mcu/Port/Dio/Gpt/Pwm/Icu/Wdg/Adc/Spi/I2c/Can/Mem/Crc/Eth/EthTrcv |
| SWS 基准 | AUTOSAR CP R25-11 | 本地抽取文本 `autosar_txt/`，共 34 个 SWS |
| SWS 规定 API 总数 | 233 | 15 个模块 §50.10.3 函数定义合计（第六轮 13 模块 160 + Eth 42 + EthTrcv 31） |
| 已实现 API | 172（覆盖率 74%） | 判定口径见 `docs/mcal/MCAL_API_TRACEABILITY.md` §0 |
| 带 DET 检查的 API | 147 | 只统计函数体内出现 `MCAL_DET_REPORT` 的 API |
| 本实现代码量 | 14959 行 | `mcal/<Mod>/{include,src,cfg}` 的 C/H 行数 |
| 上板例程 | 34 个 | 其中 24 个经 `scripts/hwrun.sh` 烧写实测 PASS，2 个有已知限制（见 第 15 节），日志在 `logs/` |
| 商用对照 | 2 家 | Infineon TC4x MCAL 2.20.0、NXP S32K3 RTD 6.0.0 |

### 50.3.3 适用场景

- **学习/教学**：想弄明白 AUTOSAR MCAL 到底怎么写，而不是只调 SDK。
- **原型验证**：先用 MCAL API 写应用层，之后换芯片时上层代码不改。
- **AUTOSAR 上层集成预研**：在 HPM5E31 上把 IoHwAb/CanIf 一类简单 BSW 接起来，验证接口设计。
- **不适用**：量产车规项目。原因见 第 9 节 的 12 项差距，其中最硬的是安全案例与 WCET。

## 50.4 工作原理：AUTOSAR CP 分层与 MCAL 的边界

### 50.4.1 分层与依赖方向

AUTOSAR CP 的分层是一条单向依赖链：应用层只通过 RTE 访问 BSW，BSW 只通过 MCAL 访问寄存器，MCAL 只通过寄存器访问硬件。图 50-1 画出了本仓库对应的位置：MCAL 之下是 HPM5E31 的寄存器，之上是 BSW 的 ECU 抽象层。

![图 50-1 AUTOSAR CP 分层与本实现的 MCAL 边界](figures/50-mcal-compliance/fig-50-1-autosar-layers.png)

这条链在代码里的落地方式是三条硬规则，第 43 章已经讲过，这里从"合规"角度再强调一次：

1. **公共头文件（`<Mod>.h`）不许 include 任何 HPM SDK 头**。否则上层 BSW 编译时会被迫带上 SDK 的 `soc.h`、中断号、基地址宏，换芯片就得改上层。
2. **板级信息（引脚、实例、时钟）只能出现在 `*_PBcfg.c`**。驱动 `.c` 里出现 `HPM_MCAN0`、`PA02` 这类硬编码，等价于把板子焊死在代码里。
3. **每个 API 入口先查状态/参数，再动作**。这是 SWS 第 07 章对每个模块的普遍要求，检查结果通过 `Det_ReportError` 上报。

表 50-2 分层与仓库对应关系

| 层 | 职责 | 本仓库对应 | 备注 |
|---|---|---|---|
| 应用层 SWC | 业务逻辑 | 例程 `main.c` 直接调 MCAL | 本章例程省略 RTE，属于"验证用最短路径" |
| RTE | SWC 与 BSW 的接口 | 无 | 不在本轮范围 |
| 服务层 BSW | Dem/Det/EcuM/NvM/ComM | `mcal/common/Det.[ch]`、`Dem.[ch]` | Det 有记录与查询的最小实现；Dem 只是桩 |
| ECU 抽象层 | IoHwAb/MemIf/CanIf/PduR/CDD | 无 | 量产项目需要，本轮不实现 |
| OS | 任务/ISR/计数器 | 例程用主循环 `bsp_shell_poll()`；ISR 直接挂中断向量 | 无 OS-Application/分区概念 |
| MCAL | 15 个驱动 | `mcal/<Mod>/` | 本章主角 |
| 硬件 | HPM5E31 + 板级外设 | `boards/hpm5e31_1/` | 寄存器级访问 |

### 50.4.2 MCAL 在 RTE/BSW/OS 中的实际位置

实际工程里，MCAL 被三类调用者使用，这三类调用对 MCAL 的要求并不相同：

- **RTE 经 IoHwAb/CanIf 下来**：要求 API 线程安全、参数校验严格、DET 只在开发阶段开。此时 `<Mod>_Init` 由 EcuM 在启动阶段调用，运行时 API 由 BSW 任务调用。
- **OS 的 ISR 上下文**：要求 ISR 里只做状态推进与通知回调，不做阻塞等待。本实现的 `<Mod>_Isr_<instance>` 就是这个约束下的产物，例如 Gpt 的周期中断只置标志并调 `Gpt_Notification_<ch>`。
- **CDD/复杂驱动直调**：允许绕开 BSW，但仍走 MCAL API，不允许直接碰寄存器——否则 MCAL 的抽象边界就漏了。

图 50-1 里从 OS 指向 MCAL 的虚线就是这个场景。它也是本实现与商用 MCAL 的一个差距点：商用 MCAL 会提供 `*_Cbk.h` 与 OS 的 Alarm/Task 映射说明，本实现只保证"在 ISR 里调 API 不炸"，没有 OS 集成层。

### 50.4.3 三种配置阶段的落地

AUTOSAR 规定 MCAL 配置分 pre-compile、link-time、post-build 三种。本仓库的做法与第 43 章一致：

- **pre-compile**：`<Mod>_Cfg.h` 里的开关，如 `<MOD>_DEV_ERROR_DETECT`、`<MOD>_VERSION_INFO_API`、通道数上限；
- **post-build**：`<Mod>_PBcfg.c` 里的 `const <Mod>_ConfigType <Mod>_Config`，含引脚、实例、时钟、回调；
- **link-time**：本轮统一用 post-build 指针替代，没有单独实现 link-time 变体。这一点在 第 9 节 的差距表里单独列出。

商用 MCAL 三种都支持，而且 post-build 配置是由 EB tresos/NXP S32CT 从 `.arxml`/`.xdm` 生成的 `.c`。本实现的 `*_PBcfg.c` 是手写的，这是"配置工具"这一项差距的由来。

## 50.5 本实现的合规定位与核对方法

### 50.5.1 模块清单与 SWS 对应

表 50-3 15 个模块与 SWS 文档、例程、章节的对应（模块 ID 取自 `mcal/common/Mcal_ModuleId.h`，与 SWS_BSWGeneral §50.13.2 一致）

| 模块 | 模块 ID | SWS 文档 | 例程（`samples/mcal/`） | 章节 |
|---|---|---|---|---|
| Mcu | 101 | SWS_MCUDriver | `mcu_clock_init`、`mcu_reset_mode` | 45 |
| Port | 124 | SWS_PortDriver | `port_pin_config` | 45 |
| Dio | 120 | SWS_DIODriver | `dio_led_button` | 45 |
| Gpt | 100 | SWS_GPTDriver | `gpt_periodic`、`gpt_oneshot` | 46 |
| Pwm | 121 | SWS_PWMDriver | `pwm_basic`、`pwm_notify` | 46 |
| Icu | 122 | SWS_ICUDriver | `icu_capture` | 46 |
| Wdg | 102 | SWS_WatchdogDriver | `wdg_trigger`、`wdg_timeout_reset` | 46 |
| Adc | 123 | SWS_ADCDriver | `adc_single`、`adc_bench` | 47 |
| Spi | 83 | SWS_SPIHandlerDriver | `spi_master_slave`、`spi_async_job` | 47 |
| I2c | 113 | SWS_I2CDriver | `i2c_master_slave`、`i2c_error` | 47 |
| Can | 80 | SWS_CANDriver | `can_classic_echo`、`can_fd_echo`、`can_irq_perf`、`can_busoff`、`can_filter` | 48 |
| Mem | 91 | SWS_MemoryDriver（R25-11 起原 Fls 并入） | `mem_flash_rw`、`mem_bench` | 49 |
| Crc | 201 | SWS_CRCLibrary | `crc_bench`、`crc_integrity` | 49 |
| Eth | 88 | SWS_EthernetDriver（+ SWS_EthernetInterface 分层参考） | `eth_raw_echo`、`eth_irq`、`eth_stats` | 50 |
| EthTrcv | 73 | SWS_EthernetTransceiverDriver | `eth_trcv` | 50 |

### 50.5.2 核对方法：把 SWS 变成机器可读的清单

人工翻 15 个 SWS 的 §8.3 表格容易漏。本仓库的做法是先用脚本把 SWS 抽取文本解析成 JSON，再用第二个脚本对着本实现头文件逐条比对：

1. `docs/mcal/parse_sws.py`（Windows 侧运行）从 31 个 `AUTOSAR_CP_SWS_*.txt` 里抽取：每个 `Definition of API function` 块（API 名、语法、Service ID、Sync/Async、重入性、说明）、§50.10.2 类型、§50.9.2 开发错误码、§8.4/8.5 回调与调度函数、§50.12 配置容器，输出 `docs/mcal/sws_api_data.json`；
2. `docs/mcal/gen_traceability.py`（仓库内运行）解析 `mcal/<Mod>/include/<Mod>.h` 的公开声明、`*_API_ID_*`、`<MOD>_E_*`、版本宏，再逐个 API 到 `.c` 里找函数体、检查有没有 `MCAL_DET_REPORT`，输出 `docs/mcal/MCAL_API_TRACEABILITY.md` 与 `traceability_stats.json`；
3. 人工只做两件事：判断哪些 API 属于"部分实现"（例如只支持轮询不支持异步），以及写清偏差理由。

这套流程的价值在于可复跑：SWS 版本升级或代码改动后，两条命令就能重出追溯表，而不是靠人记得上次核到哪。

### 50.5.3 依赖规则的自检结果

合规的第一道门槛不是 API 数量，而是"边界有没有漏"。用三条 grep 就能查：

表 50-4 依赖规则自检（命令可在仓库根目录复跑）

| 规则 | 检查命令 | 期望 | 实测结果 |
|---|---|---|---|
| 公共头不 include SDK 头 | `grep -rn "hpm_" mcal/*/include/*.h` | 0 行 | 0 行 |
| 公共头不 include SDK 头（含间接） | `grep -rn "soc.h\|drivers/inc" mcal/*/include/*.h` | 0 行 | 0 行 |
| 板级引脚只出现在 PBcfg | `grep -rn "HPM_IOC\|IOC_PAD_P" mcal/*/src/*.c` | 0 行 | 8 行（需复核） |
| 模块 ID 用公共定义 | `grep -rn "_MODULE_ID" mcal/*/include/*.h` | 全部指向 `Mcal_ModuleId.h` | 14 个公共头全部使用 Mcal_ModuleId.h（见 §50.4 表） |
| 每个 API 有 DET 检查 | `python3 docs/mcal/gen_traceability.py --check` | DET 覆盖数见 表 50-5 | 135 个已实现 API 中 113 个函数体有 DET 检查 |

表 50-4 里出现"0 行"才是合格；只要驱动 `.c` 里冒出一个 `HPM_IOC`，换板就要改驱动，抽象层就白做了。

## 50.6 API 追溯矩阵：覆盖率与偏差

### 50.6.1 覆盖到什么程度

追溯矩阵的完整版在 `docs/mcal/MCAL_API_TRACEABILITY.md`（逐 API 一行，含 Service ID、同步/异步、是否有 DET 检查、未实现理由）。这里只给汇总。图 50-2 把"模块 × API 组"的覆盖情况画成热力图，空白格表示 SWS 在该组没有定义 API，红色表示本实现尚未落地，绿色表示已实现。

![图 50-2 SWS API 覆盖率热力图（行＝模块，列＝API 组）](figures/50-mcal-compliance/fig-50-2-api-coverage.png)

表 50-5 15 个模块的 API 覆盖汇总（由 `docs/mcal/gen_traceability.py` 生成）

| 模块 | SWS API | 已实现 | 部分 | 未实现 | 覆盖率 | 有 DET 的 API | 代码行数 |
|---|---|---|---|---|---|---|---|
| Mcu | 11 | 11 | 0 | 0 | 100% | 11 | 857 |
| Port | 5 | 5 | 0 | 0 | 100% | 5 | 568 |
| Dio | 9 | 9 | 0 | 0 | 100% | 9 | 470 |
| Gpt | 14 | 11 | 0 | 3 | 79% | 11 | 902 |
| Pwm | 13 | 9 | 0 | 4 | 69% | 9 | 806 |
| Icu | 26 | 17 | 0 | 9 | 65% | 17 | 960 |
| Wdg | 4 | 4 | 0 | 0 | 100% | 4 | 379 |
| Adc | 17 | 13 | 0 | 4 | 76% | 13 | 990 |
| Spi | 14 | 14 | 0 | 0 | 100% | 13 | 1367 |
| I2c | 8 | 8 | 0 | 0 | 100% | 8 | 735 |
| Can | 18 | 13 | 0 | 5 | 72% | 13 | 2205 |
| Mem | 12 | 12 | 0 | 0 | 100% | 0 | 821 |
| Crc | 9 | 9 | 0 | 0 | 100% | 0 | 1005 |
| Eth | 42 | 20 | 0 | 22 | 48% | 19 | 1941 |
| EthTrcv | 31 | 17 | 0 | 14 | 55% | 15 | 953 |
| **合计** | **233** | **172** | **0** | **61** | **74%** | **147** | **14959** |

三个读表要点：

1. **缺口集中在"可选功能"而不是"主路径"**。未实现的 API 分三类：唤醒类（`Gpt_*Wakeup`、`Icu_*Wakeup`）、低功耗状态机（`Pwm_*PowerState`）、硬件时间戳/CAN PN（`Can_*TimeStamp`、`Can_SetCanPnFrameDataMask`），其余是 Icu 的边沿计数与异步通知族。这些都是 SWS 允许用配置开关裁剪的可选项，主路径（Init/读写/启动停止/状态查询）全部落地。
2. **Crc 是唯一"零缺口但零 DET"的模块**。SWS_CRCLibrary 明确"没有开发错误"，8 个 `Crc_CalculateCRC*` 是纯函数，所以本模块 `CRC_DEV_ERROR_DETECT=STD_OFF`，表里 DET 列为 0 是正确的，不是漏做。
3. **覆盖率不是质量指标**。一个覆盖率 70% 但每条错误路径都测过的实现，比覆盖率 100% 但只跑过 happy path 的实现更可靠。表里的数字只用来回答"哪些 API 还没写"。

### 50.6.2 与 SWS 的已知偏差

完整偏差清单（含文件与行为描述）在 `docs/mcal/MCAL_API_TRACEABILITY.md` §50.5，共 20 条。这里挑出对使用者影响最大的 8 条：

表 50-6 影响最大的 8 条偏差（不夸大合规性，逐条可查）

| # | 偏差项 | SWS 依据 | 本实现现状 | 影响 |
|---|---|---|---|---|
| 1 | AUTOSAR 版本宏写 4.4.0 | 各 SWS 文件头 R25-11 | 模块头 `<MOD>_AR_RELEASE_*_VERSION` 统一 4.4.0 | 版本追溯字段与 SWS 文件头对不上，集成商查版本时会误判 |
| 2 | 无 link-time 配置变体 | SWS 支持 pre-compile/link-time/post-build 三种 | 只做 pre-compile + post-build 指针 | 同一份二进制无法通过链接切换配置集 |
| 3 | 无多分区/多核支持 | SWS 支持 partition 映射 | `InstanceId` 固定 0，无分区概念 | 不能直接跑在多核 OS 的不同 partition 上 |
| 4 | Dem 只有桩 | Mcu/Wdg 等模块的生产错误要报 Dem | `mcal/common/Dem.c` 仅记录计数 | 生产错误无法进 DTC 管理 |
| 5 | 无 `*_Cbk.h` 回调头 | Can/Icu 等规定回调原型的头文件 | 回调在 `<Mod>.h` 里声明 | 与 CanIf/Os 集成时要手工补原型 |
| 6 | 配置参数是 SWS 容器的子集 | SWS §10 每模块 5~18 个容器 | `*_PBcfg.c` 手写字段，覆盖常用项 | 换 ECU 配置要改代码，不能由配置工具生成 |
| 7 | 无 ASIL 安全论证与安全手册 | ISO 26262 对 MCAL 的要求 | 无安全手册、无 FMEDA/DFA | 不能作为安全相关开发项 |
| 8 | Mem 的 `E_MEM_SERVICE_NOT_AVAIL` 取 0x02 | SWS 引用了该符号但未给数值 | 按 Vector/EB 惯例取 0x02 | 与其它厂商实现可能数值不同，跨平台代码要小心 |

### 50.6.3 追溯矩阵怎么用

- **选型时**：看 表 50-5 的"未实现"列，确认你要用的 API 在里面没有。比如要做 PWM 输入测量，就要接受 Icu 的测量 API 不完整。
- **集成时**：看 `docs/mcal/MCAL_API_TRACEABILITY.md` 每个模块的"配置参数对应"段，确认你要改的配置项本实现有没有。
- **升级时**：SWS 升版后重跑 `python3 docs/mcal/gen_traceability.py`，看哪些 API 新增、哪些签名变了。

## 50.7 与 Infineon TC4x MCAL 2.20.0 对比

### 50.7.1 交付物与工具链

Infineon 的 AURIX_TC4x_MC-ISAR 2.20.0 是这个量级里资料最全的参照物：它不是"一堆驱动源码"，而是源码 + 配置插件 + 需求追溯 + 安全文档 + 质量报告的整套交付。

表 50-7 TC4x MCAL 2.20.0 交付物清单（数据来自其 Release Notes 与安装目录）

| 项 | 内容 |
|---|---|
| 版本/日期 | 2.20.0，2026-03-16，Release method: via Release Area，质量等级 PR（Production） |
| AUTOSAR 版本 | AS20-11、AS21-11；Mem/MemAcc/Fee 基于 AS21-11；另提供 AS20-11 MINI 包（Dio/Mcu/Port/Spi/Smu/Mem） |
| 模块数 | 30 个模块目录（含 Bfx/Bmc/Dma/Dre/Dsadc/Eth/Gtm/Lin/Ocu/PcieEp/Smu/Uart 等） |
| 配置工具 | EB tresos v29.7.5 + Ifx-M2M-4.3.0 代码生成库；Tresos 插件在 `MCAL/01_PluginsTresos` |
| 静态分析 | Polyspace R2024b Update 5；随包 `MISRA_Justification.html`、`CERTC_Justification.html` |
| 编译器 | TASKING v10.4r1、HighTec v10.1.0、GHS v2025.1、WindRiver v5.9.9.1 |
| 测试平台 | TC4Dx AA/AB、TC45x AA、TC48x AA、TC49xN AB；TriBoard 三款 |
| 需求追溯 | `Docs/AURIX_TC4x_MC-ISAR_Req_Extract_Autosar_R20-11_R21-11.pdf` + 每模块 `.reqifz` |
| 安全文档 | Safety Manual 需求抽取（`Req_Extract_Safety Manual.pdf`）、`HWErrataAnalysis.xlsx`；Safety Case Report 单独发布 |
| 性能数据 | `AURIX_TC4x_MC-ISAR_SW_WCET.xlsx`（逐 API 的 WCET） |
| 资源数据 | `Memory_Consumption.zip`（HTML 报告 + 每模块 `.arxml`）、`CSRM_Memory_Consumption.zip` |
| 目录结构 | `MCAL/<Mod>/ssc/{inc,src}`（驱动）+ `MCAL/01_PluginsTresos/eclipse/Plugins/<Mod>_Aurix3G`（Tresos 插件）+ `MCAL/00_Common`（编译器抽象/标准类型）+ `Docs/`（UM、需求抽取、质量报告）+ `Tools/`（license 管理） |
| 构建集成 | Tresos 生成配置 → 与驱动源码一起编进工程；随包不给 CMake，集成方式见 UM 的 Build & Installation |
| 许可 | Infineon SLA（`Tools/License/SLA_IFAG_M2M_Core_and_Helper.pdf`）+ EB tresos 商业许可；经 MyICP 发布 |

这张表里最值得注意的不是"文档多"，而是**每一类证据都对应一个合规要求**：需求追溯对应 SWS 条款，MISRA 偏差说明对应编码规范，WCET 对应时序安全，内存报告对应资源预算，Safety Manual 对应 ISO 26262。本实现这五类证据目前只有第一类的一部分。

### 50.7.2 代码与资源规模

表 50-8 TC4x MCAL 15 个对标模块的公开声明与资源占用

| 模块 | 头文件公开 API 数 | 源码行数 | Code（字节） | RAM（字节） | 配置数据（字节） | 最大栈（字节） |
|---|---|---|---|---|---|---|
| Mcu | 30 | 9364 | 11192 | 45 |
| Port | 8 | 2742 | 2716 | 45 |
| Dio | 9 | 2164 | 1566 | 45 |
| Gpt | 17 | 6691 | 7402 | 46 |
| Pwm | 14 | 6775 | 4344 | 46 |
| Icu | 28 | 8731 | 7456 | 46 |
| Wdg | 5 | 1801 | 1688 | 46 |
| Adc | 19 | 11568 | 18078 | 47 |
| Spi | 21 | 10768 | 15324 | 47 |
| I2c | 52 | 8874 | 9950 | 47 |
| Can | 25 | 13736 | 14592 | 48 |
| Mem | 15 | 16634 | 7710 | 49 |
| Crc | 15 | 4059 | 3242 | 49 |

数据来源：API 数与源码行数为 2026-11-02 对本机 `C:\AURIX_TC4x_MC-ISAR_2.20.0\MCAL\<Mod>\ssc\` 的统计（只算 `inc`+`src` 的 `.c/.h`）；Code/RAM/配置数据/栈来自官方 `Memory_Consumption` 报告（配置为 `AS460_TC4D9_COM_Host_Config1` 一类参考配置，不同配置差异很大，报告本身注明"indicative"）。

这张表对做资源预算的人最有用的两个数：**Can 的 Code 约 14.6 KB、Adc 约 18 KB**，配置数据 `Pwm` 一项就 6 KB（因为比较器/通道表大）。本实现同模块的实测占用在 第 11 节。

### 50.7.3 API 风格差异

TC4x 在 SWS 之外加了一批厂商 API，最典型的是 `Mcu_InitCheck`：SWS 没有这个函数，Infineon 用它做启动后的初始化自检（校验时钟/PLL/寄存器是否被意外改动）。这类扩展在合规上没问题——SWS 允许厂商扩展，只要不改变标准 API 语义。对本实现的启示是：**想加“自检”能力，应该加扩展 API 而不是改标准 API 行为**。本实现的 `Mem_IsJobPending`、`Mem_GetInstanceCount` 就是按这个原则加的。

另一个差异是错误码。TC4x 头文件里 `<Mod>_<API>_SID` 与 `<MOD>_E_*` 是按 SWS 逐条定义的，头文件注释直接标注 `[cover parentID={...}]` 的需求追溯 ID。本实现只在头文件注释里写了 SWS 章节号，没有需求 ID——这是 第 9 节"需求追溯"差距的具体表现。

## 50.8 与 NXP S32K3 RTD 6.0.0 对比

### 50.8.1 架构：IP 层 + 高层驱动两层

NXP 的 Real-Time Drivers（RTD）6.0.0 结构与 Infineon 不同，它是两层：

- **IP 层**：`Clock_Ip`、`Power_Ip`、`Ram_Ip`、`Can_Ip` 一类，直接操作寄存器，给所有外设复用；
- **AUTOSAR 高层驱动**：`Mcu`、`Port`、`Dio` 等，实现 SWS API，内部调用 IP 层。

这个设计的好处是 IP 层可以被非 AUTOSAR 代码复用，坏处是同一个模块的代码量翻倍。表 50-9 里 Mcu 插件 3.8 万行，其中相当一部分是 IP 层与配置生成器。

表 50-9 NXP S32K3 RTD 6.0.0 交付物清单

| 项 | 内容 |
|---|---|
| 版本/AUTOSAR | 6.0.0，AUTOSAR R21-11（文档号 `UM34MCUASRR21-11 Rev0000R6.0.0`） |
| 插件数 | `eclipse/plugins` 下 51 个 `*_TS_T40D34M60I0R0` 插件：12 个 MCAL 驱动 + CanIf/Dem/Det/EcuM/Os/Rte 等 BSW + IP 层 |
| 配置工具 | EB tresos Studio 29.0.0+（`.xdm`/`.epc`）；S32 Design Studio 的 S32CT 配置器（`.mex`/`.epc`）；生成器为 JS |
| 编译器 | NXP GCC 10.2.0、Wind River Diab 7.0.6、GHS Multi 7.1.6d、IAR 8.50.10（safety version） |
| 文档 | 每模块 `doc/RTD_<MOD>_UM.pdf` 与 `RTD_<MOD>_IM.pdf`（用户手册 + 集成手册） |
| 偏差声明 | UM 第 3.4 节 "Deviations from Requirements" 逐条列 SWS 需求编号与 N/S、N/I、N/F 状态（见 6.2） |
| 目录结构 | `eclipse/plugins/<Mod>_TS_*/{include,src,config,config_ext,doc,generate_PB,generate_PC,generate_swcd,examples}`（每个插件自带配置 schema、生成器、文档、示例） |
| 构建集成 | `generate_PB`/`generate_PC` 脚本 + 各编译器 makefile 片段（GCC/DIAB/GHS/IAR），集成步骤见 `RTD_<MOD>_IM.pdf` |
| 资源数据 | 随包未见统一 memory consumption 报告；需按配置自行编译测量 |
| 许可 | `license.rtf`：NXP S32 Platform Software License Agreement v1.10（2024-09），需账号下载 |

### 50.8.2 偏差声明：值得抄的做法

NXP 的 Mcu UM 里有一张表，把"没有完全实现/实现方式不同/超范围"的 SWS 需求逐条列出，每条给需求编号（如 `SWS_Mcu_00053`）、状态（N/S 超范围、N/I 未实现、N/F 未完全实现）和备注。例如：

- `SWS_Mcu_00053`（时钟失效要报 `MCU_E_CLOCK_FAILURE`）：状态 N/S，备注"ISR 上下文不能报 Dem，改为定义 `MCU_E_ISR_CLOCK_FAILURE` 并由 CMU 中断上报"；
- `SWS_Mcu_00245`（影响多个硬件模块的寄存器要由 Mcu 初始化）：状态 N/S，备注"由单独的共享 IP 插件负责"。

这件事的意义在于：**商用 MCAL 也不追求 100% 按 SWS 字面实现，但会把偏差写清楚**。本仓库的 `docs/mcal/MCAL_API_TRACEABILITY.md` §50.5 照的就是这个格式，只是条目少、粒度粗。

### 50.8.3 规模对比

表 50-10 13 个对标模块的源码规模（只统计驱动 `include`+`src`，不含示例/生成器/文档）

| 模块 | 本实现（行） | TC4x（行） | NXP RTD（行） |
|---|---|---|---|
| Mcu | 857 | 9364 | 38063 |
| Port | 568 | 2742 | 6251 |
| Dio | 470 | 2164 | 3270 |
| Gpt | 902 | 6691 | 17993 |
| Pwm | 806 | 6775 | 24608 |
| Icu | 960 | 8731 | 16387 |
| Wdg | 379 | 1801 | 9367 |
| Adc | 1902 | 11568 | 36447 |
| Spi | 1367 | 10768 | 20693 |
| I2c | 699 | 8874 | 14202 |
| Can | 2027 | 13736 | 22525 |
| Mem | 821 | 16634 | 10834 |
| Crc | 1005 | 4059 | 7546 |
| **合计** | **12763** | **103907** | **228186** |

![图 50-3 15 个模块驱动源码规模对比（对数轴）](figures/50-mcal-compliance/fig-50-3-code-scale.png)

规模差 10~40 倍不奇怪：商用 MCAL 要做多型号衍生芯片适配、多编译器支持、全部 SWS 错误路径、配置校验、安全机制。本实现只针对一块板、一个编译器、主路径。**但规模差距不能直接换算成"工作量差距"**——真正的工作量在验证与文档上，见 第 9 节。

## 50.9 三方横向对比与选型

### 50.9.1 总表

表 50-11 本实现 / Infineon TC4x MCAL / NXP S32K3 RTD 横向对比

| 维度 | 本实现（HPM5E31 MCAL 子集） | Infineon TC4x MCAL 2.20.0 | NXP S32K3 RTD 6.0.0 |
|---|---|---|---|
| 上游 AUTOSAR 版本 | CP R25-11（抽取文本） | AS20-11、AS21-11 | R21-11 |
| 模块数 | 13 | 30 | 12 个 MCAL + BSW（51 个插件） |
| 配置工具 | 无，手写 `*_Cfg.h`/`*_PBcfg.c` | EB tresos 29.7.5 + Ifx-M2M 生成器 | EB tresos 29+ 或 S32DS S32CT |
| 配置输入格式 | C 源码 | `.xdm` + 生成 C | `.xdm`/`.mex` + `.epc` 标准化交换格式 |
| API 风格 | SWS 原名，少量扩展（`Mem_IsJobPending`） | SWS + 厂商扩展（如 `Mcu_InitCheck`） | SWS + IP 层（`Clock_Ip`/`Power_Ip`） |
| 错误处理 | `Det_ReportError` + 模块错误码 | Det/Dem + 厂商错误码 + 错误回调 | Det/Dem + 厂商错误回调 |
| 需求追溯 | 注释里写 SWS 章节号 | 每条需求带 `parentID`，随包 `.reqifz` | UM 里逐条列偏差（N/S、N/I、N/F） |
| 静态分析证据 | 仅 `-Wall -Wextra` 零告警 | MISRA C:2023 / CERT C:2016 偏差说明文档 | 随包未见（另发） |
| WCET 数据 | 无 | `SW_WCET.xlsx` 逐 API | 随包未见（另发） |
| 资源占用数据 | 本文 第 11 节实测 | 官方 `Memory_Consumption` 报告 | 随包未见，需自行编译 |
| 安全文档 | 无 | Safety Manual 需求抽取 + Safety Case 另发 | 安全手册另发 |
| 多核/分区 | 无 | 支持 partition/multicore | 支持（M7 单核，分区由 OS 提供） |
| 代码规模（13 模块） | 12763 行 | 103907 行 | 228186 行 |
| 许可 | 项目内部自用，未声明开源许可 | Infineon SLA + EB tresos 商业许可 | NXP S32 Platform Software License v1.10 |
| 获取方式 | 本仓库 | MyICP 账号 + 安装包 | NXP 官网账号 + 安装包 |
| 支持芯片 | HPM5E31 | TC4Dx/TC45x/TC48x/TC49xN 多型号 | S32K3 系列多型号 |

### 50.9.2 差距的三种性质

把 表 50-11 的差距按"怎么补"分类，比按"差多少"分类有用：

- **钱能补的**：配置工具、MISRA/Polyspace 报告、WCET 测量、安全手册、认证。这些是买工具 + 买服务 + 投入人力做流程，不需要重写驱动。
- **代码要补的**：缺的 API（Icu 测量族、Can 的 PN/时间戳、Adc 的硬件触发完整链路）、多核分区、link-time 配置、Dem 集成、`*_Cbk.h`。这些要一行行写。
- **架构决定的、补起来最贵的**：商用 MCAL 的"多型号衍生芯片适配层"（TC4x 一套代码支持 5 个型号、NXP 支持整个 S32K3 家族）。本实现与板子绑死，换芯片等于重写 PBcfg 与部分驱动。这部分差距不是工作量问题，是产品定位问题。

### 50.9.3 选型建议

表 50-12 选型建议（按场景）

| 场景 | 建议 | 理由 |
|---|---|---|
| 量产车规项目，需要 ASIL 证据 | 商用 MCAL（Infineon/NXP/Vector 等） | 安全案例、WCET、认证报告不是短期能自建的 |
| 基于 HPM5E31 做 AUTOSAR 上层预研 | 本实现 + 第 43 章的集成方式 | 接口按 SWS，上层代码可复用；缺的 API 可局部补 |
| 已有 HPM SDK 工程想接 AUTOSAR | 本实现作为 MCAL 适配层 | 第 8 节给了逐外设迁移映射，工作量可控 |
| 教学/培训 | 本实现 + 第 44~48 章 | 代码量小，能逐行讲清"MCAL 到底做了什么" |
| 只做原型、不接 AUTOSAR | 直接调 HPM SDK 更快 | MCAL 的配置/错误检查在原型阶段是负担 |

## 50.10 HPM SDK 直调 ↔ MCAL API 迁移映射

### 50.10.1 逐外设映射

现有 HPM SDK 工程的典型写法是 `board_init()` 之后直接调 `hpm_xxx_drv` 的函数、直接写 `HPM_IOC->PAD[]`。迁到 MCAL 后，这些调用要换成 SWS API，板级常量要挪进 `*_PBcfg.c`。表 50-13 是逐外设映射。

表 50-13 功能级迁移映射（SDK 函数名已在本机 `hpm_sdk/drivers/inc` 核对存在）

| 功能 | HPM SDK 直调 | MCAL API | 迁移要点 |
|---|---|---|---|
| 时钟/PLL | `board_init_clock()`、`clock_add_to_group()`、`clock_set_source_divider()` | `Mcu_Init` + `Mcu_InitClock` + `Mcu_DistributePllClock` | 时钟树参数进 `Mcu_PBcfg.c`；去掉 board.c 里的直接分频 |
| 引脚复用 | `HPM_IOC->PAD[n].FUNC_CTL` 直写 | `Port_Init` + `Port_SetPinMode` | 引脚/复用功能表进 `Port_PBcfg.c` |
| GPIO 输出 | `gpio_set_pin_output()` + `gpio_write_pin()` | `Dio_WriteChannel` / `Dio_FlipChannel` | 端口+引脚 → `Dio_ChannelType` 编号 |
| GPIO 输入 | `gpio_read_pin()` | `Dio_ReadChannel` | 分组读用 `Dio_ReadChannelGroup` 一次读 32 位 |
| 定时器 | `gptmr_channel_config()` + `gptmr_start_counter()` + `gptmr_enable_irq()` | `Gpt_Init` + `Gpt_StartTimer` + `Gpt_EnableNotification` | 时钟源/预分频进 PBcfg；通知回调在 PBcfg 注册 |
| PWM 输出 | `pwm_setup_waveform()` + `pwm_start_counter()` + `pwm_update_duty_edge_aligned()` | `Pwm_Init` + `Pwm_SetDutyCycle` + `Pwm_SetPeriodAndDuty` | 极性/空闲电平/通道表进 PBcfg |
| 输入捕获 | 手动配 GPTMR 捕获 + 中断读计数 | `Icu_Init` + `Icu_StartSignalMeasurement` + `Icu_GetDutyCycleValues` | 边沿/测量模式在 PBcfg；结果用 SWS 结构体 |
| 看门狗 | `wdg_enable()` / EWDG 寄存器 | `Wdg_Init` + `Wdg_SetMode` + `Wdg_SetTriggerCondition` | 超时窗口进 PBcfg；trigger 由周期任务调用 |
| ADC | `adc16_init()` + `adc16_init_channel()` + `adc16_get_prd_result()` | `Adc_Init` + `Adc_SetupResultBuffer` + `Adc_ReadGroup` | 通道组/触发源进 PBcfg；结果缓冲区应用提供 |
| SPI | `spi_control_init()` + `spi_transfer()` | `Spi_Init` + `Spi_SetupEB` + `Spi_AsyncTransmit` + `Spi_ReadIB` | Job/Sequence/IB 三层模型；异步要周期调 `Spi_MainFunction_Handling` |
| I2C | `i2c_master_write()` / `i2c_master_read()` + 中断状态机 | `I2C_Init` + `I2C_AsyncTransmit` + `I2C_GetSequenceResult` | 序列化传输；地址/速率进 PBcfg |
| CAN | `mcan_init()` + `mcan_write_txbuf()` + 中断收帧 | `Can_Init` + `Can_Write` + `Can_SetControllerMode` | HRH/HTH 句柄；位时序进 PBcfg；BusOff 恢复用模式 API |
| Flash 读写 | XPI NOR ROM API / 寄存器 | `Mem_Init` + `Mem_Write` + `Mem_Erase` + `Mem_MainFunction` | 作业式异步；先 `Mem_GetJobResult` 再下一次 |
| CRC | `crc_setup_channel_config()` + `crc_calc_block_bytes()` | `Crc_CalculateCRC32` 等 | 多项式/初值/异或值语义逐项对照 SWS |

### 50.10.2 一段对照代码：GPIO 翻转

SDK 直调版（节选，实际工程里这种代码到处都是）：

```c
#include "hpm_gpio_drv.h"
/* board_init() 之后：PA02 已由 board/pinmux.c 配成 GPIO 输出 */
gpio_toggle_pin(HPM_GPIO0, GPIO_DI_GPIOA, 2);   /* 翻转 PA02 */
```

MCAL 版（初始化一次，之后只调 SWS API）：

```c
#include "Dio.h"
#include "Port.h"
#include "Mcu.h"

int main(void)
{
    bsp_init();
    Mcu_Init(&Mcu_Config);
    (void)Mcu_InitClock(MCU_CLOCK_TYPE_PLL0);
    (void)Mcu_DistributePllClock();
    Port_Init(&Port_Config);              /* PA02 的复用/方向在这里配好 */
    Dio_WriteChannel(DIO_CHANNEL_USR_LED, STD_HIGH);
    while (1) {
        Dio_FlipChannel(DIO_CHANNEL_USR_LED);   /* 等价于 gpio_toggle_pin */
        board_delay_ms(500);
    }
}
```

差别在于：`DIO_CHANNEL_USR_LED` 是 PBcfg 里的逻辑通道号，不是 `GPIO_DI_GPIOA`+引脚号；换板只改 PBcfg，`main.c` 不动。代价是初始化顺序被固定成"时钟→引脚→功能"，少一步就报 `DET_E_UNINIT`。

### 50.10.3 迁移工作量估计

按本仓库 13 个例程的实际迁移经验（每个例程都是从 SDK 直调改写成 MCAL API），一个中等复杂度外设（SPI/I2C/CAN）的迁移在 1~2 人天内，简单外设（GPIO/Port/Dio）半天。主要时间不在替换函数名，而在：

1. 把板级常量抽到 PBcfg（引脚、实例、时钟）；
2. 把"寄存器直写"改成"配置结构体 + Init"；
3. 补 `MainFunction` 的周期调用（异步模块必须）；
4. 处理 SWS 与 SDK 的语义差（例如 SPI 的 IB/EB 模型、CAN 的 HRH/HTH）。

## 50.11 量产差距清单

这一节回答"能不能上车"。答案是不能，差距有 12 项，按补齐难度排序。

表 50-14 量产差距清单（商用基线 vs 本实现现状）

| # | 维度 | 商用基线（以 TC4x/NXP 随包证据为准） | 本实现现状 | 补齐路径 |
|---|---|---|---|---|
| 1 | MISRA C 合规 | MISRA C:2023 全量检查 + 偏差说明文档 | 只有 `-Wall -Wextra` 零告警 | 接 Polyspace/Coverity 跑 MISRA C:2012，逐条消警或写偏差 |
| 2 | WCET | 逐 API 的 `SW_WCET.xlsx` | 未测 | 用 Lauterbach/示波器测最坏执行路径，或静态分析工具 |
| 3 | 单元测试/覆盖率 | 模块级单元测试 + 语句/分支覆盖率报告 | 只有上板集成验证（30 个例程） | 用 Tessy/VectorCAST 或开源的 Unity + gcov 建单元测试 |
| 4 | 安全手册 | Safety Manual（假设、使用约束、安全机制） | 无 | 需按 ISO 26262-5 编写，含 FMEDA/DFA |
| 5 | ASIL 认证 | Safety Case Report + 第三方评估 | 无 | 商业认证流程，周期以年计 |
| 6 | 信息安全 | Cybersecurity Case、SBOM | 无 | 需 ISO 21434 流程 + SBOM 生成 |
| 7 | 需求追溯 | 每条需求带 ID，`.reqifz` 可导入 Polarion/DOORS | 注释里写 SWS 章节号 | 给每个 API/错误路径打需求 ID，接 ALM 工具 |
| 8 | 配置工具 | EB tresos 插件 + `.arxml` 生成 | 手写 C 配置 | 写 Tresos 插件或自研配置生成器，输出 `*_PBcfg.c` |
| 9 | 多核/OS 集成 | 分区映射、OS-Application、Alarm 映射说明 | 单核、无分区、ISR 直挂 | 引入 OS 后按 `InstanceId` 扩展分区支持 |
| 10 | 多型号适配 | 一套代码支持 5 个以上衍生型号 | 只支持 HPM5E31 一块板 | 抽 PBcfg + 增加变体层（编译期选择） |
| 11 | 缺陷管理 | Release Notes 列已知问题 + 规避方法 | 无公开缺陷清单 | 建 issue 模板，随版本出 known issues |
| 12 | 供应链 | 厂商支持合同、长期供货、工具链许可 | 项目自研，无支持承诺 | 商业采购或自建支持团队 |

反过来看，本实现有几项是商用 MCAL 反而做不到的，选型时值得算进去：

- **代码可读、可改**：13 模块 5 千行，工程师两天能通读；商用 MCAL 十万行起，改一行要走厂商支持。
- **与 HPM SDK 同源**：寄存器定义、启动流程与 SDK 一致，不存在"两套时钟初始化打架"的问题。
- **无许可成本与导出限制**：商用 MCAL 的 license 通常绑定项目与芯片型号。

## 50.12 寄存器视角：MCAL 的抽象边界在哪

MCAL 不是"不碰寄存器"，而是"只有 MCAL 碰寄存器"。表 50-15 列出本实现 15 个模块各自封装的关键寄存器块（寄存器名以 HPM SDK 的 `hpm_*_regs.h` 为准），以及对应的 SWS API 与例程。

表 50-15 外设寄存器块与 MCAL API 对照

| 外设 | 寄存器块（SDK 头） | 关键位域 | MCAL API | 例程 |
|---|---|---|---|---|
| IOC 引脚复用 | `HPM_IOC->PAD[]`（`hpm_ioc_regs.h`） | `FUNC_CTL.ALT_SELECT`、`PAD_CTL` | `Port_SetPinMode` | `port_pin_config` |
| GPIO | `HPM_GPIO0->DO/DI/TOGGLE` | 位图写 1 生效 | `Dio_WriteChannel`、`Dio_FlipChannel` | `dio_led_button` |
| PLL/时钟 | `HPM_PLLCTL`、`HPM_SYSCTL` | PLL 使能/分频/锁定状态 | `Mcu_InitClock`、`Mcu_GetPllStatus` | `mcu_clock_init` |
| GPTMR | `HPM_GPTMR0~3`（`hpm_gptmr_regs.h`） | `CR`、`RLD`、`SR` 标志 | `Gpt_StartTimer`、`Gpt_GetTimeElapsed` | `gpt_periodic` |
| PWMv2 | `HPM_PWM0`（`hpm_pwm_regs.h`） | 比较器、计数器、输出使能 | `Pwm_SetDutyCycle`、`Pwm_SetOutputToIdle` | `pwm_basic` |
| ADC16 | `HPM_ADC0`（`hpm_adc16_regs.h`） | `CONV_CFG`、`PRD_CFG`、结果寄存器 | `Adc_StartGroupConversion`、`Adc_ReadGroup` | `adc_single` |
| SPI | `HPM_SPI1/SPI3` | `CTRL`、`TRANSFMT`、`CMD` | `Spi_AsyncTransmit`、`Spi_ReadIB` | `spi_master_slave` |
| I2C | `HPM_I2C2/I2C3` | `CTRL`、`CMD`、`STATUS` | `I2C_AsyncTransmit`、`I2C_GetSequenceResult` | `i2c_master_slave` |
| MCAN | `HPM_MCAN0~3`（`hpm_mcan_regs.h`） | `CCCR`、`NBTP`、`DBTP`、`IR` | `Can_Write`、`Can_SetControllerMode` | `can_classic_echo` |
| EWDG | `HPM_EWDG0` | 超时窗口、使能位 | `Wdg_SetTriggerCondition` | `wdg_trigger` |
| XPI NOR | `HPM_XPI0` + ROM API | 命令序列、地址映射 | `Mem_Write`、`Mem_Erase` | `mem_flash_rw` |
| HPM CRC | `HPM_CRC` | 通道配置、输入/结果寄存器 | `Crc_CalculateCRC32` | `crc_bench` |

把这张表和 表 50-4 的依赖规则对照看，就能理解"抽象边界"的实际含义：寄存器写操作集中在 `mcal/<Mod>/src/*.c`，寄存器基地址与引脚号集中在 `*_PBcfg.c`，公共头文件里一个 SDK 符号都没有。这样上层的 BSW 代码才可能搬到另一颗芯片上。

## 50.13 性能与资源

### 50.13.1 和商用 MCAL 不能直接比的原因

商用 MCAL 的性能数据（WCET、内存占用）是在特定芯片型号 + 特定编译器 + 特定参考配置下测的。本实现是 RV32 + GCC 在 HPM5E31 上测的，两边没有共同的测量口径，**不能把数字直接相除得出"谁快"**。可以做的是同一实现内部的前后对比，以及同一模块在不同配置下的对比。

### 50.13.2 本实现的实测数据

表 50-16 本实现的关键性能数据（测量方法与完整日志见第 44~48 章及各例程 README）

| 指标 | 数值 | 测量方法 | 日志/位置 |
|---|---|---|---|
| CPU 主频实测误差 | 480 MHz 配置实测 479,997,400 Hz，−5 ppm，spread 2 kHz | mchtmr（24 MHz）与 rdcycle 对拍，10 ms 窗口 ×5 次 | `logs/20261102-135140-mcu_clock_init.log` |
| 软件复位耗时 | 约 4.2 µs | PPOR.SOFTWARE_RESET 递减计数（24 MHz） | `logs/20261102-140030-mcu_reset_mode.log` |
| Dio 单次写/读 | 写 127 ns、读 185 ns（1M 次，含 DET 与通道表查表） | rdcycle 前后差 | `logs/20261102-140549-dio_led_button.log` |
| Gpt 单次 API | `Gpt_StartTimer` 2973 ns、`Gpt_GetTimeRemaining` 332 ns（含 DET） | rdcycle 统计 | `logs/20261102-134707-gpt_periodic.log` |
| Gpt 单次定时误差 | 1 ms 目标：+1066.7 ns（+1066.67 ppm），20 次扫描 | 硬件计数器读回与目标比较 | `logs/20261102-141404-gpt_oneshot.log` |
| Pwm 通知抖动 | 10 kHz 边沿：mean 50.002 µs、峰峰 10.254 µs、stddev 0.052 µs；2 s 内 40006 边沿（理论 40000） | 通知回调读硬件计数器 | `logs/20261102-141427-pwm_notify.log` |
| Icu 测量精度 | 1 kHz：0 ppm、占空比 49.917%；10 kHz：−99.99 ppm、49.165%；100 kHz：占空比 41.7% | 已知 PWM 输入，测周期与高电平 | `logs/20261102-141450-icu_capture.log` |
| Adc 精度与吞吐 | ENOB 7.93 bit（1000 次）/ 8.50 bit（5000 次）；2.0 µs/样本 = 508.4 kHz | 输入已知直流分压，统计 LSB 分布与转换计时 | `logs/20261102-143144-adc_single.log` |
| Spi 双向吞吐 | SCLK 1 MHz、1024 字节：双向 249 KB/s；2 字节宽 244 KB/s | 传输计数 + 微秒计时 | `logs/20261102-142330-spi_master_slave.log` |
| I2c 线速率 | 1 MHz：写 533.9 kbps / 读 541.1 kbps（有效 474.6 / 481.0 kbps）；400 kHz：约 290 kbps | 32 字节传输计时 | `logs/20261102-142919-i2c_master_slave.log` |
| Can 流式吞吐 | 500 kbit/s、8 字节帧：4203 frame/s（单对）；4 通道并发 8366 frame/s，CPU 4.62% | 发送计数/时间 + mcycle | `logs/20261102-142930-can_classic_echo.log`、`logs/20261102-142208-can_irq_perf.log` |
| Can FD 时延 | 500k/2M、64 字节 pingpong：avg 707 µs、max 730 µs | 双板/双通道回环计时 | `logs/20261102-143020-can_fd_echo.log` |
| Mem 吞吐 | 擦 2092.6 KB/s、写 261.7 KB/s、读 44859.8 KB/s；单次 MainFunction 最大 1.908 ms（擦） | 作业计时 | `logs/20261102-141425-mem_bench.log` |
| Crc 吞吐 | SW 17.1~22.8 MB/s，HW 60.2 MB/s（约 3.5×） | mcycle 实测（CPU 480 MHz） | `logs/20261102-135730-crc_bench.log` |
| Det 上报开销 | `Det_ReportError` 0.129 µs/call、`Det_GetLastError` 0.152 µs/call | 1000 次循环平均 | `logs/20261102-133416-mcal_hello.log` |

### 50.13.3 与 TC4x 官方资源数据的对照

第 48 章表 48-8 给出了本实现 15 个模块的 `.text`/`.data`/`.bss`（用 `riscv32-unknown-elf-size` 统计每个模块的 `.obj`）。把它和 表 50-8 的 TC4x 官方 Code 字节数放在一起看，有一个反直觉的结论：

| 模块 | 本实现 .text（B） | TC4x Code（B） | 比值（本/TC4x） |
|---|---|---|---|
| Mcu | 3492 | 11192 | 0.31 |
| Port | 2020 | 2716 | 0.74 |
| Dio | 1564 | 1566 | 1.00 |
| Gpt | 5482 | 7402 | 0.74 |
| Pwm | 6344 | 4344 | 1.46 |
| Icu | 6656 | 7456 | 0.89 |
| Wdg | 1142 | 1688 | 0.68 |
| Adc | 4858 | 18078 | 0.27 |
| Spi | 9792 | 15324 | 0.64 |
| I2c | 4106 | 9950 | 0.41 |
| Can | 11084 | 14592 | 0.76 |
| Mem | 4027 | 7710 | 0.52 |
| Crc | 12041 | 3242 | 3.71 |

（数据取自第 48 章表 48-8 的 .text 与 表 50-8 的 TC4x Code；Crc 偏大是因为 8 张软件查找表放进了 Flash，TC4x 走硬件 CRC；Mem 偏小是因为作业由 ROM API 完成，驱动只做状态机与参数校验。）

**合计源码行数差 8~18 倍（TC4x 103907 行、NXP 228186 行 vs 本实现 12763 行），编译后代码只差 0.3~1.5 倍（Crc 例外）**。原因很直接：商用 MCAL 的行数主要在配置校验、多型号适配、错误路径、注释与安全机制上，这些不产生多少机器码；而本实现把查找表、字符串、防御代码也算进了模块。这说明"代码规模"不是衡量 MCAL 质量的好指标，**功能覆盖与验证证据才是**。

另外两点资源结论：

- **配置数据占比高**：TC4x 的 `Pwm` 配置数据 6024 字节 > Code 4344 字节，`Port` 配置数据 1400 字节约为 Code 2716 的一半。做资源预算时不能只算代码段。
- **栈需求**：TC4x 各模块最大栈 8~104 字节，本实现 API 也没有大局部数组；若出现栈溢出，问题更可能在应用侧而不是 MCAL。

## 50.14 上板实测汇总

13 个经典模块共 26 个例程（另 `mcal_hello`、`ecu_integration` 两个演示/集成例程；Eth/EthTrcv 的 6 个例程见第 49 章），其中 25 个已通过 `scripts/hwrun.sh` 烧写实测（日志在 `logs/`），1 个有已知限制：`wdg_timeout_reset` 需两次 hwrun 才能抓到复位后输出。表 50-17 是汇总，每个例程的详细现象、命令表、边界测试见第 44~48 章。

表 50-17 13 模块例程上板结果汇总

| 模块 | 例程 | 关键验证点 | 关键数据 | 日志 |
|---|---|---|---|---|
| Adc | `adc_single` | 单次转换、ENOB、吞吐 | ENOB 7.93 bit（1000 次）/ 8.50 bit（5000 次）；508.4 kHz（含 Start+Read） | `logs/20261102-143144-adc_single.log` |
| Adc | `adc_bench` | 连续采样吞吐基准 | 740.4 kS/s（20 000 轮，1350.64 ns/轮）、740.1 kS/s（50 000 轮）；实测/理论 53.3%，det=0 | `logs/20261102-143824-adc_bench.log` |
| Can | `can_classic_echo` | 经典 500 kbit/s 回环 | 200/200 帧 0 丢，4203 frame/s，回环延迟 avg 238 µs | `logs/20261102-142930-can_classic_echo.log` |
| Can | `can_fd_echo` | CAN FD 500k/2M、5M、64 字节 | 500k/2M 30/30、avg 707 µs；500k/5M 与 1M/5M 各 50/50（TDCO=12，avg 364/312 µs），5000 帧长测 0 丢；TDCO=1/20 反例 bus-off（TEC=248） | `logs/20261102-155424-can_fd_echo.log`（矩阵）、`logs/20261102-180603-can_fd_echo.log`（R8 复测 20/20） |
| Can | `can_filter` | 12 条过滤规则 | mask/range/dual/reject 全部 PASS | `logs/20261102-141905-can_filter.log` |
| Can | `can_busoff` | error passive / 恢复 | TEC 平台 129（对端不 ACK 到不了 bus-off），恢复后 pingpong PASS | `logs/20261102-142116-can_busoff.log` |
| Can | `can_irq_perf` | 4 通道并发与 CPU 占用 | 8366 frame/s，CPU 4.62%，pingpong avg 244 µs | `logs/20261102-142208-can_irq_perf.log` |
| Crc | `crc_bench` | 8 算法标准向量 + 吞吐 | 8 组 SWS 校验值全 PASS；SW 17.1~22.8 MB/s、HW 60.2 MB/s | `logs/20261102-135730-crc_bench.log` |
| Crc | `crc_integrity` | Flash 内容 CRC 校验 | flash/blank 双路径 PASS | `logs/20261102-140431-crc_integrity.log` |
| Dio | `dio_led_button` | LED/按键/组读写/性能 | 写 127 ns、读 185 ns（1M 次）；DET +6 | `logs/20261102-140549-dio_led_button.log` |
| Gpt | `gpt_periodic` | 1 ms 周期 tick | StartTimer 2973 ns；10 s 计数与 ISR 抖动有统计 | `logs/20261102-134707-gpt_periodic.log` |
| Gpt | `gpt_oneshot` | 单次定时精度 | 1 ms 目标误差 +1066.7 ns（+1066.67 ppm） | `logs/20261102-141404-gpt_oneshot.log` |
| I2c | `i2c_master_slave` | 100k/400k/1M 读写 | 三档各 1000 轮 0 误码（9 745/31 658/57 843 B/s）；400 kHz 旧 1 字节误差已由目标侧 T_SUDAT 修复 | `logs/20261102-155949-i2c_master_slave.log`（1000 轮）、`logs/20261102-180525-i2c_master_slave.log`（R8 复测） |
| I2c | `i2c_error` | NACK/忙错误路径 | nackPass=1、busyPass=1，Det 计数 10 | `logs/20261102-142544-i2c_error.log` |
| Icu | `icu_capture` | 1/10/100 kHz 周期与占空比 | 1 kHz：0 ppm、占空比 49.917%；10 kHz：−99.99 ppm；100 kHz 占空比 41.7% | `logs/20261102-141450-icu_capture.log` |
| Mem | `mem_flash_rw` | 保留扇区读写 | 写 261.8 KB/s、读 39.8 MB/s，PASS | `logs/20261102-141617-mem_flash_rw.log` |
| Mem | `mem_bench` | 擦/写/读吞吐 | 擦 2092.6 KB/s、写 261.7 KB/s、读 44859.8 KB/s | `logs/20261102-141425-mem_bench.log` |
| Mcu | `mcu_clock_init` | PLL 配置与主频实测 | 479,997,400 Hz（−5 ppm），spread 2 kHz | `logs/20261102-135140-mcu_clock_init.log` |
| Mcu | `mcu_reset_mode` | 复位源识别、软复位 | 软复位约 4.2 µs，RESET_FLAG 可读 | `logs/20261102-140030-mcu_reset_mode.log` |
| Port | `port_pin_config` | 回环、上下拉、开漏 | 2000 组 LFSR 回环误码 0；上下拉/开漏 PASS | `logs/20261102-135603-port_pin_config.log` |
| Pwm | `pwm_basic` | 占空比/周期扫描 | 10 kHz 下 0/25/50/75/100% 扫描，频率 9999~10000 Hz | `logs/20261102-141320-pwm_basic.log` |
| Pwm | `pwm_notify` | 周期通知抖动 | 2 s 内 40006 边沿（理论 40000），stddev 0.052 µs | `logs/20261102-141427-pwm_notify.log` |
| Spi | `spi_master_slave` | 主从回环、位宽、IB | 1 MHz、1024 字节双向 249 KB/s | `logs/20261102-142330-spi_master_slave.log` |
| Spi | `spi_async_job` | 异步 Job/Sequence | 4 Job 并发 PASS，seqResult=0 | `logs/20261102-142810-spi_async_job.log` |
| Wdg | `wdg_trigger` | 模式与触发窗口 | EWDG0 32.768 kHz，slow 1000 ms / fast 100 ms | `logs/20261102-141513-wdg_trigger.log` |
| Wdg | `wdg_timeout_reset` | 超时复位与复位源 | 单次 hwrun 抓不到复位后输出，需两次运行（见第 15 节） | `logs/20261102-142810-wdg_timeout_reset.log` |

这些例程同时也验证了 MCAL 的边界假设：所有例程的 `main.c` 都只调 SWS API（不直接写寄存器），说明抽象层没有漏。例外是 `mem_bench` 与 `crc_bench` 里的对拍基准——它们故意用寄存器/SDK 直调做参照，属于测试代码而非驱动代码。

## 50.15 易错点与注意事项

1. **SWS 抽取文本的页眉页脚会污染解析**。`pdftotext` 出来的文本每页都有 `N of M / Document ID / ▽ / △`，用正则抓 `Definition of API function` 时必须先清掉这些行，否则会把页脚当成 API 块的一部分，Service ID 抓空。现象：解析出的 API 数比实际少；解决：`parse_sws.py` 里的 `JUNK` 正则。
2. **Service ID 与 API ID 宏不是一回事**。SWS 的 "Service ID [hex]" 是 DET 上报用的编号，本实现头文件里的 `<MOD>_API_ID_*` 应该逐个对应；但有些模块（如 Mem）的 SWS 版本里编号有调整，抄旧版会错。现象：DET 日志里的 API ID 与 SWS 表对不上；解决：以 R25-11 文本为准，逐模块 diff。
3. **`AR_RELEASE_*` 宏写 4.4.0 是历史遗留**。公共头 `Mcal_Version.h`/各模块头统一 4.4.0，而本轮 SWS 是 R25-11。集成商按版本宏判断能力时会误判。已在 表 50-6 第 1 条登记，改法需要 Lead 统一决定，不要单个模块乱改。
4. **DET 开关必须真的能关到零开销**。`MCAL_DEV_ERROR_DETECT=STD_OFF` 时 `MCAL_DET_REPORT` 展开为 `((void)0)`，但参数表达式仍会被求值——如果参数里有函数调用（例如 `GetInstance()`），关掉 DET 后调用还在。现象：关 DET 后代码没变小；解决：DET 参数只用局部变量。
5. **post-build 配置指针的生命周期**。`<Mod>_Init(&<Mod>_Config)` 后驱动内部保存指针，`<Mod>_Config` 必须是 `const` 全局对象；如果传栈上临时结构，函数返回后一调 API 就崩。现象：Init 成功、第一次运行 API 就 HardFault；解决：配置实例只放 `.c` 全局。
6. **`MemMap.h` 的段切换要成对**。`#include "MemMap.h"` 进入 CODE 段后忘记切回，后续文件全部落进错误段。现象：链接时 `.text` 段溢出或函数地址错乱；解决：每个 `.c` 文件末尾再 include 一次 `MemMap.h`（恢复默认段）。
7. **商用 MCAL 的"合规"不等于"没有偏差"**。NXP UM 的偏差表、Infineon 的 Safety Case 单独发布都说明：合规是"偏差被声明并被评估"，不是"100% 按字面实现"。写自研 MCAL 的合规文档时，照抄这个思路比硬凑 100% 覆盖更可信。
8. **不要用商用 MCAL 的性能数字给自研背书**。把 TC4x 的 WCET 数字引到 HPM5E31 上没有任何意义，编译器、主频、Flash 等待周期都不同。跨平台只能比"量级"和"设计取舍"，不能比具体数值。

## 50.16 参考来源

### 50.16.1 标准与规范

1. AUTOSAR Classic Platform 标准页：<https://www.autosar.org/standards/classic-platform/>
2. AUTOSAR CP SWS 抽取文本（R25-11，本地）：`autosar_txt/AUTOSAR_CP_SWS_*.txt`，本章引用 `SWS_Mcu`、`SWS_Port`、`SWS_Dio`、`SWS_Gpt`、`SWS_Pwm`、`SWS_Icu`、`SWS_Wdg`、`SWS_Adc`、`SWS_Spi`、`SWS_I2c`、`SWS_Can`、`SWS_Mem`、`SWS_Crc`、`SWS_BSWGeneral` 共 14 份
3. AUTOSAR SWS_BSWGeneral §50.13.2 模块 ID 表（本仓库抄录为 `mcal/common/Mcal_ModuleId.h`）
4. ISO 26262 道路车辆功能安全：<https://www.iso.org/standard/68383.html>
5. MISRA C：<https://www.misra.org.uk/>
6. SEI CERT C 编码标准：<https://wiki.sei.cmu.edu/confluence/display/c/SEI+CERT+C+Coding+Standard>

### 50.16.2 芯片与 SDK

7. HPM SDK（BSD-3-Clause，77 个驱动源文件）：<https://github.com/hpmicro/hpm_sdk>
8. 先楫半导体官网：<https://www.hpmicro.com/>
9. 本仓库寄存器级例程与书稿第 4~22 章（`samples/gpio`、`samples/gptmr`、`samples/pwm`、`samples/adc`、`samples/spi`、`samples/i2c`、`samples/can` 等）
10. HPM5E31 数据手册/用户手册/勘误：`先楫官方 Doc 包（DS/RM/Errata）`

### 50.16.3 商用 MCAL 对照资料

11. Infineon AURIX TC4x MCAL 2.20.0（本机安装 `C:\AURIX_TC4x_MC-ISAR_2.20.0`）：Release Notes、`Docs/User_Manuals/AURIX_TC4x_MC-ISAR_UM_*.pdf`、`Docs/AURIX_TC4x_MC-ISAR_Req_Extract_Autosar_R20-11_R21-11.pdf`、`Docs/AURIX_TC4x_MC-ISAR_MISRA_Justification.html`、`Docs/AURIX_TC4x_MC-ISAR_SW_WCET.xlsx`、`Docs/AURIX_TC4x_MC-ISAR_Memory_Consumption.zip`
12. Infineon AURIX 32 位 TriCore 微控制器（TC4x 资料入口）：<https://www.infineon.com/cms/en/product/microcontroller/32-bit-tricore-microcontroller/>
13. Infineon MyICP 门户（MC-ISAR/MCAL 安装包与补丁发布，需账号）：<https://myicp.infineon.com/>
14. NXP S32K3 RTD 6.0.0（本机安装 `D:\NXP\SW32K3_S32M27x_RTD_R21-11_6.0.0`）：每模块 `doc/RTD_<MOD>_UM.pdf` 与 `RTD_<MOD>_IM.pdf`、`license.rtf`
15. NXP S32K3 产品页：<https://www.nxp.com/products/processors-and-microcontrollers/s32-automotive-platform/s32k3-microcontrollers-for-general-purpose:S32K3>
16. NXP S32 Design Studio：<https://www.nxp.com/design/software/development-software/s32-design-studio-ide/s32-design-studio:S32DS>（RTD 安装包在 S32K3 产品页 Software & Tools 下，需 NXP 账号）
17. Elektrobit EB tresos：<https://www.elektrobit.com/products/ecu/eb-tresos/>
18. Vector MICROSAR（另一家商用 AUTOSAR 基础软件）：<https://www.vector.com/int/en/products/products-a-z/embedded-software/microsar/>

### 50.16.4 本仓库支撑材料

19. `docs/mcal/MCAL_API_TRACEABILITY.md`（逐 API 追溯矩阵与偏差清单）
20. `docs/mcal/sws_api_data.json`（SWS 机器清单）、`docs/mcal/traceability_stats.json`（统计）
21. `docs/mcal/gen_traceability.py`、`docs/mcal/parse_sws.py`（可复跑的核对脚本）

## 50.17 待确认项与风险

表 50-18 待确认项与风险（只保留三种状态：已实测、已定位、已知限制）

| 项 | 状态 | 说明与验证方法 |
|---|---|---|
| 13 模块例程上板 | 已实测（2026-11-02） | 26 个模块例程中 25 个 PASS（日志见 表 50-17），1 个见下方已知限制（`wdg_timeout_reset`）；复跑：`bash scripts/hwrun.sh mcal/<例程名> --secs 20` |
| 公共头不依赖 SDK | 已实测（2026-11-02） | `grep -rn "hpm_\|soc.h" mcal/*/include/*.h` 输出 0 行；复跑见 表 50-4 |
| TC4x 资源占用数字 | 已定位 | 来自随包 `Memory_Consumption` 报告，属"参考配置 + 特定编译器"下的 indicative 值，换配置差异大；引用时已注明 |
| AR release 宏 4.4.0 与 R25-11 不一致 | 已定位 | 公共头由 Lead 维护，属工程约定；影响版本追溯字段，登记在 表 50-6 第 1 条；修改需统一决策 |
| Adc `adc_bench` 连续组吞吐 | 已实测（2026-11-02） | 20 000/50 000 轮 740.4/740.1 kS/s、实测/理论 53.3%、det=0；日志 `logs/20261102-143824-adc_bench.log`；复跑：`bash scripts/hwrun.sh mcal/adc_bench --secs 25 --send 'adcbench 20000'` |
| I2c 400 kHz 读方向 1 字节误差 | 已实测（已修复） | 根因=目标侧 `SETUP.T_SUDAT` 被 SDK 算成 0（100 ns 零余量），`I2c.c` 的目标通道 fixup 抬到 12 后 100k/400k/1M 各 1000 轮 0 误码；R8 复测 400k/1M 各 300 轮 0 误码。详见第 46 章；日志 `logs/20261102-155949-i2c_master_slave.log`、`logs/20261102-180525-i2c_master_slave.log`；复跑：`bash scripts/hwrun.sh mcal/i2c_master_slave --secs 35 --send "i2c init 400k;i2c ber 300"` |
| Can FD 5 Mbit/s 数据段 | 已实测（已修复） | 根因=TDC 偏移 `TDCR.TDCO` 必须小于数据段位长（5M 下 16 tq）；TDCO 20→12 后 500k/5M 与 1M/5M 各 50/50、5000 帧长测 0 丢，TDCO=1 仍 bus-off（旧 TDCO=20 配置保留为 ID18 且被驱动拒绝）。详见第 47 章；日志 `logs/20261102-155424-can_fd_echo.log`、`logs/20261102-180603-can_fd_echo.log`；复跑：`bash scripts/hwrun.sh mcal/can_fd_echo --secs 30 --send "can matrix 20"` |
| Wdg 超时复位跨复位抓包 | 已实测（跨复位输出）/ 已知限制（BOOT 计数） | 2026-11-02 18:17 重跑：烧写后抓到复位后连续输出 0~7 s，PPOR.RESET_FLAG=0x80010010（WDG0=1），固件据此判定本次启动由 EWDG0 超时复位引起（日志 logs/20261102-181720-wdg_timeout_reset.log）；但非初始化 RAM 的 BOOT 计数仍为 1，紧随的第二次 --no-flash 抓包为空（logs/20261102-181731-wdg_timeout_reset.log），"超时→复位→BOOT#2"完整链路仍未串起。复跑按 README 两条 hwrun（先 --secs 8 烧写，紧接 --no-flash --secs 12，中间不插其它烧写） |
| Icu 测量类 API 不完整 | 已知限制 | 原因：本轮只做信号测量主路径，边沿计数/时间戳族未实现；复跑缺失清单：`python3 docs/mcal/gen_traceability.py --check` |
| ASIL 安全论证/WCET/单元测试 | 已知限制 | 原因：无安全案例预算与商用静态分析工具；补齐路径见 表 50-14 第 1~5 项，无法在本轮复跑 |
| 商用 MCAL 的 ASIL 结论 | 已知限制 | 原因：只引用其公开文档（Safety Case 单独发布，未随包），未做第三方复测；引用时已标注来源 |
| 多核/分区能力 | 已知限制 | 原因：无 OS 与多核调度环境；验证方法：接入任一多核 OS 后按 `InstanceId` 扩展，本轮无复跑条件 |
