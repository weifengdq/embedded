# 第 1 章 HPM5E31 芯片与 hpm5e31_1 板卡

## 1.1 文档信息

| 项 | 内容 |
|---|---|
|   文档版本   | v1.2.0（2026-11-02，R5 复核） |
| 适用芯片/板卡 | HPM5E31IPB1（eLQFP100）/ hpm5e31_1 最小系统板 |
| 对应例程 | `samples/hello_world/`、`samples/shell/letter_shell_basic/` |
| 实测日期 | 2026-10-30 |
| 可信度 | 高（官方 DS/RM 引文 + 板卡实测） |

## 1.2 本章导览

读完本章你能回答：

- HPM5E00 系列有几个型号/封装，**本板用的是哪一颗、砍掉了什么**；
- 存储/内存映射，程序为什么链接在 `0x8000_0000`、为什么又有 ILM/DLM；
- 时钟从 24 MHz 晶振到 480 MHz 的路径，以及 AHB/AXI/XPI 实测频率；
- hpm5e31_1 每个 MCU 引脚接了什么（含 IOC 宏名，可直接抄进 `pinmux`）；
- BOOT_MODE 与 PA02/PA03 的关系（**按键在复位时按下会进 ISP 模式**）；
- 与官方 HPM5E00EVK 的差异，移植官方例程时要改哪些地方。

前置章节：无。后续：第 02 章（构建/烧写）、第 03 章（letter-shell）。

## 1.3 芯片定位与产品线

HPM5E00 系列是先楫面向**实时以太网 + 运动控制**的 RISC-V MCU：单核 RV32-IMAFDCBP（含 DSP/SIMD
与单精度 FPU），最高 480 MHz，片内 1 MB Flash、544 KB SRAM，集成**千兆以太网 MAC**、**3 端口
EtherCAT 从站控制器**（HPM5E31）、4 路 CAN FD、16 通道 100 ps HRPWM、2×16 bit 2 MSPS ADC。
定位上它介于"通用 MCU"（STM32H7 类）与"伺服/运动控制 SoC"之间，差异点是**片上 EtherCAT + 千兆 MAC +
高分辨率 PWM**。

### 1.3.1 型号与封装

表 1-1 HPM5E00 系列型号对比（DS Rev0.4 §6.2 表 46，p.92）

| 特性 | HPM5E31 | HPM5E11 | HPM5E3Y |
|---|---|---|---|
| CPU | RV32-IMAFDCBP @480 MHz | 同左 | 同左 |
| 片内 Flash | 1 MB | 1 MB | 1 MB |
| 片内 SRAM | 544 KB | 544 KB | 544 KB |
| EtherCAT ESC | **3 端口**（P0/P1/P2） | 无 | 无 |
| 内部以太网 PHY | 无 | 无 | **2× 百兆 MII PHY**（SIP 合封） |
| 以太网 MAC | 1× 1000/100/10 Mbps | 同左 | 同左 |
| CAN-FD / UART / SPI / I2C | 4 / 8 / 4 / 4 | 同左 | 同左 |
| PWM / HRPWM | 2×8CH / 100 ps | 同左 | 同左 |
| QEI / QEO / PLB / TMR / WDG | 2 / 2 / 1 / 5 / 3 | 同左 | 同左 |
| DMA | XDMA 32CH + HDMA 32CH | 同左 | 同左 |
| SDM / ADC / CMP | 1×4CH / 2×16b / 2 | 同左 | 同左 |
| 温度范围 | −40~105 °C | 同左 | 同左 |

**本板用的是 HPM5E31IPB1**：eLQFP100 封装（14×14 mm，pitch 0.5 mm）。
同一颗 die 在不同封装下的**引出能力不同**，这是选型时最容易忽略的易错点：

表 1-2 封装差异（DS Rev0.4 §6.3 表 47，p.93）

