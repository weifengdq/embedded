# STM32H503 + LAN9370 + LAN8720 百兆车载以太网交换机方案

## 项目概述

本项目实现了基于 **STM32H503** (Cortex-M33 @ 250MHz) + **LAN9370** (5口 100BASE-T1 交换机)
+ **LAN8720A** (RMII 百兆 PHY) 的完整汽车以太网解决方案。

```
  ┌─────────────┐      100BASE-T1        ┌──────────────────┐
  │ 车载以太网   │◄──────────────────────►│ LAN9370          │
  │ 转换盒       │    Port1 (Slave)       │ 5-Port Switch    │
  │ 192.168.0.2 │                        │                  │
  └─────────────┘                        │  Port2 (Master)──┼──► 192.168.0.68
                                         │                  │    (iperf server)
  ┌─────────────┐     RMII   ┌─────────┐ │  Port3..4 (空)   │
  │ LAN8720A    │◄──────────►│ Port5   │ │                  │
  │ 外部PHY      │   REFCLK   │ (RMII)  │ │                  │
  └──────┬──────┘  50MHz OSC └────┬─────┘ └──────────────────┘
         │                        │MDC/MDIO        ▲  SPI (PA1-PA4)
    RJ45 │                        │(直连)          │  nRST (PB7)
  ┌──────┴──────┐          ┌──────┴─────┐  ┌──────┴───────────┐
  │  电脑/网络   │          │ LAN9370    │  │   STM32H503      │
  │  192.168.0.x│          │ MDIO Master│  │   Shell CLI      │
  └─────────────┘          └────────────┘  └──────────────────┘

  ★ 新接线: STM32 PB5/PB6 已断开。LAN9370 内部 MDIO Master
    直接管理 LAN8720A，STM32 通过 SPI 控制 MDIO 读写。
```

### 核心特性

- **LAN9370 SPI 驱动**：通过 SPI1 (Mode 0) 配置和管理交换机
- **LAN9370 SMI_OUT**：外部 PHY 由 LAN9370 自己的 MDC/MDIO 出脚管理，MCU 不通过 SPI 直读 external PHY 寄存器
- **VPHY 间接访问**：通过 SPI 访问 T1 PHY 内部寄存器
- **LAN8720A PHY 驱动**：通过 Port5 xMII 状态派生外部 RMII PHY 链路状态
- **Letter-shell 命令行**：交互式控制和诊断
- **持久化配置**：端口模式/VLAN/PTP 配置保存到 MCU Flash
- **CMake + Ninja 构建**：一键编译和烧录

---

## 硬件接线

### 整体接线框图

```
STM32H503 (NUCLEO-H503RB)          LAN9370 EVB               LAN8720A 模块
┌─────────────────────┐       ┌──────────────────┐       ┌──────────────┐
│ PA1 (NSS)    ──────────────►  SPI_CS            │       │              │
│ PA2 (SCK)    ──────────────►  SPI_SCK           │       │              │
│ PA3 (MISO)   ◄──────────────  SPI_MISO          │       │              │
│ PA4 (MOSI)   ──────────────►  SPI_MOSI          │       │              │
│ PB7 (nRESET) ──────────────►  nRST              │       │              │
│                      │       │                  │       │              │
│                      │       │  MDC  ───────────┼───────┤ MDC          │
│                      │       │  MDIO ◄─────────►┼───────┤ MDIO         │
│                      │       │  (LAN9370 内部    │       │              │
│                      │       │   MDIO Master)   │       │              │
│                      │       │                  │       │              │
│                      │       │  Port5 RMII      │       │ RMII         │
│                      │       │  TXD[1:0] ───────┼───────┤ TXD[1:0]     │
│                      │       │  RXD[1:0] ◄──────┼───────┤ RXD[1:0]     │
│                      │       │  TXEN    ────────┼───────┤ TXEN         │
│                      │       │  CRS_DV  ◄───────┼───────┤ CRS_DV       │
│                      │       │  REFCLK  ◄───────┼───────┤ 50MHz OSC    │
│                      │       │                  │       │              │
│  GND         ────────────────┤  GND   ──────────┼───────┤ GND          │
│                      │       │                  │       │              │
│  ★ PB5/PB6 已断开   │       │                  │       │              │
└─────────────────────┘       └──────────────────┘       └──────────────┘

  ★ MDIO 拓扑变更: STM32 不再通过 GPIO 直连 MDIO 总线。
   LAN9370 通过其 SMI_OUT(MDC/MDIO) 引脚管理 LAN8720A。
   STM32 仅通过 SPI 配置 switch 和读取 Port5 xMII 状态，不能直读 external PHY Cl22 寄存器。
```

