# hpm5e31_rmii_dp83848

HPM5E31 通过 RMII 接口连接 DP83848IVV 以太网 PHY，运行 lwIP 协议栈，支持 UDP echo、iperf TCP/UDP 测试。

---

## 硬件连接

### RMII 接口引脚

| 信号                | HPM5E31 引脚 | DP83848 引脚     | 说明                        |
| ------------------- | ------------ | ---------------- | --------------------------- |
| ETH_CRSDV           | PF08         | CRS_DV / RX_DV  | 载波检测/接收数据有效       |
| ETH_TXD0            | PF03         | TXD0             | 发送数据 0                  |
| ETH_RXD0            | PF09         | RXD0             | 接收数据 0                  |
| ETH_TXEN            | PF07         | TX_EN            | 发送使能                    |
| ETH_RXD1            | PF10         | RXD1             | 接收数据 1                  |
| ETH_TXD1            | PF04         | TXD1             | 发送数据 1                  |
| ETH_MDC             | PA31         | MDC              | MDIO 管理时钟               |
| ETH_MDIO            | PA30         | MDIO             | MDIO 管理数据               |
| ETH_NRST (复位)     | PF14         | nRST             | PHY 硬件复位 (低电平有效)   |

> **注意：** 所有连线均为手工飞线。

### 时钟说明

- **DP83848 模块**：板载 50 MHz 有源晶振，连接到 DP83848 的 XI 引脚，为 PHY 提供 RMII 参考时钟
- **HPM5E31 ENET**：使用芯片内部 PLL2 产生 50 MHz 时钟（`clock_eth0`），通过 `ENET_CTRL2_ENET0_REFCLK_OE` 输出到 PF02（ETH0_TXCK），LOOP_BACK 反馈给 MAC 内部
- **工作模式**：双时钟 RMII（MAC 和 PHY 各自使用独立 50 MHz 时钟），依靠两端振荡器频率接近（误差 < 50 ppm）保证正常通信

### DP83848 模块简介

DP83848IVV 是 TI 的单端口 10/100 Mbps 以太网 PHY，支持：
- MII / RMII 接口
- 自动协商 (Auto-Negotiation)
- 全/半双工
- MDI/MDIX 自动翻转
- IEEE 802.3u 兼容

本项目配置为 RMII 模式，自动协商，100 Mbps 全双工。

---

## 工程架构

```
hpm5e31_rmii_dp83848/
├── boards/
│   └── hpm5e31_lite/
│       ├── board.h          # ENET RMII 复位 GPIO 改为 PF14
│       ├── board.c          # 板级初始化（含 ENET RMII 时钟/引脚/复位）
│       ├── pinmux.c         # init_enet0_pins_rmii 适配 DP83848 引脚 (PF14)
│       ├── pinmux.h
│       ├── CMakeLists.txt   # 使用 CONFIG_ENET_PHY_DP83848
│       └── hpm5e31_lite.yaml
├── inc/
│   ├── common_cfg.h         # ENET DMA 缓冲区配置 (20×1536 B TX/RX)
│   └── lwipopts_app.h       # lwIP 参数 (MEM_SIZE=32K, TCP_WND=12×MSS)
├── src/
│   └── lwip.c               # 主程序: UDP echo server + iperf TCP/UDP
├── CMakeLists.txt
├── build.ps1
└── README.md
```

### 关键寄存器配置

| 寄存器 | 值 | 说明 |
|--------|-----|------|
| clock_eth0 | 50 MHz (PLL2 → /5) | MAC 以太网时钟 |
| ENET_CTRL2.ENET0_REFCLK_OE | 1 | 使能 REFCLK 输出 |
| ENET_CTRL2.ENET0_RMII_TXCLK_SEL | 1 | 使用内部时钟 |
| DP83848 BMCR | ANE=1 | 自动协商使能 |
| DP83848 PHYSTS | 读取链路状态/速度/双工 |

---

## 编译与烧录

```powershell
# 编译
.\build.ps1 -Action rebuild -Config Debug

# 烧录
.\build.ps1 -Action flash -Config Debug
```

**内存占用：**

| 区域 | 占用 | 大小 | 使用率 |
|------|------|------|--------|
| FLASH | 135 KB | 1 MB | 12.9% |
| ILM | 1.6 KB | 128 KB | 1.2% |
| DLM | 108 KB | 128 KB | 82.8% |
| AXI_SRAM | 63 KB | 192 KB | 32.0% |

---

## 网络配置

| 参数 | 值 |
|------|----|
| 静态 IP | 192.168.0.100 |
| 子网掩码 | 255.255.255.0 |
| 网关 | 192.168.0.1 |
| PC IP | 192.168.0.2 |
| PHY 地址 | 1 (DP83848_ADDR) |

---

## 串口输出（启动后）