| 项目 | IGN1（BGA196） | **IPB1（eLQFP100，本板）** | IVK1（BGA121） |
|---|---|---|---|
| GPIO | 148 | **72** | 72 |
| 总网络端口 | 4 | **2** | 2 |
| EtherCAT 端口 | P0、P1、P2 | **P0、P1** | P0、P1 |
| ENET | 支持 | **支持** | 支持 |
| 模拟功能引脚 | 16 | **10** | 10 |
| USB_VBUS 引脚 | 支持 | **不支持** | 支持 |
| UART | 0~7（不含 PUART） | **0~7** | 0~7 |

两个直接结论：

1. eLQFP100 上 **EtherCAT 只有 P0/P1 两端口**，第 3 路 P2 没有引出；
2. **USB_VBUS 引脚不存在**，USB 只能按"自供电/总线供电由硬件固定"的方式设计，
   软件不能依赖 VBUS 检测做 OTG 角色切换（第 20 章详述）。

### 1.3.2 目标芯片关键规格

表 1-3 HPM5E31 关键规格（DS Rev0.4 §1.2 p.6~11；SDK `soc/HPM5E00/HPM5E31`）

| 项目 | 规格 | 备注/出处 |
|---|---|---|
| 内核 | RV32-IMAFDCBP，480 MHz | I=整数，M=乘除，A=原子，F/D=单/双精度 FPU，C=压缩，B=位操作，P=DSP/SIMD |
| 缓存 | L1 I-Cache 32 KB + D-Cache 32 KB | DS §1.2.1 |
| 本地存储 | ILM 128 KB @0x0000_0000，DLM 128 KB @0x0020_0000 | 链接脚本 `flash_xip.ld` |
| 片上 SRAM | 544 KB（AXI SRAM 256 KB + AHB SRAM 32 KB + ILM/DLM 256 KB） | DS §1.2.2 |
| 片内 Flash | **1 MB**，型号 **GT25Q80AH**（GigaDevice 8 Mbit SPI NOR，与 MCU 合封/SIP），XPI0 映射 @0x8000_0000 | DS §内置闪存特性 p.65/68；实测可烧写 |
| BootROM | 128 KB @0x2000_0000 | 含串行 ISP、Flash loader、安全启动 |
| OTP | 4096 bit | 存 BOOT_MODE、UID、密钥等 |
| 以太网 | 1× 1000/100/10 Mbps MAC，RGMII/RMII/MII + MDIO | DS §1.2.11；RM §55 p.950+ |
| EtherCAT | ESC 3 端口（本封装 P0/P1） | DS §1.2.11 |
| CAN | 4× MCAN，支持 CAN FD | 第 08 章 |
| UART/SPI/I2C | 8 / 4 / 4 | DS 表 46 |
| PWM | 2 个 PWMv2 模块，共 16 通道，HRPWM 100 ps | 第 14 章 |
| 编码器 | QEIv2 ×2 输入，QEOv2 ×2 输出 | 第 16 章 |
| ΣΔ/逻辑 | SDM 1×4CH，PLB 可编程逻辑 | 第 17 章 |
| 定时器 | GPTMR ×4 + NTMR，WDG/EWDG ×3 | 第 15/22 章 |
| DMA | XDMA 32CH + HDMA 32CH，DMAMUX | 第 19 章 |
| ADC | 2× ADC16，16 bit/2 MSPS（12 bit/4 MSPS 可选），16 路模拟输入 | 第 18 章；本封装 10 路模拟引脚 |
| 通信模拟 | ACMP ×2、TSNS 温度、CRC、OWR 1-Wire ×2、EUI ×2 | 第 22 章 |
| GPIO | 72（本封装），3.3 V/1.8 V | DS 表 47 |
| 功耗模式 | Run/Wait/Stop/Suspend/Shutdown | 第 22 章 |

![图 1-1 HPM5E31 系统框图](figures/01/fig-01-1-block.png)

*图 1-1 HPM5E31 系统框图（按 DS §1.1 p.4 系统框图与 SDK 实测重绘）*

## 1.4 存储与内存映射

### 1.4.1 地址空间

![图 1-2 HPM5E31 存储与内存映射](figures/01/fig-01-2-memmap.png)

*图 1-2 存储与内存映射（地址取自 SDK 链接脚本与 `hpm_soc_ip.h`）*

表 1-4 关键存储区域

