# tc397_selftest — TC397 综合外设自检工程

把之前 8 个独立验证过的工程合并成**一个固件**，提供：

- **出厂自检**（`selftest`）：一次跑完 9 个外设，逐项 PASS/FAIL，最后给出汇总和**失败外设清单**；
- **接口/性能测试**（`bench`）：UART / CAN / FlexRay / SD 的吞吐实测数据；
- **外设状态汇总**（`stat`）：不跑测试，只打印各外设当前实时状态；
- 原有的所有调试命令（CAN 12 路、FlexRay、SD、TLF35584、LAN8651、GETH/PHY、ADC、网络 ping 等）全部保留。

> 合并来源：`tc397_adc`、`tc397_can_x12`、`tc397_flexray`、`tc397_lan8651_t1s`、
> `tc397_lwip_iperf`、`tc397_sdmmc`、`tc397_tlf35584`、`tc397_uart_lettershell`。
> 每个子工程的 README 仍是该外设的详细文档，本 README 只讲合并后的工程。
> **8 个原工程未被修改**，所有改动都在 `tc397_selftest/` 内（见第 8 节）。

---

## 1. 硬件与 bench

| 项 | 内容 |
| --- | --- |
| 板子 | TC397XX AppKit（LFBGA292，3V3） |
| 调试串口 | **COM169**（USB-Enhanced-SERIAL CH343，`VID_1A86&PID_55D3`），921600-8N1 |
| 调试器 | DAP MiniWiggler（`VID_058B&PID_0043`）+ TAS Server |
| 烧录 | `C:\Infineon\AURIX-Studio-1.10.36\tools\AurixFlasherSoftwareTool_v3.0.18\AURIXFlasher.exe` |
| TF 卡 | 32GB microSD，FAT32（≈29818 MiB，cluster 64 扇区 = 32 KB） |
| CAN | 12 路，两两相连 CAN0-CAN1 … CAN10-CAN11，终端电阻已接 |
| FlexRay | FR0A(ERAY0) 与 FR1A(ERAY1) 的 A 通道相连，终端电阻已接 |
| 10BASE-T1S | LAN8651，接 USB-10BASE-T1S 转换器到 PC（PC `以太网 16` = 192.168.1.1） |
| 1000BASE-T1 | YT8011AN（RGMII），接千兆车载以太网转换器（1000M Master）到 PC（PC `以太网 2` = 192.168.0.2） |
| TLF35584 | MPS 已拉高 = TestMode（INIT 看门狗停止，可直接跑初始化序列） |

### 引脚分配（合并后）

| 外设 | 引脚 |
| --- | --- |
| ASCLIN0 控制台 | TX P14.0 / RX P14.1 |
| EVADC | AN0..AN47（模拟输入，无数字冲突） |
| MCMCAN ×12 | CAN0 P34.1/P33.12，CAN1 P15.2/P33.10，CAN2 P32.5/P32.6，CAN3 P32.3/P32.2，CAN4 P00.0/P00.1，CAN5 P23.6/P23.7，CAN6 P23.2/P23.3，CAN7 P33.4/P33.5，CAN8 P10.6/P34.2，CAN9 P00.2/P00.3，CAN10 P22.8/P32.7，CAN11 P22.10/P22.11 |
| ERAY0 (FR0A) | TX P02.0 / TXEN P02.4 / RX P02.1 |
| ERAY1 (FR1A) | TX P14.10 / TXEN P14.9 / RX P14.8 |
| QSPI2 → TLF35584 | SCLK P15.8 / MOSI P15.6 / MISO P15.7 / nCS(SLSO1) P14.2；WDI P14.3，SS1 P33.9，ERR P33.8 |
| SDMMC0 | CLK P15.1 / CMD P15.3 / DAT0-3 P20.7/8/10/11；CD P10.7 |
| QSPI4 → LAN8651 | SCLK P22.3 / MOSI P22.0 / MISO P33.13 / nCS(SLSO3) P22.2；nRST P23.4，nINT P33.7 |
| GETH (RGMII) | P11.x + P12.x（详见 `Configurations/Configuration.h`）；YT8011 nRST P20.1，nINT P10.8 |
| LED | P13.0（低有效） |

引脚之间无冲突（唯一需要留意的：CAN1 用 P15.2，而 QSPI2 的 TLF35584 用 P15.6/15.7/15.8、
SDMMC 用 P15.1/15.3，互不重叠）。

### 网络地址

| 网口 | lwIP netif | MAC | IP |
| --- | --- | --- | --- |
| LAN8651 10BASE-T1S | `t11`（netif 0，第二个加入） | 02:00:00:10:BA:5E | 192.168.1.100/24，GW 192.168.1.1 |
| GETH 1000BASE-T1 | `en0`（netif 1，default route） | DE:AD:BE:EF:FE:ED | 192.168.0.100/24，GW 192.168.0.1 |

**一个 lwIP 栈、两个 netif**。服务：iperf2 server TCP 5001、UDP echo 9、GETH 统计 UDP 5002 / sink 5003。

---

## 2. 构建

