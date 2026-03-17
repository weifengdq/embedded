# stm32f407_lan8651

这是一个基于 STM32F407 与 LAN8651 的 10BASE-T1S 裸机示例工程，网络栈使用 lwIP，当前实现了以下功能：

- 静态 IPv4 地址 192.168.1.108
- ICMP ping 应答
- UDP echo 服务器，监听端口 8
- TCP iperf 服务器，监听端口 5008，兼容 iperf2 协议
- 串口日志通过 USART1 输出到 PA9/PA10，对接 USB 转串口后可直接落盘分析

## 1. 硬件连接

本工程按照以下连接关系实现：

- 调试串口
  - PA9: USART1_TX
  - PA10: USART1_RX
- LAN8651 SPI 接口
  - PA15: CS_N，软件控制片选
  - PC10: SPI3_SCK
  - PC11: SPI3_MISO
  - PC12: SPI3_MOSI
  - SPI3 速率: 21 Mbit/s，来自 APB1 42 MHz 时钟分频 2
- LAN8651 控制信号
  - PD0: IRQ_N，下降沿 EXTI 中断输入
  - PD1: RESET_N，GPIO 输出

说明：

- SPI3 使用软件片选，PA15 不再交给 SPI 外设硬件 NSS 管理，而是由驱动在每次 TC6 事务前后手动拉低/拉高。
- F407 实测需要将 SPI3 的 GPIO 输出速度配置为 LOW。保持 PC10/PC11/PC12 为低速复用推挽后，运行期可以稳定工作在 21 Mbit/s；如果改成 VERY_HIGH，容易出现 TC6 footer 校验错误、ARP 学习失败或 ping 丢包。
- IRQ_N 目前主要作为状态提示，主循环仍采用轮询方式调用 ethernetif_input，避免因中断掩码或时序差异导致丢包。

## 2. 软件结构

工程由三部分组成：

- Core
  - main.c: 系统初始化、LAN8651 启动、lwIP 接口注册、UDP echo 与 iperf 服务创建
  - stm32f4xx_it.c: EXTI0 中断处理
- LAN8651
  - lan8651.c / lan8651.h: OPEN Alliance TC6 over SPI 驱动、寄存器访问、MAC/PHY 初始化、PLCA 配置、帧收发
- lwip
  - port/ethernetif.c: 将 LAN8651 原始以太网帧接入 lwIP netif
  - port/sys_arch.c: 裸机场景下的 sys_now 实现
  - src/: lwIP 核心协议栈与 lwiperf 应用

## 3. 启动流程

main.c 的启动顺序如下：

1. HAL_Init
2. SystemClock_Config，将系统时钟配置为 168 MHz
3. 初始化 GPIO、USART1、SPI3
4. 填充 lan8651_t 设备描述，绑定 SPI3、CS、RESET、IRQ 和 MAC 地址
5. 调用 LAN8651_Init 完成硬件复位、软件复位、芯片识别、AN1760 默认参数写入、PLCA 和 MAC 配置
6. 调用 LAN8651_Start 使能收发
7. 调用 Network_Init 完成 lwIP 初始化、静态地址配置和 netif 注册
8. 调用 UDP_Echo_Init 创建 UDP 回显服务
9. 调用 LWIPerf_Init 创建 TCP iperf 服务器
10. 在主循环中轮询 ethernetif_input 和 sys_check_timeouts

## 4. 网络参数

当前固定网络配置如下：

- IP: 192.168.1.108
- Mask: 255.255.255.0
- Gateway: 192.168.1.1
- UDP echo port: 8
- iperf port: 5008

如果后续需要改 IP 或端口，直接修改 Core/Src/main.c 顶部的宏即可。

## 5. 编译与烧录

工程根目录提供了 build.ps1，支持 Debug/Release 两套 CMake preset。

常用命令：

```powershell
.\build.ps1 rebuild Release
.\build.ps1 flash Release
```

脚本行为：

- rebuild Release: 重新配置并编译 Release 固件
- flash Release: 从 ELF 生成 HEX/BIN，再调用 STM32CubeProgrammer 下载到目标板

脚本依赖：

- cmake
- ninja
- arm-none-eabi 工具链
- STM32CubeProgrammer CLI

如果 STM32_Programmer_CLI.exe 不在默认位置，可以通过参数手动指定：

```powershell
.\build.ps1 flash Release -CubeProgrammerCli "C:\Path\To\STM32_Programmer_CLI.exe"
```

## 6. 日志查看

调试串口已经接到 USB-TO-Serial，日志会自动写入串口采集文件。采集文件名由串口工具按时间自动生成，通常形如：

- ReceivedTofile-COMxx-YYYY_M_D_HH-mm-ss.DAT

典型启动日志会包含：

- 芯片 banner
- LAN8651 设备 ID
- PLCA/MAC 初始化结果
- lwIP 初始化完成
- UDP echo 和 iperf 监听端口信息

如果启动异常，优先检查是否出现以下日志：

- ERROR: LAN8651 init failed!
- ERROR: LAN8651 start failed!
- ERROR: udp_new() failed
- ERROR: lwiperf_start_tcp_server() failed