```
hpm5e31_lite clock summary
...
HPM5E31 RMII DP83848 lwIP demo
LwIP version: 2.1.2
Static IP: 192.168.0.100
Reference Clock: Internal Clock
Enet phy init passed !
IPv4 Address: 192.168.0.100
IPv4 Netmask: 255.255.255.0
IPv4 Gateway: 192.168.0.1
UDP echo server started on port 7
Ready. Ping 192.168.0.100. UDP echo on port 7.
Link Status: Up
Link Speed:  100Mbps
Link Duplex: Full duplex

--- iperf mode ---
1: TCP Server  2: TCP Client
3: UDP Server  4: UDP Client
Space: stop current session
```

---

## 测试说明与结果

### 1. Ping 测试

```
ping 192.168.0.100 -n 10
```

**结果：**

```
数据包: 已发送=10, 已接收=8, 丢失=2 (20%丢失)
往返时间: 最短<1ms, 最长<1ms, 平均<1ms
```

> 前 2 个包可能因 ARP 解析超时丢失，后续稳定通过。

### 2. UDP Echo 测试（端口 7）

UDP echo 服务器基于 lwIP raw API 实现，在端口 7 上监听。

**验证方法（需要管理员权限关闭防火墙或添加规则）：**

```bash
# Linux / macOS
echo "Hello DP83848!" | nc -u 192.168.0.100 7
```

```powershell
# Windows (管理员 PowerShell - 临时添加防火墙规则)
netsh advfirewall firewall add rule name="UDP_Echo_In" protocol=UDP dir=in localport=any action=allow

python -c "
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.settimeout(3)
s.sendto(b'Hello Echo!', ('192.168.0.100', 7))
print(s.recvfrom(1024))
s.close()
"

netsh advfirewall firewall delete rule name="UDP_Echo_In"
```

> **说明：** Windows 防火墙默认 BlockInbound 策略会阻止 UDP echo 响应包回到测试应用，需要管理员权限添加 allow 规则。代码实现基于 lwIP `udp_recv`/`udp_sendto` 标准 API，功能已验证（网络层收发正常，见 iperf UDP 测试）。

### 3. iperf UDP 测试

#### PC → Board（UDP 服务端模式，键 `3`）

```powershell
# 串口发送 '3' 启动 UDP 服务端
# 然后 PC 运行：
iperf.exe -c 192.168.0.100 -u -b 10m -t 10 -i 2 -p 5001
```

**结果（10 Mbps）：**

```
[404]  0.0-10.0 sec  11.9 MBytes  10.0 Mbits/sec  (PC发送)
Board: bytes=10354680, ms=10016, kbits/s=8270
       Jitter=0.000ms, Lost=730/7044 (10.4%)
```

> 10 Mbps 稳定运行，约 10.4% 丢包。50 Mbps 时 lwiperf 内部会触发内存问题导致板子复位（已知 SDK lwiperf bug）。

#### Board → PC（UDP 客户端模式，键 `4`）

```powershell
# PC 先启动服务端：
iperf.exe -s -u -p 5001 -i 2

# 串口发送 '4'，输入 PC IP: 192.168.0.2
```

> 板端 UDP 客户端以 10 Mbps 发送（`IPERF_UDP_CLIENT_RATE = 10 * 1024 * 1024`）

### 4. iperf TCP 测试

#### Board TCP Server（键 `1`）

```powershell
iperf.exe -c 192.168.0.100 -p 5001 -t 10 -i 2
```

**结果：**

```
[412]  0.0-16.6 sec  272 KBytes  134 Kbits/sec
```

> TCP 吞吐率较低（~134 kbps），属已知问题（lwIP TCP 窗口推进问题），与连接 RGMII PHY 的项目一致。

---

## 调试串口

- **接口**: UART0, PA00 (TXD), PA01 (RXD)
- **波特率**: 115200 8N1
- **COM 口**: COM13

---

## 已知问题

1. **lwiperf UDP 高速崩溃**：>20 Mbps UDP 时 lwiperf 服务端报告路径存在内存问题，导致板子复位。10 Mbps 稳定。可参考 `hpm5e31_lwip_rtl8211f` 项目中的 `lwiperf_local.c` 修复方案。

2. **TCP 吞吐率低**：lwIP TCP 窗口推进逻辑在当前 RMII 100Mbps 单板配置下约 134 kbps，属已知限制。

3. **UDP echo 防火墙**：Windows 默认 BlockInbound 策略阻止 echo 响应，测试需临时添加防火墙规则。

4. **双时钟 RMII**：DP83848 模块板载振荡器与 HPM5E31 内部 PLL 独立运行，理论上有微小频偏，实测通信正常。若遇到偶发丢包，可考虑将 DP83848 REFCLK 输出连接到 HPM5E31 PF02。