```powershell
cd C:\github\embedded\tc397\tc397_selftest

.\build.ps1 -Compiler tasking                      # TASKING TriCore v6.3r1（本工程主用）
.\build.ps1 -Compiler tasking -Action rebuild      # 清理重编（推荐）
.\build.ps1 -Compiler tasking -Action download     # 编译+烧录+复位运行
.\build.ps1 -Compiler tasking -BuildType Release
.\build.ps1 -Action download                       # GCC 版编译+烧录
```

- Ubuntu/GCC 侧仍可用 `./build.sh`（原样保留，双工具链共存）。
- 路径可用 `-AurixStudioPath` / `-TaskingPath` 覆盖，默认
  `C:\Infineon\AURIX-Studio-1.10.36` 与 `C:\z\app\TASKING\TriCore_v6.3r1`。
- **两种工具链均已编译通过**：TASKING Debug/Release、ADS tricore-gcc11 Debug。

### 内存占用（TASKING Debug）

```
| Memory     | Code     | Data     | Reserved | Free     | Total    |
| mpe:dsram0 | 0x0      | 0x002a304| 0x0003c00| 0x000e0fc| 0x003c000|   <- DSPR0 240 KB，用 169 KB
| mpe:lmuram | 0x0      | 0x0045b38| 0x0      | 0x007a4c8| 0x00c0000|   <- LMU 768 KB，用 279 KB
| mpe:pfls0  | 0x0025c38| 0x000d4bc| 0x0      | 0x02ccf0c| 0x0300000|   <- Flash 3 MB
```

`tc397_selftest.hex` 约 **517 KB**。

**DSPR0 只有 240 KB，而 8 个工程合起来需要约 390 KB**，所以大块缓冲区被显式搬到 LMU：

| 数据 | 位置 | 说明 |
| --- | --- | --- |
| GETH DMA 描述符/缓冲、TSO scratch | LMU `lmuram`（`.lmubss`/`.lmudata`） | 原 `tc397_lwip_iperf` 就这样 |
| lwIP 堆 84 KB | DSPR0 `bss_cpu0` | CPU-only，保持零等待 |
| `g_can[12]` ≈ 84 KB | LMU | 见 `App/can12.c`；CAN Message RAM 本身在 0xF0200000，与这里无关 |
| SD 大缓冲（`s_ioBuf` 8 KB / `s_mkfsWork` 8 KB / `s_bigBuf` 48 KB） | LMU | 见 `Shell/shell_sd.c` |
| 自检 SD 缓冲 32 KB | LMU | 见 `App/selftest.c` |

`s_bigBuf` 从原工程的 320 块（160 KB）降到 **96 块（48 KB）**：仍大于 ADMA2 每页 64 条描述符，
链式描述符路径照样被覆盖，但省下 112 KB。

---

## 3. 命令总览

```
# 综合
selftest [core|uart|adc|can|fr|tlf|sd|t1s|geth|net|all|stat]  出厂自检 / 单项 / 详细
stat                                                          外设状态汇总（不跑测试）
bench [uart|can|fr|sd [MB]]                                   接口性能测试

# 通用
help version ver mcu uid uptime temp sysinfo mem led reset reboot

# ADC
adc [0..47]        adcdbg

# CAN
cansend candump canlive canstat canpair canflood canrst xcvr

# FlexRay
fr init | fr status | fr send <0|1> [hex] | fr recv <0|1> | fr test [n] | fr regs
（flexray / eray 是同一处理器的别名）

# TLF35584
tlf / tlf help

# SD 卡
sd init|info|cd|ls|cat|stat|write|read|rm|mkdir|bench|mkfs|erase|raw|label|free
sd csd|cap|dbg|lim|big|clk|peek|poke|regs|recover|tx

# 网络
ifconfig ping ethstat geth phy phyr phyw ext mmd link ytinit clks

# LAN8651 10BASE-T1S
t1stat t1r t1w plca t1link sqi plcadiag pcsdiag evcnt cable
```

> 与独立工程的两处改名：LAN8651 的 `link` 改名 **`t1link`**（GETH 的 YT8011 PHY 已占用 `link`）；
> LAN8651 的 `ifconfig`/`ping` 不再单独提供，统一由 GETH 版本实现（遍历 `netif_list`，两个网口都能用）。

---

## 4. `selftest` —— 出厂自检

### 4.1 用法

```
selftest            自动跑全部 10 项，只打印结果表（适合产线）
selftest all        同上，但每个外设额外打印证据明细（适合调试）
selftest <key>      只跑一项：core|uart|adc|can|fr|tlf|sd|t1s|geth|net
stat                不跑测试，直接打印各外设当前状态
```

判定标准（都是板上可客观判定的，不需要 PC）：

