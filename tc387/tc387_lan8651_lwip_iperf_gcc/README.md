# tc387_lan8651_lwip_iperf_gcc

TC387 + LAN8651 (10BASE-T1S, OA-TC6 over SPI/QSPI2) + LwIP 示例工程。

本工程由 `tc387_lwip_iperf_gcc` 复制并移植为 LAN8651 版本，目标功能：
- ICMP `ping`
- UDP `echo`
- `lwiperf` (TCP server)

## 1. 硬件连接

### 1.1 LAN8651 SPI/控制线

| TC387 Pin | LAN8651 信号 | 说明 |
| --- | --- | --- |
| P15.2 | nCS | QSPI2_M_SEL0 |
| P15.4 | nRST | PHY Reset |
| P15.5 | nINT | IRQ_N |
| P15.6 | MOSI | QSPI2_M_TXD |
| P15.7 | MISO | QSPI2_M_RXD |
| P15.8 | SCK | QSPI2_M_CLK |

### 1.2 调试串口

- UART4 (ASCLIN4)
- TX: P00.9
- RX: P00.12
- Windows 端口: COM9
- 波特率: 115200

### 1.3 调试器

- DAP MiniWiggler

### 1.4 以太网链路

- LAN8651 另一端连接 USB-10BASE-T1S 适配器
- PC 侧适配器建议配置：
  - IP: `192.168.1.2`
  - Netmask: `255.255.255.0`
  - Gateway: `192.168.1.1` (可选)

## 2. 软件架构

- LwIP 端口层入口：
  - `Libraries/Ethernet/lwip/port/src/netif.c`
  - `Libraries/Ethernet/lwip/port/src/ethernetif_lan8651.c`
- LAN8651 驱动：
  - `Libraries/LAN8651/lan8651.h`
  - `Libraries/LAN8651/lan8651.c`
- 主流程：
  - `Cpu0_Main.c`

移植思路：
1. 保留原 LwIP 主循环/定时框架 (`Ifx_Lwip_pollTimerFlags`, `Ifx_Lwip_pollReceiveFlags`)。
2. 将原 GETH 路径替换为 `LAN8651(TC6 over QSPI2)`。
3. 在 `ifx_netif_input()` 中调用 `lan8651_ethernetif_input()` 拉包。
4. 在 `ifx_netif_init()` 中将 `netif->state` 指向 `g_lan8651`。

## 3. 关键配置

位于 `Configurations/Configuration.h`：

- LAN8651 线序与引脚：`LAN8651_RST_*`, `LAN8651_INT_*`
- SPI 速率：`LAN8651_SPI_BAUDRATE` (默认 1MHz)
- PLCA：
  - `LAN8651_PLCA_ENABLE` = 1
  - `LAN8651_PLCA_NODE_ID` = 1
  - `LAN8651_PLCA_NODE_COUNT` = 8
- 静态 IP：
  - `LAN8651_IP_ADDR*` = `192.168.1.100`
  - `LAN8651_NETMASK*` = `255.255.255.0`
  - `LAN8651_GATEWAY*` = `192.168.1.1`
- MAC：`LAN8651_MAC*` (默认 `02:00:00:10:BA:5E`)

QSPI2 中断优先级（也在 `Configuration.h`）：
- `ISR_PRIORITY_QSPI2_TX`
- `ISR_PRIORITY_QSPI2_RX`
- `ISR_PRIORITY_QSPI2_ER`

## 4. 构建与下载

### 4.1 TASKING

```powershell
./build.ps1 -Action build -Compiler tasking
./build.ps1 -Action download -Compiler tasking
```

当前实测：
- TASKING 可完整编译并链接成功。
- 可正常通过 AURIXFlasher 下载。

### 4.2 GCC

```powershell
./build.ps1 -Action build -Compiler gcc
```

当前状态：
- 编译通过到链接阶段。
- 在当前机器上遇到 TriCore GCC 链接器临时响应文件错误：
  - `ld.exe: cannot find @C:\WINDOWS\TEMP\ccXXXXXX: Invalid argument`
- 该问题为工具链/环境问题（非本次代码语法问题）。

## 5. 功能测试步骤

### 5.1 串口观察启动日志

打开 COM9，115200，建议先确认是否打印：
- LAN8651 设备 ID
- LwIP IP 地址
- lwiperf report

### 5.2 Ping 测试

PC 执行：

```powershell
ping 192.168.1.100 -t
```

### 5.3 UDP Echo 测试

任意 UDP 工具发送到：
- 目标 IP: `192.168.1.100`
- 目标端口: `9`

应收到原样回包。

### 5.4 iPerf 测试 (TCP)

固件侧已启用 `lwiperf_start_tcp_server_default(...)`。

PC 使用：
- `C:\git\embedded\tc387\bak\iperf.exe`

示例：

```powershell
cd C:\git\embedded\tc387\bak
./iperf.exe -c 192.168.1.100 -i 1 -t 10
```

## 6. 本次修改文件清单

- `Cpu0_Main.c`
- `Configurations/Configuration.h`
- `Libraries/LAN8651/lan8651.h`
- `Libraries/LAN8651/lan8651.c`
- `Libraries/Ethernet/lwip/port/include/Ifx_Lwip.h`
- `Libraries/Ethernet/lwip/port/src/Ifx_Lwip.c`
- `Libraries/Ethernet/lwip/port/src/netif.c`
- `Libraries/Ethernet/lwip/port/include/ethernetif_lan8651.h`
- `Libraries/Ethernet/lwip/port/src/ethernetif_lan8651.c`
- `CMakeLists.txt`

## 7. 注意事项

1. 当前驱动使用 QSPI2 + iLLD `IfxQspi_SpiMaster`，SPI 模式按 LAN8651 TC6 默认配置。
2. 如果链路不通，优先检查：
   - nRST/nINT 极性与接线
   - nCS 是否为 P15.2 且独占
   - USB-10BASE-T1S 适配器的 IPv4 配置
3. PLCA 多节点测试时请避免 Node ID 冲突。
4. 若需更高吞吐，可尝试提升 `LAN8651_SPI_BAUDRATE`（并结合逻辑分析仪验证时序裕量）。