### STM32H503 引脚分配

| 功能         | 引脚 | AF   | 连接目标            | 说明                       |
|-------------|------|------|--------------------|---------------------------|
| **HSE 晶振** | -    | -    | 8MHz 外部晶振        | PLL → SYSCLK 250MHz       |
| **SWD 调试** | -    | -    | STLINK-V3MINIE     | 下载和调试                  |
| **LPUART1** | PA9  | AF3  | TX → 串口工具(COM80) | 2Mbps, Shell 交互          |
|             | PA10 | AF3  | RX ← 串口工具        |                           |
| **SPI1**    | PA1  | -    | CS → LAN9370 SPI_CS | 软件控制 CS（GPIO 输出）     |
|             | PA2  | AF4  | SCK → LAN9370       | **SPI Mode 3** (CPOL=1, CPHA=1), 3.906 MHz |
|             | PA3  | AF4  | MISO ← LAN9370      |                           |
|             | PA4  | AF4  | MOSI → LAN9370      |                           |
| **复位**     | PB7  | -    | nRST -> LAN9370      | 低电平有效，>=10ms 脉冲      |
| **PB5, PB6** | -    | -    | ★ 已断开，不连接       | MDIO 由 LAN9370 内部主控管理 |
| **PB5, PB6** | -    | -    | ★ 已断开，不连接       | MDIO 由 LAN9370 内部主控管理 |

### LAN8720A RMII 接口接线（连接到 LAN9370 Port 5）

| LAN9370 Port5 信号 | LAN8720A 引脚 | 方向      | 说明                      |
|-------------------|--------------|----------|--------------------------|
| TXD[1:0]          | TXD[1:0]     | -> PHY    | RMII 发送数据              |
| RXD[1:0]          | RXD[1:0]     | <- PHY    | RMII 接收数据              |
| TXEN              | TXEN         | -> PHY    | 发送使能                   |
| CRS_DV            | CRS_DV       | <- PHY    | **载波检测/数据有效（关键信号）** |
| REFCLK            | X1/CLKIN     | <- OSC    | **50MHz 参考时钟（关键信号）** |
| MDIO              | MDIO         | <->       | LAN9370 MDIO Master 直连   |
| MDC               | MDC          | -> PHY    | LAN9370 MDIO Master 直连   |

> **CRSDV / REFCLK 注意事项**：
> - **REFCLK** 必须由外部 50MHz 有源晶振提供，LAN9370 Port5 配置为 REFCLK input 模式
> - **CRS_DV** 是 RMII 的关键信号，如果接错或虚焊，链路层无法建立
> - LAN8720A 的 PHYAD0 引脚内部下拉，默认 PHY 地址 = 0；若外部上拉，PHY 地址 = 1
> - **MDIO 变更**: STM32 PB5/PB6 已断开。LAN9370 MDC/MDIO 引脚直连 LAN8720A MDC/MDIO

### LAN9370 端口接线汇总

| 端口    | 类型        | 默认模式 | 连接设备           | 对端 IP          |
|--------|------------|---------|-------------------|-----------------|
| Port 1 | 100BASE-T1 | Slave   | 百兆车载以太网转换盒  | 192.168.0.2 (PC) |
| Port 2 | 100BASE-T1 | Master  | 外部 100BASE-T1 设备 | 192.168.0.68    |
| Port 3 | 100BASE-T1 | Slave   | 预留               | -               |
| Port 4 | 100BASE-T1 | Slave   | 预留               | -               |
| Port 5 | RMII       | -       | LAN8720A PHY + RJ45 | 192.168.0.x (PC) |

---

## 适配情况

### LAN9370 适配

| 项目           | 状态 | 说明                                                |
|---------------|------|----------------------------------------------------|
| SPI 寄存器访问  | OK   | **Mode 3** (CPOL=1, CPHA=1), **3.906 MHz** (250MHz/64), 8/16/32 位读写均验证通过 |
| 芯片识别       | OK   | Chip ID: 0x00937010, Revision ID 正常                 |
| VPHY 间接访问  | OK   | 通过 VPHY_SPECIAL_CTRL (0x077C) bit4 使能              |
| T1 PHY 寄存器  | OK   | 4 个 T1 端口的 PHY 寄存器均能正确读写                    |
| Master/Slave  | OK   | T1_PHY_MASTER_SLAVE_CTRL bit11 控制                     |
| 端口使能/禁用  | OK   | 通过 REG_PORTn_MSTP_STATE (0x0B04) 控制                 |
| L2 转发       | OK   | L2 membership mask + MAC learning 实现全端口互通        |
| Port 5 RMII   | OK   | XMII_CTRL0/1 正确配置，REFCLK input, 100M only         |
| MDIO Master   | N/A  | **LAN9370 无 SPI→MDIO 桥**；PHY 通过 Port5 RMII 链路状态间接确认 |
| VLAN          | OK   | Port-based VLAN 配置和查询                             |
| 端口镜像       | OK   | 支持任意端口间流量镜像                                   |
| PTP/gPTP      | WIP  | 寄存器控制位已找到，功能待验证                             |
| MIB 计数器    | OK   | 支持读取各端口收发统计                                   |

