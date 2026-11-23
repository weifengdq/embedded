# 第 42 章 MCAL 总览：AUTOSAR 分层、范围与路线

## 42.1 文档信息

| 项 | 内容 |
|---|---|
| 文档版本 | v1.3.0（2026-11-02，R6 MCAL） |
| 适用芯片/板卡 | HPM5E31IPB1 / hpm5e31_1 |
| 对应例程 | `samples/mcal/mcal_hello/`、`samples/mcal/ecu_integration/`，以及第 44~49 章各模块例程 |
| 实测日期 | 2026-11-02 |
| 可信度 | 高（模块 API/例程/日志均在本板实测）；"与商用 MCAL 的差距"部分为中（基于公开资料对比，见第 50 章） |

## 42.2 本章导览

- 学到什么：AUTOSAR Classic Platform 里 MCAL 到底管什么、不管什么；HPM5E31 这种 RISC-V MCU 为什么需要一套
  自己维护的 MCAL；本仓库这套 MCAL 的范围、模块划分、验证方法与取舍。
- 前置章节：第 01 章（芯片与板卡）、第 02 章（工具链/烧写）、第 4~22 章（各外设的寄存器级实测，是 MCAL 驱动的底料）。
- 本章配套：第 43 章（基础设施与配置模型）、第 44~49 章（15 个模块逐个实现与实测，其中第 49 章是第 7 轮新增的 Eth/EthTrcv）、第 50 章（合规性与商用对比）。
- 一句话结论：本仓库实现的是 **AUTOSAR CP MCAL 的"可运行子集"**——15 个模块、AUTOSAR 命名与配置模型、
  寄存器级驱动、可上板复现的例程与日志；它 **不是** 通过认证的量产 MCAL，差距与补齐路径写在 42.8 与第 50 章。

## 42.3 概述与典型应用

### 42.3.1 MCAL 是什么

AUTOSAR Classic Platform（CP）把 ECU 软件切成四层：应用软件（SWC）→ RTE → BSW（基础软件）→ **MCAL**
（Microcontroller Abstraction Layer，微控制器抽象层）。MCAL 是唯一直接碰寄存器的标准化层，它把
"某个芯片的某个外设"包装成"AUTOSAR 规定的一组 API"，让上层的 CanIf、IoHwAb、EcuM、OS 不必关心芯片型号。

MCAL 在 AUTOSAR 文档体系里对应每个模块一份 SWS（Software Specification）。本仓库对照的是
AUTOSAR CP R21-11 / R25-11 的 SWS 文本，模块 ID 与 API 名以 SWS 为准（表 42-1 列出 15 个模块与它们的 SWS）。

### 42.3.2 为什么 HPM5E31 需要一套自己的 MCAL

先楫 HPM5E00 系列目前没有公开的、与 AUTOSAR 版本对齐的 MCAL 包；官方给的是 HPM SDK（寄存器级驱动 + 中间件）。
这带来三个现实问题：

1. **上层协议栈接不进来**：Vector/ETAS/EB 的 CanIf、CanTp、Dcm、NvM 都按 AUTOSAR API 调用底层；
   没有 MCAL，就要自己写一层适配，最后往往变成"协议栈里到处是 HPM SDK 调用"。
2. **换芯片成本高**：应用代码直接调 SDK，等于把芯片绑死在业务层；MCAL 的意义是"换 MCU 只换 MCAL + 配置"。
3. **可验证性差**：AUTOSAR 的 Det/Dem、错误码、初始化顺序、模式管理都是标准化的；自写适配层容易"能跑但说不清"。

本仓库的做法：**不追求认证，追求"照 SWS 写对、能上板跑、能被对比"**。每个模块都给三样东西——
AUTOSAR API 实现、板级配置（PBcfg）、上板例程与日志；拿它当"从 SDK 到 MCAL"的参考实现，能省掉不少试错。

### 42.3.3 范围：15 个模块

