# 第 27 章 参考设计、同类对比与扩展阅读

## 27.1 文档信息

| 项 | 内容 |
|---|---|
|   文档版本   | v1.2.0（2026-11-02，R5 复核） |
| 适用芯片/板卡 | HPM5E31IPB1（HPM5E00 系列）/ hpm5e31_1 |
| 对应例程 | 全仓库 `samples/`（本章按"参考设计入口"索引，不新增例程） |
| 实测日期 | 2026-10-30 |
| 可信度 | 中：厂商规格来自官方产品页/数据手册（链接见第 11 节）；"待确认"单元格表示未在官方资料中核实，禁止直接用于选型决策 |

## 27.2 本章导览

- **学到什么**：HPM5E00 在"高性能实时控制 MCU"赛道中的位置；与 HPM6E00、STM32H743、GD32H759、TC397 等同类芯片的差异；官方 EVK/参考设计/工具/开源项目从哪找；按应用场景怎么选。
- **前置章节**：第 01 章（芯片与板卡）、第 02 章（工具链）、第 23/24 章（性能实测）。
- **阅读建议**：先看第 9 节"选型建议"表，再回到第 8 节看对应维度的数据来源；本章所有"实测"数字都来自本仓库 `logs/`，可复核。

## 27.3 概述与典型应用

