# hpm5e31_lwip_rtl8211f

## 1. 项目定位

本工程用于验证 HPM5E31 通过 ENET0 RGMII 连接外部 RTL8211FI 的单板以太网基线，要求全部在当前工程内完成，不修改 SDK 目录。

当前已经完成的目标：

- 静态 IP 192.168.0.68 可稳定 ping 通
- UDP echo server 可用
- 自动协商可起 1Gbps Full duplex
- UDP iperf 在 1Gbps 链路下已达到稳定可复现的 100M 档
- TCP iperf 不再 reset / timeout，已能完整结束并打印 server report

当前推荐发布基线：

- 自动协商开启
- 板端与主机协商为 1Gbps Full duplex
- HPM MAC RGMII delay = TX 0 / RX 0
- RTL8211FI PHY internal delay = TX 2ns / RX 2ns
- 使用当前工程本地 `src/lwiperf_local.c`，不修改 SDK 原文件

## 2. 公开代码对照结论

这轮排查额外对照了公开代码，得到三个关键结论：

- Linux Realtek PHY 驱动对 RTL8211F 的状态解码使用的是低位 vendor status 编码，而不是 HPM SDK 当前 RTL8211 驱动里那套高位 `0xC000` 速度位定义
- Linux 驱动明确支持通过 page `0xD08` 配置 RTL8211F 的 TX/RX internal delay
- 现有公开例子并不完全一致：
  - `tc387` 的 RTL8211F 示例会开 TX delay
  - HPM SDK 的 `hpm6750evk2` 板级默认给 MAC `TX/RX = 0/7`

本项目在实机上逐轮试验后，最终选定的是：

- MAC `0/0`
- PHY `TX/RX = on/on`

这组参数是当前这块 HPM5E31 + RTL8211FI 手工飞线硬件上效果最好的组合。

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

这组连线与 HPM5E31 ENET0 的本地 pinmux 配置一致。

### 3.2 实测 strap 结果

用户预估默认 strap：

- PHYAD[2:0] = 011
- MAC 接口模式 = RGMII

实际上板后，本工程在复位后扫描 MDIO 地址 0..7，检测结果为：

- RTL8211FI 实际 PHY 地址 = 0

因此：

- RGMII 模式成立
- PHY 地址 3 的假设不成立

如果后续需要把地址固定为 3，需要检查 RTL8211FI 的 PHYAD strap 电阻/电平配置。

## 4. 工程架构

- `boards/hpm5e31_lite/`
  - 本地板级目录
  - PF26 PHY reset
  - MAC 侧 RGMII delay 配置
- `common/single/common.c`
  - ENET 控制器初始化
  - RTL8211 地址扫描
  - RTL8211FI 状态寄存器读取
  - PHY 侧 RGMII delay 配置
- `inc/common_cfg.h`
  - RGMII 调参宏
- `src/lwip.c`
  - 应用入口
  - 自动启动 UDP echo server 和 iperf TCP/UDP server
- `src/lwiperf_local.c`
  - SDK `lwiperf.c` 的本地化修复版，符号改为 `app_lwiperf_*`
- `src/app/udp_echo.c`
  - UDP echo 实现，端口 5005
- `build.ps1`
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

### 5.3 自动协商与状态寄存器

最终稳定配置已经恢复为自动协商。

关键修正点不是 BMCR，而是状态读取：

- `BMSR` 用于判断链路是否 up
- 速度/双工主来源改为寄存器 `0x1A`
- 速度/双工位定义采用与 Linux Realtek 驱动一致的低位编码方式

当前链路稳定后会打印类似：

```text
RTL8211 status probe: bmsr=0x79AD reg1A=0x312E reg11=0x0000
Link Status: Up
Link Speed:  1000Mbps
Link Duplex: Full duplex
```

其中 `reg1A=0x312E` 的低位编码对应 1000Mbps Full duplex；而 `reg11` 在当前 RTL8211FI 上并不适合作为主状态来源。

### 5.4 RTL8211FI RGMII delay

最终选定的稳定组合：

- HPM MAC delay: `TX=0`, `RX=0`
- RTL8211FI internal delay: `TX=2ns`, `RX=2ns`

PHY 侧 delay 通过 page `0xD08` 配置：

- reg `0x11`, bit8: TX delay
- reg `0x15`, bit3: RX delay

这组组合在当前硬件上明显优于：

- MAC `0/7` + PHY delay off
- MAC `0/7` + PHY TX only

### 5.5 本地 lwiperf 修复

为了不修改 SDK 原文件，同时修复当前 1G iperf 下的已知问题，本工程新增了 `src/lwiperf_local.c`，并只在当前应用里调用 `app_lwiperf_*` 私有符号。

本地修复点：

- `lwiperf_udp_search_client()` 增加 fallback reuse，减少重复 `New UDP client`
- `lwiperf_udp_close()` 中增加 `sys_untimeout()`，避免悬挂 report timeout
- UDP report clone 失败时不再触发 assert，而是 graceful close
- `lwiperf_tcp_err()` 不再走会访问失效 `conn_pcb` 的 close 路径

## 6. 网络参数

- 板端 IP: `192.168.0.68`
- 主机 IP: `192.168.0.2`
- 板端 MAC: `02:00:5E:31:00:68`
- 子网掩码: `255.255.255.0`
- 网关: `0.0.0.0`
- UDP echo 端口: `5005`
- iperf TCP/UDP server 端口: `5001`