## 7. 功能验证方法

### 7.1 ping

在 PC 侧通过 USB-TO-T1S 转换器接入同一网段后执行：

```powershell
ping 192.168.1.108
```

如果正常，应持续收到 ICMP Reply。

### 7.2 UDP echo

使用 ncat 或其他 UDP 工具发送数据到端口 8：

```powershell
echo "hello" | ncat -u 192.168.1.108 8
```

期望现象：

- 目标板原样返回 hello
- 若开启 APP_LOG_UDP_ECHO，会看到接收长度和来源地址日志

### 7.3 iperf

lwIP 的 lwiperf 使用 iperf2 协议，建议使用 iperf 或 iperf2，不建议用 iperf3 直接测试该服务。

```powershell
iperf -c 192.168.1.108 -p 5008 -t 10
```

期望现象：

- 主机能建立 TCP 连接
- 测试结束后串口会打印一次 iperf report

F407 当前稳定配置说明：

- 当前工程已将 SPI3 数据块事务切换为 DMA：68-byte TC6 数据块走 HAL SPI DMA，12-byte 控制寄存器事务仍保持阻塞 HAL。这样既保留了寄存器读写时序的确定性，也显著降低了主循环处理每个 TC6 数据块的 CPU 开销。
- 当前工程默认保留 `TCP_WND = 8 * TCP_MSS`、`TCP_SND_BUF = 8 * TCP_MSS`、`MEMP_NUM_TCP_SEG = 64`。这一配置下 Release 版本 `iperf -c 192.168.1.108 -p 5008 -t 10` 实测约 8.7 Mbit/s，且抓包中未再出现 `tcp.analysis.retransmission`、`tcp.analysis.fast_retransmission` 或 `tcp.analysis.duplicate_ack`。
- 同一版 DMA 固件在 `4 MSS` 和 `16 MSS` 下也都能稳定工作，实测吞吐仍约 8.7 Mbit/s。由于 `16 MSS` 相比 `8 MSS` 没有额外吞吐收益，但会占用更多 RAM，工程默认配置最终收敛在 `8 MSS`。
- 相比此前阻塞 HAL SPI 路径下必须收缩到 `2 MSS` 才能避免大量重传，当前工程的吞吐瓶颈已经基本解除。

## 8. 可调日志开关

日志开关位于 Core/Inc/app_config.h。

常用项：

- APP_LOG_NET_INIT: 打印 lwIP/netif 初始化过程
- APP_LOG_PERIODIC_DIAG: 周期打印 PLCA、链路和缓冲状态
- APP_LOG_UDP_ECHO: 打印 UDP echo 收包信息
- APP_LOG_IPERF: 打印 iperf 会话报告
- LAN8651_LOG_INFO: 打印驱动初始化关键信息
- ETHERNETIF_LOG_FRAMES: 打印少量 ARP/以太网收发帧信息

建议默认保持当前配置，只在定位问题时逐步打开。

## 9. 迁移说明

本工程是从 h563_spi_10baset1s_iperf 参考工程迁移而来，主要差异如下：

- HAL 头文件从 STM32H5 切换为 STM32F4
- 去掉 H5 专用的 MPU/ICACHE 配置
- SPI3 保持 21 Mbit/s，按 F407 时钟树重新配置为分频 2
- IRQ_N 改为 PD0，RESET_N 改为 PD1
- IP 从 192.168.1.100 改为 192.168.1.108
- UDP 端口从 7 改为 8
- iperf 端口从 5001 改为 5008，并改用 lwiperf_start_tcp_server 显式绑定端口

## 10. 常见问题

### 10.1 能启动但 ping 不通

优先检查：

- USB-TO-T1S 转换器是否已接入 192.168.1.0/24 网段
- 主机是否存在错误的静态 ARP 缓存
- 串口日志中是否已打印 Network ready

### 10.2 能收到 ARP 但 UDP 或 iperf 不通

优先检查：

- 测试端口是否使用了 8 和 5008
- PC 侧是否误用了 iperf3
- 是否有防火墙拦截

### 10.3 串口没有任何输出

优先检查：

- PA9/PA10 是否已正确连接 USB 转串口
- 串口参数是否为 115200 8N1
- 采集软件是否打开了正确的 COM 口

### 10.4 SPI 通讯异常

优先检查：

- PA15 是否作为普通 GPIO 输出使用，而不是硬件 NSS
- PC10/PC11/PC12 是否使用 AF6 SPI3
- PC10/PC11/PC12 的 GPIO Speed 是否为 LOW；若配置成 VERY_HIGH，可能出现 RX footer parity error、RX unexpected SWO、TX frame dropped 等现象
- CS_N、RESET_N、IRQ_N 是否与硬件连接一致

## 11. 后续建议

如果后续需要继续增强，可以按下面方向扩展：

- 增加链路状态检测和更明确的 link up/down 日志
- 增加更细的 TC6 事务调试开关
- 将静态网络参数下沉到单独配置头文件，便于批量切换板卡配置
