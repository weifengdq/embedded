# STM32F407 + LAN8651 10BASE-T1S 工程说明

本文档面向当前工程 stm32f407_lan8651，目标是把工程整理到可发布、可复现、可维护的状态，并完整说明以下内容：

- 当前工程的功能范围、默认配置和验证结论
- STM32F407 与 LAN8651 的硬件连接、SPI 工作方式和启动流程
- lwIP、UDP Echo、TCP iperf 的默认参数与调优结果
- 构建、烧录、联调和发布建议
- 与 H563 参考工程 h563_spi_10baset1s_iperf 的详细差异对比

## 1. 工程概览

本工程是一个基于 STM32F407 与 Microchip LAN8651 的裸机 10BASE-T1S 以太网示例，网络协议栈使用 lwIP，当前发布态实现了以下能力：

- 静态 IPv4 地址通信
- ICMP Ping 应答
- UDP Echo 回显服务
- TCP iperf server
- 通过 SPI3 + GPIO 以 OPEN Alliance TC6 协议连接外部 LAN8651 10BASE-T1S MAC-PHY

当前默认参数如下。

| 项目 | 值 |
| --- | --- |
| MCU | STM32F407VETx / STM32F407xx |
| MAC-PHY | LAN8651 |
| 主机接口 | SPI3 + GPIO |
| 物理层类型 | 10BASE-T1S |
| 协议栈 | lwIP 2.2.1 |
| OS 模式 | NO_SYS=1，裸机轮询 |
| 设备 IP | 192.168.1.108 |
| 子网掩码 | 255.255.255.0 |
| 网关 | 192.168.1.1 |
| MAC 地址 | 02:00:00:10:BA:5E |
| 应用服务 | UDP Echo，端口 8；TCP iperf，端口 5008 |
| 默认发布构建 | Release |

工程关键文件如下。

| 文件 | 作用 |
| --- | --- |
| [Core/Src/main.c](Core/Src/main.c) | 系统启动、时钟与外设初始化、LAN8651 启动、lwIP/UDP/iperf 初始化 |
| [Core/Src/stm32f4xx_hal_msp.c](Core/Src/stm32f4xx_hal_msp.c) | SPI3、USART1 以及 SPI3 DMA 的 MSP 配置 |
| [Core/Src/stm32f4xx_it.c](Core/Src/stm32f4xx_it.c) | EXTI0 与 DMA1 Stream0/5 中断处理 |
| [LAN8651/lan8651.h](LAN8651/lan8651.h) | LAN8651 寄存器、TC6 位定义、设备状态结构体 |
| [LAN8651/lan8651.c](LAN8651/lan8651.c) | TC6 控制/数据事务、LAN8651 初始化、PLCA 配置、帧收发实现 |
| [lwip/port/ethernetif.c](lwip/port/ethernetif.c) | lwIP netif 适配层，负责帧收发对接 |
| [lwip/port/lwipopts.h](lwip/port/lwipopts.h) | lwIP 缓冲、TCP 窗口和资源池配置 |
| [Core/Inc/app_config.h](Core/Inc/app_config.h) | 发布态与调试态日志开关 |
| [build.ps1](build.ps1) | CMake 构建、生成 hex/bin、调用 STM32CubeProgrammer 下载 |

## 2. 当前发布状态

当前工程已经整理为可发布状态，结论如下。

- 代码默认工作点为 Release + SPI3 数据块 DMA 路径。
- `TCP_WND = 8 * TCP_MSS`、`TCP_SND_BUF = 8 * TCP_MSS`、`MEMP_NUM_TCP_SEG = 64`。
- `iperf -c 192.168.1.108 -p 5008 -t 10` 实测约 8.7 Mbit/s。
- 对 `8 MSS` 和 `16 MSS` 的实测抓包均未再出现 `tcp.analysis.retransmission`、`tcp.analysis.fast_retransmission` 或 `tcp.analysis.duplicate_ack`。
- 默认日志配置已收敛为发布态，仅保留 iperf 会话汇总日志，其他高频诊断默认关闭。

发布态 `app_config.h` 的默认策略如下。

| 开关 | 默认值 | 用途 |
| --- | --- | --- |
| `APP_LOG_NET_INIT` | 0 | 关闭启动过程详细日志 |
| `APP_LOG_PERIODIC_DIAG` | 0 | 关闭周期性状态输出 |
| `APP_LOG_UDP_ECHO` | 0 | 关闭 UDP Echo 逐包日志 |
| `APP_LOG_IPERF` | 1 | 保留 iperf 会话结果输出 |
| `LAN8651_LOG_INFO` | 0 | 关闭驱动初始化细节 |
| `LAN8651_LOG_*` 其他项 | 0 | 关闭 TC6 事务高频诊断 |
| `ETHERNETIF_LOG_FRAMES` | 0 | 关闭以太网帧级打印 |
| `ETHERNETIF_USE_RBA_GATE` | 1 | 保留 RBA 门控优化 |