| 模块 | 模块 ID | 职责 | 例程数 | 章节 |
|---|---|---|---|---|
| Mcu | 101 | 时钟/PLL/复位/模式 | 2 | 第 44 章 |
| Port | 124 | 引脚复用、方向、上下拉、开漏 | 1 | 第 44 章 |
| Dio | 120 | GPIO 通道/端口/通道组读写 | 1 | 第 44 章 |
| Gpt | 100 | 通用定时器、通知、单次/连续 | 2 | 第 45 章 |
| Pwm | 121 | PWM 输出、占空比/周期/相位 | 2 | 第 45 章 |
| Icu | 122 | 输入捕获、边沿检测、信号测量 | 1 | 第 45 章 |
| Wdg | 102 | 看门狗触发、窗口/超时模式 | 2 | 第 45 章 |
| Adc | 123 | ADC 单次/连续/组转换 | 2 | 第 46 章 |
| Spi | 083 | SPI 主从、Job/Sequence、同步/异步 | 2 | 第 46 章 |
| I2c | 113 | I2C 主从、同步/异步、错误路径 | 2 | 第 46 章 |
| Can | 080 | CAN/CAN FD 控制器、HTH/HRH、过滤 | 5 | 第 47 章 |
| Mem | 091 | 片内 Flash 擦写读（R25-11 前的 Fls） | 2 | 第 48 章 |
| Crc | 201 | CRC8/16/32/64 标准算法 | 2 | 第 48 章 |
| Eth | 088 | 以太网 MAC/DMA 收发、MII/MDIO 读写、计数器 | 3 | 第 49 章 |
| EthTrcv | 073 | 以太网收发器（YT8011AN）链路/速率/主从/唤醒 | 1 | 第 49 章 |

表 42-1 的"例程数"合计 30 个（另有 `mcal_hello` 与 `ecu_integration` 两个集成例程，
以及第 49 章的两个 lwIP / FreeRTOS 集成例程 `eth_lwip`、`eth_freertos_lwip`），全部在
`samples/mcal/` 下，每个都能 `bash scripts/hwrun.sh` 一条命令编译+烧写+抓日志。

### 42.3.4 典型应用场景

| 场景 | 用到的 MCAL 模块 | 说明 |
|---|---|---|
| 车身控制器 | Dio/Adc/Pwm/Can | 按键、灯、雨刮、CAN 通信 |
| 电机控制 | Pwm/Adc/Icu/Gpt | 互补 PWM、电流采样、编码器捕获 |
| 电池管理（BMS） | Adc/Spi/I2c/Can | 采样 AFE、隔离 SPI、CAN 上报 |
| 车载网关 | Can/Spi/Mem | 多路 CAN、SPI 外设、参数存储 |
| 域控制器 | 全部 + OS/RTE | MCAL 是 BSW 的底座 |

## 42.4 工作原理

### 42.4.1 AUTOSAR CP 分层与 MCAL 的位置

![图 42-1 AUTOSAR CP 分层与本书实现的 15 个 MCAL 模块](figures/42/fig-42-1-autosar-layers.png)

图 42-1 里绿色块是本仓库真正实现的 15 个模块，黄色块（CanIf/Com/Dcm/Dem 等）只给桩或完全不做——
这是刻意的边界：**MCAL 只负责"寄存器 ↔ 标准 API"，协议栈属于 BSW 层，不在本轮范围**。

MCAL 内部又分四类（与 SWS 的分类一致，见表 42-2）：

| 类别 | 模块 | 特点 |
|---|---|---|
| 微控制器驱动（Microcontroller Drivers） | Mcu/Gpt/Wdg | 与内核、时钟、复位、看门狗相关 |
| I/O 驱动（I/O Drivers） | Port/Dio/Pwm/Icu/Adc | 直接面向引脚与片内外设，配置量大 |
| 通信驱动（Communication Drivers） | Spi/I2c/Can | 有队列/作业/中断语义，异步 API 多 |
| 存储驱动（Memory Drivers） | Mem | 擦写有寿命与时序约束，异步作业 |

表 42-2 的分类不是学术划分：它决定了模块的初始化顺序（第 43 章图 43-3）和错误处理方式（同步/异步）。

### 42.4.2 MCAL 与 HPM SDK 的关系