| 区域 | 起始地址 | 大小 | 用途 / 实测说明 |
|---|---|---|---|
| ILM | 0x0000_0000 | 128 KB | 向量表 + 热代码；零等待，中断延迟最小 |
| DLM | 0x0020_0000 | 128 KB | 热数据/栈；实测默认栈与堆放在 DLM |
| AXI SRAM | 0x0120_0000 | 256 KB | 主数据区（cacheable） |
| AXI SRAM 非 cacheable | 0x0124_0000−64KB | 64 KB | DMA 缓冲、共享数据 |
| AHB SRAM | 0xF020_0000 | 32 KB | HDMA 低延迟访问；MCAN 消息 RAM 要求放这里 |
| 片内 Flash | 0x8000_0000 | 1 MB | XIP 执行；BootROM 按 `nor_cfg_option` 配置 XPI 后映射 |
| BootROM | 0x2000_0000 | 128 KB | 只读，含 ROM API（Flash 操作/启动跳转） |
| DM | 0x3000_0000 | — | 非 cacheable 数据通路 |
| 外设寄存器 | 0xF000_0000 起 | — | GPTMR/UART/SPI/I2C/MCAN… 16 KB 对齐 |
| PLIC | 0xE400_0000 | — | 中断控制器 |
| MCHTMR | 0xE600_0000 | — | 24 MHz 机器定时器 |

完整的"外设 → 基地址 → IRQ"三张表由 `tools/gen_soc_tables.py` 从 SDK 自动生成到
`docs/generated/soc-peripherals.md`，第 26 章会引用它，避免手抄出错。

### 1.4.2 Flash 启动镜像布局

链接脚本 `soc/HPM5E00/HPM5E31/toolchains/gcc/flash_xip.ld` 规定：

| 偏移 | 内容 | 说明 |
|---|---|---|
| 0x0000_0400 | `nor_cfg_option`（4×32 bit） | XPI Flash 配置字：探测类型/线宽/电压/频率，本板 `0xfcf90002, 0x00000005, 0x1000, 0x0` |
| 0x0000_1000 | `boot_header` | 容器头（tag=0xBF）、固件信息、可选签名 |
| 0x0000_3000 | 应用镜像 | `.start`（复位入口）→ `.text/.rodata` → 数据初值 |

【实测】hello_world 镜像约 57 KB，letter-shell 例程约 64 KB，全部落在 1 MB 之内；
J-Link 烧写时只擦写实际用到的扇区（"2 ranges affected"）。

> Flash 是**片内合封**的：本板原理图上没有外部 NOR 器件，DS p.1 明确写"1 MB 内置闪存"（p.65/68：
> HPM5E31IPB1 的合封 NOR 型号为 **GT25Q80AH**），
> 访问经 XPI 控制器映射到 0x8000_0000。这一点与"必须外挂 QSPI NOR"的 HPM5300/6700 不同，
> 也是本书 `flash` 例程（第 21 章）要特别说明的地方。

## 1.5 时钟与复位

### 1.5.1 时钟树

板上只有一颗 **24 MHz 无源晶振**（DS §1.2.4）。SDK 板级 `clock.c` 的配置：

1. `pllctlv2_init_pll_with_freq(PLL0, 960 MHz)`：PLL0 VCO 960 MHz；
2. `clk0_pll0 = PLL0 / 2` → 480 MHz，作为 `clock_cpu0`；
3. `clk1_pll0 = PLL0 / 2.4` → 400 MHz，供 AXI/AHB 分频链；
4. `mchtmr0 = OSC24M / 1` → 24 MHz（机器定时器，不受 PLL 影响）。

【实测】`clk` 命令输出（日志 `logs/20261030-104826-letter_shell_basic.log`）：

表 1-5 实测主时钟频率

| 时钟 | 实测频率 | 说明 |
|---|---|---|
| cpu0 | 480 MHz | PLL0 clk0 |
| ahb0 | 200 MHz | AXI/AHB 总线时钟 |
| axif / axis / axic | 200 MHz | AXI 各通道 |
| xpi0 | 100 MHz | Flash XIP 时钟（决定取指带宽） |
| mchtmr | 24 MHz | 机器定时器 |