## 3. 硬件连接

根据当前 F407 工程代码，硬件连接如下。

| 信号 | STM32F407 引脚 | 方向 | 说明 |
| --- | --- | --- | --- |
| USART1_TX | PA9 | MCU 输出 | 调试串口发送 |
| USART1_RX | PA10 | MCU 输入 | 调试串口接收 |
| SPI3_SCK | PC10 | MCU 输出 | SPI 时钟 |
| SPI3_MISO | PC11 | MCU 输入 | 从 LAN8651 读取数据 |
| SPI3_MOSI | PC12 | MCU 输出 | 向 LAN8651 发送数据 |
| LAN_CS_N | PA15 | MCU 输出 | 软件控制片选，低有效 |
| LAN_IRQ_N | PD0 | MCU 输入 | LAN8651 中断，低有效 |
| LAN_RESET_N | PD1 | MCU 输出 | LAN8651 复位，低有效 |

### 3.1 必须保留的硬件经验

当前 F407 板子的一个关键经验是：

- SPI3 的 `PC10/PC11/PC12` GPIO 速度必须配置为 `GPIO_SPEED_FREQ_LOW`。

如果把这些管脚配置成 `VERY_HIGH`，运行期会出现如下典型问题：

- TC6 footer/header 校验异常
- ARP 学习失败
- Ping 丢包
- iperf 吞吐断崖式下降

因此本工程把这组引脚固定为低速复用推挽，并在发布态下保留这一配置，不建议修改。

## 4. SPI3 与 LAN8651 工作方式

### 4.1 SPI3 基本模式

SPI3 的当前工作方式如下。

| 项目 | 配置 |
| --- | --- |
| 模式 | Master |
| 方向 | 2 Lines，全双工 |
| 数据宽度 | 8 bit |
| CPOL / CPHA | Mode 0 |
| NSS | Software |
| First Bit | MSB |
| 运行期分频 | `SPI_BAUDRATEPRESCALER_2` |

### 4.2 启动期与运行期分频策略

F407 工程的 SPI3 不是单一固定频率，而是采用两阶段策略：

- 启动阶段先切到 `SPI_BAUDRATEPRESCALER_256`，用于复位后、初始化早期的保守访问。
- LAN8651 初始化完成后切回 `SPI_BAUDRATEPRESCALER_2`，作为运行期工作频率。

这样做的目的不是追求复杂性，而是降低 bring-up 风险，同时保留运行期吞吐。

### 4.3 TC6 事务策略

当前工程使用 OPEN Alliance TC6 over SPI，采用“控制事务保守、数据事务提速”的混合路径：

- 12-byte 控制事务继续使用阻塞式 `HAL_SPI_TransmitReceive()`。
- 68-byte 数据块事务使用 `HAL_SPI_TransmitReceive_DMA()`。
- 片选仍然由 GPIO 手动控制，而不是交给硬件 NSS 自动管理。

这样设计的原因是：

- 控制寄存器事务长度很短，阻塞 HAL 更简单、时序更可控。
- 数据块事务是吞吐瓶颈所在，切到 DMA 后能明显降低 CPU 在主循环中的事务开销。

### 4.4 当前 DMA 方案结论

SPI3 DMA 只用于数据块事务，这是当前工程性能修复的核心结果之一。它带来了两点直接收益：

- 旧的“F407 只能把 TCP 窗口缩到 `2 MSS` 才稳定”的限制已经解除。
- 在 `8 MSS` 默认配置下，实测吞吐已稳定在约 8.7 Mbit/s。

本工程中新增的 DMA 资源如下。

| 项目 | 配置 |
| --- | --- |
| RX DMA | DMA1 Stream0 |
| TX DMA | DMA1 Stream5 |
| 触发对象 | SPI3 |
| 中断 | `DMA1_Stream0_IRQHandler()`、`DMA1_Stream5_IRQHandler()` |

## 5. 软件结构与启动流程

### 5.1 目录结构

工程由三部分组成。

- Core
  系统启动、时钟配置、UART/SPI/GPIO/DMA 初始化、网络服务创建
- LAN8651
  OPEN Alliance TC6 over SPI 驱动、MAC/PHY 初始化、帧收发
- lwip
  lwIP 核心协议栈、netif 适配层、lwiperf 应用

### 5.2 启动流程

当前主流程如下。