| # | 项 | 判定 |
| --- | --- | --- |
| 1 | CORE/CLOCK | ChipID ≠ 0，CPU ≥ 100 MHz，STM = 100 MHz |
| 2 | UART0 | 从 `BITCON/BRG` 反算的波特率 = 921600 ±3%（实测 921659），发 64 字节突发成功，RX 无溢出 |
| 3 | ADC | 33 个采样通道全部读成功；VUC/3V3/1V25/0V9 四条电源轨在容差内 |
| 4 | CAN x12 | 6 对线、双向各 1 帧 FD+BRS(1M/5M) 收发一致（每方向允许重试 1 次） |
| 5 | FLEXRAY | FR0A/FR1A 都进 NORMAL_ACTIVE，双向 3 轮 × 2 方向 = 6/6 帧内容一致 |
| 6 | TLF35584 | SPI 链路 OK（DEVSTAT 读回 MISO 有效），DEVSTAT.STATE = NORMAL |
| 7 | SD CARD | `disk_initialize` 无 STA_NOINIT、`f_mount` OK、32 KB 写入→读回逐字节校验一致 |
| 8 | LAN8651 T1S | DEVID 可读，PLCA 使能且 id=1/cnt=8，MAC TXEN=1，PHY 链路或 netif 链路 UP |
| 9 | GETH 1000T1 | `MAC_PHYIF_CONTROL_STATUS`：LNKSTS=1、LNKSPEED=1000M、LNKMOD=full |
| 10 | NET (PC) | 需要 PC 对端，自动运行时恒为 **SKIP**（见第 6 节） |

### 4.2 实测日志（TASKING Debug，2026-09-21）

```
>>> selftest all

==================== TC397 FACTORY SELF TEST ====================
 FW Sep 21 2026 16:13:20  ChipID 0xAF239793  uptime 44959 ms
 #  PERIPHERAL   RESULT  EVIDENCE
-----------------------------------------------------------------
 1 CORE/CLOCK   PASS  ChipID 0xAF239793 CPU 300 MHz STM 100 MHz tick 44959 ms
01234567890123456789012345678901234567890123456789012345678901
  uart: BITCON=0x8C0F0000 BRG=0x001000D9 FRAMECON=0x00010200 -> 921659 baud
  uart: 64 B burst in 10 us (line rate 921600 would need 694 us), overrun 0
 2 UART0        PASS  921659 baud 8N1 (BITCON 0x8C0F0000 FRAMECON 0x00010200), 64 B tx 10 us, overrun 0
  adc : 33 channels sampled, 0 read errors, 0 rails out of range
  adc : VUC=3.31(3.31) 3V3=3.31(3.25) 1V25=1.25(1.24) 0V9=0.92(0.92)
 3 ADC          PASS  33/48 ch ok, 0 err, 0 rail bad [VUC=3.31(3.31) 3V3=3.31(3.25) 1V25=1.25(1.24) 0V9=0.92(0.92) ]
    can0->can1 PASS (id=0x100 len=8 FD+BRS)
    can1->can0 PASS (id=0x140 len=8 FD+BRS)
    can2->can3 PASS (id=0x102 len=8 FD+BRS)
    can3->can2 PASS (id=0x142 len=8 FD+BRS)
    can4->can5 PASS (id=0x104 len=8 FD+BRS)
    can5->can4 PASS (id=0x144 len=8 FD+BRS)
    can6->can7 PASS (id=0x106 len=8 FD+BRS)
    can7->can6 PASS (id=0x146 len=8 FD+BRS)
    can8->can9 PASS (id=0x108 len=8 FD+BRS)
    can9->can8 PASS (id=0x148 len=8 FD+BRS)
    can10->can11 PASS (id=0x10A len=8 FD+BRS)
    can11->can10 PASS (id=0x14A len=8 FD+BRS)
  can : 12/12 links ok (0 fail, 0 retried), FD+BRS 1M/5M, rx overflow 0
 4 CAN x12      PASS  12/12 links ok (0 fail, 0 retried), FD+BRS 1M/5M, rx overflow 0
    fr  : FR0->FR1 slot11 OK (round 0)
    fr  : FR1->FR0 slot12 OK (round 0)
    fr  : FR0->FR1 slot11 OK (round 1)
    fr  : FR1->FR0 slot12 OK (round 1)
    fr  : FR0->FR1 slot11 OK (round 2)
    fr  : FR1->FR0 slot12 OK (round 2)
 5 FLEXRAY      PASS  FR0A=NORMAL_ACTIVE FR1A=NORMAL_ACTIVE, 6/6 frames bidirectional (10Mbit, 5ms cycle)
  tlf : SPI link ok, DEVSTAT 0xFA (NORMAL) SS1=1 IF=0x84 PROTSTAT=0xF1
 6 TLF35584     PASS  SPI link ok, DEVSTAT 0xFA (NORMAL) SS1=1 IF=0x84 PROTSTAT=0xF1
  sd  : init=0x00 mount=OK capacity=29818 MiB (LBA probe)
  sd  : 32 KB write 5825 KB/s read 18801 KB/s verify=OK
 7 SD CARD      PASS  SDHC 29818 MiB FAT32, 32KB w 5825/ r 18801 KB/s, verify OK
  t1s : DEVID 0x00086512 PLCA on id=1 cnt=8 sync=0 phy_link=1 mac_tx=1 sts1=0x0000
 8 LAN8651 T1S  PASS  DEVID 0x00086512 PLCA on id=1 cnt=8 sync=0 phy_link=1 mac_tx=1 sts1=0x0000
  geth: MAC.PHYIF 0x000D0000 link=1 speed=1000M duplex=full netif en0 link-up
 9 GETH 1000T1  PASS  MAC.PHYIF 0x000D0000 link=1 speed=1000M duplex=full netif en0 link-up
10 NET (PC)     SKIP  needs PC: ping 192.168.0.100 (1000T1) / 192.168.1.100 (T1S); iperf TCP 5001 (see README)
-----------------------------------------------------------------
SUMMARY: 9 PASS  0 FAIL  1 SKIP   -> OVERALL PASS
FAILED PERIPHERALS: none
Network throughput needs a PC: see 'bench' and README section 'iperf'.
=================================================================
Return: 0, 0x00000000
```