HPM SDK 是"芯片厂商 SDK"，AUTOSAR MCAL 是"标准接口"。两者不是替代关系，而是**分层**关系（表 42-3）：

| 维度 | HPM SDK 直调 | 本仓库 MCAL |
|---|---|---|
| 接口 | `hpm_mcan_send()`、`hpm_pwm_set_duty()` 等芯片私有 API | `Can_Write()`、`Pwm_SetDutyCycle()` 等 AUTOSAR API |
| 配置 | 结构体 + 板级 `pinmux.c` | `*_Cfg.h`（预编译）+ `*_PBcfg.c`（post-build） |
| 错误处理 | 返回值/断言 | `Std_ReturnType` + Det 错误码 |
| 换芯片 | 重写应用 | 换 MCAL + 改 PBcfg |
| 适用 | 快速原型、裸机产品 | 需要 AUTOSAR 协议栈/多供应商协作的项目 |
| 代价 | 无标准化开销 | API 间接层、配置代码、Det 检查（可裁剪） |

表 42-3 的最后一列很关键：MCAL 不是免费的标准，每一次 API 调用都有成本。第 43 章会给出 Det 的实测周期数。

本仓库的驱动实现**参考 SDK 的寄存器操作**（同一套 RM 章节），但不 include SDK 的高层 API 到公共头里；
例程层可以用 SDK 的 `clock_get_frequency()` 做交叉验证，仅测试用。

### 42.4.3 15 个模块与 HPM5E31 硬件的映射

![图 42-2 MCAL 模块与 HPM5E31 外设、板级资源的对应关系](figures/42/fig-42-2-module-mapping.png)

图 42-2 把"模块 → 片内外设 → 板上怎么用"钉在一起。几个容易踩的点先说明：

- **PF26 既是 CAN0 红色状态灯，也是 ADC0_IN01**：Adc 例程独占 PF26 时，Dio 例程改用其它灯，否则互相打架；
- **PA02/PA03 兼 BOOT_MODE0/1**：复位时按住 PA03 会进 ISP，程序不跑；Port 例程里要提醒；
- **SPI2 被 LAN8651 占用**：MCAL Spi 只用 SPI1（主）/SPI3（从），两者已用杜邦线互连；
- **I2C2/I2C3 已互连**：MCAL I2c 在 I2C2（主）与 I2C3（从）之间做真实总线通信；
- **Mem 只能用保留扇区**：`0x800F0000` 起的 64 KB 是既有工程约定的安全窗口（第 21 章），app 镜像在其下方。

### 42.4.4 从 SWS 到代码：一份驱动的实现路线

![图 42-3 从 AUTOSAR SWS 到上板实测与书稿的开发闭环](figures/42/fig-42-3-workflow.png)

每个模块都走同一条流水线（图 42-3）：

1. **读 SWS**：抽 API 清单、参数、返回码、状态机、Det 错误码、可选/必选标记；
2. **设计配置**：把"芯片寄存器参数"翻译成 `*_Cfg.h`（预编译）+ `*_PBcfg.c`（post-build 常量表）；
3. **写驱动**：`include/<Mod>.h` 只放 AUTOSAR 类型；`src/<Mod>.c` 直接操作寄存器（HPM RM 章节对照）；
4. **写例程**：`samples/mcal/<name>`，letter-shell 命令 + 串口自证；
5. **上板实测**：`hwrun.sh` 一条命令编译/烧写/抓日志，数据落 `logs/`；
6. **回写文档**：章节里的每个数字都能指到日志。

## 42.5 关键寄存器与驱动 API 对照

表 42-4 是总览级对照，每个模块的完整寄存器/位域对照放在各自章节（第 44~49 章）。