### LAN8720A 适配

| 项目           | 状态 | 说明                                                |
|---------------|------|----------------------------------------------------|
| PHY 自动探测   | OK   | PHY addr = **1** (PHYAD0 strapped，0x5302=0x01 确认)        |
| PHY ID 读取   | N/A  | 无 MDIO 通路；LAN9370 无 SPI→MDIO 桥，GPIO PB5/PB6 已断开    |
| RMII 链路确认  | OK   | Port5 status 0x0D = 100M Full Duplex 证明 LAN8720 已 link   |
| 检测方式       | OK   | MIIM 探测失败后回退到 Port5 xMII 状态寄存器 (0x5030)          |
| 软复位/协商    | N/A  | 需 MDIO 通路，当前不可用；LAN8720 上电后自动协商至 100M Full   |

---

## 测试情况

### 测试环境

- **MCU 平台**: STM32H503RB (NUCLEO-H503RB), SYSCLK 250MHz
- **交换机**: LAN9370 5-Port 100BASE-T1 Switch EVB
- **外部 PHY**: LAN8720A RMII 模块 (RJ45 接口)
- **测试 PC**: Windows 11, IP 192.168.0.2/24
- **对端设备**: 192.168.0.68 (100BASE-T1, iperf server)
- **编译工具**: arm-none-eabi-gcc 14.3.1, -Os 优化
- **固件大小**: Flash 44.7KB / 120KB (36.5%), RAM 3.9KB / 32KB (11.8%)

### Ping 测试

**测试 1: PC -> 100BASE-T1 设备 (192.168.0.68)**

```
> ping -n 4 192.168.0.68
来自 192.168.0.68 的回复: 字节=32 时间<1ms TTL=255
来自 192.168.0.68 的回复: 字节=32 时间<1ms TTL=255
来自 192.168.0.68 的回复: 字节=32 时间<1ms TTL=255
来自 192.168.0.68 的回复: 字节=32 时间<1ms TTL=255

数据包: 已发送 = 4，已接收 = 4，丢失 = 0 (0% 丢失)
```

**测试 2: PC 自环测试 (192.168.0.2)**

```
> ping -n 4 192.168.0.2
数据包: 已发送 = 4，已接收 = 4，丢失 = 0 (0% 丢失)
往返行程: 最短 = 0ms，最长 = 0ms，平均 = 0ms
```

### iperf TCP 吞吐量测试

**正向测试（PC -> 100BASE-T1 设备）**

```
> iperf.exe -c 192.168.0.68 -p 5001 -t 10 -i 1
------------------------------------------------------------
[ ID] Interval       Transfer     Bandwidth
[416]  0.0- 1.0 sec  11.3 MBytes  95.0 Mbits/sec
[416]  1.0- 2.0 sec  11.3 MBytes  94.4 Mbits/sec
[416]  2.0- 3.0 sec  11.3 MBytes  94.7 Mbits/sec
[416]  3.0- 4.0 sec  11.2 MBytes  94.2 Mbits/sec
[416]  4.0- 5.0 sec  11.3 MBytes  94.6 Mbits/sec
[416]  5.0- 6.0 sec  11.3 MBytes  94.7 Mbits/sec
[416]  6.0- 7.0 sec  11.3 MBytes  94.8 Mbits/sec
[416]  7.0- 8.0 sec  11.3 MBytes  94.6 Mbits/sec
[416]  8.0- 9.0 sec  11.3 MBytes  95.0 Mbits/sec
[416]  9.0-10.0 sec  11.3 MBytes  95.0 Mbits/sec
[416]  0.0-10.0 sec   113 MBytes  94.6 Mbits/sec
```

**反向测试（100BASE-T1 设备 -> PC）**

```
> iperf.exe -c 192.168.0.68 -p 5001 -t 5 -i 1 -R
[400]  0.0- 5.0 sec  56.6 MBytes  94.8 Mbits/sec
```