失败时的输出形状（示例：拔掉 SD 卡跑 `selftest sd`）：

```
 7 SD CARD      FAIL  disk_initialize=0x01 STA_NOINIT (card missing/not responding)
-----------------------------------------------------------------
SUMMARY: 8 PASS  1 FAIL  1 SKIP   -> OVERALL FAIL
FAILED PERIPHERALS: SD CARD
  * SD CARD      disk_initialize=0x01 STA_NOINIT (card missing/not responding)
```

### 4.3 `stat` 外设状态汇总

```
>>> stat

===== PERIPHERAL STATUS SUMMARY (live, no test) =====
 MCU      : ChipID 0xAF239793  CPU 300 MHz  STM 100 MHz  temp 24 C  uptime 226840 ms
 UART0    : ASCLIN0 921600 8N1, rx overrun 0, shell rx pending
 ADC      : EVADC AN0..AN47, VREF 5.0V, 12 bit
 CAN      : 12 nodes MCMCAN, 1M arb / 5M data FD+BRS, xcvr ok
 FLEXRAY  : FR0A NORMAL_ACTIVE / FR1A NORMAL_ACTIVE
 TLF35584 : DEVSTAT 0xFA (NORMAL) SS1=1
 SD       : initialised, capacity 29818 MiB (LBA probe)
 LAN8651  : PLCA on id=1 cnt=8, link UP, irq idle
 GETH     : link 1 speed 1000M duplex full, netif en0 up
 TRAFFIC  : geth rx_ok 91 rx_err 0 tx 1 | t1s netif up
=====================================================
```

FlexRay 未启动时显示 `FR0A not prepared / FR1A not prepared`（正常：跑 `fr init` 或 `selftest fr` 之后才进 NORMAL）。

---

## 5. `bench` —— 接口性能实测

```
>>> bench

=================== ON-BOARD PERFORMANCE ===================
 UART0  : 4096 B in 33725 us (1214 kbps effective); 3072 B are paced by the
         921600 baud line rate -> 33333 us expected (full 4096 B would be 44444 us)
 CAN0->1 : 3450 frames x64B FD+BRS in 500 ms = 6900 frame/s (~3532 kbit/s payload),
           TX-queue busy retries 703286, RX overflow 0 (1 Mbit arb / 5 Mbit data)
 FLEXRAY: 200 frames x16B in 1000 ms = 200 frame/s sustained
          (static slot 11 occurs once per 5 ms cycle -> 200 frame/s per slot;
           the 91 static slots of the 10 Mbit/s cluster carry up to ~18 kframe/s)
 SD     : 2 MB write 190 ms = 10778 KB/s | read 118 ms = 17355 KB/s | verify OK
 ETH    : end-to-end needs the PC side, use the iperf commands in README:
          10BASE-T1S : tools\iperf.exe -c 192.168.1.100 -p 5001 -t 15 -w 32K -M 1024
          1000BASE-T1: tools\iperf.exe -c 192.168.0.100 -p 5001 -t 15 -w 16K
          reference measured values: 8.66 Mbps (T1S) / 359 Mbps (1000T1)
===========================================================
```

### 5.1 性能数据汇总（TASKING Debug，2026-09-21）

| 接口 | 指标 | 实测 |
| --- | --- | --- |
| UART0 (921600 8N1) | 4 KB 连续发送 | 33725 µs（理论 33333 µs，误差 +1.2%，见下面第 2 条） |
| CAN0→CAN1 | 64 B FD+BRS 帧，500 ms 窗口 | **6900 frame/s**，≈ 3532 kbit/s 净荷，RX 无溢出 |
| FlexRay FR0A→FR1A | 静态槽 11，5 ms 周期 | **200 frame/s**（= 1 帧/周期，单槽上限；91 槽理论 ≈18 kframe/s） |
| SD 卡（32 KB 块） | 2 MB 写 / 读 | 写 **10778 KB/s**，读 **17355 KB/s**，校验 OK |
| SD 卡（32 KB 块） | 8 MB 写 / 读 | 写 **8402 KB/s**，读 **17319 KB/s**，校验 OK |
| SD 卡（8 KB 块，`sd bench 8`） | 8 MB 写 / 读 | 写 2273 KB/s，读 12100 KB/s |
| 10BASE-T1S | iperf2 TCP，PC 侧 | **8.79 Mbits/sec**（15.8 MB / 15 s） |
| 1000BASE-T1 | iperf2 TCP，PC 侧 | **342 Mbits/sec**（613 MB / 15 s） |
| 10BASE-T1S / 1000BASE-T1 | PC↔板 ping | 4/4，<1 ms |
| 1000BASE-T1 | 板→PC ping | 4/4，0~1 ms |