先楫官网对 HPM5E00 的定位是"**新一代 EtherCAT 运动控制 MCU**"（[HPM5E00 产品页](https://www.hpmicro.com/product-center/microcontroller/hpm5e00)）。这句话包含三个关键词：

1. **EtherCAT**：片内集成 EtherCAT 从站控制器（ESC），最多 3 个端口，不需要外挂 ET1100/ET1200 之类 ESC 芯片；
2. **运动控制**：2 个 8 通道增强型 PWM（调制精度 100 ps）、增量式/磁编码器接口、脉冲式位置输出、可编程逻辑单元 PLB；
3. **MCU 性价比**：单核 RISC-V 480 MHz，1 MB 片内 Flash + 256 KB ILM/DLM + 256 KB SRAM，BGA196/BGA121 等封装（[HPM5E00 产品页](https://www.hpmicro.com/product-center/microcontroller/hpm5e00)）。

**表 27-1 HPM5E00 系列关键规格（官方产品页 + DS Rev0.4）**

| 维度 | 规格 | 来源 |
|---|---|---|
| 内核 | RISC-V（RV32-IMAFDCBP），双精度浮点 + DSP 扩展，480 MHz | 产品页；实测 `clk` 480 MHz |
| 性能 | 2712 CoreMark / 1368 DMIPS（官方标称） | 产品页 |
| 缓存/存储 | 32 KB I/D Cache；256 KB ILM/DLM；256 KB 通用 SRAM；1 MB 片内 Flash | 产品页；DS §4.2 (p.68) |
| 实时以太网 | EtherCAT 从站（≤3 端口）；1 Gbps 以太网 MAC；2 个内置 100 Mbps PHY | 产品页 |
| 通信 | 最多 4 路 CAN/CAN-FD；USB HS（内置 PHY）；UART/SPI/I2C | 产品页 |
| 运动控制 | 2×8 通道 PWM，100 ps 精度；编码器/磁编码器接口；PLB | 产品页 |
| 模拟 | 2×16 位 ADC（2 MSPS，12 位模式 4 MSPS，16 通道）；2×模拟比较器；SDM | 产品页 |
| 定时器 | 最多 20 路 32 位定时器；3 个看门狗 | 产品页 |
| 工作条件 | 环境温度 -40~105 °C；结温 -40~125 °C | DS §4.1 表10 (p.68) |
| 封装 | BGA196 / BGA121 / ELQFP100 等（按型号） | DS §2.4 (p.18) |

**典型应用**（本仓库已验证或部分验证）：

- **EtherCAT 从站/伺服驱动**：HPM5E00 的 ESC + PWM + 编码器组合就是为伺服/变频器设计的；本仓库第 12/13 章的千兆 PHY + lwIP 也可作普通工业以太网口。
- **CAN FD 网关/节点**：4 路 CAN FD + 4 路 CAN 收发器（本板），第 8/9 章有 500 kbps~2 Mbps 实测。
- **10BASE-T1S 末端节点**：通过 SPI 外接 LAN8651（第 10/11 章），适合车载/工业"最后一公里"总线。
- **高精度数据采集**：2×16 位 ADC + SDM（第 18 章），2 MSPS 采样对电机电流环足够。
- **教学/评估板**：本仓库 `hpm5e31_1` 最小系统板（第 01 章），成本低、接口全。

## 27.4 工作原理：怎么做一次靠谱的选型对比

选型不是"跑分排行榜"，而是"**把应用约束映射到芯片能力，再验证证据链**"。本章采用的方法见图 27-1 与下面四步：

1. **列硬约束**：核架构（RISC-V/ARM/TriCore）、主频、Flash/SRAM、工作温度、封装、功能安全等级、是否需要 EtherCAT/TSN。
2. **列软约束**：工具链成熟度（IDE/调试/量产烧录）、中间件（lwIP/USB/RTOS）、社区资料、长期供货。
3. **找一手证据**：厂商数据手册/用户手册/勘误表 + 官方产品页；**不采信二手文章里的数字**。
4. **上板验证**：至少跑通"时钟 + 串口 + 目标外设 + 性能基准"四件事，再回填对比表。

![图 27-1 HPM5E00 生态与软件栈全景（SDK/中间件/官方工具/参考设计/本仓库关系）](figures/27/fig-27-1-ecosystem.png)

**图 27-1** 说明 HPM5E00 的软件栈是"一份 SDK 通吃全系列"：`drivers/` 提供外设驱动，`soc/HPM5E00/` 提供寄存器头文件与启动代码，`middleware/` 提供 lwIP/CherryUSB/FreeRTOS 等中间件，官方还有 Pinmux Tool、HPM Monitor Studio、Manufacturing Tool/OBox、ADC Evaluation Studio 等配套工具。本仓库的 `samples/` 与 `scripts/` 是在这套生态上做"可复现实测"的胶水层。

## 27.5 外设/寄存器级能力对照

选型表里最容易被含糊处理的是"外设能力"：同样写"4 路 CAN FD"，消息 RAM 大小、是否支持 TDC、时间戳精度差别很大。表 27-2 把与寄存器/外设配置直接相关的能力列出来，便于对照 RM 的寄存器章节。

**表 27-2 关键外设/寄存器级能力对照（HPM5E00 vs 同类）**

| 能力 | HPM5E31 | HPM6E00 | STM32H743 | GD32H759 | 依据 |
|---|---|---|---|---|---|
| 内核/主频 | RISC-V 480 MHz | RISC-V 双核 >600 MHz | Cortex-M7 480 MHz | Cortex-M7 600 MHz | 各厂商产品页 |
| PWM 通道/精度 | 2×8 通道，100 ps | 4×8 通道，100 ps | 待确认（HRTIM 精度以 datasheet 为准） | 待确认 | 产品页 |
| ADC | 2×16 位 2 MSPS（12 位 4 MSPS），16 通道 | 待确认 | 16 位 ADC（数量/速率待确认） | 待确认 | 产品页 |
| CAN FD | 最多 4 路 | 最多 8 路 | 待确认 | 3 路 | 产品页 |
| EtherCAT 从站 | 支持（≤3 端口） | 支持（3+1 端口 GbE 交换机，支持 TSN） | 不支持（需外置 ESC） | 不支持（需外置 ESC） | 产品页 |
| 千兆以太网 | 1 Gbps MAC + 2×100 Mbps PHY | 千兆以太网控制器 + TSN 交换机 | 10/100 MAC（待确认） | 以太网 MAC（速率待确认） | 产品页 |
| 编码器/旋变 | 增量式/磁编码器/脉冲输出 | 增量式/绝对值/旋变/磁编码器 | 待确认 | 待确认 | 产品页 |
| 功能安全 | 待确认 | 待确认 | 待确认 | 待确认 | 均需查功能安全手册 |

> 说明：表中"待确认"不是"没有"，而是本章没有在官方资料中找到可直接引用的数字。**做选型决策前必须查对应型号的数据手册/用户手册**，不要用二手资料填空。

## 27.6 例程与参考设计入口

本章不新增例程，而是把"去哪找能跑的参考设计"整理成表。官方与本仓库的例程入口见表 27-3、表 27-4。

**表 27-3 官方参考设计、EVK 与工具**

| 资源 | 内容 | 入口 |
|---|---|---|
| HPM5E00 EVK | HPM5E00 系列官方评估板（UG 在 SDK `doc/` 下：`HPM5E00EVK_UG_V1.1.pdf`） | SDK 文档目录 |
| HPM6E00 EVK | 双核 600 MHz + EtherCAT/TSN 旗舰评估板 | SDK 文档目录 |
| HPM_APPS | 官方应用方案包：EtherCAT 伺服、CAN 网关、ADC EVK、机器人关节等 | [hpm_apps](https://github.com/hpmicro/hpm_apps) |
| HPM SDK | 驱动 + SoC 头文件 + 外设例程（BSD-3-Clause） | [hpm_sdk](https://github.com/hpmicro/hpm_sdk) |
| Pinmux Tool | 引脚复用配置，生成可直接加入 SDK 的 C 代码 | 官网"设计资源" |
| HPM Monitor Studio | 在线调试/寄存器查看/变量监视 | 官网"设计资源" |
| Manufacturing Tool / OBox | 量产烧录、OTP/镜像管理 | 官网"设计资源" |
| ADC Evaluation Studio | 高精度 ADC 评估（配合 ADC EVK + 信号源） | 官网"设计资源" |

**表 27-4 本仓库可直接上板的例程入口**（详见各章"例程逐条讲解"）

| 主题 | 例程目录 | 验证要点 |
|---|---|---|
| 调试框架 | `samples/shell/letter_shell_basic/` | UART0 921600、`ver/clk/reg/mem` 命令 |
| GPIO | `samples/gpio/blinky_btn/` | LED/按键中断 |
| CAN FD | `samples/can/mcan_*/` | 回环/双节点/吞吐 |
| 10BASE-T1S | `samples/lan8651/*/` | SPI 探测 + PLCA |
| 千兆以太网 | `samples/enet/*/` | PHY 配置 + lwIP + iperf |
| PWM/定时器 | `samples/pwm/*`、`samples/gptmr/*` | 频率/占空比/捕获 |
| ADC | `samples/adc/*` | 采样率/精度 |

上板跑一个例程的最短路径（本仓库 `scripts/hwrun.sh` 已封装编译+烧写+抓串口）：

```bash
cd hpm5e31_1
source scripts/env.sh
bash scripts/build.sh shell/letter_shell_basic          # 只编译
bash scripts/hwrun.sh shell/letter_shell_basic \        # 编译+烧写+抓串口
     --secs 8 --send "ver;clk"
ls -t logs/*letter_shell_basic.log | head -1            # 实测日志
```

## 27.7 上板实测数据

选型对比里的"实测"必须能复现。表 27-5 是本仓库在 `hpm5e31_1` 上跑出的关键数字（全部可在 `logs/` 复核）。

**表 27-5 本仓库实测性能（HPM5E31IPB1 @480 MHz，GCC 11.1 -O3）**

| 指标 | 实测值 | 测试条件/日志 |
|---|---|---|
| CoreMark | 1898.1 | `-O3`、栈在 DLM；`logs/20261030-110654-bench_coremark.log` |
| 千兆网 TCP 吞吐 | 934.73 Mbit/s | YT8011AN 1000BASE-T1 + lwIP + iperf2，10 s 传输；`logs/20261030-113501-enet_lwip_iperf.log` |
| CAN FD 帧率 | 4305 frame/s | classic 500 kbps、8 B 帧、双节点；`logs/20261030-112832-mcan_two_node.log` |
| 内存拷贝 | 395.6 MB/s | DMA 2D 拷贝 256 B burst；`logs/20261030-110400-dma_mem2mem.log` |
| UART 吞吐 | 307 KB/s | 3 Mbps 波特率 + HDMA；`logs/20261030-112250-uart_dma.log` |
| 时钟一致性 | diff_ppm=0.0 | mcycle vs MCHTMR 15 s 对比；`logs/20261030-110853-bench_coremark.log` |

两条对选型重要的结论：

1. **实测 CoreMark（1898）明显低于官方标称（2712，降幅约 30%）**。原因待确认：官方分数可能用不同编译器版本/优化选项/代码与栈放置策略（本仓库用 GCC 11.1 `-O3`、栈放 DLM）。这说明"官方跑分"与"你手上的工具链"存在差距，选型时应要求厂商提供**同工具链**的跑分或用评估板自测（见第 6 节）。
2. **千兆以太网实测 934.73 Mbit/s 已接近线速**，说明 HPM5E31 的 1 Gbps MAC + 外部 YT8011AN PHY 方案在 TCP 层可用；对工业相机/网关类应用足够。

## 27.8 性能与调优

**表 27-6 官方标称与实测差异、可能原因与优化方向**

| 指标 | 官方/标称 | 本仓库实测 | 差异原因（待确认项） | 优化方向 |
|---|---|---|---|---|
| CoreMark | 2712 | 1898.1 | 编译器版本/优化等级、代码放 Flash vs ILM、栈位置、库实现 | 热点代码放 ILM、开 I/D Cache、用 `-O3`/LTO；对比不同放置策略 |
| 千兆 TCP | 线速 1000 Mbit/s | 934.73 Mbit/s | 协议栈开销、iperf 参数、CPU 参与拷贝 | 调大 TCP 窗口、零拷贝/分散聚集、DMA 卸载 |
| CAN FD | 理论帧率随位时序 | 4305 frame/s（classic 500k，8B） | 帧类型/负载/终端电阻/驱动开销 | 提高波特率到 2 Mbps、减少中断次数、批量发送 |
| 内存带宽 | AHB 200 MHz | 395.6 MB/s（DMA 256B burst） | burst 长度、总线竞争、cache 策略 | 增大 burst、避免与 CPU 抢总线、用 DMA2D |

> 说明：第 23/24 章会给完整基准方法；本章只给"影响选型判断"的结论。若要把 CoreMark 差异归因，需要固定工具链后做"ILM vs Flash""-O2 vs -O3"的对照实验（列入第 12 节待确认）。

## 27.9 坑与注意事项

以下是"生态/工具/对比"层面真实踩到的坑（区别于第 26 章的寄存器/勘误坑）：

1. **现象**：拿官方 CoreMark 数字和自测对比，差了 30%。
   **原因**：官方跑分通常用最优编译器/优化选项/代码放置；本仓库用 GCC 11.1 `-O3`、栈在 DLM。
   **解决**：对比性能必须固定"工具链版本 + 优化选项 + 代码放置 + 基准版本"四要素；否则只能同条件相对比较。

2. **现象**：照抄 STM32 的工程结构，HPM 编译不过。
   **原因**：HPM SDK 用 CMake + 板级 `BOARD` 变量（`boards/hpm5e31_1/`），不是 STM32CubeMX 的 `.ioc` 工程；启动文件/链接脚本由 SDK 按 `flash_xip` 等构建类型生成。
   **解决**：按第 02 章"仓库与板级支持包"理解 SDK 目录结构；新增外设优先复制官方例程的 CMakeLists。

3. **现象**：以为"RISC-V 工具链随便装一个就行"，结果链接报 multilib 错误。
   **原因**：HPM SDK 需要 `riscv32-unknown-elf` + multilib（RV32-IMAFDCBP，含 B 扩展），普通发行版工具链常缺 multilib。
   **解决**：用官方推荐的 GCC 11.1 multilib 工具链（本仓库路径 `$HOME/toolchains/riscv32-unknown-elf-newlib-multilib`）。

4. **现象**：找不到"像 CubeMX 那样的图形化配置工具"。
   **原因**：先楫的生态是"SDK + Pinmux Tool + Monitor Studio"组合，不是单一 IDE 全家桶。
   **解决**：引脚复用用 Pinmux Tool 生成 C 代码；时钟/外设初始化参考 SDK 例程；调试用 J-Link + VS Code/命令行。

5. **现象**：选型表里把"EtherCAT"当成所有 HPM5E00 型号标配。
   **原因**：HPM5E00 系列不同型号外设集合有差异（HPM5E31/HPM5E3Y 等）。
   **解决**：以具体型号的 `hpm_soc_ip.h`/`hpm_soc_ip_feature.h` 和产品页为准；本板 HPM5E31IPB1 的 IRQ 表里有 ESC（IRQ 61~64），可作佐证。

6. **现象**：把 HPM5E00 与 HPM6E00 混为一谈。
   **原因**：HPM6E00 是双核 >600 MHz + 3+1 端口 GbE/TSN 交换机 + 8 路 CAN FD 的旗舰；HPM5E00 是单核 480 MHz 的性价比型号。
   **解决**：先按"算力/端口数/是否需要 TSN"分流，再看价格与封装（见第 9 节）。

7. **现象**：以为 10BASE-T1S 是 HPM5E31 内置。
   **原因**：HPM5E31 内置的是 2 个 100 Mbps 以太网 PHY 和 1 Gbps MAC；10BASE-T1S 需要外接 LAN8651（本板 SPI2）。
   **解决**：把 LAN8651 当作独立外设设计（第 10/11 章），注意 RST/INT 引脚与 SPI 时序。

8. **现象**：二手文章/群里说"HPM5E31 是 2 路 CAN"，与手册矛盾。
   **原因**：不同封装的 HPM5E00 型号 CAN 路数不同；官方产品页写"最多 4 路 CAN/CAN-FD"。
   **解决**：以具体型号 datasheet 的引脚表为准（本板 4 路，见第 08 章）。

9. **现象**：用 OpenOCD 调试 HPM5E31 失败。
   **原因**：本板调试链路是 J-Link（JTAG），本仓库 `scripts/flash.sh` 用 JLinkExe 命令行；OpenOCD 需要额外的 flash 算法/配置支持。
   **解决**：照第 02 章用 J-Link；需要 OpenOCD 时先确认官方是否有对应 target 配置。

10. **现象**：评测板跑分正常，自己板子跑分低。
    **原因**：Flash 等待周期/引脚复用/电源/时钟配置不同（本板 `xpi0 100 MHz`、`ahb0 200 MHz`）。
    **解决**：先用 `clk` 命令核对时钟树，再对比代码放置与 cache 配置；不要直接跨板比跑分。

## 27.10 同类 MCU 对比

**表 27-7 同类高性能 MCU 横向对比（数据来自各厂商官方产品页/数据手册，未核实项标"待确认"）**

| 维度 | HPM5E31 | HPM6E00 | STM32H743 | GD32H759 | Infineon TC397 |
|---|---|---|---|---|---|
| 厂商/系列 | 先楫 HPM5E00 | 先楫 HPM6E00 | ST STM32H7 | 兆易 GD32H7 | 英飞凌 AURIX TC3xx |
| 内核 | RISC-V 单核 480 MHz | RISC-V 双核 >600 MHz | Cortex-M7 480 MHz | Cortex-M7 600 MHz | TriCore 多核（核数/主频待确认） |
| 性能（官方） | 2712 CoreMark / 1368 DMIPS | >6780 CoreMark / 3420 DMIPS | 待确认 | 待确认 | 待确认 |
| 片内 Flash | 1 MB | 待确认 | ≤2 MB | ≤3.8 MB | 待确认 |
| 片内 SRAM | 256 KB ILM/DLM + 256 KB | 待确认 | ≤1 MB | 待确认 | 待确认 |
| EtherCAT | 从站 ≤3 端口 | 从站 + 3+1 端口 GbE 交换机（TSN） | 需外置 ESC | 需外置 ESC | 待确认 |
| 以太网 | 1 Gbps MAC + 2×100 Mbps PHY | 千兆 + TSN 交换机 | 10/100 MAC（待确认） | 以太网（速率待确认） | 待确认 |
| CAN FD | ≤4 路 | ≤8 路 | 待确认 | 3 路 | 待确认 |
| USB | HS（内置 PHY） | HS（内置 PHY） | HS/FS（按型号） | 待确认 | 待确认 |
| 模拟 | 2×16 位 2 MSPS ADC + 2 ACMP + SDM | 待确认 | 16 位 ADC（待确认） | 待确认 | 待确认 |
| 电机控制 | 2×8ch PWM 100 ps + 编码器/PLB | 4×8ch PWM + 旋变 + 硬件环路 | HRTIM（待确认） | 待确认 | 待确认 |
| 工作温度 | -40~105 °C（TA） | 待确认 | 待确认 | 待确认 | 车规（待确认等级） |
| 生态 | HPM SDK + 官方工具（中英文资料） | 同左 + EtherCAT SDK | CubeMX/CubeIDE，资料最丰富 | 国产工具链，资料中等 | 车规工具链（贵），ASIL-D 生态 |
| 适合场景 | 性价比 EtherCAT 从站/伺服/CAN 网关 | 高端伺服/多轴/TSN | 通用高性能，生态成熟 | 显示/HMI + 控制 | 汽车/功能安全 |

> 表 27-7 的"待确认"是刻意保留的：网上常见的"参数对比表"经常把不同型号/不同封装的数字混在一起。**选型时请以具体订货型号的数据手册为准**，并用评估板实测。

![图 27-2 同类 MCU 关键能力矩阵（绿色=支持，黄色=部分/需外置，红色=不支持，灰色=待确认）](figures/27/fig-27-2-feature-matrix.png)

**图 27-2** 把表 27-7 压缩成"能力矩阵"：HPM5E31 的差异化优势集中在 **RISC-V 生态 + 内置 EtherCAT 从站 + 高精度 PWM**，而 STM32H743/GD32H759 的优势是 ARM 生态与显示/通用外设，TC397 则面向车规与功能安全。注意灰色格子——它们代表"本章未核实"，不是"不支持"。

## 27.11 选型建议

**表 27-8 按应用场景的选型建议**

| 应用场景 | 首选 | 理由 | 备选/备注 |
|---|---|---|---|
| EtherCAT 从站（伺服/变频器/I/O） | HPM5E00（HPM5E31） | 内置 ESC（≤3 端口）+ 100 ps PWM + 编码器接口，BOM 省一颗 ESC | 多轴/高端选 HPM6E00（双核 + TSN） |
| 多轴伺服/机器人关节 | HPM6E00 | 双核 >600 MHz、4×8ch PWM、硬件运动控制环路、旋变接口 | 成本敏感时用 HPM5E00 + 外部逻辑 |
| CAN FD 网关/车载节点 | HPM5E00 | 4 路 CAN FD、USB HS、低功耗模式；本仓库有双节点实测 | HPM6E00 有 8 路 CAN FD，适合多总线网关 |
| 10BASE-T1S 末端节点 | HPM5E00 + LAN8651 | 通过 SPI 扩 10BASE-T1S，成本低于"内置 T1S"方案 | 见第 10/11 章 |
| 工业以太网 + 运动控制二合一 | HPM6E00 | 千兆 + TSN + EtherCAT + 多轴 PWM | HPM5E00 的 1G MAC 也可做普通以太网 |
| 高精度数据采集 | HPM5E00 | 2×16 位 2 MSPS ADC + SDM + PLB | 需要更高精度时看官方 ADC EVK 与应用笔记 |
| 显示/HMI 为主 | GD32H759 或 HPM6800 | GD32H759 带 TFT-LCD 加速；HPM6800 面向数字仪表 | 本板无显示接口，不适合 |
| 汽车功能安全（ASIL） | TC397 等车规芯片 | 车规认证/功能安全生态成熟 | HPM5E00 的功能安全等级待确认 |
| 教学/快速验证 | HPM5E00 + 本仓库 | 开源 SDK + 本仓库例程/书稿/一条龙脚本 | 成本低、资料可复现 |

**结论**：如果项目核心需求是"**EtherCAT 从站 + 电机控制 + 成本**"，HPM5E00 是目前国产芯片里少有的"内置 ESC + 高精度 PWM"组合；如果还需要 TSN/多轴/更强算力，向上选 HPM6E00；如果团队强依赖 ARM 生态与图形化工具链，STM32H7/GD32H7 更稳；车规功能安全场景不要用消费/工业级芯片硬扛。

## 27.12 扩展阅读

**表 27-9 开源项目与社区资源**

| 资源 | 说明 | 链接 |
|---|---|---|
| HPM SDK | 先楫官方 SDK（BSD-3-Clause），驱动/SoC/例程 | [github.com/hpmicro/hpm_sdk](https://github.com/hpmicro/hpm_sdk) |
| HPM_APPS | 官方应用方案：EtherCAT 伺服、CAN 网关、ADC EVK 等 | [github.com/hpmicro/hpm_apps](https://github.com/hpmicro/hpm_apps) |
| letter-shell | 本仓库调试框架的上游（MIT） | [github.com/NevermindZZT/letter-shell](https://github.com/NevermindZZT/letter-shell) |
| lwIP | 轻量 TCP/IP 协议栈（本仓库千兆网实测使用） | [savannah.nongnu.org/projects/lwip](https://savannah.nongnu.org/projects/lwip/) |
| CherryUSB | 官方 SDK 集成的 USB 协议栈 | [github.com/sakumisu/CherryUSB](https://github.com/sakumisu/CherryUSB) |
| FreeRTOS | 官方 SDK 支持的 RTOS | [freertos.org](https://www.freertos.org/) |
| 先楫官网 | DS/RM/勘误/工具/参考设计下载入口 | [hpmicro.com](https://www.hpmicro.com/) |
| 先楫文档站 | 在线文档与产品资料 | [doc.hpmicro.com](https://doc.hpmicro.com/) |

**延伸阅读顺序建议**：

1. 先读本仓库第 1/2 章，把板子跑起来；
2. 再读第 8/12/14 章，理解 CAN FD/以太网/PWM 三条主线；
3. 然后读第 23/24 章，建立性能直觉；
4. 最后读第 25/26 章掌握可靠性与勘误规避，遇到具体引脚/接线问题查第 51 章附录。


### 27.12.1 外部资料索引（ref / ref2 下载物的上游链接）

写这本书时，我们从 GitHub 和厂商官网下载了大量参考实现与文档（放在开发机的 `ref/`、`ref2/` 目录，
这些目录**不随本仓库分发**，按下表的上游链接即可自行获取）。
为了让读者不用依赖我们的本地目录，表 27-10 把每个下载物对应的**上游仓库/官网**列出来；
表 27-11 汇总协议标准与芯片手册的官方入口。版本号是下载时的 HEAD 或文档日期。

**表 27-10 参考实现与工具的上游仓库**

| 本地目录（ref2/） | 项目 | 上游链接 | 用途（书中章节） |
|---|---|---|---|
| `CANopenNode` | CANopenNode（CiA 301 开源栈） | <https://github.com/CANopenNode/CANopenNode> | 第 33 章板端从站/主站 |
| `CANopenDemo` | CANopenNode 官方演示 | <https://github.com/CANopenNode/CANopenDemo> | 第 33 章对象字典/例程对照 |
| `CANopenLinux` | Linux 侧 CANopen 主站 | <https://github.com/CANopenNode/CANopenLinux> | 第 33 章上位机主站 |
| `cannelloni` | CAN over IP 隧道（UDP/TCP） | <https://github.com/mguentner/cannelloni> | 第 29/33/38 章网关 |
| `cannelloni_ports` | cannelloni 移植补丁集 | <https://github.com/mguentner/cannelloni_ports> | 第 28 章 |
| `can-utils` | Linux CAN 工具集（candump/cansend/cangen/canplayer） | <https://github.com/linux-can/can-utils> | 第 9/29/38/40 章 |
| `can-doc` | Linux CAN 文档（内核文档） | <https://github.com/linux-can/can-doc> | 第 8/9 章 SocketCAN |
| `gs_usb_ref` | Linux 内核 gs_usb 驱动（7.0.0） | <https://github.com/torvalds/linux/blob/master/drivers/net/can/usb/gs_usb.c> | 第 41 章协议权威 |
| `candleLight_fw_hpm_port` | candleLight 固件的 HPM 移植 | <https://github.com/zhangjiance/candleLight_fw_hpm_port> | 第 41 章行为对照（GPL，只读参考） |
| `roboparty_EtherCANFD` | HPM5E31 4 路 CAN FD 适配器 | <https://github.com/Roboparty/roboparty_EtherCANFD> | 第 41 章同 SoC 参考（GPL，只读参考） |
| `vsomeip` | COVESA vsomeip（SOME/IP 实现） | <https://github.com/COVESA/vsomeip> | 第 35 章互操作 |
| `capicxx-core-tools` | AUTOSAR C++ 核心工具 | <https://github.com/COVESA/capicxx-core-tools> | 第 35 章代码生成对照 |
| `capicxx-someip-runtime` | AUTOSAR SOME/IP 运行时 | <https://github.com/COVESA/capicxx-someip-runtime> | 第 35 章 |
| `Micro-XRCE-DDS` / `-Client` / `-Agent` | eProsima Micro XRCE-DDS 三件套 | <https://github.com/eProsima/Micro-XRCE-DDS>、<https://github.com/eProsima/Micro-XRCE-DDS-Client>、<https://github.com/eProsima/Micro-XRCE-DDS-Agent> | 第 36 章 ROS 2 桥 |
| `Fast-DDS` / `Fast-DDS-Gen` | eProsima Fast DDS 与代码生成 | <https://github.com/eProsima/Fast-DDS>、<https://github.com/eProsima/Fast-DDS-Gen> | 第 36 章主机侧对照 |
| `DDS-Monitor` | DDS 主题监视工具 | <https://github.com/eProsima/DDS-Monitor> | 第 36 章调试 |
| `XCPlite` | Vector XCPlite（XCP on Ethernet 参考） | <https://github.com/vectorgrp/XCPlite> | 第 34 章 |
| `a2lfile` / `a2ltool` | A2L 解析/编辑（Rust） | <https://github.com/DanielT/a2lfile>、<https://github.com/DanielT/a2ltool> | 第 34 章 A2L |
| `autosar-data` / `autosar-data-py` | AUTOSAR ARXML 数据模型 | <https://github.com/DanielT/autosar-data>、<https://github.com/DanielT/autosar-data-py> | 第 33/34 章 ARXML |
| `dlt-daemon` / `dlt-viewer` | COVESA DLT 日志守护与查看器 | <https://github.com/COVESA/dlt-daemon>、<https://github.com/COVESA/dlt-viewer> | 第 39 章 |
| `vehicle_signal_specification` | COVESA VSS 车辆信号规范 | <https://github.com/COVESA/vehicle_signal_specification> | 第 39 章 VSS |
| `flexPTP` | 轻量 PTP 协议栈 | <https://github.com/epagris/flexPTP> | 第 38 章主机侧 PTP |
| `EcuBus-Pro` | 开源 ECU 诊断/仿真工具 | <https://github.com/ecubus/EcuBus-Pro> | 第 30 章 UDS 对照 |
| `web-can-utils` | 浏览器端 CAN 工具 | <https://github.com/zhangjiance/web-can-utils> | 第 39 章 |
| `hpm_dfu_boot` | HPM DFU 引导参考 | <https://github.com/zhangjiance/hpm_dfu_boot> | 第 20/21 章 |
| `stm32h723_tja1103_ptp` | STM32H723 + TJA1103 PTP 参考 | 内网工程（未公开），思路见第 38 章 | 第 38 章 |
| `SlcanX`（ref/） | HPM5321 4 路 CAN FD 分析仪 | <https://github.com/weifengdq/SlcanX> | 第 8/39 章同类实现 |
| `letter-shell`（ref/） | letter-shell 命令行 | <https://github.com/NevermindZZT/letter-shell> | 第 03 章 |

**表 27-11 芯片手册、协议标准与官方文档**

| 资料 | 官方入口 | 说明 |
|---|---|---|
| HPM5E00 数据手册/用户手册/勘误 | <https://www.hpmicro.com/>、<https://doc.hpmicro.com/> | 本地方案 Rev0.4/Rev0.4/V0.1；设计前核对官网最新版 |
| HPM SDK（含文档） | <https://github.com/hpmicro/hpm_sdk>、<https://hpm-sdk.readthedocs.io/zh-cn/latest/> | v1.13.0 |
| HPM 官方工具链 | <https://github.com/hpmicro/riscv-gnu-toolchain> | Linux 版 GCC 11.1.0 |
| LAN8650/1 10BASE-T1S | <https://www.microchip.com/en-us/product/LAN8651> | 数据手册、应用笔记、Errata 均在此页 |
| YT8011AN 千兆 PHY | <https://www.motor-comm.com/> | 裕太微官网；完整数据手册需 NDA |
| TCAN1044A-Q1 CAN FD 收发器 | <https://www.ti.com/product/TCAN1044A-Q1> | 5 Mbit/s 上限（第 41 章 8M 限制依据） |
| CAN 规范（ISO 11898-1/2） | <https://www.iso.org/standard/63648.html> | 经典 CAN 与 CAN FD |
| ISO-TP（ISO 15765-2） | <https://www.iso.org/standard/66574.html> | 第 29 章 |
| UDS（ISO 14229-1） | <https://www.iso.org/standard/72439.html> | 第 30 章 |
| SAE J1939 | <https://www.sae.org/standards/content/j1939_202208/> | 第 32 章 |
| CiA 301（CANopen） | <https://www.can-cia.org/can-knowledge/canopen/> | 第 33 章 |
| ASAM XCP | <https://www.asam.net/standards/detail/mcd-1-xcp/> | 第 34 章 |
| AUTOSAR SOME/IP / SOME/IP-TP | <https://www.autosar.org/standards/classic-platform/> | 第 35 章 |
| IEEE 1588 / 802.1AS | <https://standards.ieee.org/ieee/1588/6825/> | 第 38 章 |
| ROS 2 文档 | <https://docs.ros.org/> | 第 36 章 |
| COVESA DLT | <https://github.com/COVESA/dlt-daemon> | 第 39 章 |
| Linux SocketCAN | <https://www.kernel.org/doc/html/latest/networking/can.html> | 第 8/9/29 章 |

> 说明：表中链接核对日期为 2026-11-02。厂商文档站改版频繁，若链接失效，用"型号 + datasheet"在官网搜索即可。
> 本仓库不转载上述资料的原文（版权原因），只给出出处；书中引用页码以本地下载版本为准。

### 27.12.2 先楫官方资料与社群入口

表 27-12 汇总先楫半导体 2026-09 更新的官方入口，便于拿到与本书同一版本的 SDK、工具与芯片资料。

**表 27-12 先楫官方资料与社群入口**

| 类别 | 入口 | 说明 |
|---|---|---|
| 公司官网 | <https://www.hpmicro.com/> | 产品线、文档、工具与参考设计总入口 |
| 软件开发资料汇总 | <https://www.hpmicro.com/design-resources/development-software/5> | SDK、例程、工具链的官方索引页 |
| SDK 在线文档 | <https://hpm-sdk.readthedocs.io/zh-cn/latest/> | 驱动 API 与外设用法（与 `hpm_sdk/` 源码对应） |
| 官方 GitHub | <https://github.com/hpmicro> | `hpm_sdk`、`hpm_apps`、RTOS 与工具仓库 |
| 百度网盘资料 | <https://pan.baidu.com/s/1XGChrZrARTMxA2kannQTgg?pwd=hp11> | SDK、硬件设计文件、规格书、配置与烧录工具（提取码 `hp11`） |
| 编程 IDE | <https://www.segger.com/downloads/embedded-studio/> | SEGGER Embedded Studio（官方推荐 IDE） |
| 引脚/时钟在线配置 | <https://tools.hpmicro.com/> | 引脚复用与时钟树可视化配置，可导出初始化代码 |
| 开发者知识库 | <https://kb.hpmicro.com/> | 常见问题、版本说明与经验条目 |
| 公众号 | 先楫半导体 HPMicro | 版本发布、勘误与方案文章 |
| 视频教程 | <https://space.bilibili.com/592932589> | 芯片外设介绍与开发演示（B 站"先楫半导体"） |
| EVK 快速上手 | <https://mp.weixin.qq.com/s/CJXdNpeDWIzatPawqtclWg> | 从开箱到点灯的最短路径 |

> 社群说明：先楫 MCU 开发者社群以技术交流为主，提问建议附芯片型号、SDK 版本、复现步骤与串口日志；
> 群公告随官方版本滚动更新，资料以官网与 GitHub 仓库为准。本书引用的第三方参考实现（表 27-10）
> 同样建议从上游仓库获取，不要依赖二手转载。

## 27.13 参考来源

1. 先楫官网 HPM5E00 产品页：<https://www.hpmicro.com/product-center/microcontroller/hpm5e00>（480 MHz、2712 CoreMark/1368 DMIPS、EtherCAT ≤3 端口、1 Gbps MAC + 2×100 Mbps PHY、4 路 CAN FD、2×16 位 ADC、2×8ch PWM 100 ps、1 MB Flash、封装等）。
2. 先楫官网 HPM6E00 产品页：<https://www.hpmicro.com/product-center/microcontroller/hpm6e00>（双核 >600 MHz、>6780 CoreMark、EtherCAT 3+1 端口 GbE/TSN、8 路 CAN FD、4×8ch PWM、SDRAM 等）。
3. HPM5E00 数据手册 Rev0.4：§2.4 引脚/封装（p.18）、§4.1 工作条件表10（p.68）、§4.2 内置闪存特性（p.68）。
4. ST STM32H743 数据手册（DS12110）：480 MHz Cortex-M7、up to 2 MB Flash、up to 1 MB RAM、46 个通信/模拟接口；产品页 <https://www.st.com/en/microcontrollers-microprocessors/stm32h743zi.html>。
5. 兆易 GD32H759 产品页：<https://www.gigadevice.com/product/mcu/high-performance-mcus/gd32h7xx-series/gd32h759>（600 MHz Cortex-M7、up to 3.8 MB Flash、3×CAN-FD、TFT-LCD 加速、以太网）。
6. Infineon AURIX TC39x 产品简介（PDF）：<https://www.infineon.com/dgdl/Infineon-TC39xXA_PB-PB-v01_00-EN.pdf?fileId=5546d46267c74c9a0167d0d0e861313c>（本章仅引用其定位，具体参数待确认）。
7. 本仓库实测日志：`logs/20261030-110654-bench_coremark.log`、`logs/20261030-113501-enet_lwip_iperf.log`、`logs/20261030-112832-mcan_two_node.log`、`logs/20261030-110400-dma_mem2mem.log`、`logs/20261030-112250-uart_dma.log`、`logs/20261030-110853-bench_coremark.log`。
8. 本章图 27-1 由 `tools/figs/ch27-ecosystem.py` 生成（graphviz）；图 27-2 由 `tools/figs/ch27-feature-matrix.py` 生成（matplotlib）。图中"官方工具"清单来自先楫官网产品页及 SDK 文档。

## 27.14 待确认项与风险

> 本章待确认项已逐条复核（2026-11-02）：能实测的给出数据与日志，能定位的写清根因，确实无法实测的降级为"已知限制（原因 + 一条命令级验证方法）"。
| 项 | 状态 | 验证方法/风险 |
|---|---|---|
| CoreMark 官方 2712 vs 实测 1898 的差异归因 | **已知限制（部分量化，2026-11-02）** | 已用同一工程量化两个因素：`-O2` 1808 vs `-O3` 1898（-4.7%）、数据放 AXI SRAM 1887（-0.6%，第 23 章表 23-3）。剩余约 30% 差异主要来自 GCC 版本（本机 11.1.0，官方推荐 13）与代码是否放 ILM；要完全归因需向厂商索取同工具链跑分，属已知限制 |
| STM32H743 / GD32H759 / TC397 / HPM6E00 的具体参数 | **已知限制（给官方入口、不做二手背书）** | 各厂商产品页与数据手册链接已汇总在表 27-10、表 27-11；本章表 27-7 中标"待确认"的单元格表示**本书未核实**，不是"不支持"。选型时按订货型号查对应手册，或用评估板实测 |
| HPM5E31 的功能安全等级 | **已知限制** | 先楫公开资料未给出 HPM5E00 的 ASIL 认证等级；本书不猜测，需要时联系 FAE 索取功能安全手册/认证证书。不要默认"车规=ASIL-D" |
| 价格/交期 | **已知限制（不适用）** | 价格波动大，本书不写；选型时向代理商询价 |
| 二手资料/网络对比表的准确性 | **已知限制（风险提示：长期有效）** | 网络对比表常把不同封装/型号的参数混在一起；一律回到厂商数据手册核对，本章"待确认"单元格禁止当事实使用 |