### 测试结论

| 指标           | 结果       | 说明                      |
|---------------|-----------|--------------------------|
| Ping 丢包率    | 0%        | 双向稳定                  |
| Ping 延迟      | <1ms      | LAN 级别延迟              |
| TCP 吞吐量     | ~94.6 Mbps | 接近 100BASE-T1 线速      |
| 双向吞吐量     | ~94.8 Mbps | 双向对称，无瓶颈           |
| 稳定性         | 优秀       | 10 秒测试无波动            |

---

## 工程架构

```
stm32h503_lan9370_lan8720/
├── Core/                           # STM32CubeMX 生成
│   ├── Inc/
│   │   ├── main.h                  # HAL 句柄声明
│   │   ├── app_extensions.h        # lwIP/FreeRTOS 扩展接口
│   │   ├── stm32h5xx_hal_conf.h    # HAL 配置
│   │   └── stm32h5xx_it.h         # 中断服务声明
│   └── Src/
│       ├── main.c                  # * 主程序入口，初始化全流程
│       ├── app_extensions.c        # 扩展模块初始桩
│       ├── stm32h5xx_hal_msp.c    # HAL 底层硬件初始化
│       ├── stm32h5xx_it.c         # 中断向量表实现
│       ├── syscalls.c / sysmem.c  # newlib 系统调用
│       └── system_stm32h5xx.c     # 系统时钟初始化
│
├── LAN9370/                        # * LAN9370 驱动模块
│   ├── lan9370_reg.h              # 完整寄存器定义 (>200个寄存器宏)
│   ├── lan9370_spi.h / .c         # SPI 底层：读/写 8/16/32位 + Burst
│   ├── lan9370_smi.h / .c         # GPIO 模拟 SMI/MDIO 协议
│   ├── lan9370_driver.h / .c      # 高层 API：初始化/端口/VLAN/PHY/MIB
│   ├── lan9370_persist.h / .c     # 持久化：MCU Flash 存储配置
│   └── lan8720_driver.h / .c      # * LAN8720A PHY 驱动（共享 MDIO）
│
├── Shell/                          # * 命令行交互模块
│   ├── shell_port.h / .c          # Shell 端口层 + 全部命令实现
│   ├── shell_cfg_user.h           # letter-shell 配置
│   └── letter-shell/              # 开源 letter-shell (v3.x)
│       └── src/                   # shell.c, shell_cmd_list.c, shell_ext.c
│
├── Drivers/                        # STM32 HAL 库（只读）
│   ├── CMSIS/                     # ARM CMSIS Core + Device
│   └── STM32H5xx_HAL_Driver/     # STM32H5 HAL 驱动
│
├── cmake/                          # CMake 工具链
│   ├── gcc-arm-none-eabi.cmake    # GCC ARM 交叉编译配置
│   ├── starm-clang.cmake          # (备用) ARM Clang 配置
│   └── stm32cubemx/              # STM32CubeMX 生成的 CMake 片段
│
├── build/                          # 构建输出目录（gitignore）
│   ├── Debug/                     # Debug 构建 ( -O0 -g3 )
│   └── Release/                   # Release 构建 ( -Os -g0 )
│
├── CMakeLists.txt                  # * 顶层 CMake 配置
├── CMakePresets.json              # CMake 预设（Debug / Release）
├── build.ps1                      # * PowerShell 一键构建/烧录脚本
├── serial_monitor.py              # 串口读取调试工具
├── startup_stm32h503xx.s          # 汇编启动文件
├── STM32H503xx_FLASH.ld           # 链接脚本 (FLASH 120KB + 持久化 8KB)
├── STM32H503xx_RAM.ld             # RAM 链接脚本 (调试用)
├── stm32h503_lan9370.ioc          # STM32CubeMX 工程文件
└── README.md                      # 本文档
```

---

## 工程详解

### 1. 主程序初始化流程 (`main.c`)

