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

- **DP83848 模块**：板载 50 MHz 有源晶振，为 DP83848 提供 RMII 参考时钟
- **共享时钟方案**：DP83848 模块的 50 MHz 晶振输出同时连接到 HPM5E31 PF02（ETH0_TXCK）
  - `BOARD_ENET_RMII_INT_REF_CLK = 0`（外部时钟模式，内部 PLL 仍配置 50MHz 供 MAC 总线）
  - PF02 配置为 `ETH0_TXCK`（无 LOOP_BACK，作为 50MHz 输入）
- **免去频偏问题**：MAC 和 PHY 共用同一 50 MHz 时钟源 → 0% 时钟漂移丢包

> ⚠️ **硬件接线要求**：DP83848 模块 50MHz 晶振输出引脚 → HPM5E31 PF02（+ 共地）

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
│       ├── board.h          # ENET RMII 复位 GPIO 改为 PF14; INT_REF_CLK=0(外部时钟)
│       ├── board.c          # 板级初始化; 外部时钟模式也配置 PLL2→50MHz 供 MAC 总线
│       ├── pinmux.c         # init_enet0_pins_rmii 适配 DP83848 (PF14 复位, PF02 外部时钟)
│       ├── pinmux.h
│       ├── CMakeLists.txt   # 使用 CONFIG_ENET_PHY_DP83848
│       └── hpm5e31_lite.yaml
├── inc/
│   ├── common_cfg.h         # ENET DMA 缓冲区配置 (24×1536 B TX/RX)
│   └── lwipopts_app.h       # lwIP 参数优化 (MEM_SIZE=24K, 24/24 descriptors)
├── src/
│   ├── lwip.c               # 主程序: UDP echo server + 非阻塞 iperf 菜单
│   ├── lwiperf_local.c      # 本地修改版 lwiperf (修复见下方)
│   └── ethernetif_local.c   # 本地修改版 ethernetif (TX pbuf 链拷贝修复)
├── CMakeLists.txt
├── build.ps1
└── README.md
```

### 软件修复清单

| 修复 | 文件 | 说明 |
|------|------|------|
| TX pbuf 链拷贝 | `ethernetif_local.c` | 原 SDK 代码只保留多段 pbuf 的最后一段 payload；改为整条链连续拷贝到 DMA buffer |
| RX pool OOM 清理 | `ethernetif_local.c` | pbuf_alloced_custom 失败时释放 pool 项和描述符，避免半占用 |
| tcp_err 悬空 PCB | `lwiperf_local.c` | tcp_err 中将 conn_pcb 置 NULL 再 close，防止使用已释放的 PCB 指针导致内存破坏 |
| TCP settings 分片 | `lwiperf_local.c` | 24-byte settings header 分段到达时累计接收而非直接报错关闭 |
| UDP close 超时清理 | `lwiperf_local.c` | 关闭前用 sys_untimeout 取消待处理 report 回调，避免回调访问已释放状态 |
| pbuf_clone OOM | `lwiperf_local.c` | pbuf_clone 失败时优雅关闭而非 LWIP_ASSERT 挂死 |
| UDP 新客户端 OOM | `lwiperf_local.c` | OOM 时尝试复用已有客户端状态而非直接丢弃报文

### 关键寄存器配置

| 寄存器 | 值 | 说明 |
|--------|-----|------|
| clock_eth0 | 50 MHz (PLL2 → 250MHz /5) | MAC AHB 总线时钟（外部时钟模式也配置） |
| ENET_CTRL2.ENET0_RMII_TXCLK_SEL | 1 | 选择 RMII TXCLK（外部时钟源） |
| ENET_CTRL2.ENET0_REFCLK_OE | 0 | 不输出 REFCLK（外部时钟源驱动 PF02） |
| DP83848 BMCR | ANE=1 | 自动协商使能 |
| DP83848 PHYSTS | 读取链路状态/速度/双工 |
| ENET TX/RX descriptors | 24 / 24 | 每描述符 1536 B |

---

## 编译与烧录

```powershell
# 编译
.\build.ps1 -Action rebuild -Config Debug