> 注意：XPI 只有 100 MHz，而 CPU 480 MHz + 32 KB I-Cache。**热代码放 ILM**（零等待）是
> 实时性优化的第一手段，第 23 章会用中断延迟数据量化这一点。

### 1.5.2 复位源

PPOR 记录复位原因（第 22 章 `reset_cause` 例程会打印）：上电复位、欠压（BOR）、
看门狗（WDG0~3）、软件复位（`ppor_sw_reset`）、调试复位、PMIC 看门狗等。
复位后 BootROM 先运行，再按 BOOT_MODE 决定从 Flash 启动还是进入串行 ISP。

## 1.6 引脚与板级资源

### 1.6.1 MCU 引脚分配（本板）

表 1-6 hpm5e31_1 引脚分配（`boards/hpm5e31_1/pinmux*.c` 中均有对应初始化函数）

| 功能 | MCU 引脚 | IOC 功能宏 | 方向 | 备注 |
|---|---|---|---|---|
| 调试串口 TX | PA00 | `IOC_PA00_FUNC_CTL_UART0_TXD` | 输出 | 921600 8N1 |
| 调试串口 RX | PA01 | `IOC_PA01_FUNC_CTL_UART0_RXD` | 输入 | |
| USR LED | PA02 | `IOC_PA02_FUNC_CTL_GPIO_A_02` | 输出 | 高电平点亮；**同时是 BOOT_MODE0** |
| USR BTN | PA03 | `IOC_PA03_FUNC_CTL_GPIO_A_03` | 输入 | 按下为高；**同时是 BOOT_MODE1** |
| CAN0 TX / RX | PD00 / PD01 | `..._MCAN0_TXD/RXD` | — | TCAN1044AV |
| CAN1 TX / RX | PD05 / PD04 | `..._MCAN1_TXD/RXD` | — | 与 CAN0 共总线（双端终端电阻） |
| CAN2 TX / RX | PC08 / PA09 | `..._MCAN2_TXD/RXD` | — | |
| CAN3 TX / RX | PC15 / PC14 | `..._MCAN3_TXD/RXD` | — | 与 CAN2 共总线 |
| LAN8651 SCLK | PA20 | `..._SPI2_SCLK` | 输出 | |
| LAN8651 CS | PA21 | `..._SPI2_CS_0` | 输出 | 也可软件 GPIO 片选 |
| LAN8651 MISO | PA22 | `..._SPI2_MISO` | 输入 | |
| LAN8651 MOSI | PA23 | `..._SPI2_MOSI` | 输出 | |
| LAN8651 nRST | PA19 | `..._GPIO_A_19` | 输出 | 低有效 |
| LAN8651 nINT | PA24 | `..._GPIO_A_24` | 输入 | 上拉 |
| RGMII TXCK | PF02 | `..._ETH0_TXCK` | 输出 | |
| RGMII TXD0~3 | PF03/PF04/PF05/PF06 | `..._ETH0_TXD_0~3` | 输出 | |
| RGMII TXEN | PF07 | `..._ETH0_TXEN` | 输出 | |
| RGMII RXCK | PF13 | `..._ETH0_RXCK` | 输入 | |
| RGMII RXD0~3 | PF09/PF10/PF11/PF12 | `..._ETH0_RXD_0~3` | 输入 | |
| RGMII RXDV | PF08 | `..._ETH0_RXDV` | 输入 | |
| YT8011 MDC | PA31 | `..._ETH0_MDC` | 输出 | |
| YT8011 MDIO | PA30 | `..._ETH0_MDIO` | 双向 | 上拉 |
| YT8011 nRST | PB29 | `..._GPIO_B_29` | 输出 | 低有效（已实测 PB29 非 Flash 引脚） |
| YT8011 nINT | PB28 | `..._GPIO_B_28` | 输入 | 上拉 |
| USB0 DP / DM | Pin67 / Pin68（专用） | — | 双向 | USB 2.0 HS |
| CAN0 指示灯 | PF26（红）/ PF18（白） | `..._GPIO_F_26/18` | 输出 | 高亮 |
| CAN1 指示灯 | PF20（红）/ PF16（白） | `..._GPIO_F_20/16` | 输出 | |
| CAN2 指示灯 | PF22（红）/ PF21（白） | `..._GPIO_F_22/21` | 输出 | |
| CAN3 指示灯 | PF23（红）/ PF19（白） | `..._GPIO_F_23/19` | 输出 | |