```
main()
  ├── MPU_Config()                  # 内存保护单元
  ├── HAL_Init()                    # HAL 库初始化
  ├── SystemClock_Config()         # HSE 8MHz -> PLL -> SYSCLK 250MHz
  ├── MX_GPIO_Init()               # GPIO 默认配置
  ├── MX_ICACHE_Init()             # 指令缓存使能
  ├── MX_LPUART1_UART_Init()       # 调试串口 2Mbps
  ├── Shell_Init(&hlpuart1)        # * 提前初始化 UART 输出，使 printf 在后续初始化中生效
  ├── MX_SPI1_Init()               # SPI1 Mode 3 (CPOL=1,CPHA=1), ~3.9MHz (250/64)
  ├── LAN9370_Init(&hspi1)         # * 初始化 SPI + SMI + 复位 + VPHY
  ├── LAN9370_GetChipInfo()        # 读取芯片 ID
  ├── VPHY 间接访问使能             # 重试 5 次确保 VPHY 通路
  ├── T1 端口配置                   # Port1=Slave, Port2=Master, Port3/4=Slave
  ├── Port 5 RMII 配置             # XMII_CTRL0/1/2 寄存器配置
  ├── L2 成员掩码设置               # 5 端口全互通 (member=0x1F)
  ├── MAC 学习使能                  # 动态 MAC 地址学习
  ├── Shell_Init(&hlpuart1)        # * letter-shell 初始化
  └── App_Extensions_Init()        # lwIP/FreeRTOS 扩展桩（暂未使用）
```

### 2. SPI 驱动层 (`lan9370_spi.c`)

LAN9370 通过 SPI 从接口进行所有寄存器配置：

```
读帧: [CMD=0x03][ADDR_HI][ADDR_LO][DUMMY] -> [DATA]
写帧: [CMD=0x02][ADDR_HI][ADDR_LO][DATA]

参数:
  - SPI Mode 3 (CPOL=1, CPHA=1)  ← 实测 LAN9370 需 Mode 3，Mode 0 读到 0x00
  - MSB First, 8-bit 帧
  - 速度: SYSCLK (250MHz) / 64 = 3.906 MHz
  - CS: 手动 GPIO 控制 (PA1)

通过 spiprobe 命令验证:
  spiprobe mode0: id=00 00 00 00  (×)
  spiprobe mode2: id=00 49 38 08  (×)
  spiprobe mode3: id=00 93 70 10  (✓ = LAN9370 正确 ID)
```

提供的 API 接口：
- `LAN9370_SPI_ReadReg8/16/32()` -- 单寄存器读
- `LAN9370_SPI_WriteReg8/16/32()` -- 单寄存器写
- `LAN9370_SPI_ReadBurst/WriteBurst()` -- 批量读写
- `LAN9370_SPI_ReadBurstDMA/WriteBurstDMA()` -- DMA 批量传输

### 3. MIIM (MDIO Master) 驱动层 (`lan9370_driver.c`)

> **⚠️ 实测结论（2026-06）**: **LAN9370/KSZ9370 无 SPI→MDIO 桥**。
> 原先假设的寄存器 0x006C-0x006E 在 LAN9370 中不存在（均读为 0x00，写不粘）。
> Linux 内核 ksz9477 驱动对无内置 PHY 的端口（如 Port5 RMII）也直接返回模拟数据，
> 证实 KSZ9370/LAN9370 无内置 SPI→MDIO 桥。

#### LAN8720 PHY 管理路径

```
不可用路径:
  STM32 PB5/PB6 (GPIO MDIO)  -- 已从 MDIO 总线断开，不可用
  LAN9370 MIIM 寄存器         -- 0x006C-0x006E 不存在于本芯片

实际 PHY 状态获取:
  Port5 xMII 状态寄存器 (0x5030): bit3=100M, bit2=FD, bit0=RxFC
  实测值: 0x0D = 0b0000_1101 → 100M + Full Duplex + RxFC
  结论: LAN8720 已通过 RMII link, PHY addr=1 (0x5302=0x01 strapped)
```

> **GPIO SMI (`lan9370_smi.c`) 状态**：
> - STM32 PB5/PB6 已从 MDIO 总线断开，`smiread` 对 LAN8720 不可用
> - 使用 `phyread`/`phywrite`（VPHY 方式）访问内部 T1 PHY (Port1-4)

### 4. LAN9370 高层驱动 (`lan9370_driver.c`)

封装了完整的交换机管理功能。关键实现细节：

#### VPHY 间接访问
LAN9370 内置的 T1 PHY 通过 VPHY (Virtual PHY) 机制访问。需要先使能 SPI 间接访问路径：
1. 设置 `VPHY_SPECIAL_CTRL` bit4 使能 VPHY
2. 等待 `VPHY_IND_BUSY` 清除
3. 通过 `REG_VPHY_IND_ADDR__2` 设置目标端口和寄存器地址
4. 通过 `REG_VPHY_IND_DATA__2` 读写数据

