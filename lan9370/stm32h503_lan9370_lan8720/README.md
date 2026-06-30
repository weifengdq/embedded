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
- **LAN9370 MIIM (MDIO Master)**：通过 SPI 控制 LAN9370 内部 MDIO 主控，管理外部 PHY
- **VPHY 间接访问**：通过 SPI 访问 T1 PHY 内部寄存器
- **LAN8720A PHY 驱动**：通过 LAN9370 MIIM Master 自动探测和初始化外部 RMII PHY
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
    LAN9370 内部 MDIO Master 通过其 MDC/MDIO 引脚直接管理 LAN8720A。
    STM32 通过 SPI 寄存器 (0x006C-0x006E) 控制 MDIO 读写。
```

### STM32H503 引脚分配

| 功能         | 引脚 | AF   | 连接目标            | 说明                       |
|-------------|------|------|--------------------|---------------------------|
| **HSE 晶振** | -    | -    | 8MHz 外部晶振        | PLL → SYSCLK 250MHz       |
| **SWD 调试** | -    | -    | STLINK-V3MINIE     | 下载和调试                  |
| **LPUART1** | PA9  | AF3  | TX → 串口工具(COM80) | 2Mbps, Shell 交互          |
|             | PA10 | AF3  | RX ← 串口工具        |                           |
| **SPI1**    | PA1  | -    | CS → LAN9370 SPI_CS | 软件控制 CS（GPIO 输出）     |
|             | PA2  | AF4  | SCK → LAN9370       | SPI Mode 0                |
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
| SPI 寄存器访问  | OK   | Mode 0, ~3.9MHz, 8/16/32 位读写均验证通过              |
| 芯片识别       | OK   | Chip ID: 0x00937010, Revision ID 正常                 |
| VPHY 间接访问  | OK   | 通过 VPHY_SPECIAL_CTRL (0x077C) bit4 使能              |
| T1 PHY 寄存器  | OK   | 4 个 T1 端口的 PHY 寄存器均能正确读写                    |
| Master/Slave  | OK   | T1_PHY_MASTER_SLAVE_CTRL bit11 控制                     |
| 端口使能/禁用  | OK   | 通过 REG_PORTn_MSTP_STATE (0x0B04) 控制                 |
| L2 转发       | OK   | L2 membership mask + MAC learning 实现全端口互通        |
| Port 5 RMII   | OK   | XMII_CTRL0/1 正确配置，REFCLK input, 100M only         |
| MDIO Master   | OK   | Port5 内部 MDIO master 管理外部 LAN8720A               |
| VLAN          | OK   | Port-based VLAN 配置和查询                             |
| 端口镜像       | OK   | 支持任意端口间流量镜像                                   |
| PTP/gPTP      | WIP  | 寄存器控制位已找到，功能待验证                             |
| MIB 计数器    | OK   | 支持读取各端口收发统计                                   |

### LAN8720A 适配

| 项目           | 状态 | 说明                                                |
|---------------|------|----------------------------------------------------|
| PHY 自动探测   | OK   | 扫描地址 0-31，通过 PHYID (0x0007C0xx) 识别            |
| 软复位        | OK   | BMCR bit15 复位，等待 <= 600ms 完成                     |
| RMII 模式      | OK   | SM register bit14 = 1                                |
| 4B5B 编码      | OK   | SCSR register bit6 = 1（LAN8720A 必须）               |
| 自动协商       | OK   | 广播 10/100 FD/HD，等待 AN complete                   |
| 链路状态       | OK   | BMSR bit2 实时读取                                    |
| 共享 MDIO 总线 | OK   | 与 LAN9370 内置 PHY 共用 PB5/PB6，地址隔离              |

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
  ├── MX_SPI1_Init()               # SPI1 Mode 2, ~3.9MHz
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
  - SPI Mode 0 (CPOL=0, CPHA=0)
  - MSB First, 8-bit 帧
  - 速度: SYSCLK / 64 ~ 3.9MHz (最大 50MHz)
  - CS: 手动 GPIO 控制 (PA1)
```

提供的 API 接口：
- `LAN9370_SPI_ReadReg8/16/32()` -- 单寄存器读
- `LAN9370_SPI_WriteReg8/16/32()` -- 单寄存器写
- `LAN9370_SPI_ReadBurst/WriteBurst()` -- 批量读写
- `LAN9370_SPI_ReadBurstDMA/WriteBurstDMA()` -- DMA 批量传输

### 3. MIIM (MDIO Master) 驱动层 (`lan9370_driver.c`)

LAN9370 内部集成了 MDIO 主控器 (MIIM Master)，可直接管理外部 PHY。
STM32 通过 SPI 寄存器 (0x006C-0x006E) 控制 MDIO 读写，不再需要 GPIO 模拟。