1. `HAL_Init()`
2. `SystemClock_Config()`，将系统时钟配置为 168 MHz
3. 初始化 GPIO、USART1、SPI3
4. 初始化 SPI3 DMA 句柄并链接到 SPI3
5. 准备 `lan8651_t` 设备结构，绑定 SPI、CS、RESET、IRQ 和 MAC 地址
6. 启动期先把 SPI3 分频切到 `/256`
7. 调用 `LAN8651_Init()` 完成硬件复位、软件复位、器件识别、默认配置、PLCA 配置和 MAC 配置
8. 调用 `LAN8651_Start()` 使能收发
9. 将 SPI3 切回运行期 `/2`
10. 调用 `Network_Init()` 完成 lwIP 初始化、静态地址配置和 netif 注册
11. 调用 `UDP_Echo_Init()` 创建 UDP 回显服务
12. 调用 `LWIPerf_Init()` 创建 TCP iperf 服务
13. 主循环中持续调用 `ethernetif_input()` 与 `sys_check_timeouts()`

## 6. 网络参数与 lwIP 配置

### 6.1 默认网络参数

| 项目 | 当前值 |
| --- | --- |
| IP | 192.168.1.108 |
| Mask | 255.255.255.0 |
| Gateway | 192.168.1.1 |
| UDP Echo 端口 | 8 |
| iperf 端口 | 5008 |

### 6.2 当前 lwIP 发布态调优

当前 F407 工程在 `lwipopts.h` 中保留如下默认配置：

| 项目 | 当前值 | 说明 |
| --- | --- | --- |
| `TCP_MSS` | 1460 | 标准以太网 MSS |
| `TCP_WND` | `8 * TCP_MSS` | 默认接收窗口 |
| `TCP_SND_BUF` | `8 * TCP_MSS` | 默认发送缓存 |
| `MEMP_NUM_TCP_SEG` | 64 | TCP 段池 |
| `PBUF_POOL_SIZE` | 32 | PBUF 池大小 |
| `PBUF_POOL_BUFSIZE` | 1536 | 单个 PBUF 缓冲区 |

### 6.3 调优结果总结

在本工程的调优过程中，曾经得到过以下几个阶段性结论：

- 纯阻塞 HAL SPI 路径下，`4 MSS` 与 `8 MSS` 会出现大量重传，必须退到 `2 MSS` 才能稳定。
- 引入 SPI3 数据块 DMA 后，`4 MSS`、`8 MSS` 和 `16 MSS` 都能稳定运行。
- `16 MSS` 相比 `8 MSS` 没有测得更高吞吐，但会占用更多 RAM。

因此当前发布态最终收敛为：

- 默认 `8 MSS`
- 不追求更大的窗口
- 优先保留更好的 RAM 占用与更清晰的默认配置

## 7. 构建、烧录与发布建议

### 7.1 常用构建命令

工程根目录提供 `build.ps1`，支持 Debug/Release 两套 CMake preset。

常用命令如下。

```powershell
.\build.ps1 rebuild Release
.\build.ps1 flash Release
```

### 7.2 工具依赖

脚本依赖以下工具。

- cmake
- ninja
- arm-none-eabi 工具链
- STM32CubeProgrammer CLI

如果 `STM32_Programmer_CLI.exe` 不在默认位置，可以通过参数手动指定。

```powershell
.\build.ps1 flash Release -CubeProgrammerCli "C:\Path\To\STM32_Programmer_CLI.exe"
```

### 7.3 当前建议的发布方式

对于当前工程，建议按下面方式交付或存档：

- 默认交付 Release 构建参数
- 保持 `Core/Inc/app_config.h` 的低日志配置
- 保持 `lwip/port/lwipopts.h` 的 `8 MSS` 默认值
- 保持 SPI3 数据块 DMA 路径启用
- 保留 README.md 与 README_CN.md 两份文档，其中 README_CN.md 作为主文档

## 8. 功能验证方法

### 8.1 ping

```powershell
ping 192.168.1.108
```

期望现象：持续收到 ICMP Reply。

### 8.2 UDP Echo

```powershell
echo "hello" | ncat -u 192.168.1.108 8
```

期望现象：

- 目标板原样返回 `hello`
- 若打开 `APP_LOG_UDP_ECHO`，会输出接收长度和来源地址

### 8.3 iperf

lwIP 的 lwiperf 使用 iperf2 协议，建议使用 iperf 或 iperf2，不建议直接用 iperf3 测试。

```powershell
iperf -c 192.168.1.108 -p 5008 -t 10
```

期望现象：

- 主机能建立 TCP 连接
- 测试结束后串口打印一次 iperf report
- PC 侧吞吐约 8.7 Mbit/s

## 9. 与 H563 参考工程的详细对比

本工程最重要的定位不是“独立新设计”，而是“在 F407 平台上把 H563 参考工程迁移、修正并整理成发布态”。因此，理解两者差异比单独看 F407 更重要。

### 9.1 平台与硬件差异