#### External PHY (SMI_OUT) 访问边界
LAN9370 的 external PHY 管理由芯片自己的 SMI_OUT(MDC/MDIO) 状态机负责。
当前硬件与上游实现下，MCU **不能**通过 SPI 穿透读取 external PHY Clause-22 寄存器。
因此本工程对 LAN8720 采用 Port5 xMII 状态寄存器派生链路状态，而不是读取 PHY ID/BMCR/BMSR。

#### 端口使能
正确做法是通过 `REG_PORTn_MSTP_STATE` (0x0B04) 控制转发状态，而非直接操作 OP_CTRL 寄存器。错误的 tail-tag 位操作会导致帧格式损坏。

#### Master/Slave 读取
T1 端口的 Master/Slave 状态应从 `T1_PHY_MASTER_SLAVE_CTRL` bit11 (`T1_PHY_MS_CFG_VALUE`) 读取，而非 `T1_PHY_BASIC_CTRL` bit14（该位是 loopback 控制）。

### 5. LAN8720A PHY 驱动 (`lan8720_driver.c`)

> **实测更新 (2026-07)**: external PHY 走 LAN9370 的 SMI_OUT，由 switch 自己管理；
> MCU 不具备经 SPI 直读 external PHY 寄存器的代理接口。
> 驱动现采用 Port5 xMII 状态寄存器派生 LAN8720 的链路/速率/双工状态。

```
初始化流程（派生状态模式）:
  1. 配置 Port5 为 RMII + 100M only，设置 0x5302=0x01（PHY addr strapped = 1）
  2. 读 Port5 xMII 状态 (0x5030)
     - speed bits != NONE → link up
     - bit3 = 100M, bit2 = Full Duplex
  3. 设 s_initialized=true, s_phyAddr=1, registerAccess=false
  4. `LAN8720_GetStatus()` 之后持续从 Port5 状态派生 link/speed/duplex

启动日志示例:
   [LAN8720] Probing PHY via LAN9370 MIIM master...
   [LAN8720] MIIM probe failed. Scanning all addresses...
   [LAN8720] Port5 xMII status = 0x0D
   [LAN8720] PHY detected via Port5 xMII status
  [LAN8720] PHY addr = 1 (strapped, 0x5302=0x01)
   [LAN8720] NOTE: external PHY registers are not SPI-accessible on LAN9370
  [LAN8720] Init OK
```

### 6. Letter-shell 命令行 (`shell_port.c`)

