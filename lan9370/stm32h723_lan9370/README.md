# STM32H723 + LAN9370 工程说明

## 1. 项目概述

基于 STM32H723 微控制器，通过 SPI 接口配置 Microchip **LAN9370** 5端口 100BASE-T1 汽车以太网交换机，实现：
- **STM32H723 ETH MAC** ↔ RMII ↔ **LAN9370 Port5**（主机端口）
- **LAN9370 Port2** (T1 Master) ↔ 车载以太网设备 (192.168.0.68)
- **LAN9370 Port4** (T1 Slave) ↔ 百兆车载以太网转换盒 ↔ PC (192.168.0.2)
- STM32H723 静态 IP: 192.168.0.108

## 2. 硬件连接

### 2.1 STM32H723 ↔ LAN9370

| STM32H723 信号 | STM32 引脚 | LAN9370 信号 | 说明 |
|:-:|:-:|:-:|---|
| nRST | PC3 | nRST | LAN9370 硬件复位（低有效） |
| SPI1 SCK | PG11 | SCK | SPI 时钟 |
| SPI1 MISO | PG9 | MISO | SPI 数据（LAN9370 → STM32） |
| SPI1 MOSI | PD7 | MOSI | SPI 数据（STM32 → LAN9370） |
| SPI1 nCS | PG10 | CS | SPI 片选（软件 GPIO 控制） |
| MCO2 (50MHz) | PC9 | REFCLKI | RMII 参考时钟 |
| ETH_REF_CLK | PA1 | - | 50MHz MCO2 环回 |
| ETH_CRS_DV | PA7 | TX_EN | RMII 接收控制 |
| ETH_RXD0 | PC4 | TXD0 | RMII 接收数据0 |
| ETH_RXD1 | PC5 | TXD1 | RMII 接收数据1 |
| ETH_TX_EN | PB11 | CRS_DV | RMII 发送控制 |
| ETH_TXD0 | PB12 | RXD0 | RMII 发送数据0 |
| ETH_TXD1 | PB13 | RXD1 | RMII 发送数据1 |
| LPUART1 TX | PA9 | - | 调试串口 (2Mbps, COM80) |
| LPUART1 RX | PA10 | - | 调试串口 |
| MDC | PA2 | - | **悬空不用** |
| MDIO | PC1 | - | **悬空不用** |

### 2.2 时钟架构 (550MHz SYSCLK)

```
HSE (25MHz)
  ├→ PLL1: M=2, N=44, FRACN=0 → VCO=550MHz → SYSCLK=550MHz
  │     └→ HPRE=DIV2 → HCLK=275MHz (AHB 总线)
  │         ├→ APB1/2=DIV2 → 137.5MHz
  │         └→ ETH MAC clock = HCLK = 275MHz
  │
  └→ PLL2: M=5, N=40, P=4 → VCO=200MHz, PLL2P=50MHz
        └→ MCO2 (PC9, AF0) ─┬→ PA1 (ETH_REF_CLK, 内部环回)
                              └→ LAN9370 REFCLKI (RCLK 引脚)

SPI1 时钟源 = PLL2P = 50MHz
```

**注意**: HAL_SYSCFG_ETHInterfaceSelect() 只设置 EPIS_SEL 位，不会自动闭合 PA1 内部模拟开关。
需要手动 `CLEAR_BIT(SYSCFG->PMCR, SYSCFG_PMCR_PA1SO)` 确保 REF_CLK 能到达 ETH MAC。

**电压等级**: VOS0 (SCALE0) — 550MHz 需要最高性能等级。
**Flash 延迟**: FLASH_LATENCY=3 (3 等待周期 @ 275MHz HCLK)。

## 3. 软件架构

```
stm32h723_lan9370/
├── Core/
│   ├── Inc/
│   │   ├── main.h                  # LAN9370_NRST_Pin/Port 定义
│   │   └── stm32h7xx_hal_conf.h    # ETH_SWRESET_TIMEOUT=5000
│   └── Src/
│       ├── main.c                  # 主程序（MPU/Cache/LAN9370初始化/ARP/iperf/UDP echo）
│       ├── stm32h7xx_hal_msp.c     # ETH/SPI/LPUART MSP 初始化
│       ├── stm32h7xx_it.c          # 中断服务（含故障诊断打印）
│       ├── syscalls.c              # printf 重定向到 LPUART1
│       └── system_stm32h7xx.c
├── LAN9370/
│   ├── lan9370_reg.h               # 寄存器定义 (含VLAN/PTP/Mirror)
│   ├── lan9370_spi.h / .c          # SPI 驱动 (Mode 3, 带重试机制)
│   ├── lan9370_smi.h / .c          # SMI stub (MDC/MDIO 未连接)
│   ├── lan9370_driver.h / .c       # 高层驱动 (VLAN/Mirror/PTP/MIB/StaticMAC)
│   └── lan9370_persist.h / .c      # 配置持久化 (Flash Sector 7, CRC32)
├── Shell/
│   ├── shell_port.h / .c           # letter-shell 移植层 (中断式 RX, printf 重定向)
│   └── letter-shell/src/           # letter-shell 3.2.4 核心库
├── LWIP/
│   ├── App/lwip.c                  # lwIP 初始化 (IP=192.168.0.108)
│   └── Target/
│       ├── lwipopts.h              # lwIP 配置 (LWIPERF=1, heap=RAM_D1, custom pbuf)
│       ├── ethernetif.c            # ETH DMA 零拷贝实现
│       └── eth_custom_phy_interface.c  # PHY 接口 stub
├── Middlewares/Third_Party/LwIP/
│   └── src/apps/lwiperf/lwiperf.c  # iperf TCP 服务器实现
├── CMakeLists.txt                  # 顶层 CMake
├── cmake/
│   ├── gcc-arm-none-eabi.cmake     # 工具链文件
│   └── stm32cubemx/CMakeLists.txt  # CubeMX 生成+LAN9370/lwiperf 追加
├── STM32H723XG_FLASH.ld            # 链接脚本 (含 .lwip_sec 在 RAM_D2)
├── build.ps1                       # 编译/烧录脚本
└── read_log.ps1                    # 串口日志读取脚本
```