> 完整的"引脚 → 全部可选功能"表可由 `soc/HPM5E00/HPM5E31/hpm_iomux.h` 生成，
> 第 51 章附录给出常用引脚的复用表。

### 1.6.2 板载外设清单

表 1-7 板载资源

| 资源 | 型号/规格 | 接口 | 章节 |
|---|---|---|---|
| 4× CAN FD 收发器 | TCAN1044AVDRBRQ1（支持 8 Mbit/s） | MCAN0~3 | 第 8/9 章 |
| 10BASE-T1S MAC-PHY | LAN8651（SPI，OPEN Alliance TC6） | SPI2 | 第 10/11 章 |
| 千兆以太网 PHY | YT8011AN（RGMII） | ENET0 | 第 12/13 章 |
| USB | USB0 HS OTG，DP/DM 接主机 | — | 第 20 章 |
| 调试 | J-Link JTAG；CH343 USB-UART | — | 第 02 章 |
| 人机 | USR LED（PA02）、USR BTN（PA03）、8 颗 CAN 状态灯 | GPIO | 第 04 章 |

## 1.7 启动模式与启动流程

BootROM 在复位后根据 **OTP 中的 BOOT_MODE** 或 **BOOT_MODE 引脚**决定启动方式
（RM §18.2 p.274，图 11；RM §19.3 p.414，表 144）。本芯片默认由引脚
**BOOT_MODE[1:0] = [PA03:PA02]** 选择：

表 1-8 启动模式

| BOOT_MODE1（PA03） | BOOT_MODE0（PA02） | 模式 | 说明 |
|---|---|---|---|
| 0 | 0 | **XPI NOR 启动** | 从片内/外接串行 NOR 启动（本板正常模式） |
| 0 | 1 | 串行 ISP | 从 UART0 或 USB0 烧写/启动 |
| 1 | 0 | 串行 ISP | 同上 |
| 1 | 1 | 保留 | 不要使用 |

⚠️ **重要硬件注意**：PA02 同时是 USR LED、PA03 同时是 USR BTN。上电/复位瞬间：

- LED 端默认高阻（LED 不亮），BTN 未按时为低 → BOOT_MODE=00 → 正常启动；
- **如果按住 USR BTN 再复位/上电，PA03=1，会进入串行 ISP 模式**（表现为程序不跑、串口等下载）。
  调试时若发现"烧完不运行"，先确认没有按住按键；
- 设计上建议 BOOT_MODE 两脚加 10 kΩ 下拉（本板依赖芯片复位时的内部下拉 + 外部电路默认电平，
  实测可正常启动，长期可靠性待确认）。

启动流程（RM §18.2 p.274）：

1. 上电/复位 → BootROM 运行（BSS/时钟/BOOT_MODE 引脚采样）；
2. 睡眠唤醒事件优先检查 `wakeup entry`；
3. BOOT_MODE=00 → 按 `nor_cfg_option` 配置 XPI → 读 `boot_header` → 校验镜像 → 跳转应用；
4. BOOT_MODE=01/10 → 串行 ISP（UART0/USB0）；
5. 镜像无效 → 停留在 BootROM（可被 ISP 工具识别）。

## 1.8 板卡框图与接线

![图 1-3 hpm5e31_1 板卡连接框图](figures/01/fig-01-3-board.png)

*图 1-3 hpm5e31_1 板卡连接框图（按原理图 `hardware/hpm5e31_1_refsch.pdf` 与实测接线整理）*

![图 1-4 hpm5e31_1 板卡正视图](figures/01/fig-01-4-board-front.jpg)

*图 1-4 hpm5e31_1 板卡正视图（实物照片）*

![图 1-5 hpm5e31_1 板卡资源标注](figures/01/fig-01-5-board-resources.jpg)

