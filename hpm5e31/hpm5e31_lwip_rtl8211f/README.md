# hpm5e31_lwip_rtl8211f

## 1. 项目定位

本工程用于验证 HPM5E31 通过 ENET0 RGMII 连接外部 RTL8211FI 的最小可用以太网基线，目标是：

- 不修改 SDK 本体，只在当前工程内完成板级、PHY 和应用层适配
- 固化静态 IP 192.168.0.68，可被主机 192.168.0.2 正常 ping 通
- 提供 UDP echo server
- 提供 iperf server，用于 UDP/TCP 连通性与吞吐测试

当前已经完成并实测通过的发布基线为：100Mbps 全双工、关闭自动协商、RTL8211F 内部 TX/RX 2ns delay 打开。

## 2. RTL8211FI 简介

RTL8211FI 是一颗千兆以太网 PHY，负责：

- MAC 侧通过 RGMII 与 HPM5E31 ENET0 相连
- 线缆侧通过 RJ45 与 PC 网卡进行物理层协商
- 管理侧通过 MDC/MDIO 暴露寄存器访问接口

本工程中用到的关键点：

- PHY ID 通过寄存器 2/3 识别
- BMCR 用于软件配置 100Mbps 全双工和自动协商开关
- page 0xD08 的 TXCR/RXCR 用于 RTL8211F RGMII internal delay 配置

## 3. 硬件连接

### 3.1 当前接线

| RTL8211FI 信号 | HPM5E31 引脚 | 结论 |
| --- | --- | --- |
| ETH_TXCK | PF02 | 正确 |
| ETH_TXD0 | PF03 | 正确 |
| ETH_TXD1 | PF04 | 正确 |
| ETH_TXD2 | PF05 | 正确 |
| ETH_TXD3 | PF06 | 正确 |
| ETH_TXEN / TX_CTL | PF07 | 正确 |
| ETH_RXDV / RX_CTL | PF08 | 正确 |
| ETH_RXD0 | PF09 | 正确 |
| ETH_RXD1 | PF10 | 正确 |
| ETH_RXD2 | PF11 | 正确 |
| ETH_RXD3 | PF12 | 正确 |
| ETH_RXCK | PF13 | 正确 |
| ETH_MDIO | PA30 | 正确 |
| ETH_MDC | PA31 | 正确 |
| ETH_NRST | PF26 | 正确 |

这些信号与 HPM5E31 ENET0 的本地板级 pinmux 配置一致，软件侧已按这组连线落地。

### 3.2 实测 strap 结果

用户预估的默认 strap 是：

- PHYAD[2:0] = 011
- MAC 接口模式 = RGMII

实际上板后，当前工程在复位后扫描 MDIO 地址 0..7，检测结果为：

- RTL8211FI 实际 PHY 地址 = 0
- 因此当前硬件 strap 不是 011，而是更接近 000

换句话说：

- RGMII 模式是成立的，否则不会完成当前链路 bring-up
- PHY 地址 3 的假设不成立，如果你希望固定为 3，需要检查 RTL8211FI 的 PHYAD strap 电阻/电平配置

## 4. 工程架构

- boards/hpm5e31_lite/
  - 本地板级目录
  - RGMII pinmux、PF26 PHY reset、时钟与 build board 选择都在这里
- common/single/common.c
  - ENET 控制器初始化
  - RTL8211 地址扫描
  - RTL8211F 链路状态读取
  - RTL8211F RGMII delay 配置
- common/netinfo.h
  - 静态 IP、远端主机 IP、MAC 地址常量
- src/lwip.c
  - 应用入口
  - 自动启动 UDP echo server 和 iperf TCP/UDP server
- src/app/udp_echo.c
  - UDP echo 实现，端口 5005
- build.ps1
  - configure/build/rebuild/flash/gdbserver/clean 一体脚本

## 5. 关键软件配置

### 5.1 板级引脚与复位

- ENET0 RGMII data/clock: PF02-PF13
- MDC/MDIO: PA31 / PA30
- PHY reset: PF26

软件行为：

- 启动时把 PF26 配成 GPIO 输出
- 复位流程：拉低 10ms，拉高后再等待 50ms

### 5.2 PHY 地址探测

本工程没有把 RTL8211 地址写死。
启动后会：

- 扫描 MDIO 地址 0..7
- 读取寄存器 2/3 判断 PHY ID
- 发现实际地址后再继续执行 PHY 初始化

当前实测结果：地址 0。

### 5.3 PHY 工作模式

当前发布基线采用：

- 关闭自动协商
- 强制 100Mbps Full duplex

原因：

- 当前硬件是手工飞线，优先先拿到稳定可复现的 ping / UDP echo / UDP iperf 基线
- 100Mbps 对 RGMII 时序容忍度更高，便于先完成功能验证

### 5.4 RTL8211F RGMII delay

参考 Linux Realtek PHY 驱动，本工程在 PHY 侧启用了 internal delay：

- page 0xD08, reg 0x11, bit8: TX delay = 2ns
- page 0xD08, reg 0x15, bit3: RX delay = 2ns

当前 HPM MAC 侧 delay 仍保持 0/0，RGMII delay 由 PHY 提供。

这是当前 ping 能稳定通过的关键修正之一。

### 5.5 链路状态读取

HPM SDK 自带 RTL8211 状态解码对当前 RTL8211FI 板卡并不完全适用，会把已建立的链路误判成 Down。
本工程在本地 common.c 中改成了：

- 用标准 BMSR link bit 判链路是否 Up
- 在固定 100Mbps 全双工基线下，直接把 PHY 状态同步到 MAC

因此不需要修改 SDK 目录内任何文件。

## 6. 网络参数

