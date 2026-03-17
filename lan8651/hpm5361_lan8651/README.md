# HPM5361 + LAN8651 10BASE-T1S 裸机示例

本工程基于 HPM SDK 1.11.0，在 hpm5300evk 上使用 HPM5361 的 SPI1 + DMA 驱动 Microchip LAN8651，并接入 SDK 自带 lwIP baremetal port，提供以下功能：

- 固定 IP: 192.168.1.109
- ping、UDP echo、lwiperf 的软件路径已经接入
- UDP echo，端口 9
- lwiperf TCP server，端口 5009
- PLCA 节点 ID: 2

## 目录说明

- 应用入口: src/main.c
- LAN8651 驱动: src/lan8651.c, inc/lan8651.h
- lwIP netif 适配: src/ethernetif_lan8651.c, inc/ethernetif_lan8651.h
- 应用配置: inc/app_config.h
- lwIP 覆盖配置: inc/lwipopts_app.h
- 构建与烧录脚本: build.ps1

## 硬件连接

LAN8651 与 HPM5361 连接如下：

- CS_N: PA26
- SCK: PA27
- MISO: PA28
- MOSI: PA29
- IRQ_N: PA12
- RESET_N: PA10

说明：

- SPI 使用 SPI1，CS 采用 GPIO 手动控制。
- SPI 控制面事务当前使用 blocking 传输，数据面事务使用 SPI DMA。
- 运行时 SPI 时钟当前配置为 20 MHz，低于 25 Mbit/s 约束。

## 软件架构

初始化顺序如下：

1. 初始化 board、console、GPIO、SPI1、DMA。
2. 低速 SPI 复位并识别 LAN8651。
3. 写入 LAN8651 默认推荐寄存器配置。
4. 配置 PLCA，节点 ID 固定为 2。
5. 切换 SPI 运行时频率到 10 MHz。
6. 启动 lwIP，注册自定义 netif。
7. 启动 UDP echo 和 lwiperf server。
8. 主循环轮询收包并调用 sys_check_timeouts。

## 默认网络参数

- 设备 IP: 192.168.1.109
- 子网掩码: 255.255.255.0
- 网关: 192.168.1.1
- UDP echo 端口: 9
- iperf 端口: 5009
- PLCA 节点 ID: 2
- PLCA 节点总数: 8

这些参数集中定义在 inc/app_config.h。

如果需要修改：

- IP、子网掩码、网关：修改 APP_IP_ADDRx、APP_NETMASK_ADDRx、APP_GW_ADDRx
- UDP echo 端口：修改 APP_UDP_ECHO_PORT
- iperf 端口：修改 APP_IPERF_PORT
- PLCA 节点 ID：修改 APP_LAN8651_PLCA_NODE_ID
- PLCA 节点数：修改 APP_LAN8651_PLCA_NODE_COUNT
- SPI 频率：修改 APP_LAN8651_SPI_INIT_FREQ 和 APP_LAN8651_SPI_RUNTIME_FREQ
- 日志档位：修改 APP_LAN8651_BRINGUP_PROFILE

## 编译

在工程目录下执行：

```powershell
.\build.ps1 rebuild Release
```

脚本行为：

- 自动设置 HPM_SDK_BASE 为 sdk_env_v1.11.0 根目录
- 使用 SDK 自带 cmake 和 ninja
- 目标 board 固定为 hpm5300evk
- build type 使用 flash_xip

Release 输出目录：

- hpm5300evk_flash_xip_release

生成的 ELF 路径通常为：

- hpm5300evk_flash_xip_release/output/demo.elf

## 烧录

```powershell
.\build.ps1 flash Release
```

脚本使用 SDK 自带 OpenOCD 和如下配置文件：

- hpm_sdk/boards/openocd/hpm5300_all_in_one.cfg

## 串口日志

启动后可通过串口观察以下关键日志：

- 工程名称与 LAN8651 bring-up 信息
- IP ready 信息
- 默认每 5 秒输出一次诊断摘要
- lwiperf 会在测试结束时打印吞吐报告

默认配置已经收敛掉高频抓包日志，适合长时间跑 ping、UDP echo 和 iperf。若需要重新打开详细日志，可在 inc/app_config.h 中调整以下开关：

- APP_LAN8651_DIAG_LOG_PERIOD_MS：诊断日志周期，默认 5000 ms
- APP_LAN8651_FRAME_LOG_LIMIT：启动阶段收发帧摘要条数，默认 4
- APP_LAN8651_ENABLE_RX_OK_LOG：是否打印 netif_input_ok，默认关闭
- APP_LAN8651_TX_CHUNK_LOG_LIMIT：TC6 TX chunk 日志条数，默认关闭
- APP_LAN8651_RAW_SPI_LOG_ENABLE：68 字节原始 SPI 块日志，默认关闭

另外，inc/app_config.h 提供了一个更直接的日志档位开关：

- APP_LAN8651_BRINGUP_PROFILE = 0：release 风格，默认只保留必要摘要日志
- APP_LAN8651_BRINGUP_PROFILE = 1：bring-up 风格，放开更多帧摘要和收发诊断日志

如果仍然只看到 hello world，说明板子上还是旧固件，需要重新执行 rebuild 和 flash。

## 功能验证

### 当前状态

当前 bring-up 结论如下：

