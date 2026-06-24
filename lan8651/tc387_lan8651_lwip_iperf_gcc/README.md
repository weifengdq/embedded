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
- SPI 速率：`LAN8651_SPI_BAUDRATE` (默认 20MHz)
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

### 5.5 最新实测结果（2026-06-24）

- SPI 速率：20MHz
- 编译：
  - **TASKING**: 0 errors, 自定义代码 **零警告**（仅 lwIP/iLLD 库有第三方警告）
  - **GCC**: 0 errors, 自定义代码 **零警告**
  - 两个编译器均可正常下载运行
- 串口启动日志确认：
  - `Booting TC387 LAN8651 firmware`
  - LAN8651 寄存器配置输出
  - `LAN8651 started, MAC=02:00:00:10:BA:5E`
  - `Static IP=192.168.1.100 MASK=255.255.255.0 GW=192.168.1.1`
  - `UDP echo listening on port 9`
  - `lwIP iperf server ready`
  - `LAN8651 link up` + `gratuitous_arp=sent`
- 网络功能：
  - ✅ **Ping**: `ping 192.168.1.100 -n 4` → **4/4 回复**，平均延迟 3ms
  - ✅ **UDP Echo**: 发送到 `192.168.1.100:9` → 正确回显
  - ✅ **iperf TCP**:

| 编译器 | iperf 吞吐 | 说明 |
|--------|-----------|------|
| TASKING | **8.22 Mbits/sec** | 接近 10BASE-T1S 理论极限 |
| GCC | **8.30 Mbits/sec** | 略优于 TASKING |

> STM32/HPM5361 基准约 8.7 Mbits/sec，TC387 在 20MHz SPI 下已达 8.3 Mbits/sec（~95%），
> 剩余差距主要来自 CPU 架构差异和 10BASE-T1S 物理层开销。

### 5.6 性能优化建议

当前 `LAN8651_SPI_BAUDRATE = 20MHz`，LAN8651 最大 SPI 时钟为 25MHz。若需进一步提升：

1. **提高 SPI 速率**：将 `Configuration.h` 中 `LAN8651_SPI_BAUDRATE` 设为 `25000000U`
2. **调整 lwIP TCP 参数**（`lwipopts.h`）：
   - `TCP_MSS`: 可尝试 1460
   - `TCP_WND`: 增大窗口（如 `4 * TCP_MSS`）
   - `MEM_SIZE`: 增大内存池
3. **编译器优化**：改为 `-O2` 或 `-Os` 构建（当前为 Debug）
4. **中断优化**：减少 QSPI ISR 中的处理延迟

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

1. **SPI 模式**：须使用 Mode 0，即 `shiftTransmitDataOnTrailingEdge`，`autoCS=0`（手动 GPIO 控制 CS）。
2. **lwIP ETH_PAD_SIZE**：`ETH_PAD_SIZE=2` 时 `struct eth_hdr` 含 2 字节填充：
   - RX：`PBUF_RAW` pbuf 需在帧前手动添加 2 字节零填充
   - TX：从 lwIP pbuf 复制帧到发送缓冲区时需跳过前 2 字节
3. **PLCA**：Node ID 不可与对端（USB 适配器）冲突；默认 Node ID=1, Count=8。
4. **中断优先级**：QSPI2 TX/RX/ER 三个 ISR 均需注册，优先级 110-112。
5. 当前驱动使用 QSPI2 + iLLD `IfxQspi_SpiMaster`，SPI 模式按 LAN8651 TC6 默认配置。
6. 如果链路不通，优先检查：
   - nRST/nINT 极性与接线
   - nCS 是否为 P15.2 且独占
   - USB-10BASE-T1S 适配器的 IPv4 配置
7. PLCA 多节点测试时请避免 Node ID 冲突。
8. 若需更高吞吐，可尝试提升 `LAN8651_SPI_BAUDRATE`（并结合逻辑分析仪验证时序裕量）。
9. 若 COM9 被占用，串口抓日志会失败（`Access to the path 'COM9' is denied`）；请先关闭其他串口工具后再测试。
10. **GCC 构建**：若出现 `ld.exe: cannot find @C:\WINDOWS\TEMP\ccXXXXXX` 错误，参考第 4.2 节 `TEMP` 环境变量解决方案。