**两个值得注意的实测结论**

1. **SD 写速度对缓冲区大小极其敏感**：FAT32 的 cluster 是 32 KB，用 8 KB 缓冲写会退化成
   "读-改-写"（2273 KB/s），换成 32 KB（= 一个 cluster）就变成 8402~10778 KB/s，**快约 4.5 倍**。
   自检和 `bench sd` 因此都用 32 KB 缓冲；`sd bench`（原 sdmmc 工程命令，8 KB 缓冲）保留原行为以便对照。
2. **UART 的 4 KB 突发不能按 4096 B 算线速**：`IfxAsclin_Asc_write` 只要把数据放进 1 KB 软件 TX 环就返回，
   所以真正被线速"节流"的只有 3072 B → 理论 33333 µs。实测 33725 µs（+1.2%），说明 921600 波特率是对的。
   `selftest` 里的 UART 检查不靠这个时间，而是直接从 `BITCON/BRG` 反算波特率（921659，误差 0.006%）。

---

## 6. 网络端到端测试（需要 PC）

PC 侧网卡已设好：`以太网 16` = 192.168.1.1（10BASE-T1S USB 适配器），
`以太网 2` = 192.168.0.2（Intel I350-T4 #2，接 1000BASE-T1 转换器，转换器设为 1000M Master）。

```powershell
cd C:\github\embedded\tc397

# ping
ping -n 4 192.168.1.100      # 10BASE-T1S
ping -n 4 192.168.0.100      # 1000BASE-T1

# iperf2（板端 lwiperf 是 iperf2 server，TCP 5001，随固件自启动）
.\tools\iperf.exe             -c 192.168.1.100 -p 5001 -t 15 -w 32K -M 1024   # T1S
.\tools\iperf-2.2.1-win64.exe -c 192.168.0.100 -p 5001 -t 15 -w 16K            # 1000T1
```

实测：

```
PS> ping -n 4 192.168.1.100
来自 192.168.1.100 的回复: 字节=32 时间=1ms TTL=255
来自 192.168.1.100 的回复: 字节=32 时间<1ms TTL=255
来自 192.168.1.100 的回复: 字节=32 时间<1ms TTL=255
来自 192.168.1.100 的回复: 字节=32 时间<1ms TTL=255
    数据包: 已发送 = 4，已接收 = 4，丢失 = 0 (0% 丢失)

PS> ping -n 4 192.168.0.100
来自 192.168.0.100 的回复: 字节=32 时间<1ms TTL=255   （4/4，0% 丢失）

PS> .\tools\iperf.exe -c 192.168.1.100 -p 5001 -t 15 -w 32K -M 1024
[ ID] Interval       Transfer     Bandwidth
[376]  0.0-15.0 sec  15.8 MBytes  8.79 Mbits/sec

PS> .\tools\iperf-2.2.1-win64.exe -c 192.168.0.100 -p 5001 -t 15 -w 16K
[ ID] Interval       Transfer     Bandwidth
[  1] 0.00-15.01 sec   613 MBytes   342 Mbits/sec
```

板端反向 ping（板 → PC）：

```
>>> ping 192.168.1.1
Reply from 192.168.1.1: bytes=32 seq=0 time=2 ms
Reply from 192.168.1.1: bytes=32 seq=1 time=0 ms
Reply from 192.168.1.1: bytes=32 seq=2 time=0 ms
Reply from 192.168.1.1: bytes=32 seq=3 time=0 ms
PING statistics: 4 sent, 4 received, 0% loss

>>> ping 192.168.0.2
PING statistics: 4 sent, 4 received, 0% loss
```

### 与独立工程的对照

| 指标 | 独立工程 | 本工程 | 说明 |
| --- | --- | --- | --- |
| 10BASE-T1S iperf | 8.66 Mbps（`tc397_lan8651_t1s`） | **8.79 Mbps** | 瓶颈是 10 Mbit/s 线速，一致 |
| 1000BASE-T1 iperf | 359 Mbps（`tc397_lwip_iperf`） | **342 Mbps** | 同一 PC、同一 `TCP_WND=16K`，受 PC 侧中断裁决限制，两次测量差 5% 以内 |
| SD 写 / 读 | 4855 / 11505 KB/s（`tc397_sdmmc`，8 KB 缓冲） | **10778 / 17355 KB/s**（32 KB 缓冲） | 缓冲放到 LMU 后 DMA 不再和 CPU 抢 DSPR0，且 32 KB = 1 cluster |

> 千兆 342 Mbps 与 GCC/Ubuntu 基线 592 Mbps 的差距在 **PC 侧**（网卡中断裁决），不是板子：
> 详见 `../tc397_lwip_iperf/README.md` §7.8 与 `../handover/2026-09-20_tc397_iperf_window_fix.md`。

---

## 7. 各外设单独验证命令与日志

### 7.1 ADC

```
>>> adc
AN   Signal    Pin      Ext      Raw   Src
AN16 VUC       3.309V   ...
AN20 3V3       3.248V
AN21 1V25      1.235V
AN22 0V9       0.916V
...
VREF=5.0V(12bit); ext=pin*50/3 (47K+3K); HW_VERSION vuc~=pin*11
```