| 模块 | 关键寄存器（HPM5E31） | 关键位域/字段 | 对应 MCAL API | 例程位置 |
|---|---|---|---|---|
| Mcu | SYSCTL PLL/CLOCK/DIV、PPOR RESET_FLAG | PLL 倍频/分频、CLK_SEL、RESET_FLAG | `Mcu_InitClock`/`Mcu_DistributePllClock`/`Mcu_GetResetReason` | `samples/mcal/mcu_clock_init` |
| Port | IOC PAD/FUNC/CFG | FUNC_CTL、PAD_CTL（上下拉/开漏）、DS | `Port_SetPinMode`/`Port_SetPinDirection` | `samples/mcal/port_pin_config` |
| Dio | GPIO OE/DO/DI/IF | OE、DO、DI、IF 标志 | `Dio_ReadChannel`/`Dio_WriteChannel`/`Dio_FlipChannel` | `samples/mcal/dio_led_button` |
| Gpt | GPTMR RELOAD/CR/CNT/CMP | RELOAD、CR（计数/使能）、CMP | `Gpt_StartTimer`/`Gpt_GetTimeElapsed` | `samples/mcal/gpt_periodic` |
| Pwm | PWM CNT/RLD/CMP/CFG | 周期、占空比、极性、同步更新 | `Pwm_SetPeriodAndDuty`/`Pwm_SetDutyCycle` | `samples/mcal/pwm_basic` |
| Icu | GPTMR CR/CAP/CHANNEL | 捕获边沿、输入极性、计数源 | `Icu_StartSignalMeasurement`/`Icu_GetDutyCycleValues` | `samples/mcal/icu_capture` |
| Wdg | EWDG CTRL/CFG/REF | 超时值、窗口、使能/复位 | `Wdg_SetTriggerCondition`/`Wdg_SetMode` | `samples/mcal/wdg_trigger` |
| Adc | ADC16 CONV/CFG/SEQ/DMA | 采样周期、分辨率、触发源 | `Adc_StartGroupConversion`/`Adc_ReadGroup` | `samples/mcal/adc_single` |
| Spi | SPI CTRL/CFG/DATA/TIMING | 帧长、CPOL/CPHA、分频、FIFO | `Spi_SyncTransmit`/`Spi_AsyncTransmit` | `samples/mcal/spi_master_slave` |
| I2c | I2C CTRL/CMD/DATA/TIMING | 速率、地址、START/STOP、ACK | `I2c_Write`/`I2c_Read`（按 SWS） | `samples/mcal/i2c_master_slave` |
| Can | MCAN CCCR/NBTP/DBTP/RXFIFO/TXBCR | 位时序、FD 数据段、过滤器、FIFO | `Can_Init`/`Can_Write`/`Can_SetBaudRate` | `samples/mcal/can_fd_echo` |
| Mem | XPI0 命令序列、ROM API | 扇区擦除、页编程、空白检查 | `Mem_Erase`/`Mem_Write`/`Mem_BlankCheck` | `samples/mcal/mem_flash_rw` |
| Crc | CRC 外设（若可用）+ 软件表 | 多项式、初值、结果异或 | `Crc_CalculateCRC32` 等 | `samples/mcal/crc_bench` |
| Eth | ENET0 DMA 描述符环、MAC 配置、INTR_ENABLE | 描述符 OWN/STATUS、RX 缓冲环、cache 维护 | `Eth_Transmit`/`Eth_Receive`/`Eth_MainFunction` | `samples/mcal/eth_raw_echo` |
| EthTrcv | MDIO/MDC（ENET0 MAC 的 MII 接口） | Clause 22/EXT/MMD 寄存器、链路状态 | `EthTrcv_GetLinkState`/`EthTrcv_GetBaudRate` | `samples/mcal/eth_trcv` |

表 42-4 的"寄存器"列只给入口；真正写代码时必须对着 RM 的具体章节看位域，否则很容易把
"时钟分频"写成"总线分频"（第 44 章有一个真实案例）。

## 42.6 例程逐条讲解

表 42-5 是全部 MCAL 例程的索引；本章只讲公共基础设施例程，模块例程在第 44~49 章逐条展开。