- 板端 IP: 192.168.0.68
- 主机 IP: 192.168.0.2
- 板端 MAC: 02:00:5E:31:00:68
- 子网掩码: 255.255.255.0
- 网关: 0.0.0.0
- UDP echo 端口: 5005
- iperf TCP/UDP server 端口: 5001

## 7. 构建与下载

### 7.1 构建

在工程根目录执行：

```powershell
.\build.ps1 -Action rebuild -Config Debug
```

### 7.2 下载

```powershell
.\build.ps1 -Action flash -Config Debug
```

当前实测下载环境：

- J-Link 路径: C:\Program Files\SEGGER\JLink_V844
- J-Link SN: 1120000012
- UART0 串口: COM61

## 8. 测试方法

### 8.1 启动日志

打开 COM61，115200 8N1，复位后可看到如下关键信息：

- MDIO 扫描出的 RTL8211 实际地址
- RGMII internal delay 是否启用
- Link Status / Link Speed / Link Duplex

### 8.2 ping

```powershell
ping -n 6 192.168.0.68
```

### 8.3 UDP echo

可用 PowerShell UdpClient 发送测试报文到 5005：

```powershell
$client = New-Object System.Net.Sockets.UdpClient
$bytes = [System.Text.Encoding]::ASCII.GetBytes('udp-echo-from-pc')
[void]$client.Send($bytes, $bytes.Length, '192.168.0.68', 5005)
```

### 8.4 UDP iperf

```powershell
D:\hpm\embedded\hpm5e31\bak\iperf.exe -u -c 192.168.0.68 -b 10M -t 5
```

### 8.5 TCP iperf

```powershell
D:\hpm\embedded\hpm5e31\bak\iperf.exe -c 192.168.0.68 -t 5
```

## 9. 实测结果

测试日期：2026-06-10

### 9.1 构建与下载

- Debug 构建：通过
- Debug 下载：通过
- 下载目标：J-Link SN 1120000012

### 9.2 串口与链路

实测串口日志关键片段：

```text
RTL8211 detected at MDIO address 0 (ID1=0x001C ID2=0xC916)
RTL8211 strap differs from configured default address 3
RTL8211F RGMII internal delay enabled: TX=2ns RX=2ns
Enet phy init passed !
Link Status: Up
Link Speed:  100Mbps
Link Duplex: Full duplex
```

主机侧 Intel I350-T4 网卡同步显示：

```text
Status               : Up
LinkSpeed            : 100 Mbps
MediaConnectionState : Connected
```

### 9.3 ping

结果：通过

```text
正在 Ping 192.168.0.68 具有 32 字节的数据:
来自 192.168.0.68 的回复: 字节=32 时间<1ms TTL=64
来自 192.168.0.68 的回复: 字节=32 时间<1ms TTL=64
来自 192.168.0.68 的回复: 字节=32 时间<1ms TTL=64
来自 192.168.0.68 的回复: 字节=32 时间<1ms TTL=64
来自 192.168.0.68 的回复: 字节=32 时间<1ms TTL=64
来自 192.168.0.68 的回复: 字节=32 时间<1ms TTL=64
```

### 9.4 UDP echo

结果：通过

```text
Remote : 192.168.0.68:5005
Reply  : udp-echo-from-pc
```

### 9.5 UDP iperf

结果：通过

PC 侧输出：

```text
Client connecting to 192.168.0.68, UDP port 5001
[412] local 192.168.0.2 port 60632 connected with 192.168.0.68 port 5001
[412]  0.0- 5.0 sec  5.96 MBytes  10.0 Mbits/sec
[412] Server Report:
[412]  0.0- 5.0 sec  5.85 MBytes  9.83 Mbits/sec  0.005 ms   -6/ 4175 (-0.14%)
[412]  0.0- 5.0 sec  41 datagrams received out-of-order
```

板端串口输出：

```text
New UDP client (settings flags 0x0)
New UDP client (settings flags 0xa000000)
Sending report back to client (0x80).
Jitter 0.005, Lost 35/4175 datagrams, OoO 41
iperf report:
type=6
remote_port=60632
total_bytes=6137250
duration_ms=5011
kbits_per_s=9798
```

结论：10Mbps 档 UDP iperf server 已可稳定工作，实际吞吐约 9.8-10.0 Mbps。

### 9.6 TCP iperf

结果：未通过

PC 侧输出：

```text
Client connecting to 192.168.0.68, TCP port 5001
[420] local 192.168.0.2 port 9256 connected with 192.168.0.68 port 5001
write failed: Connection reset by peer
read on server close failed: Connection reset by peer
[420]  0.0- 2.4 sec   104 KBytes   358 Kbits/sec
```

板端串口在该轮 TCP 测试中没有输出对应的 iperf report，说明当前 TCP server 仍未稳定跑通。

## 10. 当前结论

当前工程已经完成以下目标：

- HPM5E31 + RTL8211FI 的 RGMII 基础 bring-up
- 静态 IP 192.168.0.68 可 ping 通
- UDP echo 可用
- UDP iperf server 可用
- 不修改 SDK，仅在当前工程内完成适配

当前尚未完成的部分：

- TCP iperf server 稳定性
- 自动协商 / 1Gbps 稳定配置

## 11. 后续建议

- 如果你硬件上原本想把 PHYAD 配成 3，需要检查 RTL8211FI 的 strap 电阻或 strap 引脚电平，因为当前实测地址是 0。
- 如果后续目标是恢复 1Gbps，需要继续沿 RTL8211F 的状态寄存器和 RGMII delay 做细调，而不是直接回退到 SDK 默认 RTL8211 配置。
- 如果后续目标是补齐 TCP iperf，建议优先围绕当前工程本地拷贝的 ethernetif.c 与 lwiperf 交互行为做抓包/日志，而不是先动 SDK。