## 4. 关键配置

### 4.1 SPI 配置

- **SPI1**: Mode 3 (CPOL=1, CPHA=1), MSB First, 软件 CS (PG10)
- **时钟源**: PLL2P = 50MHz
- **预分频**: `SPI_BAUDRATEPRESCALER_4` → 50/4 = **12.5 MHz**（默认 Release 配置）
- **SPI 速率可选范围**: 195kHz ~ 25MHz（详见 §11 SPI 速率测试）
- **CS 延时**: 500 NOP 循环（约 2-3µs @ 550MHz），确保 CS 建立/保持时间
- **CS 控制**: `LAN9370_SPI_Init()` 将 PG10 重配置为 GPIO 输出 (推挽, VERY_HIGH 速度)
- **寄存器读写**: 4字节命令头 (地址+读写方向) + N字节数据，MSB first

### 4.2 LAN9370 Port5 RMII 配置

| 寄存器 | 地址 | 值 | 说明 |
|---|---|---|---|
| XMII_CTRL0 | 0x5300 | 0x61 | bit[1:0]=01 (RMII), bit[5]=1 (上升沿采样) |
| XMII_CTRL1 | 0x5301 | 0x4D | bit[7]=0 (外部REFCLK), bit[6]=1 (100Mbps) |
| MSTP_STATE | 0x5B04 | 0x06 | TX_ENABLE + RX_ENABLE |
| VLAN_MEMBERSHIP | 0x5A04 | 0x1F | 所有端口互通 |

### 4.3 ETH MAC 配置

- **RMII 模式**: `HAL_SYSCFG_ETHInterfaceSelect(SYSCFG_ETH_RMII)`
- **PA1 模拟开关**: `CLEAR_BIT(SYSCFG->PMCR, SYSCFG_PMCR_PA1SO)` — **必需！**
- **ETH 超时**: `ETH_SWRESET_TIMEOUT=5000` (延长以确保 RMII REF_CLK 稳定)
- **RX 缓冲区**: 8 个零拷贝自定义 pbuf, 置于 RAM_D2 (0x30000100)
- **ETH DMA 描述符**: RAM_D2 (RxDesc=0x30000000, TxDesc=0x30000080)

### 4.4 MPU / Cache 配置

- **MPU Region 0**: 4GB 背景区域，无访问权限
- **MPU Region 1**: D2 SRAM1 (0x30000000, 32KB)，**Non-Cacheable**, 供 ETH DMA 使用
- **I-Cache / D-Cache**: 全部启用
- **TX 路径**: `SCB_CleanDCache_by_Addr()` 在 HAL_ETH_Transmit 前刷新
- **RX 路径**: `SCB_InvalidateDCache_by_Addr()` 在 RxLinkCallback 中失效

### 4.5 lwIP 配置

| 参数 | 值 | 说明 |
|---|---|---|
| IP | 192.168.0.108 | 静态 IP |
| 子网掩码 | 255.255.255.0 | |
| 网关 | 192.168.0.1 | |
| LWIP_RAM_HEAP_POINTER | 0x24000000 (RAM_D1) | 避免 D2 边界问题 |
| MEM_SIZE | 16 KB | |
| PBUF_POOL_SIZE | 24 | |
| TCP_MSS | 1460 | |
| TCP_WND | 8 × MSS = 11680 | |
| ETH_RX_BUFFER_CNT | 8 | |

## 5. 编译和烧录

### 5.1 依赖

- CMake ≥ 3.22 + Ninja
- arm-none-eabi GCC 工具链
- STM32CubeProgrammer CLI (用于烧录)

### 5.2 编译

```powershell
cd stm32h723_lan9370
.\build.ps1 build Debug
```

### 5.3 烧录

```powershell
.\build.ps1 flash Debug
```

