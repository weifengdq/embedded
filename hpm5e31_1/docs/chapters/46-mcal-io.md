# 第 46 章 MCAL I/O 驱动：Spi / I2c / Adc

## 46.1 文档信息

| 项目 | 内容 |
|---|---|
| 章标题 | MCAL I/O 驱动：Spi / I2c / Adc |
| 版本 | v1.3.0（2026-11-02，R6 MCAL） |
| 适用硬件 | hpm5e31_1（HPM5E31xGNx，24 MHz XTAL，8 MB Flash XIP）；SPI1 主 / SPI3 从、I2C2 控制器 / I2C3 目标、ADC0_IN01 = PF26 |
| 适用软件 | HPM SDK v1.13.0、GCC RISC-V（-Wall -Wextra）、本仓库 autosar 分支 |
| 对应例程 | samples/mcal/spi_master_slave、spi_async_job、i2c_master_slave、i2c_error；ADC 部分对应 mcal-core 交付的 adc_single、adc_bench |
| 实测日期 | 2026-11-02（SPI/I2C 数据全部为本板上板实测；ADC 实测由 mcal-core 提供，见 §46.13） |
| 可信度 | 高：吞吐、时延、误码都能在 logs/ 的串口日志里逐行复核；ADC 章节的设计部分按 SWS 核对，实测数据引用 mcal-core 日志 |
| 代码位置 | mcal/Spi/**、mcal/I2c/**（驱动）、4 个 IO 例程目录；ADC 驱动在 mcal/Adc/** |

## 46.2 本章导览

这一章写 MCAL 的三类 I/O 驱动：Spi（第 83 号模块）、I2c（113）、Adc（123）。三个模块的共同点是
"上层看到的是一套异步/同步服务，底层是位流时序"，差别在于 SPI 全双工、I2C 半双工带地址、
ADC 是纯输入。写作顺序：

1. §46.3 先给职责边界与硬件分配，避免和 LAN8651 的 SPI2、qei 的 PD06/PD07 抢资源；
2. §46.4 讲三类驱动的原理：SPI 的 EB/IB 与 Job/Sequence 链，I2C 的 controller/target 模型，ADC 的组与结果缓冲；
3. §46.5 是寄存器对照表，驱动源码基本就是按这张表逐条落的；
4. §46.6、§46.7 给配置模型与例程命令；
5. §46.8、§46.9 是上板实测：SPI 1 MHz 与 10 MHz 全双工吞吐、1 MB 连续传输误码、I2C 三档速率时延；
6. §46.10 是踩过的坑（DMA 使能残留、IB 缓冲共享、从机多发 1 字节、UART 抓包丢字节等）；
7. §46.11 与 HPM SDK 直调、Infineon TC4x QSPI、NXP S32K3 Lpspi 做对照；
8. §46.13 列已知限制，全部带复跑命令。

## 46.3 概述

三个模块的分工与例程绑定关系如下（表 46-1）。

| 模块 | AUTOSAR 模块 ID | 本实现覆盖面 | 例程 | 板级资源 |
|---|---|---|---|---|
| Spi | 83 | LEVEL 2：Init/DeInit/WriteIB/ReadIB/SetupEB/SyncTransmit/AsyncTransmit/GetStatus/GetJobResult/GetSequenceResult/GetHWUnitStatus/Cancel/SetAsyncMode/GetVersionInfo + JobEnd/SeqEnd 回调 | spi_master_slave、spi_async_job | SPI1 主（PC10/11/12/13）、SPI3 从（PA26/27/28/29）；SPI2 留给 LAN8651 |
| I2c | 113 | R25-11 draft API：Init/DeInit/SetupEB/AsyncTransmit/SyncTransmit/GetSequenceResult/GetChannelStatus/GetVersionInfo/MainFunction + SeqEnd 回调 | i2c_master_slave、i2c_error | I2C2 控制器（PD02/PD03）、I2C3 目标（PD07/PD06，地址 0x50） |
| Adc | 123 | 13 个 API 全部实现：Init(0x00)/DeInit(0x01)/StartGroupConversion(0x02)/StopGroupConversion(0x03)/ReadGroup(0x04)/EnableHardwareTrigger(0x05)/DisableHardwareTrigger(0x06)/EnableGroupNotification(0x07)/DisableGroupNotification(0x08)/GetGroupStatus(0x09)/GetVersionInfo(0x0A)/GetStreamLastPointer(0x0B)/SetupResultBuffer(0x0C) | adc_single、adc_bench | ADC0_IN01（PF26，独占；该脚同时是 CAN0 红灯） |

表 46-1 的"覆盖面"一栏是按 SWS 必选 API 逐条核对的。ADC 驱动由 mcal-core 实现，
范围是 SWS_ADCDriver R21-11 的 13 个 API，Service ID 与 DET 码（ADC_E_UNINIT 0x0A、BUSY 0x0B、
IDLE 0x0C、ALREADY_INITIALIZED 0x0D、PARAM_POINTER 0x14、PARAM_GROUP 0x15、WRONG_CONV_MODE 0x16、
WRONG_TRIGG_SRC 0x17、NOTIF_CAPABILITY 0x18、BUFFER_UNINIT 0x19）全部对齐 SWS 原文。

## 46.4 工作原理

### 46.4.1 Spi：EB/IB 与 Job/Sequence 链

AUTOSAR SPI 的数据模型分三层（SWS_SPIHandlerDriver §7）：

- **Channel**：一次交换的最小单位，带数据宽度（8/16/32 bit）。EB（External Buffer）通道由用户用
  Spi_SetupEB 传缓冲指针；IB（Internal Buffer）通道用驱动内部缓冲，Spi_WriteIB/Spi_ReadIB 读写。
- **Job**：一组 Channel 的顺序执行，绑定一个硬件单元（HW Unit），带优先级与异步模式。
- **Sequence**：一组 Job 的顺序执行，带序列结束通知。

本实现的状态机是"一个全局运行槽 + 顺序推进"：

```text
Spi_SyncTransmit(seq) / Spi_AsyncTransmit(seq)
        |
        v
  seq_start: 取 seq->JobList[0]
        |
        v
  chan_start: 取 job->ChannelList[0]，按 HWUnit 是主/从分支
     |-- 主: spi_transfer()（8/16/32 bit，EB/IB 缓冲） -> 通道完成 -> 下一个通道
     |-- 从: DMA 武装（RX 先、TX 后） -> 立即推进到下一个 Job（让主机发时钟）
        |
        v
  主机通道完成时回收从机 DMA TC -> job_finish(SPI_JOB_OK) + JobEndNotification
        |
        v
  所有 Job 完成 -> seq_finish(SPI_SEQ_OK) + SeqEndNotification
```

这里有一个必须写进契约的约定：**从机 Job 武装完成后立即推进序列**，而不是等它传完。
原因是同一颗 MCU 内部主从互测时，从机的时钟由序列里下一个主机 Job 提供，等下去就是死锁。
SWS 的 Job 同步语义在这种"片内主从"场景下不适用，工程上用"武装即推进 + 主机完成后回收"实现。

### 46.4.2 I2c：R25-11 的 Controller / Target 模型

R24-11 起 AUTOSAR 把 I2C 驱动重写成 job/sequence 模型（SWS_I2CDriver R25-11 仍是 draft），
与老的 I2c_Init/I2c_Write/I2c_Read 语义差别很大，对照见表 46-2。

| 项目 | 老版 I2C 驱动（R4.x，I2c_* 前缀） | 本实现（R25-11 draft，I2C_* 前缀） |
|---|---|---|
| 初始化 | I2c_Init(ConfigPtr) | I2C_Init(ConfigPtr)，Config 里含 Channel/Job/Sequence |
| 数据缓冲 | I2c_Write/Read 直接给指针，无 SetupEB | 必须 I2C_SetupEB 先登记 Job 的 TX/RX 指针与长度 |
| 传输 | I2c_Write/I2c_Read 调用即传 | I2C_AsyncTransmit/I2C_SyncTransmit 按 Sequence 执行 |
| 结果查询 | 返回值 + 内部状态 | I2C_GetSequenceResult（OK/PENDING/QUEUED/NACK/FAILED） |
| 硬件状态 | 无标准接口 | I2C_GetChannelStatus（I2C_IDLE=1 / I2C_BUSY=2） |
| 主从命名 | Master/Slave | Controller/Target |
| 调度 | 无 | I2C_MainFunction（异步完成） |

主机侧本实现用阻塞写/读实现，目标侧用"I2C3 地址命中中断 + HDMA ch6"实现：
中断里读方向位，控制器写则武装 RX DMA，控制器读则武装 TX DMA，事务完成中断置标志。
这样目标侧不需要 CPU 参与移位。

### 46.4.3 Adc：组、结果缓冲与触发

ADC 的 AUTOSAR 模型是"Group"而不是"Channel"：一个 Group 含若干通道，一次转换把整组结果写进
用户通过 Adc_SetupResultBuffer 注册的缓冲。软件触发组由 Adc_StartGroupConversion 启动；
硬件触发组要先 Adc_EnableHardwareTrigger，再由外部事件触发。转换完成可以通过
Adc_GetGroupStatus 查询，也可以打开 Adc_EnableGroupNotification 走通知回调。
HPM5E31 的 ADC16 有两个必须知道的实现细节：一是 **oneshot 是 bus mode**——读 BUS_RESULT
就是"触发一次转换并阻塞到 VALID"，前提是 `BUF_CFG0.BUS_MODE_EN=1`、`WAIT_DIS=0`；
二是 **转换结束计数** `ADC16_CONFIG1.COV_END_CNT` 必须配成 `MAX_CONV_CLK_NUM - res + 1`（本板 21-16+1=6），
漏配的后果见 §46.10 第 8 条。寄存器级基线见 samples/adc/adc_basic_poll（第 18 章），
本章的 ADC 数据引 mcal-core 的 MCAL 驱动实测。

## 46.5 寄存器对照

表 46-3 列出驱动用到的关键寄存器。SPI 与 I2C 的位域名来自 HPM5E31 RM 对应章节，
驱动源码注释里逐条标注了位域名。

| 外设 | 寄存器 | 关键位域 | 驱动里的用法 | 例程位置 |
|---|---|---|---|---|
| SPI | TRANSFMT | DATALEN[12:8] / SLVMODE[2] / CPOL[1] / CPHA[0] | 每个 Channel 开始前按 8/16/32 bit 重写 DATALEN；主从模式在 Spi_Init 定 | spi_master_slave 的 spi width 命令 |
| SPI | TIMING | SCLK_DIV[7:0] / CS2SCLK / CSHT | spi_master_timing_init 按 SrcClk/JobBaudrate 算分频；1 MHz 分频 0x27 | spi init 1m / spi init 10m |
| SPI | TRANSCTRL | WRTRANCNT[20:12] / RDTRANCNT[8:0] / TRANSMODE | 每次传输前写收发元素数（spi_control_init） | 全部 SPI 例程 |
| SPI | STATUS | TXFULL[23] / TXEMPTY[22] / RXEMPTY[14] / SPIACTIVE[0] | CPU 泵的流控与"传输是否结束"判定 | spi_master_slave 数据相 |
| SPI | CTRL | CS_EN[27:24] / TXTHRES[23:16] / RXTHRES[15:8] / TXDMAEN[4] / RXDMAEN[3] | 阈值设 FIFO 深度一半；DMA 使能必须在 arm 通道前清零 | spi_slave_arm/spi_master_dma_arm |
| SPI | INTREN/INTRST | ENDINTEN[4] | 异步中断模式开 END 中断 | spi_async_job 的 hint 命令 |
| I2C | SETUP | 速度模式 / 主从 / FIFO 阈值 | i2c_init_master / i2c_init_slave 写入 | i2c init 100k/400k/1m |
| I2C | ADDR | ADDR[9:0] | 目标模式本机地址 0x50 | I2c_PBcfg.c |
| I2C | STATUS | ADDRESS_HIT / TRANSACTION_COMPLETE / FIFO 状态 | 目标 ISR 里按 ADDRESS_HIT 方向武装 DMA，CMPL 置完成 | I2C_Isr_Channel |
| I2C | DATA | 8 bit FIFO 入口 | 主机读写、目标 DMA 的源/目的 | i2c_master_job |
| ADC16 | ADC_CFG0 | ADC_AHB_EN 等 | 序列/抢占模式需要 AHB 使能，oneshot 不需要 | mcal/Adc/src/Adc.c |

## 46.6 配置模型与代码

三个模块都按"pre-compile + post-build"两段配置（契约 §5）。预编译开关在 `<Mod>_Cfg.h`：
通道数、Job/Sequence 数、IB 缓冲大小、超时轮询上限、DevErrorDetect。板级参数全部集中在
`<Mod>_PBcfg.c`，驱动源码里没有引脚名和外设实例名。下面是 SPI 从机 HW Unit 的配置片段
（真实代码在 mcal/Spi/cfg/Spi_PBcfg.c）：

```c
{   /* SPI3 slave：PA26 SCLK / PA27 CS / PA28 MISO / PA29 MOSI */
    .Base = (void *)HPM_SPI3,
    .DmaBase = (void *)HPM_HDMA,
    .DmamuxBase = (void *)HPM_DMAMUX,
    .PinInit = init_spi3_slave_pins,     /* 板级引脚：改板只改这里 */
    .ClockEnable = spi3_clock_enable,    /* 板级时钟门 + 返回源频率 */
    .IsMaster = FALSE,
    .Cpol = 0u, .Cpha = 0u,              /* mode 0 */
    .SclkFreqHz = 1000000u,
    .IrqNum = (uint16)IRQn_SPI3,
    .TxDmaChannel = 4u, .RxDmaChannel = 5u,
    .TxDmaSrc = (uint8)HPM_DMA_SRC_SPI3_TX,
    .RxDmaSrc = (uint8)HPM_DMA_SRC_SPI3_RX,
    .ExtraTxElements = 1u,   /* HPM 从机 TX FIFO 首个时钟前欠载，多发 1 个哑元补偿 */
},
```

多套 post-build 配置集用来演示运行期换 SCLK：`Spi_Config` 是 1 MHz，`Spi_Config_10M` 是 10 MHz，
例程用 `Spi_DeInit()` + `Spi_Init(&Spi_Config_10M)` 切换。这比"Spi_SetBaudrate 私有扩展"更符合
AUTOSAR 的配置集语义，也避免了驱动里额外开一个非标准 API。

## 46.7 例程

四个例程的命令与验证点见表 46-4。所有例程都只用 MCAL API，不直接写寄存器（除了诊断命令
spi dbg / spi raw，它们用于对照 SDK 直调路径，单独标注）。

| 例程 | 命令 | 验证点 | 日志 |
|---|---|---|---|
| spi_master_slave | spi info / spi init 1m\|10m / spi xfer [n] / spi width 8\|16\|32 [n] / spi bench / spi ber [MB] [chunk] / spi ib / spi det | 1 MHz/10 MHz 双向 1 MB 零误码、8/16/32 bit、IB 通道、Det 计数 | logs/20261102-142330-spi_master_slave.log |
| spi_async_job | spi_job info / multi [n8] [n16] / cpu [n] / hint [n] / det | 4 Job 异步序列完成、JobEnd/SeqEnd 回调次数、同步/异步 CPU 周期 | logs/20261102-142516-spi_async_job.log |
| i2c_master_slave | i2c info / init 100k\|400k\|1m / xfer [n] / async [n] / ber [count] / det | 三档速率读写、异步 PENDING→OK、连续 0 误码 | logs/20261102-142612-i2c_master_slave.log |
| i2c_error | i2c_err nack / busy / det / all | NACK 上报、总线被占上报、DevError 计数 | logs/20261102-142544-i2c_error.log |

## 46.8 实测数据

### 46.8.1 SPI 全双工：1 MHz 与 10 MHz

SPI1 主、SPI3 从、mode 0、8 bit，每次 Spi_SyncTransmit 双向各 n 字节，逐字节比对
（主收 vs 从发、从收 vs 主发）。表 46-5 是三次重复取中位数的结果。

| SCLK | 负载 | 时延 | 双向吞吐 | 理论双向 | 效率 | 误码 |
|---|---|---|---|---|---|---|
| 1 MHz | 64 B | 534.5 us | 239 KB/s | 250 KB/s | 95.6% | 0 |
| 1 MHz | 256 B | 2064.7 us | 247 KB/s | 250 KB/s | 98.8% | 0 |
| 1 MHz | 1024 B | 8208.6 us | 249 KB/s | 250 KB/s | 99.6% | 0 |
| 10 MHz | 64 B | 63.7 us | 2008 KB/s | 2500 KB/s | 80.3% | 0 |
| 10 MHz | 256 B | 217.3 us | 2356 KB/s | 2500 KB/s | 94.2% | 0 |
| 10 MHz | 1024 B | 831.6 us | 2462 KB/s | 2500 KB/s | 98.5% | 0 |

小负载效率低是因为每次传输的固定开销：Spi_DeInit/Init 不算，但 spi_control_init 的 FIFO 复位、
DMA 武装、从机 CS 建立时间都在 64 B 的 63.7 us 里占掉约 12 us。负载到 1 KB 时效率接近 100%，
说明数据相通路没有额外等待。图 46-1 是 1 MHz 下时延与吞吐的关系。

![图 46-1 SPI1 主与 SPI3 从 1 MHz 全双工的时延与吞吐（柱：实测双向吞吐；虚线：理论 250 KB/s）](figures/46-mcal-io/fig-46-1-spi-throughput.png)

### 46.8.2 SPI 连续 1 MB 传输

命令 `spi ber 1 1024`：1 MB 按 1024 B 分块，每块一次 Spi_SyncTransmit，累计 1024 次，
统计误码、失败次数与吞吐。1 MHz 与 10 MHz 各跑一次（表 46-6）。

| SCLK | 传输量 | 分块 | 次数 | 失败 | 误码 | 总时间 | 单向吞吐 | 双向吞吐 | 结论 |
|---|---|---|---|---|---|---|---|---|---|
| 1 MHz | 1 MB | 1024 B | 1024 | 0 | 0 / 16777216 bit | 8755.5 ms | 119 KB/s | 238 KB/s | PASS |
| 10 MHz | 1 MB | 1024 B | 1024 | 0 | 0 / 16777216 bit | 1201.7 ms | 872 KB/s | 1744 KB/s | PASS |

这两组数据暴露了一个工程事实：分块连续传输的效率低于单次大块。10 MHz 时单次 1 KB 能到
2462 KB/s 双向（98.5%），但连续 1024 次只有 1744 KB/s（69.8%），差值来自每次传输之间的
软件切换开销（EB 指针重设、从机 DMA 重新武装、FIFO 复位）。想再往上只能把多次 Job 放进一个
Sequence 里连续发，或者用 DMA 链式描述符——这是后续优化方向，不是当前缺陷。

![图 46-3 SPI 连续 1 MB 传输实测吞吐与理论上限对比（1 MHz 95.2%，10 MHz 69.8%）](figures/46-mcal-io/fig-46-3-spi-1mb.png)

### 46.8.3 SPI 数据宽度与 IB 通道

8/16/32 bit 三种宽度用同一套 Job/Sequence，只是 Channel 配置不同（表 46-7）。
IB 通道（驱动内部缓冲）单独用 SEQ_IB8 验证，收发各 64 B、逐字节比对通过。

| 用例 | 元素数 | 元素宽度 | 字节数 | 时延 | 双向吞吐 | 结果 |
|---|---|---|---|---|---|---|
| spi width 16 64 | 64 | 16 bit | 128 B | 1058.2 us | 241 KB/s | PASS |
| spi width 32 32 | 32 | 32 bit | 128 B | 1051.3 us | 243 KB/s | PASS |
| spi ib（IB 通道） | 64 | 8 bit | 64 B | 535.2 us | 239 KB/s | PASS |

16 bit 与 32 bit 的时延几乎相同（1058 vs 1051 us），说明位宽换算没有额外软件开销；
瓶颈在 SCLK 频率，不在 CPU 打包。

### 46.8.4 ADC：单次组与连续组（mcal-core 实测）

ADC0.IN01 = PF26，16 bit，conv_clk = 50 MHz，采样保持 20 cycles，VREFH 3.3 V，输入悬空。
数据来自 logs/20261102-143752-adc_single.log（单次组）与 logs/20261102-143824-adc_bench.log（连续组）。

| 组类型 | 样本数 | mean | sd | 峰峰值 pp | ENOB | 吞吐 | 结论 |
|---|---|---|---|---|---|---|---|
| 单次组 | 1000 | 35406.77 LSB（1.782900 V） | 62.66 LSB | 1378 LSB | 8.43 bit | 491.8 kS/s | PASS |
| 单次组 | 5000 | 35404.32 LSB | 60.29 LSB | 1749 LSB | 8.48 bit | 506.4 kS/s | PASS |
| 连续组 | 20000 轮 | — | — | — | — | 740.4 kS/s（1350.64 ns/轮） | PASS |
| 连续组 | 50000 轮 | — | — | — | — | 740.1 kS/s（1351.11 ns/轮） | PASS |

关键结论：

1. **吞吐**：单次组含 StartGroupConversion + ReadGroup 的软件开销，491.8~506.4 kS/s；
   连续组把"一次 ReadGroup = 一轮"做成软件泵，2 万轮与 5 万轮重复性 0.04%，740.1~740.4 kS/s。
   理论采样率 = conv_clk / (16 + 20) = 1388.9 kS/s，实测 740 kHz 是**软件泵的上限**（53.3%），
   硬件自主连续转换需要中断或 PRD 模式，见 §46.13 的实现路径。
2. **噪声**：悬空输入下 sd ≈ 60~63 LSB、ENOB ≈ 8.4~8.5 bit。这个 sd 不是 ADC 本底噪声，
   而是悬空引脚的板级偏置与工频耦合；mean ≈ 35405 LSB（1.78 V）同样来自输入偏置，不是 0。
   要评估线性度必须外接基准源，本轮没有做，写进 §46.13 的已知限制。
3. **资源占用**（Debug 构建，build/adc_single-debug）：text 4870 B、data 0 B、bss 69 B。
   对 MCAL 模块来说这个体积很小，适合放进 Flash 紧张的 ECU。
4. **Det 错误路径**：单次组注入 delta=6、连续组 delta=3，与例程断言一致。

![图 46-4 ADC0.IN01 单次组与连续组的采样率、噪声 sd 与 ENOB（mcal-core 上板实测）](figures/46-mcal-io/fig-46-4-adc-group.png)

### 46.8.5 I2C 三档速率与错误路径

I2C2 控制器写/读 32 B，目标 I2C3 地址 0x50。2026-11-02 复测时定位并修掉了一个只在 400 kHz
出现的**目标侧时序问题**（下面单独讲），修复后三档速率的写、读全部稳定通过。结果见表 46-8，
理论时延按 9 bit/字节（8 数据位 + 1 ACK）估算。

**表 46-8 I2C 三档速率 32 B 传输实测（目标侧 T_SUDAT 修复后，日志 logs/20261102-161116-i2c_master_slave.log）**

| 速率 | 写 32 B | 读 32 B | 理论时延 | 理论/实测 | 读方向 |
|---|---|---|---|---|---|
| 100 kHz | 3238.8 us | 3233.8 us | 2880 us | 89% | PASS（0 错） |
| 400 kHz | 992.8 us | 988.8 us | 720 us | 72% | PASS（0 错；修复前 100% FAIL） |
| 1 MHz | 538.4 us | 536.6 us | 288 us | 53% | PASS（0 错） |

注：实测"线速率"低于标称（400 kHz 档折合约 290 kbit/s、1 MHz 档约 535 kbit/s）。原因是目标
（从机）侧只有 4 字节 FIFO + DMA 装填，每个字节会产生约 8~11 us 的 SCL 延展；这是板内两个
I2C 实例互测的固有限制，与驱动无关，接真实外部从机时不会有这种延展。

连续误码用 `i2c ber 1000` 在三档速率各跑 1000 轮（每轮 24 B 写 + 24 B 读），结果见表 46-9：

**表 46-9 1000 轮连续误码（24 B 写 + 24 B 读，日志 logs/20261102-155949-i2c_master_slave.log）**

| 速率 | 轮数 | 传输量 | 失败（写/读） | 错误字节 | 总时间 | 有效吞吐 | 单次 24 B 往返 | 结论 |
|---|---|---|---|---|---|---|---|---|
| 100 kHz | 1000 | 48000 B | 0 / 0 | 0 | 4925.39 ms | 9745 B/s | 4.93 ms | PASS |
| 400 kHz | 1000 | 48000 B | 0 / 0 | 0 | 1516.18 ms | 31658 B/s | 1.52 ms | PASS |
| 1 MHz | 1000 | 48000 B | 0 / 0 | 0 | 829.82 ms | 57843 B/s | 0.83 ms | PASS |

另外 256 B 大块读回 ×50：400 kHz 单次 7.62 ms、1 MHz 单次 4.10 ms，均 0 误码。

**400 kHz 读失败的根因与修复（2026-11-02）**

改前只有 400 kHz 的读方向失败，写方向 0 错；100 kHz 与 1 MHz 都正常。定位过程：

1. 排除接线/上拉：同一对杜邦线、同一组引脚，另外两档 0 错；
2. 回读两侧 `SETUP`（`i2c regs` 命令）并按 RM §50.2.2 的公式 (6)~(10) 解码，发现 fast（400 kHz）
   和 fast_plus（1 MHz）两档的 `T_SUDAT` 都被 SDK 算成 0，数据建立时间只有 100 ns
   （= 快速模式规范下限，没有任何余量），而标准模式（100 kHz）是 12（250 ns）；
3. 用临时诊断命令只改 `SETUP.T_SUDAT` 做控制变量（表 46-10）。

**表 46-10 目标侧 T_SUDAT 控制变量实验（400 kHz，24 B 读 ×50）**

| 控制器侧 T_SUDAT | 目标侧 T_SUDAT | 结果 | 日志 |
|---|---|---|---|
| 0 | 0（改前） | fail=49/50，错误字节集中在低位 | logs/20261102-153604-i2c_master_slave.log |
| 12 | 0 | fail=50/50（只改控制器无效） | 同上 |
| 0 | 12 | **PASS 50/50**（再改回 0 又 100/100 失败） | 同上、logs/20261102-153857-i2c_master_slave.log |
| 0 | 4 / 8 / 16 | PASS 50/50（阈值在 0~4 之间） | logs/20261102-153728-i2c_master_slave.log |

结论：根因在**目标（从机）侧的数据建立时间余量**，唯一变量是 `T_SUDAT`，不是控制器读时序、
不是 SCL 时钟延展，也不是接线。修复放在驱动里：`I2C_Init` 对目标通道初始化后调用
`i2c_target_timing_fixup()`，把 `T_SUDAT` 按"标准模式 250 ns 下限"抬高（80 MHz 功能时钟下算得 12）：

```c
/* SetupTime = 2*Tclk + (2 + T_SP + T_SUDAT)*Tclk >= 250 ns（标准模式下限） */
need    = (2500u > 2u * tpclk) ? ((2500u - 2u * tpclk) / tpclk) : 0u;
t_sudat = (need > (2u + t_sp)) ? (need - 2u - t_sp) : 0u;
if (I2C_SETUP_T_SUDAT_GET(p->SETUP) < t_sudat) {
    p->SETUP = (p->SETUP & ~I2C_SETUP_T_SUDAT_MASK) | I2C_SETUP_T_SUDAT_SET(t_sudat);
}
```

I2C 从机不产生 SCL，`T_SUDAT/T_HDDAT` 只影响从机自身的采样/建立窗口，抬高它不改变总线速率
（实测修复前后 400 kHz/1 MHz 传输时间不变），也不影响与外部主机的互通。修复后两侧寄存器：
400 kHz 目标 `SETUP` 由 0x009023d1 变成 0x0c9023d1（`T_SUDAT=12`），控制器保持 0x009023d5。

![图 46-2 I2C2 控制器与 I2C3 目标 32 B 传输的三档速率实测时延与效率](figures/46-mcal-io/fig-46-2-i2c-rate-latency.png)

错误路径（i2c_error 例程）：

- 访问不存在的地址 0x33：I2C_SyncTransmit 返回 E_NOT_OK，I2C_GetSequenceResult 返回
  I2C_SEQ_NACK（0x03），驱动按 SWS §7.3.2 上报 Det 运行时错误 I2C_E_NACK_RECEIVED；
- 人为把 SDA 拉低模拟总线被占：传输失败并上报 I2C_E_ARBITRATION_FAILURE；
- 8 条非法参数调用：Det 计数 +8，与预期一致。

异步路径：I2C_AsyncTransmit 返回 E_OK 时 I2C_GetSequenceResult 为 I2C_SEQ_PENDING（1），
主循环调用 I2C_MainFunction 后变 I2C_SEQ_OK（0），16 B 传输 937 us。

## 46.9 性能

### 46.9.1 CPU 占用：同步 vs 异步

spi_async_job 的 `spi_job cpu 64` 用内核周期计数比较两条路径（64 B 双向）：

- Spi_SyncTransmit：258113 周期，537 us，期间 CPU 100% 在驱动里；
- Spi_AsyncTransmit + Spi_MainFunction_Handling 轮询：254169 周期，529 us。

异步轮询模式的 CPU 消耗与同步基本相同——因为"轮询"本身就是把 CPU 交出去执行传输。
真正省 CPU 的是中断模式（DMA 搬数据 + END 中断收尾），本实现的中断路径尚未通过实测，
见 §46.13 的已知限制。这一点必须在选型时说清楚：AUTOSAR 的"异步"不等于"非阻塞"，
ASYNC_MODE_POLLING 只是把阻塞点从 API 挪到 MainFunction。

### 46.9.2 Job/Sequence 链的开销

`spi_job multi 64 32` 跑 4 个 Job 的序列（8 bit 一对 + 16 bit 一对），总耗时 1056 us，
四个 Job 结果全为 SPI_JOB_OK（0），JobEndNotification 回调 4 次、SeqEndNotification 2 次
（boot 自检一次 + 命令一次）。按单次传输 535 us 加 1058 us 估算，序列开销约 0 us 量级，
说明状态机推进本身不是瓶颈。

### 46.9.3 与理论上限的差距

| 模块 | 理论上限 | 实测最好 | 达成率 | 差距来源 |
|---|---|---|---|---|
| SPI 1 MHz 单次 | 250 KB/s 双向 | 249 KB/s | 99.6% | 固定开销摊薄后接近 0 |
| SPI 10 MHz 单次 | 2500 KB/s 双向 | 2462 KB/s | 98.5% | FIFO 复位与 CS 建立 |
| SPI 10 MHz 连续 | 1250 KB/s 单向 | 872 KB/s | 69.8% | 每次传输的软件重新武装 |
| I2C 1 MHz | 111 kB/s 单向 | 68 kB/s | 61% | 目标模式地址命中窗口 |

## 46.10 坑

1. **DMA 使能残留导致数据整体丢头**。现象：SPI 从机发 64 B，主机收到的数据从第 4~8 字节开始。
   原因：上一次传输残留 TXDMAEN/RXDMAEN=1，武装 DMA 通道后 DMA 立刻往 TX FIFO 灌数，
   紧接着 spi_control_init 复位 FIFO，数据被清掉但 DMA 已计数。修法：arm 通道前先
   `spi_disable_dma(SPI_CTRL_TXDMAEN_MASK | SPI_CTRL_RXDMAEN_MASK)`。
2. **IB 通道 TX/RX 共用一块缓冲会互相覆盖**。现象：IB 测试收到全 0 或收到自己发的数据。
   修法：驱动内部给 IB 通道分 TX/RX 两块缓冲（SWS 的 IB 只有一个逻辑缓冲，但全双工下必须分开）。
3. **IB 缓冲必须放 DMA 可访问内存**。HPM5E31 的 ILM/DLM 段 DMA 访问不到，静态数组默认可能落进去，
   现象是 DMA 读写全 0。修法：`ATTR_PLACE_AT_NONCACHEABLE` 放到 AXI SRAM。EB 缓冲同理，由调用者保证。
4. **HPM SPI 从机 TX FIFO 首个时钟前会欠载**，只发 n 字节时会整体错位 1 字节。
   修法：PBcfg 里 `ExtraTxElements = 1`，从机多发一个哑元；这一点 SWS 没有对应配置项，
   只能放在板级配置里并写进 README。
5. **Job 切换必须同步切换 HW Unit**。状态机里只更新 job 索引、忘了更新 hw 索引时，
   主机 Job 会按从机分支走，表现为"序列永远等从机 DMA 完成"。
6. **串口抓包丢字节不等于固件没输出**。实测串口日志会整段丢 40~50 字节，
   定位时先用 `--expect` 或心跳打印确认真实进度，不要拿串口片段当唯一证据。
7. **目标发送方向"预装首字节"看起来能修 400 kHz，但会引入更大问题**。把首字节写进 DATA、
   DMA 只搬 n-1 字节后，I2C 从机的事务计数与 DMA 长度不一致，100 kHz 下末字节变成 0x00 且
   单次传输涨到 9.3 ms。当时的结论是"要修必须同时改从机计数配置"；2026-11-02 的复查推翻了这个
   判断——真正的根因是目标侧 `SETUP.T_SUDAT=0`（数据建立时间只有 100 ns，见 §46.8.5 表 46-10），
   预装首字节只是把"从机准备数据"的时间提前、掩盖了建立时间不足，属于治标不治本。最终修法在
   驱动里抬高 `T_SUDAT`，没有动从机事务计数。
8. **ADC16 漏配 COV_END_CNT 会让噪声爆表**。必须配 `ADC16_CONFIG1.COV_END_CNT =
   ADC16_SOC_MAX_CONV_CLK_NUM - res + 1`（本板 21-16+1=6）；漏配时 sd 从 60 LSB 飙到 13137 LSB、
   ENOB 从 8.48 掉到 0.67，看起来像"ADC 坏了"，实际是转换结束点配错。
9. **ADC16 oneshot 是 bus mode**：读 BUS_RESULT 即触发转换并阻塞到 VALID，必须
   `BUF_CFG0.BUS_MODE_EN=1`、`WAIT_DIS=0`，否则读到的 VALID 永远不置位（老 SDK 版本有
   `ADC_SOC_BUSMODE_ENABLE_CTRL_SUPPORT` 分支，新版本已默认处理）。
10. **PF26 兼作 CAN0 红灯**：ADC 例程独占 PF26 时不能同时跑 Dio/CAN 的点灯测试，否则 ADC 读数被 LED 驱动拉偏。

## 46.11 对比

### 46.11.1 与 HPM SDK 直调

同一块板、同一对 SPI 例程，SDK 直调路径（samples/mcal/spi_master_slave 的 `spi raw` 命令）
与本 MCAL 驱动路径的对照：

| 项目 | SDK 直调 | 本 MCAL 实现 |
|---|---|---|
| 初始化 | 手工调 spi_format_init/spi_master_timing_init + 引脚 + 时钟 | Spi_Init(&Spi_Config)，配置集选择 |
| 数据相 | spi_transfer 阻塞，或自己配 DMA | Job/Sequence 状态机；主数据相复用 SDK 的寄存器级 FIFO 泵 |
| 从机 | 手工 arm DMA + 手工等 TC | 从机 Job 配置化，DMA 通道/源/中断全在 PBcfg |
| 参数检查 | 无 | 每个 API 有 DevError 检查，Det 可计数 |
| 移植成本 | 换板要改 main.c | 换板只改 `*_PBcfg.c` |

### 46.11.2 与 Infineon TC4x QSPI、NXP S32K3 Lpspi

| 维度 | Infineon TC4x MCAL（QSPI） | NXP S32K3 RTD（Lpspi） | 本实现（HPM5E31 Spi） |
|---|---|---|---|
| 模块/API | Spi_* 标准 API，LEVEL 0/1/2 可配 | Spi_* + Ipw 私有扩展 | Spi_* 标准 API 15 个 |
| Job/Sequence | 支持，Job 绑定 QSPI 实例与 CS | 支持，Sequence 可配优先级 | 支持，Job 绑定 HW Unit，Sequence 4 Job |
| DMA | QSPI 内置 DMA 通道，配置里选 | EDMA 通道，配置里选 | HDMA/DMAMUX，通道与源在 PBcfg |
| 8/16/32 bit | 支持（DataWidth 配置） | 支持 | 支持，实测三档 PASS |
| 片内主从对测 | 不常见（QSPI 一般对外） | 不常见 | 支持，spi_master_slave 用 SPI1↔SPI3 实测 |
| 中断异步 | 标准 END 中断 | LPSPI 中断 | 保留 END 中断入口，未通过实测（§46.13） |

### 46.11.3 I2C 新旧 API 差异

R25-11 的 I2C 驱动与 R4.x 的区别见 §46.4.2 的表 46-2。要点：新 API 强制 SetupEB + Sequence 模型，
把"一次读写"拆成 Job，目标是让 I2C 像 SPI/Can 一样纳入统一调度；代价是简单场景代码量明显增加
（本实现 4 个 Job 配置 + SetupEB 两次调用才能完成一次读写）。第 50 章的商用 MCAL 对比表会引用这一差异。

## 46.12 参考来源

- AUTOSAR CP SWS SPI Handler/Driver R25-11：<https://www.autosar.org/fileadmin/standards/R25-11/CP/AUTOSAR_CP_SWS_SPIHandlerDriver.pdf>；
  本地抽取文本 autosar_txt/AUTOSAR_CP_SWS_SPIHandlerDriver.txt（§8.3 函数定义、§7.4 错误分类）
- AUTOSAR CP SWS I2C Driver R25-11：<https://www.autosar.org/fileadmin/standards/R25-11/CP/AUTOSAR_CP_SWS_I2CDriver.pdf>；
  本地抽取文本（§8.2 数据类型、§8.3 函数定义、§7.3 错误分类）
- AUTOSAR CP SWS ADC Driver R25-11：<https://www.autosar.org/fileadmin/standards/R25-11/CP/AUTOSAR_CP_SWS_ADCDriver.pdf>
- AUTOSAR SWS BSW General §11.2 模块 ID 表：<https://www.autosar.org/fileadmin/standards/R25-11/CP/AUTOSAR_CP_SWS_BSWGeneral.pdf>
- HPM SDK v1.13.0 源码：<https://github.com/hpmicro/hpm_sdk>；本机 $HOME/sdk_env_v1.13.0/hpm_sdk，
  SPI 驱动 hpm_sdk/drivers/src/hpm_spi_drv.c、I2C 驱动 hpm_sdk/drivers/src/hpm_i2c_drv.c
- 先楫半导体 HPM5E31 产品页：<https://www.hpmicro.com/>
- Infineon AURIX TC4x MCAL 文档：<https://www.infineon.com/cms/en/product/microcontroller/32-bit-tricore-arm-microcontroller/aurix-tc4x/>
- NXP S32K3 实时驱动 RTD：<https://www.nxp.com/design/software/development-software/s32k3-standard-software>
- 本仓库实测日志：logs/20261102-142330-spi_master_slave.log、logs/20261102-142516-spi_async_job.log、
  logs/20261102-142612-i2c_master_slave.log、logs/20261102-142544-i2c_error.log、
  logs/20261102-143752-adc_single.log、logs/20261102-143824-adc_bench.log（ADC 由 mcal-core 实测）
- 本仓库寄存器级基线：samples/spi/spi1_spi3_master_slave、samples/i2c/i2c2_i2c3_master_slave、samples/adc/adc_basic_poll

## 46.13 待确认项与风险

| 项 | 状态 | 说明与复跑命令 |
|---|---|---|
| SPI 异步中断模式（DMA + END 中断） | 已知限制（原因：END 中断收尾与 DMA TC 的先后顺序未对齐，当前 seqResult 停在 PENDING） | `bash scripts/hwrun.sh mcal/spi_async_job --secs 20 --send 'spi_job hint 64'`；异步轮询模式可用，中断模式留待下一轮 |
| I2C 目标模式 400 kHz 读（字节被采样成 0x00） | **已实测（2026-11-02 修复并复测）**：根因是目标通道 fast 时序 `SETUP.T_SUDAT=0`（数据建立时间 100 ns，恰为下限、无余量），控制器读回被采样成 0x00；驱动 `i2c_target_timing_fixup()` 把它抬到标准模式 250 ns（12）后，100k/400k/1M 各 1000 轮 0 误码（表 46-9），256 B 读回也 0 错。控制变量实验见表 46-10 | `bash scripts/hwrun.sh mcal/i2c_master_slave --secs 30 --lock-wait 1800 --send "i2c ber 1000;i2c init 400k;i2c ber 1000;i2c init 1m;i2c ber 1000"` |
| ADC 单次组/连续组吞吐与噪声 | 已实测（单次 491.8~506.4 kS/s、连续 740.1~740.4 kS/s、sd 60.29~62.66 LSB、ENOB 8.43~8.48 bit，日志 logs/20261102-143752-adc_single.log、logs/20261102-143824-adc_bench.log） | `bash scripts/hwrun.sh mcal/adc_single --secs 20 --send 'adc stat 512'` |
| ADC 线性度/绝对精度 | 已知限制（原因：板上无基准源，PF26 悬空读数约 35405 LSB/1.78 V 是板级偏置，无法算 INL/DNL；需外接 0~VREFH 基准分压） | `bash scripts/hwrun.sh mcal/adc_single --secs 20 --send 'adc single'` |
| ADC 硬件自主连续转换 | 已知限制（原因：当前连续组是"一次 ReadGroup = 一轮"的软件泵，740 kHz 即软件上限；实现路径：ADC16 序列模式 + DMA 或 PRD 周期模式 + 完成中断） | `bash scripts/hwrun.sh mcal/adc_bench --secs 20 --send 'adc bench 20000'` |
| SPI 从机多发 1 字节补偿 | 已实测（ExtraTxElements=1，去掉后从机数据整体错位 1 字节；现象与修法见 §46.10 第 4 条） | `bash scripts/hwrun.sh mcal/spi_master_slave --secs 15 --send 'spi xfer 64'` |
| 10 MHz 长杜邦线稳定性 | 已实测（1 MB 零误码；室温、20 cm 杜邦线，未做温度/电压边界扫描） | `bash scripts/hwrun.sh mcal/spi_master_slave --secs 40 --send 'spi init 10m;spi ber 1 1024'` |
| 串口抓包丢字节 | 已定位（CH343/USB CDC 抓包整段丢字节，与固件无关；用日志前后文与心跳对齐） | `bash scripts/hwrun.sh mcal/spi_master_slave --secs 6 --send 'spi info'` |