自检容差：VUC 3.31±0.35、3V3 3.25±0.35、1V25 1.24±0.15、0V9 0.92±0.12。
AN17/18/19/24..29/32/33/36..39 是 P40.x 复用的 GPIO，不采样（表中标 `Reserved`），所以是 33/48。

### 7.2 CAN

```
>>> canpair
canpair: 1 round(s), len=8 FD+BRS, pairs 0-1..10-11
  can0->can1: PASS (id=100 len=8 FD+BRS)
  can1->can0: PASS (id=140 len=8 FD+BRS)
  ...
canpair done: ALL PASS (0 fail(s))

>>> canstat
fMCAN=80.00 MHz nFAULT=0 live=off
ch | rx/tx/ovf/bo/rst | pend/drop | TEC REC BO | NBTP DBTP
 0 | 0/3453/0/0/0 | 0/0 | 0 0 0 | 0x06030E03 0x00800B22
 1 | 3453/0/0/0/0 | 16/0 | 0 0 0 | 0x06030E03 0x00800B22
 ...
```

### 7.3 FlexRay

```
>>> fr init
FR bring-up start ...
FR bring-up OK (rc=0)
FR0A(ERAY0) boot: rc=0@OK poc=2(NORMAL_ACTIVE) SUCC1=0x04004304 CCSV=0x00360302 EIR=0x00000016
FR1A(ERAY1) boot: rc=0@OK poc=2(NORMAL_ACTIVE) SUCC1=0x04004304 CCSV=0x00410302 EIR=0x00000016

>>> fr test 5
... 5 轮双向 10/10 PASS

>>> bench fr
 FLEXRAY: 200 frames x16B in 1000 ms = 200 frame/s sustained
```

### 7.4 TLF35584

```
>>> tlf
DEVSTAT=0xFA STATE=NORMAL TRK2=1 TRK1=1 COM=1 STBY=1 VREF=1
VMONSTAT=0xFC (TRK2/1/VREF/COM/VCORE/STBY ready)
SS1(P33.9)=1(high/normal) WDI(P14.3)=0 ERR(P33.8)=1 lock=locked baud=2000000 xfer=7 tout=0
```

上电时 `Tlf_Init()` + `Tlf_AutoInit()` 自动跑 INIT→NORMAL 序列并打印：
`TLF35584: auto-init OK (DEVSTAT 0x?? -> 0xFA NORMAL, SS1=1)`。

> `IF=0x84` 是 WWD/FWD 相关的历史中断标志，`DEVSTAT` 仍是 NORMAL、`SS1=1`，
> 与独立 `tc397_tlf35584` 工程的现象一致；要清零可跑 `tlf help` 里的命令。

### 7.5 SD 卡

```
>>> sd init
disk_initialize(0) -> 0x00
f_mount -> OK (0)
SDMMC: RCA=0x0001 state=0x00 type=SDmem cap=SDHC/SDXC(block)(0x0C)
Clk: SDCLK=50000 kHz (CLKCTL=0x000F FREQ_SEL=0 PRESET_VAL_ENABLE=1)
Capacity: 61067264 sectors x 512B = 29818 MiB (~29.1 GiB)
  source: read probe (32 CMD17 attempts) - this IP does not latch R2, see 'sd csd'
FS: FAT32, cluster=64 sectors, free=953801 clusters (~29806 MiB)

>>> bench sd 8
 SD     : 8 MB write 975 ms = 8402 KB/s | read 473 ms = 17319 KB/s | verify OK
```

### 7.6 LAN8651 10BASE-T1S

```
>>> t1stat
DEVID=0x00086512 SYNC=0 RESETC=0 oa_cfg=0x9006
PLCA en=1 id=1 ncnt=8 pst=1 tot=0x0020 burst=0x0080
PHY bmcr=0x0000 bmsr=0x0805(link=1) id=0x0007/0xC1B3
MAC ncr=0x0C(TXEN=1 RXEN=1) ncfgr=0x02020040 nsr=0x04
BUF rba=0 txc=48 irq=idle(1)
```

### 7.7 GETH 1000BASE-T1

```
>>> ifconfig
netif 0: t11 IP 192.168.1.100 NM 255.255.255.0 GW 192.168.1.1
  HWaddr 02:00:00:10:BA:5E MTU 1500 flags 0x1F
  link UP
netif 1: en0 IP 192.168.0.100 NM 255.255.255.0 GW 192.168.0.1
  HWaddr DE:AD:BE:EF:FE:ED MTU 1500 flags 0x0F
  link UP

>>> ethstat
ETH rx_ok=91 rx_err=0 rx_nobuf=0 tx=1 rbu=0 isrRx=... isrTx=...
sysbus=0x00005000 rxctl=0x00200c01 txctl=0x00200011
```

### 7.8 上电启动日志