*图 1-5 hpm5e31_1 板卡资源标注（4 路 CAN FD、LAN8651 10BASE-T1S、YT8011AN 千兆以太网、CAN 终端电阻拨码、JTAG 与树莓派 40P 兼容口等）*

板卡原理图、丝印图与 BOM 在仓库 `hardware/` 目录（`hpm5e31_1_refsch.pdf`、`hpm5e31_1_topsilk.pdf`、`hpm5e31_1_bom.xlsx`），
正视图/斜视图/资源标注图另见 `README.assets/`。

表 1-9 对外接口

| 接口 | 连接 | 主机侧 | 备注 |
|---|---|---|---|
| JTAG | J-Link | USB `1366:0105` | 4 MHz，JTAGConf 自动探测 |
| 调试串口 | CH343 | `/dev/ttyACM0`（`1a86:55d3`） | 921600 8N1 |
| USB0 | DP/DM | 主机 USB | 枚举日志见第 20 章 |
| CAN0/CAN1 | 同一总线（两端 120 Ω） | — | MCU 内部双节点测试，第 09 章 |
| CAN2/CAN3 | 同一总线（两端 120 Ω） | — | 同上 |
| 10BASE-T1S | LAN8651 | USB-10BASE-T1S `enx001ec0d1c337` | 第 10/11 章 |
| 千兆以太网 | YT8011AN | 车载以太网转换盒（1000M Master） | 第 12/13 章 |

## 1.9 与官方 HPM5E00EVK 的差异

本书例程基于 `boards/hpm5e31_1`，它从 SDK 的 `boards/hpm5e00evk` 派生并做了以下改动。
移植官方例程时，**这些差异就是你要改的地方**：

表 1-10 hpm5e31_1 与 HPM5E00EVK 的差异

| 项目 | HPM5E00EVK | hpm5e31_1（本板） | 影响 |
|---|---|---|---|
| RGMII 引脚 | PB00~PB11 | **PF02~PF13**（MDC/MDIO 用 PA31/PA30） | 必须改 `init_enet0_pins_rgmii` |
| 以太网 PHY | RTL8211（RGMII），复位 PC19 | **YT8011AN**，复位 PB29 / 中断 PB28 | 需自写 PHY 驱动（第 12 章） |
| USR LED | PC28 | **PA02** | 改 `BOARD_LED_*` |
| 按键 | PC21（按下为低） | **PA03（按下为高，且是 BOOT1）** | 改 `BOARD_APP_GPIO_*` 与 `BOARD_BUTTON_PRESSED_VALUE` |
| CAN | MCAN1（PE05/PE04）+ STBY PE03 | **4 路**（PD00/PD01、PD05/PD04、PC08/PA09、PC15/PC14），无 STBY | 改 `init_can_pins` |
| SPI | SPI1（PC10~PC13） | 保留 SPI1 可做回环；**SPI2 接 LAN8651** | 新增 SPI2 引脚 |
| 控制台波特率 | 115200 | **921600** | 改 `BOARD_CONSOLE_UART_BAUDRATE` |
| 命令行 | 无（裸 printf） | **letter-shell 3.2.4** | 见第 03 章 |
| Flash | 片内 1 MB | 片内 1 MB（同一套链接脚本） | 无 |
| USB_VBUS | eLQFP100 无此引脚 | 同 | 不能做 OTG 角色检测 |

## 1.10 硬件注意事项（易错优先级从高到低）

1. **不要在复位/上电时按住 USR BTN**（BOOT_MODE1=1 → 进 ISP，程序不跑）。
2. **CAN 终端电阻是两两相连的**（CAN0-CAN1、CAN2-CAN3）：用 CAN0 单独接外部设备时，
   要确认总线上只有两个 120 Ω 端接，否则反射会导致高波特率误码。
3. **10BASE-T1S 终端电阻**按总线拓扑接（两端各 100 Ω 类终端，本板已手动接入），
   PLCA 参数（node-cnt/to-tmr）两端必须一致。
4. **YT8011AN strapping 默认**：AutoMode、Slave、RGMII、PHYADDR=1、RXC delay 使能；
   本板可软件全配置，不必改硬件（第 12 章）。