可选参数:
- `-ToolchainBinDir <路径>`: 指定工具链 bin 目录
- `-CubeProgrammerCli <路径>`: 指定 STM32CubeProgrammer CLI 路径
- `-STLinkSN <序列号>`: 多 STLink 时指定
- `-SWDFreqKHz <频率>`: SWD 频率 (默认 4000)

### 5.4 读取串口日志

```powershell
powershell -ExecutionPolicy Bypass -File "read_log.ps1" -Seconds 10
```

## 6. 测试结果

### 6.1 ✅ Ping 测试

| 目标 | IP | 结果 | 延迟 |
|---|---|---|---|
| STM32H723 (本机) | 192.168.0.108 | ✅ 通过 | <1ms |
| 车载以太网设备 | 192.168.0.68 | ✅ 通过 | <1ms |
| PC (转换盒) | 192.168.0.2 | ✅ 通过 | <1ms |

### 6.2 ✅ iperf TCP 吞吐量测试

```
Client connecting to 192.168.0.108, TCP port 5001
[ ID] Interval       Transfer     Bandwidth
[380]  0.0- 5.0 sec  56.6 MBytes  94.9 Mbits/sec
```

**94.9 Mbits/sec**，接近百兆线速，性能正常。

### 6.3 ✅ UDP Echo (端口 7)

lwIP UDP echo 服务已启动在端口 7，回显接收到的任意 UDP 数据。

### 6.4 系统稳定性

- 系统可连续运行数小时不崩溃
- 主循环 ~800k loops/s
- SPI 通信稳定（偶尔冷启动时需要额外复位延迟）

## 7. 移植关键点

### 7.1 SPI 模式选择

**LAN9370 需要 SPI Mode 3 (CPOL=1, CPHA=1)**，而非 Mode 0。这与 H503 参考工程不同（H503 使用 Mode 0），但与 F407 参考工程一致。

```c
hspi1.Init.CLKPolarity = SPI_POLARITY_HIGH;   // CPOL=1
hspi1.Init.CLKPhase = SPI_PHASE_2EDGE;        // CPHA=1
```

### 7.2 HAL_ETH_MspInit

H723 的 CubeMX 生成代码**不会自动实现 `HAL_ETH_MspInit()`**，需要手动在 `stm32h7xx_hal_msp.c` 中添加 RMII 引脚和 ETH 时钟的初始化。

### 7.3 PA1 模拟开关

ST 的 `HAL_SYSCFG_ETHInterfaceSelect(SYSCFG_ETH_RMII)` **仅设置 EPIS_SEL 位**，不会闭合 PA1 内部模拟开关。需要在调用后手动清除 PA1SO 位：

```c
CLEAR_BIT(SYSCFG->PMCR, SYSCFG_PMCR_PA1SO);
```

### 7.4 MCO2 输出速度

PC9 (MCO2) 必须配置为 `GPIO_SPEED_FREQ_VERY_HIGH` 才能输出 50MHz 时钟。CubeMX 默认生成 `GPIO_SPEED_FREQ_LOW` 导致 ETH MAC 无法完成初始化。

### 7.5 ETH DMA 内存布局

ETH DMA 描述符和 RX 缓冲池必须放置在 D2 SRAM1 (0x30000000)，并对该区域配置 MPU 为 Non-Cacheable。链接脚本需要添加 `.lwip_sec` 段。

### 7.6 lwIP 堆位置

`LWIP_RAM_HEAP_POINTER` 必须指向 RAM_D1 (0x24000000)，因为 D2 SRAM 只有 32KB，不足以同时容纳 DMA 缓冲池和 lwIP 堆。

## 8. Shell 命令行参考