- LAN8651 控制面已打通，可稳定读出 DEVID。
- PLCA、SYNC、链路状态和接收状态已验证正常。
- lwIP 已确认能够收到主机发来的 ARP 和 IPv4 帧。
- 启动阶段已验证会发送 60 字节 Gratuitous ARP，串口会打印 netif: gratuitous_arp=sent。
- 固件已确认会构造并提交正确的单播 ARP reply。
- 当前版本已经补上最小以太帧长度补零逻辑，ARP reply 会按 60 字节发出。
- MAC_TSR 在发送后最终可以看到 TXCOMP 置位，说明 MAC 侧认为发送已经完成。
- 已定位并修复根因：ethernetif 传给 lan8651_transmit() 的发送缓冲区与驱动内部 dev->tx_frame 是同一块内存，而驱动进入发送函数后又先清零 dev->tx_frame 再拷贝，导致实际下发到 TC6 数据事务中的 payload 被清空。
- 修复后，Windows 主机已经可以正常学习 192.168.1.109 的 ARP 项，ping、UDP echo 和 iperf 都已通过实测。

### 1. ping

在 PC 端执行：

```powershell
ping 192.168.1.109
```

当前实测结果：已通过。

### 2. UDP echo

向 192.168.1.109 的 9 号端口发送任意 UDP 数据，设备会原样回显。

例如可使用任意 UDP 调试工具，目标配置如下：

- 目标 IP: 192.168.1.109
- 目标端口: 9

当前实测结果：已通过。使用 PowerShell UDP 客户端向端口 9 发送字符串 hello-lan8651，设备成功原样回显。

### 3. iperf

用户指定的 iperf 工具路径为：

- D:/hpm/sdk_env_v1.11.0/ref/iperf.exe

在 PC 端执行：

```powershell
D:\hpm\sdk_env_v1.11.0\ref\iperf.exe -c 192.168.1.109 -p 5009 -t 10
```

建议先验证 ping，再验证 UDP echo，最后进行 iperf。

当前实测结果：已通过。使用用户指定的 iperf 工具执行 D:\hpm\sdk_env_v1.11.0\ref\iperf.exe -c 192.168.1.109 -p 5009 -t 3，可建立 TCP 连接并完成吞吐测试。

## 吞吐优化说明

本轮已完成一轮以“接近 10BASE-T1S 理想速率”为目标的吞吐优化，当前收敛配置如下：

- 主循环去掉了每次 1 ms 的阻塞延时，改为板级 1 ms timer 驱动 sys_now()
- 状态 LED 改为低频闪烁，不再影响收发主循环
- SPI 运行频率从 10 MHz 提升到 20 MHz
- LAN8651 轮询收包预算提升到 32
- lwIP TCP 参数保守提升到当前板子可稳定工作的最佳点

当前最终保留的 lwIP 参数为：

- MEM_SIZE: 40 KB
- PBUF_POOL_SIZE: 24
- MEMP_NUM_TCP_SEG: 72
- TCP_SND_BUF / TCP_WND: 12 x MSS

调优过程中已经验证：

- 去掉主循环阻塞延时后，iperf 从不到 1 Mbit/s 提升到 8.21 Mbits/s
- 继续把 SPI 提到 24 MHz，或继续放大 TCP 窗口到 16 x MSS，并没有带来更高吞吐，反而会退回到约 6.5 到 7.5 Mbits/s
- 因此当前版本保留的是“实测最快且稳定”的组合，而不是参数上最激进的组合

当前实测 iperf 结果：

- D:\hpm\sdk_env_v1.11.0\ref\iperf.exe -c 192.168.1.109 -p 5009 -t 5
- 4.97 MBytes / 8.21 Mbits/sec

对于当前 baremetal + SPI DMA + TC6 实现，这已经比较接近 10BASE-T1S 的理想状态；若后续还要继续逼近 10 Mbits/s，重点就不再是简单加大 lwIP 缓冲，而要继续优化收发并发、SPI 事务组织和可能的中断/事件驱动路径。

## 根因分析

本次问题最后不是出在 lwIP 版本差异，也不是 SPI mode 配置错误，而是发送缓冲区别名问题。

发送路径里，ethernetif 会把整理好的以太网帧放到 dev->tx_frame，然后再调用 lan8651_transmit(dev, dev->tx_frame, wire_len)。驱动里原先又对 dev->tx_frame 做了一次“先清零、再 memcpy”的处理。由于传入的 frame 和驱动内部 dev->tx_frame 实际上是同一块内存，这一步会先把源数据清空，导致真正送入 TC6 数据事务的 payload 变成全 0。

这个问题之所以难定位，是因为 lwIP 层面的 tx_frame 摘要打印仍然是正确的，只有继续看原始 68 字节 TC6 数据事务，才能发现 header 正常而 payload 全 0。修复后，ARP reply 立即恢复正常，Windows 也开始学习 192.168.1.109 的动态 ARP 项，随后 ping、UDP echo、iperf 全部恢复。

## 调试建议

如果 LAN8651 初始化失败，优先检查以下项目：

- PA10 复位脚是否连接正确
- PA12 中断脚是否上拉且空闲时为高电平
- SPI1 四线是否与 LAN8651 对应正确
- 10BASE-T1S 总线端是否已经具备正确的电源、终端与 PLCA 节点配置

如果 ping 不通但固件已启动：

- 确认对端与本设备处于同一网段
- 确认 PLCA 节点 ID 没有冲突
- 观察串口中 link 状态是否为 1
- 如果再次出现串口显示已生成 ARP reply 但主机不学 ARP，优先检查 lan8651_transmit() 的 source/destination 缓冲区是否发生同址覆盖

如果 iperf 无法建立连接：

- 确认端口为 5009
- 确认 PC 侧使用的是 TCP client 模式
- 确认防火墙没有拦截本地测试程序

## 当前实现边界

本工程当前采用裸机轮询模式：

- 不使用 RTOS
- 不依赖片上 ENET MAC
- 不使用 DHCP
- 不使用中断驱动收包，主循环中按预算轮询 LAN8651

这种实现方式优先保证 bring-up 简洁、可定位、便于后续继续优化 DMA 收发和异常恢复逻辑。