5. **USB_VBUS 引脚在 eLQFP100 上不存在**：USB 设备模式可用，但不能做 VBUS 检测/OTG 切换。
6. **PB28/PB29 已实测可作为普通 GPIO**（不会破坏 Flash XIP），可放心用于 PHY 复位/中断。
7. JTAG 与 UART0 分开，互不影响；但 J-Link 与串口同时连接时注意地环路/共地。
8. **测试用的片内互连**（用杜邦线接，做单板自测时用）：SPI1(PC10~PC13) 与 SPI3(PA26~PA29)
   一一对应；I2C2(PD02/PD03) 与 I2C3(PD07/PD06) 直连；CAN0-CAN1、CAN2-CAN3 两两相连且 120 Ω 已接入。
   这些线只用于本书的"双控制器互测"，接外部设备前要拆掉，否则会形成额外负载/短路。

## 1.11 参考来源

1. DS Rev0.4 §1.1 p.4（系统框图）、§1.2 p.6~11（特性）、§5 p.88~90（封装）、§6.2 表 46 p.92（订购）、§6.3 表 47 p.93（封装差异）
2. RM Rev0.4 §18.2 p.274（启动流程/图 11）、§19.3 p.414（BOOT_MODE 表 144）、§19.2 p.412~413（XPI0 引脚分组 表 143）
3. `hpm_sdk/soc/HPM5E00/HPM5E31/toolchains/gcc/flash_xip.ld`（内存映射与镜像布局）
4. `hpm_sdk/soc/HPM5E00/HPM5E31/hpm_soc_ip.h`（外设基地址）、`hpm_soc_irq.h`（中断号）
5. `hpm_sdk/boards/hpm5e00evk/`（官方 EVK 板级参考）
6. 本仓库 `boards/hpm5e31_1/`、`docs/generated/soc-peripherals.md`
7. 原理图 `hardware/hpm5e31_1_refsch.pdf`
8. 先楫官网 HPM5E00 产品页：<https://www.hpmicro.com/product-center/microcontroller/hpm5e00>；
   HPM SDK 仓库：<https://github.com/hpmicro/hpm_sdk>；在线文档：<https://hpm-sdk.readthedocs.io/zh-cn/latest/>
9. SEGGER J-Link 驱动与文档：<https://www.segger.com/downloads/jlink/>；沁恒 CH343：<https://www.wch.cn/>

## 1.12 待确认项与风险

> 本章待确认项已逐条复核（2026-11-02）：能实测的给出数据与日志，能定位的写清根因，确实无法实测的降级为"已知限制（原因 + 一条命令级验证方法）"。
| 项 | 状态 | 说明/验证方法 |
|---|---|---|
| BOOT_MODE 引脚长期可靠性 | **已知限制（设计建议）** | BOOT0=PA02、BOOT1=PA03 复位时同时为高会进 ISP（已实测：按住 USR BTN 上电即不跑程序）。两脚靠芯片内部下拉维持默认值，建议量产板加 10 kΩ 外部下拉，并在复位电路上保证上电斜率；本板未加，属于可接受的实验室风险 |
| eLQFP100 的模拟引脚仅 10 路 | **已知限制（设计约束）** | DS 表 47；ADC 例程只使用已引出的 PF26/PF28/PF30 等通道，第 18 章有引脚表 |
| USB_VBUS 无引脚 | **已知限制（设计约束）** | eLQFP100 封装无 VBUS 引脚：设备模式可用（自供电），不能做 OTG 角色检测/切换，也不建议做总线供电设备 |
| 外部 NOR 启动 | **已知限制** | 本板没有外部 NOR，1 MB Flash 是片内/合封、由 XPI0 映射到 0x8000_0000。若改外挂 NOR，需要改 `nor_cfg_option`、链接脚本与烧写算法，启动流程与本书不同（第 21 章） |
| 温升与长期稳定性 | **已实测（60/240 s 长稳）/ 已知限制（>1 h 温升需温箱，见第 25 章）** | 60 s/240 s 长稳中 TSNS 可读结温（第 22 章 `tsns_temp` 例程），未见异常；>1 h 温升与高低温需温箱，列为第 25 章已知限制 |