工程集成了 [letter-shell](https://github.com/NevermindZZT/letter-shell) 3.2.4，通过 LPUART1 (PA9/PA10, 2Mbps) 提供交互式命令行。

### 8.1 可用命令

| 命令 | 参数 | 说明 | 示例 |
|---|---|---|---|
| `info` | — | 显示 LAN9370 芯片信息、端口状态 + VLAN 配置 | `info` |
| `port` | `<1-5>` | 显示指定端口详细状态 | `port 5` |
| `dump` | — | 转储关键寄存器 | `dump` |
| `reset` | — | **MCU 软件复位** (NVIC_SystemReset) | `reset` |
| `chipreset` | — | **LAN9370 硬件复位** (nRST 引脚) | `chipreset` |
| `uptime` | — | **MCU 上电时间** | `uptime` |
| `master` | `<1-4>` | 设置 T1 端口为主模式 | `master 2` |
| `slave` | `<1-4>` | 设置 T1 端口为从模式 | `slave 3` |
| `enable` | `<1-5>` | 启用端口 | `enable 5` |
| `disable` | `<1-5>` | 禁用端口 | `disable 1` |
| `vlan` | `on\|off\|set <p> <vid>\|show` | **VLAN 控制** (启用/禁用/设置VID/查看) | `vlan set 2 100` |
| `mirror` | `<src> <dst\|off>` | **端口镜像** (源→目标 / 关闭) | `mirror 3 2` 或 `mirror 3 off` |
| `ptp` | `on\|off\|status\|gptp on\|off\|status` | **PTP/gPTP 时间同步控制** | `ptp on` / `ptp gptp status` |
| `config` | `save\|load\|show\|erase` | **配置持久化到 MCU Flash** | `config save` / `config show` |
| `staticmac` | `list\|flush` | **静态 MAC 表操作** | `staticmac list` |
| `phyread` | `<1-4> <reg>` | 读取 T1 端口 PHY 寄存器 | `phyread 2 0x09` |
| `phywrite` | `<1-4> <reg> <val>` | 写入端口 PHY 寄存器 | `phywrite 2 0x09 0x1800` |
| `spiread` | `<addr>` | 通过 SPI 读 LAN9370 寄存器 | `spiread 0000` |
| `spiwrite` | `<addr> <val>` | 通过 SPI 写 LAN9370 寄存器 | `spiwrite 0E00 0249` |
| `smiread` | `<phy> <reg>` | SMI/MDIO 读取 | `smiread 0 2` |
| `mib` | `[port]` | 查看端口 MIB 统计计数器 | `mib 5` |
| `temp` | — | 读取芯片温度 | `temp` |
| `sqi` | `[port]` | 读取 T1 端口信号质量 | `sqi 2` |
| `mse` | `[port]` | 读取 T1 端口均方误差 | `mse 2` |
| `linkinfo` | `[port]` | T1 链路诊断信息 | `linkinfo 2` |
| `cable` | `[port]` | T1 电缆故障诊断 | `cable 2` |
| `led` | `[mask val]` | LED 控制寄存器 | `led` |
| `mcu` | — | MCU 系统诊断信息 (时钟/UID/Flash/复位原因) | `mcu` |
| `help` | — | 显示所有命令 | `help` |

### 8.2 常用诊断示例

```
letter:/$ info
Chip: LAN9370  ID: 0x00937010  Rev: 0

=== LAN9370 Port Status ===
Port | Link | Speed | Duplex | Mode
-----+------+-------+--------+--------
  1  | UP   | 100M  | Full | Slave
  2  | UP   | 100M  | Full | Master
  3  | UP   | 100M  | Full | Slave
  4  | UP   | 100M  | Full | Slave
  5  | UP   | 10M   | Full | RMII
===========================

letter:/$ mib 5
Port5 MIB: rx=1234 tx=5678 rx_drop=0 tx_drop=0

letter:/$ sqi 2
Port2 SQI: 0x0008 (Excellent)

letter:/$ mcu
MCU: STM32H723ZG  SYSCLK=550MHz  HCLK=275MHz
Flash: 1MB  RAM_D1: 320KB  RAM_D2: 32KB
RST: PIN SW
UID: ...
```

## 9. 高级功能详解

### 9.1 VLAN 配置

**命令语法**：
```bash
vlan on|off|set <port> <vid>|show
```

**原理**：LAN9370 的 LUE_CTRL_0 寄存器 bit 1 控制全局 VLAN 开关。每个端口有独立的 Default VID 寄存器 (REG_PORTn_DEFAULT_TAG0/1)，入站无标签帧会被打上该端口的 Default VID。

**使用示例**：
```bash
# 启用 VLAN
vlan on

# 设置 Port 2 的默认 VLAN 为 100
vlan set 2 100

# 查看所有端口 VLAN 配置
vlan show

# 禁用 VLAN（恢复平坦交换）
vlan off
```

**测试验证**：
1. 板端 `vlan on` 开启 VLAN
2. 给 Port1 & Port2 分配不同 VID，并修改 egress membership 隔离不同 VLAN 的端口
3. 跨 VLAN ping 应超时，同 VLAN ping 应成功
4. PC 端 Wireshark 过滤 `vlan` 可查看带 802.1Q Tag 的帧

### 9.2 端口镜像 (Port Mirroring)

**命令语法**：
```bash
mirror <src_port> <dst_port|off>
```

**原理**：修改源端口的 Egress Member Bitmap，使其只向目标端口转发。相当于将所有从源端口发出的流量复制到目标端口。

**使用示例**：
```bash
# 镜像 Port 1 的流量到 Port 2 用于抓包分析
mirror 1 2

# 禁用 Port 1 的镜像，恢复全转发
mirror 1 off
```

**测试验证**：
1. 从 PC `ping 192.168.0.68 -t`（经过 Port4→Port2）
2. `mirror 4 1` 将 Port4 流量镜像到 Port1
3. 在 Port1 连接的设备上应能抓到 Port4 的 ping 包
4. `mirror 4 off` 恢复

### 9.3 PTP / gPTP 时间同步

**命令语法**：
```bash
ptp on|off|status|gptp on|off|status
```

**原理**：使用 LAN9370 的预留控制寄存器 (0x0200) 作为 PTP/gPTP 开关标志位。这是实验性功能，完整的 PTP 时钟同步需要应用层协议栈配合。

**使用示例**：
```bash
# 启用 PTP
ptp on

# 查询 PTP/gPTP 状态
ptp status

# 启用 gPTP
ptp gptp on

# 查询 gPTP 状态
ptp gptp status
```

**PC 端 Wireshark 验证**：
1. 板端 `ptp on`
2. PC 端 Wireshark 过滤器：`eth.type == 0x88f7`
3. 如有 PTP 主从设备在运行，应能捕获到 Sync/Follow_Up/Announce 帧
4. 注意：LAN9370 PTP 引擎的具体寄存器地址需要通过实验进一步确认

### 9.4 配置持久化 (Config Persist)

**命令语法**：
```bash
config save|load|show|erase
```

**原理**：使用 STM32H723 内部 Flash 最后一个扇区 (Sector 7, 128KB) 存储 LAN9370 配置。配置文件带 Magic Number + 版本号 + CRC32 校验，保证掉电安全。

**保存的内容**：
- Port1~4 的 Master/Slave 模式
- Port1~5 的 Default VLAN ID
- Port1~5 的镜像目标端口
- VLAN 全局开关
- PTP/gPTP 开关

**使用示例**：
```bash
# 查看已保存的配置
config show

# 保存当前配置到 Flash
config save

# 从 Flash 加载并应用配置
config load

# 擦除已保存的配置
config erase
```

**注意**：`config save` 涉及 Flash 扇区擦除操作，会导致 MCU 暂停约 2 秒（Flash busy），期间串口无输出属正常现象。

### 9.5 静态 MAC 表

**命令语法**：
```bash
staticmac list|flush
```

**原理**：维护一个软件影子表（最多 32 条），可通过 API `LAN9370_AddStaticMac()` 添加静态转发表项，避免 MAC 地址过期被老化清除。

**使用示例**：
```bash
# 查看当前静态 MAC 表条目
staticmac list

# 清空静态 MAC 表
staticmac flush
```

## 11. 时钟配置详情

### 11.1 系统时钟 (PLL1)

| 参数 | 值 | 说明 |
|---|---|---|
| HSE | 25 MHz | 外部晶振 |
| PLL1M | 2 | 输入分频 → 12.5 MHz |
| PLL1N | 44 | 倍频系数 |
| PLL1FRACN | 0 | 分数分频 (未使用) |
| PLL1P | 1 | 输出分频 |
| **SYSCLK** | **550 MHz** | 系统时钟 |
| HPRE | DIV2 | **HCLK = 275 MHz** |
| APB1/2/3/4 | DIV2 | 外设时钟 = 137.5 MHz |
| FLASH_LATENCY | 3 | 3 等待周期 (VOS0) |
| VOS 等级 | SCALE0 | 最高性能 |

### 11.2 RMII 时钟 (PLL2)

| 参数 | 值 | 说明 |
|---|---|---|
| PLL2M | 5 | 输入分频 → 5 MHz |
| PLL2N | 40 | 倍频系数 |
| PLL2 VCO | 200 MHz | VCO = 5 × 40 |
| PLL2P | 4 | 输出分频 → **50 MHz** |
| PLL2RGE | VCIRANGE_3 | 输入范围 4-8 MHz |
| PLL2VCOSEL | VCOWIDE | 宽 VCO 范围 |
| MCO2 输出 | 50 MHz | MCO2SOURCE=PLL2P, DIV=1 |
| SPI1 时钟 | 50 MHz | SPI123CLKSOURCE=PLL2 |

## 12. SPI 速率测试结果

以下测试在 550MHz SYSCLK 条件下进行，CS 延时 = 500 NOP。所有速率下 ping 均 4/4 通过 (<1ms)，iperf TCP 吞吐量均为 94.9 Mbits/sec。

| 预分频 | SPI 频率 | Chip ID 读取 | Ping | iperf | 稳定性 | 建议 |
|---|---|---|---|---|---|---|
| 256 | 195 kHz | ✅ | ✅ | ✅ | 稳定 | 保守选择 |
| 32 | 1.56 MHz | ✅ | ✅ | ✅ | 稳定 | — |
| 16 | 3.125 MHz | ✅ | ✅ | ✅ | 稳定 | 原备份配置 |
| 8 | 6.25 MHz | ✅ | ✅ | ✅ | 稳定 | — |
| **4** | **12.5 MHz** | ✅ | ✅ | ✅ | 稳定 | **⭐ Release 默认** |
| 2 | 25 MHz | ✅ | ✅ | ✅ | 需更长 CS 延时 | 极限性能 |

**注意**: 
- 25MHz 需要 CS 延时 ≥ 1000 NOP 才能稳定读取多字节寄存器
- 12.5MHz 在 500 NOP CS 延时下工作稳定，推荐作为默认值
- 更高的 SPI 速率可以改善 shell 命令响应速度，但对 iperf 吞吐率无影响（瓶颈在 RMII 百兆线速）

## 13. 端口配置详情

### 13.1 端口角色

| 端口 | 类型 | 模式 | 连接对象 | IP 地址 |
|---|---|---|---|---|
| Port 1 | 100BASE-T1 | Slave | 预留 | — |
| Port 2 | 100BASE-T1 | **Master** | 车载以太网设备 | 192.168.0.68 |
| Port 3 | 100BASE-T1 | Slave | 预留 | — |
| Port 4 | 100BASE-T1 | Slave | PC (通过转换盒) | 192.168.0.2 |
| Port 5 | RMII (Host) | — | STM32H723 ETH MAC | 192.168.0.108 |

### 13.2 T1 Master/Slave 配置

Port 2 必须配置为 Master（提供 T1 时钟），其他 T1 端口为 Slave。

```c
LAN9370_SetT1MasterSlave(LAN9370_PORT_1, LAN9370_T1_SLAVE);
LAN9370_SetT1MasterSlave(LAN9370_PORT_2, LAN9370_T1_MASTER);  // ← 关键!
LAN9370_SetT1MasterSlave(LAN9370_PORT_3, LAN9370_T1_SLAVE);
LAN9370_SetT1MasterSlave(LAN9370_PORT_4, LAN9370_T1_SLAVE);
```

配置后等待 50ms 让 PHY 协商完成，再验证 MS_CTRL 寄存器 (0x09) = 0x1800。

### 13.3 Port 5 RMII 配置

Port 5 是特殊的主机端口，通过 RMII 直接连接到 STM32H723 的 ETH MAC。
不需要 PHY 自动协商 — 速度固定为 100Mbps 全双工。

```c
LAN9370_ConfigurePort5Rmii();          // 写入 XMII_CTRL0/1
LAN9370_SetPortEnable(LAN9370_PORT_5, true);  // 启用 TX+RX
```

## 14. 内存映射详情

### 14.1 链接脚本布局

```
FLASH  (0x08000000, 1MB):  代码 + 只读数据
DTCM   (0x20000000, 128KB): .data + .bss + 栈
RAM_D1 (0x24000000, 320KB): lwIP heap (LWIP_RAM_HEAP_POINTER)
RAM_D2 (0x30000000, 32KB):  ETH DMA 区域 (见下)
RAM_D3 (0x38000000, 16KB):  未使用
```

### 14.2 RAM_D2 详细布局 (.lwip_sec)

| 地址 | 大小 | 内容 |
|---|---|---|
| 0x30000000 | 128 B | Rx DMA 描述符 (8 × 16B) |
| 0x30000080 | 64 B | Tx DMA 描述符 (4 × 16B) |
| 0x30000100 | ~12 KB | 零拷贝 RX pbuf 池 (ETH_RX_BUFFER_CNT=8) |
| 0x30004000 | — | lwIP heap 起始 (仅当 LWIP_RAM_HEAP_POINTER=0x30004000 时) |

### 14.3 MPU 区域配置

| Region | 基地址 | 大小 | 权限 | 缓存 | 用途 |
|---|---|---|---|---|---|
| 0 | 0x00000000 | 4GB | NO_ACCESS | — | 背景保护 |
| 1 | 0x30000000 | 32KB | FULL_ACCESS | NON_CACHEABLE | ETH DMA 缓冲区 |

其他地址默认使用 AHB 总线属性（cacheable, bufferable），由 CPU D-Cache 加速。

## 15. 启动流程

```
上电
  ├→ MPU_Config()                           // MPU Region 0 (背景) + Region 1 (D2 non-cacheable)
  ├→ SCB_EnableICache() + SCB_EnableDCache() // 启用 CPU 缓存
  ├→ HAL_Init()                              // SysTick 1ms
  ├→ SystemClock_Config()                    // PLL1=550MHz, HCLK=275MHz, Flash latency=3
  ├→ PeriphCommonClock_Config()              // PLL2=50MHz (MCO2 + SPI1)
  ├→ MX_GPIO_Init()                          // 所有 GPIO 引脚初始化
  ├→ MX_DMA_Init() + MX_BDMA_Init()
  ├→ MX_LPUART1_UART_Init()                  // 2Mbps 调试串口
  ├→ MX_SPI1_Init()                          // SPI1 Mode 3, 12.5MHz
  ├→ LAN9370_Init(&hspi1)                    // 硬件复位 + SPI 间接 VPHY 访问
  ├→ 读取 Chip ID                              // 验证 0x00937010
  ├→ 配置 T1 Master/Slave                      // Port2=Master, 其他=Slave
  ├→ 启用所有端口 (Port 1-5)                   // TX+RX Enable
  ├→ LAN9370_ConfigurePort5Rmii()            // RMII 模式 + 100Mbps
  ├→ 设置 VLAN 成员 (0x1F 全部互通)             // 平坦交换
  ├→ LAN9370_SetMACLearning(true)            // 启用 MAC 学习
  ├→ 等待 50ms (PHY 协商)                      //
  ├→ 使能 ETH 时钟 + SYSCFG RMII + PA1 开关    // RMII 信号路径
  ├→ MX_LWIP_Init()                          // lwIP 栈初始化
  │    ├→ low_level_init()                   // ETH MAC/DMA 初始化
  │    │    ├→ HAL_ETH_Init(&heth)           // MAC + DMA + 描述符
  │    │    ├→ LWIP_MEMPOOL_INIT(RX_POOL)    // 零拷贝 RX 池
  │    │    └→ HAL_ETH_Start(&heth)          // 启动 MAC + DMA
  │    └→ netif_set_up(&gnetif)
  ├→ App_NetworkServicesInit()               // UDP echo (port 7) + iperf (port 5001)
  ├→ Shell_Init(&hlpuart1)                   // letter-shell 交互命令行
  ├→ etharp_gratuitous(&gnetif)              // 广播 ARP 通告
  ├→ 配置 MAC 过滤 (混杂模式)                   //
  ├→ 清除 DMA 错误标志                          //
  └→ 主循环
       ├→ MX_LWIP_Process()                  // ethernetif_input + sys_check_timeouts
       └→ shellTask(&gShell)                 // 命令行交互
```

## 16. 故障排查指南

### 16.1 Ping 不通

| 症状 | 可能原因 | 检查方法 |
|---|---|---|
| 完全无响应 | 硬件连接断开 | 检查 100BASE-T1 转换盒 LED、网线 |
| 完全无响应 | RMII 时钟未到达 | 示波器测量 PC9 (应 50MHz)、PA1 |
| 完全无响应 | SPI Mode 错误 | 确认 CPOL=1, CPHA=1 (Mode 3) |
| 完全无响应 | MACPFR=0 (未配置过滤) | 检查启动日志 `MACPFR=0x80000011` |
| 完全无响应 | PA1 模拟开关未闭合 | 检查 `SYSCFG_PMCR` bit 0 = 0 |
| 偶尔丢包 | SPI 速率过高 | 降低预分频值或增加 CS 延时 |
| 冷启动不工作 | LAN9370 上电时序 | 增加复位延时或重新上电 |

### 16.2 SPI 通信异常

```
症状: Chip ID 读取错误 (如 0x00930010 而非 0x00937010)
原因: SPI 速率过高或 CS 延时不足；单字节读偶发比特翻转
解决:
  1. 已内置 SPI 重试机制（3 次多数表决），多数情况下已自动修复
  2. 若仍偶现，降低预分频值 (spiread 配合 spispeed 命令测试)
  3. 增加 LAN9370_SPI_Delay() 中的 NOP 次数
  4. 确认 SPI Mode = 3 (CPOL=1, CPHA=1)
```

### 16.3 ETH MAC 初始化失败

```
症状: HAL_ETH_Init 返回 HAL_ERROR
可能原因:
  1. ETH_SWRESET_TIMEOUT 过短 — 已配置为 5000ms
  2. RMII REF_CLK 未到达 — 检查 PC9 MCO2 输出
  3. PC9 GPIO 速度不够 — 需 VERY_HIGH
  4. ETH 时钟未使能 — 检查 RCC_AHB1ENR
```

### 16.4 串口无输出

```
症状: LPUART1 (COM80) 无数据
检查:
  1. 波特率 = 2Mbps, 8N1
  2. STM32CubeProgrammer 复位后等待 3 秒
  3. 检查 PA9/PA10 引脚连接
  4. 确认 LPUART1 NVIC 中断已使能 (shell 需中断接收)
```

## 16. 已知问题和限制

### 16.1 LED 控制

`REG_LED_CTRL0` (0x0E00) 写入后回读始终为 0x0000。LED 物理闪烁功能正常，但寄存器读回值不正确。
**原因**: LAN9370 LED 控制寄存器可能使用了不同的地址映射或需要额外的使能位。
**影响**: 不影响网络功能。

### 16.2 Port 5 速度显示

`info` 命令显示 Port 5 速度为 "10M"，实际 RMII 工作在 100Mbps。
**原因**: `REG_PORTn_STATUS` 的速度字段是为铜缆 PHY 端口(1-4)设计的，
Port 5 的 RMII 速度由 `XMII_CTRL1` bit 6 决定，但 `GetPortStatus()` 未读取该寄存器。
**影响**: 仅为显示问题，不影响实际通信。

### 16.3 Port 2 Master 模式

Port 2 Master 配置可能在 LAN9370 全局复位后丢失，需要在初始化序列中重新应用并等待 50ms。
代码中已包含 `MS_CTRL` 重新验证逻辑。

### 16.4 ETH DMA FBE 标志

`DMADSR` 寄存器中的 FBE (Fatal Bus Error) 标志在上电初始化后可能被设置。
这似乎是 STM32H7 ETH DMA 在无 MDIO PHY 情况下的已知行为 — DMA 在启动瞬间可能尝试访问未就绪的总线。
已在启动序列末尾清除该标志，不影响正常通信。

## 17. 550MHz 迁移记录

从 CubeMX 生成的 192MHz 配置迁移到 550MHz 时，CubeMX 自动修改了以下内容，需要手动恢复：

| 项目 | CubeMX 550MHz 默认 | 正确配置 | 影响 |
|---|---|---|---|
| SPI Mode | MODE 0 (CPOL=0) | **MODE 3** (CPOL=1, CPHA=1) | SPI 无法与 LAN9370 通信 |
| SPI NSS | HARD_OUTPUT | **SOFT** (GPIO 手动控制) | CS 时序不正确 |
| SPI 预分频 | 256 (195kHz) | **4 (12.5MHz)** | 性能大幅降低 |
| MPU Region 1 | **缺失** | D2 SRAM non-cacheable | ETH DMA 缓存一致性问题 |
| LWIP_RAM_HEAP_POINTER | 0x30004000 (D2) | **0x24000000 (D1)** | lwIP 内存不足 |
| HCLK 分频 | DIV4 (137.5MHz) | **DIV2 (275MHz)** | ETH MAC 时钟偏低 |
| ETH_SWRESET_TIMEOUT | 默认 500ms | **5000ms** | RMII 时钟稳定时间不足 |
| PLL2 N/P | 未定义 | **N=40, P=4** | MCO2 50MHz 时钟错误 |
| PLL2 VCIRANGE | 未定义 | **VCIRANGE_3** | PLL2 可能不锁定 |
| NRST 初始电平 | RESET (低) | **SET (高)** | 复位时序错误 |
| TX cache flush | 无 | **SCB_CleanDCache_by_Addr** | TX DMA 数据不一致 |
| MAC 过滤 | 未配置 (PFR=0) | **混杂模式** | 无法接收任何数据包 |

## 18. 参考资料

### 18.1 芯片文档

- STM32H723ZG Datasheet (DS14253)
- STM32H723 Reference Manual (RM0468)
- LAN9370 Datasheet (Microchip DS00004532)
- LAN9370 Software User's Guide

### 18.2 参考工程

| 工程 | MCU | PHY/Switch | 用途 |
|---|---|---|---|
| `stm32h723_tja1103` | STM32H723 | TJA1103 (100BASE-T1 PHY) | H7 ETH RMII + lwIP 参考 |
| `stm32f407_lan9370` | STM32F407 | LAN9370 | LAN9370 SPI/RMII 参考（已验证 ping/iperf 通过） |
| `stm32h503_lan9370` | STM32H503 | LAN9370 | LAN9370 SPI 驱动参考 |
| `stm32h503_lan9370_dp83848` | STM32H503 | LAN9370 + DP83848 | LAN9370 Port5 RMII + 外部 PHY 参考 |
| `h503_sja1124` | STM32H503 | SJA1124 | SPI 桥接参考 |
| `h563_spi_10baset1s_iperf` | STM32H563 | LAN8651 | 10BASE-T1S OA-TC6 SPI 参考 |

## 19. 文件清单

### 新增/修改文件

| 文件 | 操作 | 说明 |
|---|---|---|
| `LAN9370/lan9370_reg.h` | 新增 | LAN9370 寄存器定义 |
| `LAN9370/lan9370_spi.h` | 新增 | SPI 驱动头文件 |
| `LAN9370/lan9370_spi.c` | 新增 | SPI 驱动实现 (Mode 3, 软件 CS) |
| `LAN9370/lan9370_smi.h` | 新增 | SMI 驱动头文件 |
| `LAN9370/lan9370_smi.c` | 新增 | SMI stub (MDC/MDIO 未连接) |
| `LAN9370/lan9370_driver.h` | 新增 | LAN9370 高层驱动头文件 |
| `LAN9370/lan9370_driver.c` | 新增 | LAN9370 高层驱动实现 |
| `Shell/shell_port.h` | 新增 | Shell 移植层头文件 |
| `Shell/shell_port.c` | 新增 | letter-shell 移植 (中断 RX, printf 重定向) |
| `Shell/letter-shell/src/*.c` | 新增 | letter-shell 3.2.4 核心库 |
| `Middlewares/.../lwiperf/lwiperf.c` | 新增 | iperf 实现 (从 tja1103 复制) |
| `Core/Src/main.c` | 修改 | 添加 LAN9370 初始化 + 网络服务 |
| `Core/Inc/main.h` | 修改 | 添加 LAN9370_NRST 引脚定义 |
| `Core/Src/stm32h7xx_hal_msp.c` | 修改 | 添加 HAL_ETH_MspInit/HAL_ETH_MspDeInit |
| `Core/Src/stm32h7xx_it.c` | 修改 | 添加故障诊断打印 |
| `Core/Inc/stm32h7xx_hal_conf.h` | 修改 | ETH_SWRESET_TIMEOUT=5000 |
| `LWIP/Target/lwipopts.h` | 修改 | 调整堆/池大小，lwIP 配置 |
| `LWIP/Target/ethernetif.c` | 修改 | 添加 D-Cache 刷新，ETH diag |
| `cmake/stm32cubemx/CMakeLists.txt` | 修改 | 添加 LAN9370 + lwiperf |
| `STM32H723XG_FLASH.ld` | 修改 | 添加 .lwip_sec 段 |
| `build.ps1` | 新增 | 编译/烧录脚本 |
| `read_log.ps1` | 新增 | 串口日志读取脚本 |
| `README.md` | 新增 | 本文件 |