```
TC397 Letter-Shell v1.1.0 (no shell)

TC397 COMBINED PERIPHERAL SELF-TEST
ADC | CAN x12 | FlexRay | TLF35584 | SD | 10BASE-T1S | 1000BASE-T1 | UART shell
Console: ASCLIN0 P14.0/P14.1 921600 8N1.  Try 'selftest' or 'help'
ChipID: 0xAF239793 CHREV=0x..  SCU_ID: 0x........ RSTSTAT: 0x........
STM: 100000000 Hz  CPU: 300000000 Hz  uptime: 0 ms
DTS raw=0x.... -> 24.00 C
TLF35584: auto-init OK (DEVSTAT 0x.. -> 0xFA NORMAL, SS1=1)
YT8011AN: link UP after 1953 polls
LWIP   : MAC DE:AD:BE:EF:FE:ED
LWIP   : IP  192.168.0.100
LWIP   : NM  255.255.255.0
LWIP   : GW  192.168.0.1
LAN8651 DEVID=0x00086512 model=0x8651 rev=2
LAN8651: TC6 over QSPI4 20000000 Hz, PLCA id=1 cnt=8, MAC 02:00:00:10:BA:5E
LAN8651: netif t1 max up, IP 192.168.1.100
LAN8651: IP=192.168.1.100 MASK=255.255.255.0 GW=192.168.1.1
lwIP iperf server ready (TCP 5001)
UDP echo listening on port 9
LINK   : UP   1000M full-duplex
```

---

## 8. 与独立工程相比改了什么

合并本身必须动的地方，以及顺手修掉的问题：

| # | 文件 | 改动 | 为什么 |
| --- | --- | --- | --- |
| 1 | `Configurations/lwipopts.h` | 新增 `LWIP_CHECKSUM_CTRL_PER_NETIF=1`，`CHECKSUM_GEN_*/CHECKSUM_CHECK_*` 改回 1 | GETH 有硬件校验和（TDES3.CIC=3），LAN8651 没有。改成"按 netif 运行期开关"：GETH netif 保持 `DISABLE_ALL`（硬件做），LAN8651 netif 设 `ENABLE_ALL`（软件做） |
| 2 | `Libraries/.../netif_lan8651.c` | **新增**：LAN8651 的 netif 注册 + 链路轮询 + RX 抽取 | 一个 lwIP 两个 netif；GETH 仍是 default route |
| 3 | `Libraries/.../Ifx_Lwip.c` | `Ifx_Lwip_pollReceiveFlags()` 里追加 `IfxLan8651_poll()`；删掉两处 `initUART()` | 前者是唯一"开中断"的主循环轮询点（QSPI4 是中断驱动，不能在关中断区里跑）；后者会打断启动日志（踩坑 6） |
| 4 | `App/can12.c` | `g_can` 移到 LMU | DSPR0 装不下 |
| 5 | `Shell/shell_sd.c` | `s_ioBuf/s_mkfsWork/s_bigBuf` 移到 LMU；`s_bigBuf` 320→96 块 | 同上；96 块仍覆盖链式 ADMA2 |
| 6 | `Libraries/FatFS/mmc_sdmmc.c` | `disk_write_sdmmc()` 增加 `sdmmc_cache_clean(buff, count*512)` | 原工程靠"DSPR 不是 cache"隐式正确；缓冲搬到 cache 的 LMU 后必须显式写回，否则卡上写的是旧数据（表现为 `sd bench` VERIFY-FAIL at word 0）。对 DSPR 地址是 no-op，两种放置都安全 |
| 7 | `Libraries/FlexRay/flexray_dual.c` | `frd_is_ready()` 增加 `prepared` 判断 | 原函数在 `frd_prepare()` 之前调用会解引用空指针 → 数据访问 Trap → **整机静默**（本工程 `bench` 触发过）。独立工程没触发是因为 `fr` 命令总是先 `fr init` |
| 8 | `Shell/shell_flexray.c` | `Shell_Get()` → `shellGetCurrent()` | 本工程用的 letter-shell 版本 API 名不同 |
| 9 | `Shell/shell_t1s.c` | **新增**：从 `tc397_lan8651_t1s` 的 `shell_port.c` 抽出 LAN8651 命令；`link`→`t1link` | 避免与 GETH 的 YT8011 `link` 重名；`ifconfig`/`ping` 用 GETH 版（遍历 `netif_list`，两个网口都能用） |
| 10 | `Configurations/ConfigurationIsr.h` | 新增 QSPI2(50/51/52)、QSPI4(60/61/62) 优先级 | GETH 保持 100/101 最高；QSPI2/QSPI4 用不同优先级避免同级别互不抢占 |
| 11 | `build.ps1` | ninja 优先用 AURIX Studio 自带的；**每次构建前删除 `.ninja_deps`** | 见第 9 节踩坑 1 |
| 12 | `App/selftest.c/.h`、`Shell/shell_port.c`、`Cpu0_Main.c` | **新增** `selftest`/`stat`/`bench` 命令与合并后的启动/主循环 | 本工程的核心交付 |

---

## 9. 踩坑记录（新会话别重踩）