| 例程 | 覆盖内容 | 关键命令 | 章节 |
|---|---|---|---|
| `mcal/mcal_hello` | 公共头、模块 ID、Det 计数、Det 周期开销 | `mcal;det;detbench` | 本章 |
| `mcal/mcu_clock_init` | PLL 配置、主频自证 | `mcu;clk` | 第 44 章 |
| `mcal/mcu_reset_mode` | 软件复位与复位源 | `mcu reset` | 第 44 章 |
| `mcal/port_pin_config` | 复用/方向/上下拉/开漏/回环 | `port ...` | 第 44 章 |
| `mcal/dio_led_button` | LED/按键/端口/通道组 | `dio ...` | 第 44 章 |
| `mcal/gpt_periodic` | 1 ms 周期通知/抖动 | `gpt start 0` | 第 45 章 |
| `mcal/gpt_oneshot` | 单次定时精度 | `gpt one 1000` | 第 45 章 |
| `mcal/pwm_basic` | 占空比/周期/相位 | `pwm duty 0 50` | 第 45 章 |
| `mcal/pwm_notify` | PWM 周期通知 | `pwm notify on` | 第 45 章 |
| `mcal/icu_capture` | 捕获周期/占空比 | `icu stat` | 第 45 章 |
| `mcal/wdg_trigger` | 喂狗/窗口 | `wdg feed` | 第 45 章 |
| `mcal/wdg_timeout_reset` | 超时复位自证 | `wdg run` | 第 45 章 |
| `mcal/adc_single` | 单次/统计/ENOB | `adc stat 1000` | 第 46 章 |
| `mcal/adc_bench` | 连续转换吞吐 | `adc bench` | 第 46 章 |
| `mcal/spi_master_slave` | 主从 1 MB 传输 | `spi xfer 1024` | 第 46 章 |
| `mcal/spi_async_job` | Job/Sequence 异步 | `spi job` | 第 46 章 |
| `mcal/i2c_master_slave` | 主从读写吞吐 | `i2c wr` | 第 46 章 |
| `mcal/i2c_error` | NACK/仲裁错误 | `i2c err` | 第 46 章 |
| `mcal/can_classic_echo` | 经典帧收发 | `can echo` | 第 47 章 |
| `mcal/can_fd_echo` | CAN FD 长帧 | `can fd` | 第 47 章 |
| `mcal/can_irq_perf` | 中断吞吐/延迟 | `can perf` | 第 47 章 |
| `mcal/can_busoff` | 错误状态与恢复 | `can busoff` | 第 47 章 |
| `mcal/can_filter` | 硬件过滤 | `can filter` | 第 47 章 |
| `mcal/mem_flash_rw` | 擦写读/空白检查 | `mem rw` | 第 48 章 |
| `mcal/mem_bench` | 擦除/编程吞吐 | `mem bench` | 第 48 章 |
| `mcal/crc_bench` | CRC 算法吞吐 | `crc bench` | 第 48 章 |
| `mcal/crc_integrity` | Flash 完整性校验 | `crc check` | 第 48 章 |
| `mcal/ecu_integration` | 13 模块集成 + 最小 ECU 框架 | `ecu ...` | 第 50 章 |

公共例程 `mcal_hello` 的关键代码只有三行——这三行验证了"类型系统 + 模块 ID + Det 通路"：

```c
#include "Mcal.h"

Det_Init();                                     /* Det 初始化（清计数） */
(void)Det_ReportError(MCU_MODULE_ID, 0u, 0x5Au, DET_E_PARAM_VALUE);  /* 故意报错 */
if (Det_GetLastError(&mod, &inst, &api, &err) == E_OK) { /* 能回读 */ }
```

## 42.7 上板实测数据

公共基础设施在 hpm5e31_1 上的实测见表 42-6（日志 `logs/20261102-133416-mcal_hello.log`）：

| 项 | 实测值 | 测量方法 |
|---|---|---|
| 内核主频 | 480 000 000 Hz | `clock_get_frequency(clock_cpu0)`（SDK 读 SYSCTL） |
| `uint8/16/32/64` 宽度 | 1/2/4/8 B | `sizeof` 打印 |
| `Std_ReturnType` 宽度 | 1 B | `sizeof` 打印 |
| Det 记录数 | 注入 1 条后 count=1，可回读 module/api/error | `det` 命令 |
| `Det_ReportError` 周期 | 62.09 cyc/call ≈ 0.129 µs @480 MHz | `detbench` 1000 次 mcycle 差值 |
| `Det_GetLastError` 周期 | 73.11 cyc/call ≈ 0.152 µs @480 MHz | 同上 |
| 基线循环 | 9.71 cyc/iter | 同上（扣除循环开销） |