| 项目 | H563 参考工程 | F407 当前工程 |
| --- | --- | --- |
| MCU 平台 | STM32H563 | STM32F407 |
| HAL 系列 | STM32H5 HAL | STM32F4 HAL |
| RESET_N | PB3 | PD1 |
| IRQ_N | PD2 | PD0 |
| SPI3 引脚 | PA15 + PC10/11/12 | PA15 + PC10/11/12 |
| USART1 | PA9 / PA10 | PA9 / PA10 |
| GPIO 速度要求 | 低速可正常工作 | 低速是必须条件 |

### 9.2 网络参数差异

| 项目 | H563 参考工程 | F407 当前工程 |
| --- | --- | --- |
| IP | 192.168.1.100 | 192.168.1.108 |
| UDP Echo 端口 | 7 | 8 |
| iperf 端口 | 5001 默认值 | 5008 显式绑定 |

### 9.3 SPI 与收发路径差异

| 项目 | H563 参考工程 | F407 当前工程 |
| --- | --- | --- |
| SPI3 默认分频 | `/16` | 启动期 `/256`，运行期 `/2` |
| TC6 控制事务 | 阻塞 HAL SPI | 阻塞 HAL SPI |
| TC6 数据事务 | 阻塞 HAL SPI | DMA + 阻塞 HAL 混合 |
| DMA | 无 | 有，DMA1 Stream0/5 |

### 9.4 lwIP 配置差异

| 项目 | H563 参考工程 | F407 当前工程 |
| --- | --- | --- |
| `TCP_WND` | `8 * TCP_MSS` | `8 * TCP_MSS` |
| `TCP_SND_BUF` | `8 * TCP_MSS` | `8 * TCP_MSS` |
| `MEMP_NUM_TCP_SEG` | 32 | 64 |
| 默认吞吐结论 | 参考基线 | F407 实测约 8.7 Mbit/s |

### 9.5 软件结构差异

F407 工程相对于 H563 参考工程，除了 MCU/HAL 迁移，还加入了以下实质性修正。

1. 增加了 SPI3 数据块 DMA 路径，解决 F407 在较大 TCP 窗口下的重传问题。
2. 修复了 UART 输出路径，避免日志换行异常和发送中断导致的观测失真。
3. 修正了 `LAN8651_Receive()` 中 TC6 双帧共享块的 pending frame 保存逻辑。
4. 增加了 SPI 启动期低速、运行期高速的两阶段分频切换。
5. 扩展了适用于 F407 调优过程的链路、MAC、lwIP、iperf 诊断结构，并最终将发布态日志重新收敛。

### 9.6 为什么 F407 最终没有完全照搬 H563

迁移过程里最重要的经验是：

- H563 参考工程的结构和功能可以迁移。
- 但 F407 的 SPI3 电气特性、HAL 执行开销和接收承压能力与 H563 并不相同。
- 因此“代码能编译过”并不等于“性能和稳定性也完全等价”。

F407 最终必须引入以下平台特定策略，才能达到当前发布态结果：

- SPI3 GPIO 速度固定为 LOW
- 启动期 `/256`、运行期 `/2`
- 数据块事务走 DMA
- `MEMP_NUM_TCP_SEG` 扩大到 64

这些都属于 F407 平台化收敛的结果，而不是对 H563 工程的机械复制。

## 10. 常见问题

### 10.1 能启动但 ping 不通

优先检查：

- 主机是否位于 `192.168.1.0/24` 网段
- 串口日志中是否已打印 `Network ready`
- SPI3 引脚速度是否被误改为 `VERY_HIGH`

### 10.2 能 ping 但 UDP 或 iperf 不通

优先检查：

- PC 侧是否使用了正确端口 8 和 5008
- 是否误用了 iperf3
- 防火墙是否拦截

### 10.3 吞吐下降

优先检查：

- `lwipopts.h` 是否仍保持 `8 MSS` 默认值
- `LAN8651/lan8651.c` 的 DMA 路径是否被回退
- `Core/Inc/app_config.h` 是否重新打开了高频日志
- SPI3 GPIO 速度是否仍为 LOW

### 10.4 串口没有任何输出

优先检查：

- PA9/PA10 是否连接正确
- 串口参数是否为 115200 8N1
- 采集软件是否打开了正确的 COM 口

## 11. 发布建议

如果该工程后续需要对外发布、归档或交接，建议至少保留以下内容：

- Release 固件默认配置
- README.md
- README_CN.md
- `build.ps1` 与 CMake 配置
- 参考抓包结论和最终性能数据

如果后续还要继续增强，可按下面方向扩展：

1. 把静态网络参数下沉到独立配置头文件，减少板卡适配时的修改范围。
2. 在 README_CN.md 中继续补充更细的寄存器级初始化表。
3. 如需更严谨的发布包，可再增加一份专门的 `RELEASE_NOTES.md` 记录本次 F407 收敛过程和性能结论。