1. **`.ninja_deps` 污染 → ninja 增量构建必失败**
   TASKING 的 `--dep-file` 写成 make 风格的两行对（`target : dep` / `"dep" :`），ninja 解析后会把
   一个截断的目录路径写进 `.ninja_deps`（本工程是 `.../App`，`tc397_lwip_iperf` 是
   `.../Configurations/Debug`）。下次 ninja 启动 stat 这个路径直接报错退出：
   ```
   ninja: error: FindFirstFileExA("C:/.../tc397_selftest/App): The filename, directory name,
   or volume label syntax is incorrect.
   ```
   **与 ninja 版本无关**（PyPI 1.13.0、AURIX Studio 1.13.2、WinGet 1.13.2 都复现）。
   规避：构建前删掉 `.ninja_deps`（本工程 `build.ps1` 已自动做），或者干脆 `-Action rebuild`。
   代价：删掉后头文件改动不再自动触发重编，改头文件请用 `-Action rebuild`。
2. **`lwipopts.h` 注释里不能出现 `*/`**：写 `CHECKSUM_GEN_*/CHECKSUM_CHECK_*` 会把注释提前结束，
   cctc 报几十条 `E207/E208` 语法错误。
3. **DSPR0 只有 240 KB**：8 个工程合并后要 ~390 KB。合并时先按第 8 节把大缓冲搬到 LMU，
   否则链接报 `ltc E112: cannot locate N section(s)` + `requirement: xxx bytes of RAM area in
   space mpe:vtc:linear`。`ltc` 只列前 10 条，**要看 `-Wl-mf` 生成的 `.map` 里的 "Memory usage" 表**。
4. **`#pragma section` 作用域要收窄**：TriCore-GCC 的 `#pragma section ".lmubss" aw` 会连 const 对象
   一起改默认段，导致 `SHELL_EXPORT_CMD` 的 `section("shellCommand")` 冲突：
   `error: section of 'shellCommandsd' conflicts with previous declaration`。
   必须在 `SHELL_EXPORT_CMD` 之前用 `#pragma section`（GCC）/ `#pragma section farbss "bss_cpu0"`（TASKING）恢复。
5. **缺 `<stdio.h>` 会静默产生错误代码**：`netif_lan8651.c` 里漏了 `<stdio.h>`，`snprintf` 被隐式声明
   （`ctc W505`），打印出来的 MAC 是 `10:BA:5E:80032390:9006:`（栈上的垃圾）。
   凡是新写的文件，用 `snprintf` 就必须 `#include <stdio.h>`。
6. **`Ifx_Lwip_init*()` 内部会再调一次 `initUART()`**：会重新初始化 ASCLIN0 的收发环，
   把正在输出的启动横幅切成碎片（`ADC | CAN x12 | FlexRa YT8011AN: link UP after 1953 polls`）。
   本工程已去掉这两处调用。
7. **板子"没反应"先看是不是被 Trap 了**：`frd_is_ready()` 的空指针 Trap 让整机静默、串口无输出、
   复位才恢复。遇到这种情况先用 `AURIXFlasher -start on` 复位，再想是不是某个"看起来无害"的查询函数
   在未初始化时解引用了外设指针。
8. **串口脚本要能放大单条命令的空闲超时**：`selftest`（FlexRay 冷启动 ~1 s）、`sd mkfs`（3.7 s）
   中途可能长时间无输出。`tc397/temp/st_shell.py` 支持 `@<秒>:<命令>` 前缀。
9. **烧录偶发 `Cannot initialize device connection`**（约 1/5 概率）：等 3 秒重试即可，与固件无关。

---

## 10. 中间脚本

`C:\github\embedded\tc397\temp\`（**git-ignored，不进仓库**）：

| 文件 | 用途 |
| --- | --- |
| `st_shell.py` | 本工程主力串口驱动：`python st_shell.py -p COM169 --boot 6 "@180:selftest all"` |
| `bld.ps1` | 直接调 ninja 构建（绕过 build.ps1，方便看完整错误） |
| `memsum2.ps1` | 从 `.map` 提取 "Memory usage" 表 |
| `cmpdir.ps1` | 比较两个工程同名目录的文件差异（合并前确认库一致性） |

---

## 11. 已知限制 / 后续可做

1. `selftest` 的 NET 项恒为 SKIP（需要 PC 对端）。要做成全自动，需要 PC 侧常驻一个应答服务。
2. LAN8651 netif 名字是 `t11`（它是第二个加入 lwIP 的 netif，`num=1`）；纯属显示问题。
3. `sd big` / `sd erase` 单次最大 96 块（48 KB）——为省内存从 320 块降下来的；
   要更大需要恢复 `s_bigBuf` 尺寸并确认 LMU 余量（当前 LMU 还空 500 KB，可以调）。
4. 千兆 342 Mbps 受 PC 侧网卡中断裁决限制；要在板侧继续挖需先解决 PC 侧
   （见 `../tc397_lwip_iperf/README.md` §7.7）。
5. `sd bench`（8 KB 缓冲）比 `bench sd`（32 KB 缓冲）慢约 4.5 倍，两者都保留了，便于对照。

---

## 12. 相关文档

- `../handover/2026-09-21_tc397_selftest.md` —— 本次会话的交接说明
- `../tc397_uart_lettershell/README.md`、`../tc397_adc/README.md`、`../tc397_can_x12/README.md`、
  `../tc397_flexray/README.md`、`../tc397_tlf35584/README.md`、`../tc397_sdmmc/README.md`、
  `../tc397_lan8651_t1s/README.md`、`../tc397_lwip_iperf/README.md` —— 各外设的详细文档