表 42-6 里 Det 的开销是"一次错误上报"的成本，正常路径不付这个钱；但每个 API 入口的参数检查仍然要几个周期，这部分在 43.5 量化。

串口原始输出（节选，完整日志见 `logs/20261102-133416-mcal_hello.log`）：

```text
MCAL: HPM5E31 MCAL 1.0.0 (AUTOSAR CP R21-11 subset)
module ids: Mcu=101 Port=124 Dio=120 Gpt=100 Pwm=121 Icu=122
            Adc=123 Spi=83 I2c=113 Can=80 Wdg=102 Mem=91 Crc=201 Det=15
dev error detect: ON
DETBENCH: Det_ReportError    = 62090 cyc total (62.09 cyc/iter)
DETBENCH: Det_GetLastError   = 73114 cyc total (73.11 cyc/iter)
```

各模块的实测数据（吞吐、周期、误差、CPU 占用）在表 42-1 对应的第 44~49 章，本章不重复罗列。

### 42.7.1 集成实测：13 个模块跑在同一个固件里（Eth 有意不进集成）

单个模块的例程只能证明"这块驱动能跑"，不能证明"13 个模块能共存"。`samples/mcal/ecu_integration` 就是干这件事的：
把 13 个模块编进同一个 `demo.elf`，按 43 章图 43-3 的顺序初始化，用 1 ms GPT 通知驱动最小调度器，
通过 shell 命令逐项观测。表 42-10 是一次完整运行的实测结果（日志 `logs/20261102-143218-ecu_integration.log`，
命令见例程 README）。

> **为什么 Eth/EthTrcv 不进集成固件**：ENET0 一旦 ACTIVE 会持续收到广播帧，EthTrcv 的链路轮询走 MDIO，
> 两者都会和 1 ms 调度抢时间片；更麻烦的是 YT8011AN 的 MMD1 0x834 软复位会让链路短暂断开，
> 与同固件里的 lwIP 例程互相干扰。因此 Eth 采用“独立例程 + 独立 lwIP/FreeRTOS 例程”的方式验证（第 49 章），
> 集成固件仍是 13 个模块。这个取舍本身也是工程经验：**能共存不等于该共存**。

| 验证项 | 实测结果 | 说明 |
|---|---|---|
| 13 模块初始化 | errors=0、Det=0、PLL 锁定、主频 480 MHz | 时钟→Port→外设→Wdg 顺序正确 |
| 1 ms 调度节拍 | 30 s 内 tick 连续增长、无丢失 | `ecu tick 1000` 实测 delta=1000 |
| 模块状态（一屏） | Spi IDLE、Can STARTED×2、Pwm/PWM0、Icu 捕获中、Wdg state=1、Mem 1 实例 | `ecu` 命令 |
| CAN 回环 | `ecu can send 10` accepted=9、rx=9、tx_cfm=9、busoff=0 | 轮询 MainFunction 释放 HTH；第 10 帧被 HTH 忙拒 |
| PWM→ICU 回环 | ICU period=0x1389（5001 tick × 10 ns ≈ 50 µs） | PD02/03→PD07/06 杜邦线 + TRGM |
| ADC | `ecu adc 200`：mean=35590、min=34896、max=35755（PF26 悬空） | 16 bit，VREFH 3.3 V，约 1.78 V |
| CRC32 | SW=0xCBF43926、HW=0xCBF43926 | 标准向量 "123456789" |
| Mem 几何 | base=0x80000000、size=1 MB、sector=4096、page=256 | 异步作业 + MainFunction |
| 引脚分时复用 | `ecu i2c` → I2c 写序列 rc=0；`ecu pwm 30` → 切回 PWM/ICU | PD02/03、PD07/06 两组线只能分时用 |
| 看门狗 | 100 ms 喂狗、1000 ms 超时，30 s 运行不复位 | 阻塞命令内部也要喂狗（见 43 章坑 8） |