基于开源 [letter-shell](https://github.com/NevermindZZT/letter-shell) 框架，通过 LPUART1 提供交互式控制台：

- **波特率**: 2Mbps
- **缓冲区**: 512 字节命令缓冲 + 512 字节环形接收缓冲
- **中断驱动**: UART RX 中断 -> 环形缓冲区 -> Shell 轮询处理
- **printf 重定向**: `_write()` 系统调用重定向到 UART TX

### 7. 持久化存储 (`lan9370_persist.c`)

使用 MCU Flash 最后一个 8KB 扇区存储配置：

```
Flash 布局:
┌──────────────────────────┐ 0x08000000
│  固件代码 (120KB)         │
│                          │
├──────────────────────────┤ 0x0801E000
│  持久化区域 (8KB)         │ <- PERSIST_FLASH_BASE
│  ┌──────────────────────┐│
│  │ PersistBlob_t        ││
│  │ - magic: 0x4C394337  ││  ('L9C7')
│  │ - version: 1         ││
│  │ - payloadSize        ││
│  │ - crc32              ││
│  │ - settings           ││
│  └──────────────────────┘│
└──────────────────────────┘ 0x08020000
```

- 链接脚本 `STM32H503xx_FLASH.ld` 限制 FLASH=120KB，保留最后 8KB
- 写入前自动擦除扇区
- CRC32 校验保证数据完整性
- 通过 Shell `config save|load|show|erase` 管理

---

## 构建和烧录

### 依赖工具

| 工具                  | 要求       | 下载                                                        |
|----------------------|-----------|-------------------------------------------------------------|
| CMake                | >= 3.22   | https://cmake.org                                           |
| Ninja                | 最新即可   | https://ninja-build.org                                     |
| arm-none-eabi-gcc    | 10.3+     | https://developer.arm.com (或 CubeIDE 内置)                   |
| STM32CubeProgrammer  | 2.x       | https://www.st.com                                          |

### 快速上手

```powershell
# 进入工程目录
cd stm32h503_lan9370_lan8720

# Release 构建 + 烧录
.\build.ps1 flash Release

# Debug 构建（含调试符号）
.\build.ps1 build Debug

# 清理构建
.\build.ps1 distclean Release

# 查看帮助
.\build.ps1 help
```

### build.ps1 命令参考

| 命令       | 说明                       |
|-----------|---------------------------|
| `build`   | 构建工程（默认 Debug）       |
| `rebuild` | 清理后重新构建              |
| `clean`   | 清理编译中间文件            |
| `distclean`| 删除整个构建目录            |
| `flash`   | 构建 + 烧录到 STM32         |
| `gen`     | 仅生成 CMake 构建文件        |

---

## Shell 命令参考

### 基础命令
| 命令     | 参数 | 说明                                |
|---------|------|------------------------------------|
| `help`  | -    | 显示所有命令帮助                      |
| `info`  | -    | 芯片信息 + 全端口状态 + VLAN 配置       |
| `dump`  | -    | 导出所有寄存器状态                     |
| `reset` | -    | 硬件复位 LAN9370                     |

### 端口控制
| 命令       | 参数       | 说明                        |
|-----------|-----------|----------------------------|
| `port`    | `<1-5>`   | 显示端口详细状态              |
| `master`  | `<1-4>`   | 设置 T1 端口为 Master        |
| `slave`   | `<1-4>`   | 设置 T1 端口为 Slave         |
| `enable`  | `<1-5>`   | 使能端口转发                 |
| `disable` | `<1-5>`   | 禁用端口转发                 |

### PHY 诊断
| 命令        | 参数                       | 说明                      |
|------------|---------------------------|--------------------------|
| `phyread`  | `<port 1-4> <reg 0-31>`   | 读 T1 PHY 寄存器 (VPHY)    |
| `phywrite` | `<port 1-4> <reg> <val>`  | 写 T1 PHY 寄存器           |
| `smiread`  | `<phy 0-31> <reg 0-31>`   | SMI 读 PHY 寄存器 (★ GPIO已断开) |
| `smiwrite` | `<phy 0-31> <reg> <val>`  | SMI 写 PHY 寄存器 (★ GPIO已断开) |
| `lan8720`  | -                         | 显示 LAN8720A 链路状态（由 Port5 xMII 状态派生） |

### 高级功能
| 命令         | 参数                       | 说明                       |
|-------------|---------------------------|---------------------------|
| `vlan`      | `on|off|show`            | VLAN 使能/状态              |
| `vlan`      | `set <1-5> <1-4094>`      | 设置端口默认 VLAN ID         |
| `mirror`    | `<src> <dst|off>`        | 端口镜像配置                 |
| `ptp`       | `on|off|status`          | PTP 配置                    |
| `ptp`       | `gptp on|off|status`     | gPTP 配置                   |
| `staticmac` | `list|flush`              | 静态 MAC 表管理              |
| `mib`       | `<1-5>`                   | 端口 MIB 统计                |
| `config`    | `save|load|show|erase`  | 持久化配置管理               |

### 低级调试
| 命令        | 参数                       | 说明                       |
|------------|---------------------------|---------------------------|
| `spiread`  | `<addr> [count]`          | 直接 SPI 读寄存器            |
| `spiwrite` | `<addr> <val>`            | 直接 SPI 写寄存器            |
| `miimscan` | -                         | 提示：LAN9370 不支持经 SPI 扫描 external PHY 寄存器 |
| `diagbus`  | -                         | SPI/SMI 总线诊断             |
| `rstprobe` | `[rounds]`                | 复位时序探测                  |
| `spiprobe` | -                         | SPI 模式扫描 (CPOL/CPHA)，确认 Mode 3 |
| `spispeed` | `<prescaler>`             | 动态调整 SPI 速度             |
| `sysreset` | -                         | MCU 软复位 (用于捕获完整启动日志) |

---

## 注意事项 & 踩坑记录

### 硬件相关

1. **CRSDV / REFCLK 接线**
   - CRS_DV 是 RMII 最关键的信号之一，接错会导致 LAN8720 无法建立链路
   - REFCLK 必须由外部 50MHz 有源晶振供给 LAN9370 Port5（配置为 REFCLK input），同时供给 LAN8720A
   - 排查时先检查这两根线的连通性

2. **MDIO 拓扑（重要变更 + 实测结论）**
   - **当前接线**: LAN9370 MDC/MDIO 直连 LAN8720A MDC/MDIO。STM32 PB5/PB6 已断开。
   - **实测结论**: **LAN9370/KSZ9370 无内置 SPI→MDIO 桥**，寄存器 0x006C-0x006E 不存在于本芯片。
   - LAN8720 PHY 寄存器无法通过 SPI 读取，PHY ID 未知 (0x00000000)
   - **PHY addr = 1**（PHYAD0 strapped，通过 Port5 xMII ctrl2 寄存器 0x5302=0x01 确认）
   - **LAN8720 存在性通过 Port5 RMII 链路状态确认**：Port5 status 0x0D = 100M Full Duplex
   - Shell `lan8720` 命令显示 `addr=1, PHY ID=0x00000000` 和 Port5 状态
   - Shell `smiread`/`smiwrite` 命令无法访问外部 PHY（无 GPIO 直连）
   - 使用 `phyread`/`phywrite`（VPHY 方式）访问内部 T1 PHY (Port1-4)

3. **100BASE-T1 Master/Slave 配对**
   - 链路两端必须一端 Master + 一端 Slave
   - 如果两边都是 Slave 或都是 Master，PHY 会一直处于 Link Down 状态
   - 通过 Shell `port <n>` 命令查看 T1_PHY_MS_CFG_VALUE 确认模式

### 软件相关

11. **VPHY 间接访问使能**
   - 芯片复位后必须重新使能 VPHY 间接访问（`VPHY_SPECIAL_CTRL` bit4）
   - 复位后首次写入可能失败，需要重试（当前代码重试 5 次）

12. **端口使能不要用 OP_CTRL 寄存器**
   - `REG_PORTn_OP_CTRL0` (0x0020) 的 bit2 是 `PORT_TAIL_TAG_ENABLE`，不是 TX/RX enable
   - 错误地设置 tail-tag 会导致帧格式损坏，网络不通
   - 正确做法：使用 `REG_PORTn_MSTP_STATE` (0x0B04) 控制端口转发状态

13. **Master/Slave 状态读取**
   - `T1_PHY_BASIC_CTRL` bit14 是 loopback 控制位，不是 Master/Slave 状态
   - 正确读取：`T1_PHY_MASTER_SLAVE_CTRL` bit11 (`T1_PHY_MS_CFG_VALUE`)

14. **寄存器字节序**
   - LAN9370 寄存器为大端序（big-endian），16位寄存器的 MSB 在低地址
   - 使用 WriteReg8 按字节操作比 WriteReg16 更可靠

15. **SMI 操作中断保护（旧版 GPIO SMI）**
   - SMI 是软件模拟的时序协议，操作期间必须关闭全局中断
   - 新版 MIIM Master 方式由 LAN9370 硬件控制时序，无此限制

16. **持久化 Flash 区域保护**
   - 链接脚本 FLASH 限制为 120KB（而非完整的 128KB），保留最后 8KB
   - 固件超过 120KB 会导致持久化数据被覆盖
   - 当前固件约 45KB，有充足余量

17. **LAN8720A 4B5B 编码**
    - LAN8720A 的 SCSR 寄存器 bit6 必须写 1，否则在某些模式下可能不正常工作
    - 这是 LAN8720A 特有的要求，其他 PHY 可能不需要

### 调试技巧

- **链路不通时**：先用 `info` 命令查看所有端口状态，确认 PHY link 是否 UP
- **SPI 通信异常时**：用 `diagbus` 快速诊断 SPI/SMI 总线状态
- **不确定 SPI 模式时**：用 `spiprobe` 扫描所有 CPOL/CPHA 组合
- **怀疑复位时序时**：用 `rstprobe` 多次复位并读取芯片 ID 验证时序余量
- **跟踪网络转发问题时**：用 `mib <port>` 查看端口收发计数，判断丢包位置
- **保存工作配置**：调试完成后用 `config save` 保存当前配置到 Flash

---

## 参考资料

- [LAN9370 数据手册](https://www.microchip.com/en-us/product/LAN9370)
- [LAN8720A 数据手册](https://www.microchip.com/en-us/product/LAN8720A)
- [IEEE 802.3 Clause 22 - SMI/MDIO 协议](https://standards.ieee.org/)
- [RMII 规范 v1.2](https://www.rmii.org/)
- [letter-shell 开源项目](https://github.com/NevermindZZT/letter-shell)
- [STM32H503 参考手册 (RM0492)](https://www.st.com)
- Linux 内核 LAN9370 驱动: `drivers/net/dsa/microchip/ksz9477_spi.c`
- CycloneTCP LAN9370 驱动: `cyclone_tcp/drivers/switch/lan937x_driver.c`

---

## 许可证

- STM32 HAL 驱动代码版权归 STMicroelectronics 所有
- LAN9370/LAN8720 驱动代码基于数据手册独立实现
- letter-shell 组件使用 MIT 许可证
- 其余代码采用项目内部许可