## 7. 构建与下载

### 7.1 构建

```powershell
.\build.ps1 -Action rebuild -Config Debug
```

### 7.2 下载

```powershell
.\build.ps1 -Action flash -Config Debug
```

当前实测下载环境：

- J-Link: `C:\Program Files\SEGGER\JLink_V844`
- J-Link SN: `1120000012`
- UART0 串口: `COM61`

## 8. 测试方法

### 8.1 ping

```powershell
ping -n 12 192.168.0.68
```

### 8.2 UDP echo

```powershell
$client = New-Object System.Net.Sockets.UdpClient
$bytes = [System.Text.Encoding]::ASCII.GetBytes('udp-echo-1g')
[void]$client.Send($bytes, $bytes.Length, '192.168.0.68', 5005)
```

### 8.3 UDP iperf

```powershell
D:\hpm\embedded\hpm5e31\bak\iperf.exe -u -c 192.168.0.68 -b 20M -t 5
D:\hpm\embedded\hpm5e31\bak\iperf.exe -u -c 192.168.0.68 -b 50M -t 5
D:\hpm\embedded\hpm5e31\bak\iperf.exe -u -c 192.168.0.68 -b 100M -t 5
```

### 8.4 TCP iperf

```powershell
D:\hpm\embedded\hpm5e31\bak\iperf.exe -c 192.168.0.68 -t 5
```

## 9. 实测结果

测试日期：2026-06-10

### 9.1 构建与下载

- Debug 构建：通过
- Debug 下载：通过
- 下载目标：J-Link SN `1120000012`

### 9.2 1Gbps 链路

板端串口：

```text
RTL8211 detected at MDIO address 0 (ID1=0x001C ID2=0xC916)
RTL8211 strap differs from configured default address 3
RTL8211F RGMII delay config: PHY_TX=2ns PHY_RX=2ns, MAC_TX=0 MAC_RX=0
RTL8211 status probe: bmsr=0x79AD reg1A=0x312E reg11=0x0000
Link Status: Up
Link Speed:  1000Mbps
Link Duplex: Full duplex
```

主机侧 Intel I350-T4：

```text
Status               : Up
LinkSpeed            : 1 Gbps
MediaConnectionState : Connected
```

### 9.3 ping

结果：通过

```text
正在 Ping 192.168.0.68 具有 32 字节的数据:
来自 192.168.0.68 的回复: 字节=32 时间<1ms TTL=64
...
12/12 成功
```

### 9.4 UDP echo

结果：通过

```text
Remote : 192.168.0.68:5005
Reply  : udp-echo-1g
```

### 9.5 UDP iperf

结果：通过，且当前 1G 基线已经稳定到 100M 档。

代表性结果：

- `20M` 档
  - PC server report: `18.9 Mbits/sec`, `30/8047 (0.37%)`
  - 板端 report: `kbits_per_s=18873`
- `50M` 档
  - PC server report: `47.7 Mbits/sec`, `114/20264 (0.56%)`
  - 板端 report: `kbits_per_s=47518`
- `100M` 档
  - PC server report: `95.8 Mbits/sec`, `311/40692 (0.76%)`
  - 板端 report: `kbits_per_s=95421`

连续 3 轮 `100M` UDP 回归结果：

- Round 1: `97.8 Mbits/sec`, `126/41540 (0.30%)`
- Round 2: `97.8 Mbits/sec`, `118/41565 (0.28%)`
- Round 3: `97.7 Mbits/sec`, `126/41532 (0.30%)`

说明：当前工程在 1Gbps Full duplex 链路上，`100M` UDP iperf 已达到稳定可复现。

### 9.6 TCP iperf

结果：不再 reset / timeout，已能完整结束并打印 server report，但吞吐仍然偏低，当前不作为性能发布项。

代表性结果：

- PC 侧：

```text
Client connecting to 192.168.0.68, TCP port 5001
[400]  0.0- 6.3 sec   496 KBytes   649 Kbits/sec
```

- 板端：

```text
iperf report:
type=0
remote_port=8326
total_bytes=507928
duration_ms=6249
kbits_per_s=648
```

当前判断：

- TCP 会话稳定性已经明显好于最初的 `Connection reset by peer`
- 但 TCP 吞吐还很低，后续如果要继续优化，需要单独沿 `lwiperf` TCP 路径和 `ethernetif.c` 收发调度继续挖

## 10. 当前结论

当前工程已经完成以下目标：

- RTL8211FI 自动协商 1Gbps Full duplex bring-up
- 静态 IP `192.168.0.68` 可稳定 ping 通
- UDP echo 可用
- UDP iperf 在 1Gbps 链路下稳定跑到 `100M` 档，且连续 3 轮可复现
- TCP iperf 已从早期 reset 改进到可完整结束并出 report
- 全部改动均在当前工程内完成，SDK 原目录未改动

## 11. 后续建议

- 如果要把 UDP 继续往 `200M+` 推，建议继续围绕当前已稳定的 `MAC 0/0 + PHY TX/RX on` 组合做 ring / pbuf / poll 调优，不要再回到错误的状态寄存器解码路径。
- 如果要把 TCP 吞吐做上来，优先继续查 `app_lwiperf_*` TCP 路径，而不是 PHY bring-up 本身；当前 PHY/MAC 链路层已经足够稳定。

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
