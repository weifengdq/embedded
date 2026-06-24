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

当前实测：
- GCC 已可完整编译并链接成功。
- GCC 产物可正常通过 AURIXFlasher 下载。

若出现 TriCore GCC 链接器临时响应文件错误（例如 `ld.exe: cannot find @C:\WINDOWS\TEMP\ccXXXXXX`），可在构建前指定临时目录后重试：

```powershell
New-Item -ItemType Directory -Path C:\Temp -Force | Out-Null
$env:TEMP = "C:\Temp"
$env:TMP  = "C:\Temp"
./build.ps1 -Action rebuild -Compiler gcc
```

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

### 5.5 最新实测结果（2026-06-23）

- TASKING：
  - 编译链接通过。
  - 下载通过。
  - 串口启动日志确认：
    - `Booting TC387 LAN8651 firmware`
    - LAN8651 寄存器配置输出
    - `LAN8651 started, MAC=02:00:00:10:BA:5E`
    - `Static IP=192.168.1.100 MASK=255.255.255.0 GW=192.168.1.1`
    - `UDP echo listening on port 9`
    - `lwIP iperf server ready`
    - `LAN8651 link up` + `gratuitous_arp=sent`
- 网络功能：
  - ✅ **Ping**: `ping 192.168.1.100 -n 4` → **4/4 回复**，平均延迟 7ms（最短 3ms，最长 20ms）
  - ✅ **UDP Echo**: 发送到 `192.168.1.100:9` → 正确回显
  - ✅ **iperf TCP**: 连接成功，平均吞吐 ~173 Kbits/sec（10BASE-T1S + 1MHz SPI 限制）
- GCC：
  - 清理重建成功。
  - 下载成功（若遇到链接器临时文件错误，参考第 4.2 节的 `TEMP` 环境变量解决方法）。

#### 关键修复说明

网络不通的根因是 **lwIP `ETH_PAD_SIZE` 与 pbuf 对齐**：
- `ETH_PAD_SIZE=2` 时，lwIP 的 `struct eth_hdr` 在 MAC 字段前包含 2 字节填充
- **RX 路径**：`PBUF_RAW` pbuf 必须在以太网帧前手动添加 `ETH_PAD_SIZE` 字节零填充
- **TX 路径**：lwIP pbuf payload 前 `ETH_PAD_SIZE` 字节为填充，发送到线路上时必须跳过
- 同时需要正确配置 QSPI SPI 模式（Mode 0：`shiftTransmitDataOnTrailingEdge`）和手动 CS 控制（`autoCS=0`）

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
5. 若 COM9 被占用，串口抓日志会失败（`Access to the path 'COM9' is denied`）；请先关闭其他串口工具后再测试。