```
MIIM 寄存器 (通过 SPI 访问):
  REG_MIIM_CTRL      (0x006C): bit4=READ, bit3=WRITE, bit0=BUSY
  REG_MIIM_PHY_ADDR  (0x006D): [12:8]=PHYAD, [4:0]=REGAD
  REG_MIIM_IND_DATA  (0x006E): [15:0]=读写数据

访问路径: STM32 → SPI → LAN9370 MIIM Master → MDC/MDIO pins → LAN8720A
```

> **GPIO SMI (`lan9370_smi.c`) 状态**：
> - 保留代码用于内部 T1 PHY 的 SMI 诊断（通过 VPHY 是主要路径）
> - STM32 PB5/PB6 已从 MDIO 总线断开，不能直接访问外部 PHY
> - Shell `smiread`/`smiwrite` 命令对 LAN8720 不可用（无 GPIO 连接）
> - 使用 `phyread`/`phywrite`（VPHY 方式）访问内部 T1 PHY

### 4. LAN9370 高层驱动 (`lan9370_driver.c`)

封装了完整的交换机管理功能。关键实现细节：

#### VPHY 间接访问
LAN9370 内置的 T1 PHY 通过 VPHY (Virtual PHY) 机制访问。需要先使能 SPI 间接访问路径：
1. 设置 `VPHY_SPECIAL_CTRL` bit4 使能 VPHY
2. 等待 `VPHY_IND_BUSY` 清除
3. 通过 `REG_VPHY_IND_ADDR__2` 设置目标端口和寄存器地址
4. 通过 `REG_VPHY_IND_DATA__2` 读写数据

#### MIIM (MDIO Master) 外部 PHY 访问
通过 LAN9370 内部 MDIO 主控器访问外部 PHY：
1. `LAN9370_MIIM_Read(phyAddr, regAddr, &data)` -- 通过 SPI 启动 MDIO 读
2. `LAN9370_MIIM_Write(phyAddr, regAddr, data)` -- 通过 SPI 启动 MDIO 写
3. LAN9370 硬件自动完成 MDIO 帧的时序控制

#### 端口使能
正确做法是通过 `REG_PORTn_MSTP_STATE` (0x0B04) 控制转发状态，而非直接操作 OP_CTRL 寄存器。错误的 tail-tag 位操作会导致帧格式损坏。

#### Master/Slave 读取
T1 端口的 Master/Slave 状态应从 `T1_PHY_MASTER_SLAVE_CTRL` bit11 (`T1_PHY_MS_CFG_VALUE`) 读取，而非 `T1_PHY_BASIC_CTRL` bit14（该位是 loopback 控制）。

### 5. LAN8720A PHY 驱动 (`lan8720_driver.c`)

外部 PHY 驱动，通过 **LAN9370 MIIM Master (SPI)** 管理 LAN8720A（不再通过 GPIO SMI）：

```
初始化流程 (通过 LAN9370 MIIM Master):
  1. 探测 PHY 地址（优先 addr=1, 然后 addr=0, 最后扫描 0-31）
  2. 验证 PHY ID (OUI=0x0007C0)
  3. 软复位 (BMCR bit15) -> 等待完成 (<=600ms)
  4. 配置 RMII 模式 (SM bit14=1)
  5. 使能 4B5B 编码 (SCSR bit6=1, LAN8720A 必须)
  6. 广播能力 (ANAR: 100FD/100HD/10FD/10HD)
  7. 启动自动协商 (BMCR bit12=1, bit9=1)
  8. 等待 AN 完成 (<=5s)
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
| `lan8720`  | -                         | 显示 LAN8720A PHY 状态 (MIIM)   |

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
| `diagbus`  | -                         | SPI/SMI 总线诊断             |
| `rstprobe` | `[rounds]`                | 复位时序探测                  |
| `spiprobe` | -                         | SPI 模式扫描 (CPOL/CPHA)     |
| `spispeed` | `<prescaler>`             | 动态调整 SPI 速度             |

---

## 注意事项 & 踩坑记录

### 硬件相关

1. **CRSDV / REFCLK 接线**
   - CRS_DV 是 RMII 最关键的信号之一，接错会导致 LAN8720 无法建立链路
   - REFCLK 必须由外部 50MHz 有源晶振供给 LAN9370 Port5（配置为 REFCLK input），同时供给 LAN8720A
   - 排查时先检查这两根线的连通性

2. **MDIO 拓扑（重要变更）**
   - **当前接线**: LAN9370 MDC/MDIO 直连 LAN8720A MDC/MDIO。STM32 PB5/PB6 已断开。
   - 外部 PHY 访问路径: STM32 → SPI → LAN9370 MIIM Master → MDC/MDIO pins → LAN8720A
   - Shell `smiread`/`smiwrite` 命令无法访问外部 PHY（无 GPIO 直连）
   - 使用 Shell `lan8720` 命令查看外部 PHY 状态（通过 MIIM Master）
   - LAN9370 Port5 XMII_CTRL2=0x01 使能 MDIO Master（PHY addr=1），硬件自动维护链路

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