# 烧录
.\build.ps1 -Action flash -Config Debug
```

**内存占用（Debug 构建）：**

| 区域 | 占用 | 大小 | 使用率 |
|------|------|------|--------|
| FLASH | 135 KB | 1 MB | 12.9% |
| ILM | 1.6 KB | 128 KB | 1.2% |
| DLM | 92 KB | 128 KB | 71.7% |
| AXI_SRAM | 74 KB | 192 KB | 38.3% |
| AXI_SRAM_NONCACHEABLE | 1.5 KB | 64 KB | 2.3% |

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
Reference Clock: External Clock
Enet phy init passed !
IPv4 Address: 192.168.0.100
IPv4 Netmask: 255.255.255.0
IPv4 Gateway: 192.168.0.1
UDP echo server started on port 7
Ready. Ping 192.168.0.100. UDP echo on port 7.
Press 1-4 to start iperf once link is up.
Link Status: Up
Link Speed:  100Mbps
Link Duplex: Full duplex

--- iperf mode ---
1: TCP Server  2: TCP Client
3: UDP Server  4: UDP Client
Space: stop current session
```

---

## 测试结果

### 1. Ping 测试

```
ping 192.168.0.100 -n 5
```

**结果：**

```
数据包: 已发送=5, 已接收=5, 丢失=0 (0%丢失)
往返时间: 最短<1ms, 最长<1ms, 平均<1ms
```

> 共享时钟方案解决了双时钟频偏问题，连续多轮 ping 均 0% 丢包。无 ARP 超时丢失。

### 2. UDP Echo 测试（端口 7）

UDP echo 服务器基于 lwIP raw API 实现在端口 7 上监听。PC 发送字节内容原样返回。

**验证：**

```python
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(('192.168.0.2', 0))
s.settimeout(3)
s.sendto(b'Hello DP83848!', ('192.168.0.100', 7))
data, addr = s.recvfrom(1024)
print(f'Echo: {data} from {addr}')  # 应返回原内容
s.close()
```

**实测结果：** `Echo: b'Hello DP83848!' from ('192.168.0.100', 7)`

### 3. iperf UDP 测试（PC → Board）

串口发送 `3` 启动 UDP 服务端，在 PC 运行 iperf：

```powershell
iperf.exe -c 192.168.0.100 -u -b 10m -t 10 -i 2 -p 5001
```

| 发送速率 | 实测带宽 | 丢包率 | Jitter | 说明 |
|---------|---------|--------|--------|------|
| 10 Mbps | 10.0 Mbps | 0% | 0.000 ms | 稳定 ✅ |
| 20 Mbps | 20.0 Mbps | 0% | — | 稳定 ✅ |
| 60 Mbps | 60.0 Mbps | 0% | 0.000 ms | 稳定 ✅ |
| 85 Mbps | 84.6 Mbps | — | — | 接近线速上限 |

> 对比改进前：10 Mbps 下 10.4% 丢包、50 Mbps 时板复位。共享时钟 + lwiperf/ethernetif 修复后全速率无丢包稳定运行。

### 4. iperf TCP 测试（Board Server）

串口发送 `1` 启动 TCP 服务端，在 PC 运行 iperf：

```powershell
iperf.exe -c 192.168.0.100 -p 5001 -t 10 -i 2
```

**结果：**

```
[412] local 192.168.0.2 port 10432 connected with 192.168.0.100 port 5001
[ ID] Interval       Transfer     Bandwidth
[412]  0.0-10.0 sec   113 MBytes  94.6 Mbits/sec
```

> 对比改进前：~134 kbps → **94.6 Mbps**（提升约 700 倍）。TX pbuf 链拷贝、lwiperf tcp_err 释放、settings 分片累计三项修复共同使 TCP 路径正常工作。

### 5. 测试总结

| 测试项 | 改进前 | 改进后 | 变更 |
|--------|--------|--------|------|
| Ping 丢包 | 10~20%（ARP+频偏） | 0% | 共享时钟 |
| UDP 10 Mbps 丢包 | 10.4% | 0% | 共享时钟 |
| UDP 50 Mbps | 板复位 | 49.3 Mbps, 稳定 | lwiperf 修复 |
| TCP 带宽 | ~134 kbps | 94.6 Mbps | TX 链拷贝 + lwiperf 修复 |

---

## 调试串口

- **接口**: UART0, PA00 (TXD), PA01 (RXD)
- **波特率**: 115200 8N1
- **COM 口**: COM13

---

## 已知问题 / 限制

1. **iPerf 客户端结束时的 `Connection reset by peer`**：此消息出现在 iperf 客户端 10 秒测试结束后，原因是板端 lwiperf 发送完报告 TCP 数据后关闭连接，PC iperf 检测到 RST。不影响测试数据完整性，属于预期行为。

2. **UDP > 85 Mbps / TCP > 95 Mbps**：受限于 100Mbps RMII 链路物理层开销（IFG、前导码、帧间隙），UDP 应用层吞吐上限约 94 Mbps，TCP 约 94 Mbps。当前结果已接近线速。