表 42-10 暴露了一个真实的设计约束：**任何超过看门狗超时的阻塞操作都必须自己喂狗**。
第一版集成 demo 的 `ecu tick 1000`（busy-wait 1 s）直接把看门狗等超时、板子复位（复位源 0x00010010 = WDG0）；
在等待循环里插入 `Wdg_SetTriggerCondition()` 后复测通过。这类问题是"单模块例程永远测不出来"的，
也是集成 demo 存在的意义。

## 42.8 性能与调优

MCAL 相对 SDK 直调的额外开销来自三处（表 42-7），调优也对应三处：

| 开销来源 | 量级（本板实测/估算） | 优化手段 |
|---|---|---|
| API 间接层（一次函数调用 + 参数检查） | Det 上报 62 cyc；一般 API 入口 20~60 cyc | `MCAL_DEV_ERROR_DETECT=STD_OFF`；热点路径用内联小函数 |
| 配置查表（PBcfg 常量表） | 1~2 次 Flash 读（命中 Cache 时 ~3 cyc） | 常量表放 `const`；热点索引用编译期常量 |
| 中断/通知回调 | 每次中断多一层函数指针 | 回调函数加 `MCAL_SECTION` 放 ILM（可选） |

表 42-7 的量级来自第 43、45、46 章的实测；如果项目对某条路径极敏感，优先关 Det、再把 PBcfg 常量表放进 ILM。

关键结论：**不要为了"标准"而牺牲实时性**。本仓库的 1 ms GPT 通知、CAN 中断、SPI DMA 传输都没有因为
MCAL 层而明显变慢（第 45~47 章有对比数据）；真正慢的是"配置不当"（如 SPI 用轮询、CAN 用 MainFunction 轮询）。

## 42.9 坑与注意事项

1. **AUTOSAR 版本差异不是小事**：R25-11 的模块 ID 表把 I2C 定为 113，而 NXP S32K3 RTD 把 I2C 做成 CDD（255）；
   R25-11 还把 Fls（92）并入 Memory Driver（Mem，91）。照抄某家厂商的头文件会"看起来对、实际报错码不对"。
   本仓库以 SWS_BSWGeneral §11.2 为准，逐条列在 `mcal/common/Mcal_ModuleId.h`。
2. **Dio 没有 Dio_Init**：AUTOSAR 规定 DIO 的引脚配置由 Port 负责，Dio 只有读写/翻转。自造 `Dio_Init` 的代码
   在正式 MCAL 上编不过。
3. **Mem/Fls 名字**：R25-11 起叫 Mem，之前叫 Fls；API 也有差异（Mem 是作业模型）。读老代码时先确认版本。
4. **公共头不要 include SDK 头**：否则上层（比如 PC 上做静态检查、或者换 SDK 版本）会被 SDK 依赖拖死。
   本仓库驱动 `.c` 可以 include `hpm_*_drv.h`，但 `.h` 不行。
5. **开发错误检测不是"越全越好"**：每次 `Det_ReportError` 要 ~62 cyc，量产热点路径必须能关掉；
   本仓库用 `MCAL_DEV_ERROR_DETECT` 统一开关（`Mcal_Cfg.h`）。
6. **板级资源冲突**：PF26（LED/ADC）、PA03（BOOT）、SPI2（LAN8651）都是"看起来空闲、其实有人用"的引脚；
   写 PBcfg 前先看第 51 章接线索引。
7. **不要用 printf 调试驱动**：MCAL 驱动里 printf 会破坏时序（尤其 CAN/SPI 中断里）；本仓库只允许 `MCAL_DET_PRINTF` 调试开关。

## 42.10 优劣评价与同类对比

表 42-8 是与两个量产级 MCAL 的横向对比：

| 维度 | HPM SDK 直调 | 本仓库 MCAL | Infineon TC4x MCAL 2.20.0 | NXP S32K3 RTD 6.0.0 |
|---|---|---|---|---|
| 标准符合性 | 无 | AUTOSAR CP 子集（自评，非认证） | AUTOSAR R21-11（厂商认证流程） | AUTOSAR R21-11（厂商） |
| 模块覆盖 | 全外设 | 15 个常用模块 | 30 个（含 Eth/Lin/Fee/安全模块） | 30+（含 Eth/Lin/Fee/I2s） |
| 配置工具 | 手写 board yaml/结构体 | 手写 `*_Cfg.h` + `*_PBcfg.c` | EB tresos 插件（图形化） | S32DS + RTD 配置工具 |
| 代码来源 | 厂商 SDK | 本仓库原创（对照 SWS/SDK） | 厂商（含安全手册） | 厂商 |
| 上板验证 | 需自己写例程 | 31 个例程 + 日志 | 厂商示例 + 集成参考例程 | 厂商示例 + 集成参考例程 |
| 安全/认证 | 无 | 无（仅文档讨论） | ASIL-D 安全手册、MISRA 报告 | 安全手册/认证包 |
| 适用 | 原型/量产裸机 | 学习、迁移参考、协议栈适配验证 | 量产（车规项目） | 量产（车规项目） |

表 42-8 的数据来源是三家公开文档与本地安装包（TC4x 2.20.0、S32K3 RTD 6.0.0），不是估算。

一句话评价：**这套 MCAL 的价值在"可读、可跑、可对比"，不在"能过认证"**。要上量产项目，
应当以它作为"接口/配置/集成"的参照，再用厂商认证 MCAL（或补齐第 50 章列出的差距项）。

## 42.11 参考来源

- AUTOSAR Classic Platform 标准与 SWS 下载：<https://www.autosar.org/standards/classic-platform/>
- AUTOSAR CP R25-11《SWS BSW General》§11.2 模块 ID 表（本地文本 `autosar_txt/AUTOSAR_CP_SWS_BSWGeneral.txt`）
- HPM SDK v1.13.0：<https://github.com/hpmicro/hpm_sdk>；文档入口 <https://doc.hpmicro.com/>
- 先楫半导体官网：<https://www.hpmicro.com/>
- Infineon AURIX TC4x MCAL / iLLD：<https://github.com/Infineon/illd_release_tc4x>、<https://www.infineon.com/>
- NXP S32K3 标准软件（RTD）：<https://www.nxp.com/design/software/development-software/s32k3-standard-software>
- Elektrobit EB tresos：<https://www.elektrobit.com/products/ecu/eb-tresos/studio/>
- 本仓库第 02 章（工具链）、第 4~22 章（外设寄存器级实测）、第 27 章（同类对比方法论）、第 51 章（引脚/接线索引）

## 42.12 待确认项与风险

表 42-9 是本章登记的遗留项；每一条都能在第 50 章找到对应的差距分析。

| # | 项 | 状态 | 说明 / 复跑方法 |
|---|---|---|---|
| 1 | 本 MCAL 未做认证 | 已知限制 | 无 ISO 26262 安全手册、无 MISRA 报告、无认证证书；第 50 章给出差距清单 |
| 2 | 配置工具缺失 | 已知限制 | 手写 `*_Cfg.h`/`*_PBcfg.c`，没有 EB tresos 那样的图形化生成器；换板时需人工改 PBcfg |
| 3 | 多核/OS 集成 | 已知限制 | 本仓库裸机 + shell 轮询；AUTOSAR OS 的 `SchM_Enter/Exit` 临界区未实现（`MemMap` 也是空桩） |
| 4 | 中断向量表集成 | 已实测 | 15 个模块都走 SDK 的中断注册；若接 AUTOSAR OS 需要改 `Os_Isr` 封装（第 43 章说明） |
| 5 | 覆盖率/WCET | 已知限制 | 未做语句/分支覆盖率与 WCET 测量；可用 `-fprofile`/Trace32 补测 |
| 6 | 部分模块只覆盖必选 API | 已定位 | 见各章"待确认项"表与 `docs/mcal/MCAL_API_TRACEABILITY.md` |