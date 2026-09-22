# tc397_lwip_iperf — TC397 + YT8011AN (RGMII) + LwIP + iperf

TC397XX（292pin）通过 RGMII 接裕泰微 **YT8011AN** 千兆车载以太网 PHY，
跑 LwIP（NO_SYS）+ `lwiperf` TCP server + Letter-Shell（UART0 921600）。
移植自 `tc397_uart_lettershell`（Shell/UART 基线）与 `/home/z/lz/tc387/tc387_2`
（LwIP/GETH/iperf 参考，RTL8211F → YT8011AN）。

实测（Release，`./build.sh download --build-type Release`）：

- `LINK : UP 1000M full-duplex`（Slave，对端转换盒 1000M Master）
- PC↔MCU ping：100/100，0% 丢失，avg ~0.14ms
- iperf2 TCP（PC→MCU，port 5001）：**~592 Mbits/sec，30s 稳定 2.07GB，零丢包零错包**

---

## 1 硬件接线

| 信号 | TC397 Pin | 说明 |
| --- | --- | --- |
| TXD3/TXD2/TXD1/TXD0 | P11.0/P11.1/P11.2/P11.3 | RGMII TX（ALT6） |
| TXCLK | P11.4 | MAC→PHY 125MHz（RGMII 输出，由 GREFCLK 分频产生） |
| GREFCLK | P11.5 | **外部 125MHz 有源晶振输入**（RGMII 必需，见 §5.4） |
| TCTL | P11.6 | TXCTL |
| RXD3/RXD2/RXD1/RXD0 | P11.7/P11.8/P11.9/P11.10 | RGMII RX |
| RCTL | P11.11 | RXCTL |
| RXCLK | P11.12 | PHY→MAC 125MHz |
| MDC/MDIO | P12.0/P12.1 | Clause-22 管理口，PHYAD=1 |
| nINT | P10.8 | 未用（预留） |
| nRST | P20.1 | 软件复位（低 50ms → 高，延时 50ms） |
| UART TX/RX | P14.0/P14.1 | ASCLIN0 921600-8N1，Shell |
| LED | P13.0 | 低电平点亮，1Hz 心跳 |

网络拓扑：`TC397—(RGMII)—YT8011AN—(1000BASE-T1)—转换盒(Master)—(1000BASE-T)—PC enp6s0`。
MCU 静态配置 `MAC DE:AD:BE:EF:FE:ED，IP 192.168.0.100/24，GW 192.168.0.1`；
PC 侧 `enp6s0 = 192.168.0.1/24`（见 §4）。

---

## 2 YT8011AN 总结（重点）

裕泰微 Motorcomm 100/1000BASE-T1 车载 PHY。手册已转文本方便 grep：

- `ref/YT8011A_1.txt`（寄存器手册，V1.4）：MII 0x00–0x1F、EXT（经 MII 0x1E/0x1F 间接）、MMD（经 MII 0x0D/0x0E 间接）
- `ref/YT8011Ax_应用说明_V2.2.txt`：上电初始化（§3 基本配置＋RGMII 配置）、RGMII delay（§6.1）、驱动能力（§6.2）

### 2.1 Strapping（本板，可忽略，纯寄存器配置即可）

| 引脚 | 上下拉 | 含义 |
| --- | --- | --- |
| RCTL | 上拉 4.7K | AutoMode（自协商使能） |
| RXD3 | 下拉 4.7K | Slave（T1 主从，本端 Slave，对端 Master） |
| RXD2 | 下拉 4.7K | RGMII 模式 |
| RXD1 下拉 / RXD0 上拉 | — | PHYAD = 1（MDIO 地址） |
| RXCLK | 上拉 4.7K | RXC delay enable（RGMII RX 2ns 延迟） |

### 2.2 PHY ID 与关键 MII 寄存器（PHYAD=1 实测）

- `0x02/0x03 = 0x4F51 / 0xEB01`：ID1=0x4F51；ID2 解码 `15:10=0x3A(OUI)`、`9:4=0x30(type)`、`3:0=0x1(rev)`，与手册 §4.1.3/4.1.4 一致
- `0x00 BMCR = 0x0140`：1000M 全双工，**自协商关闭**（bit12=0，强制模式；主从由 strapping=Slave 决定）
- `0x01 BMSR`：bit2=link（注意 latch-low，**读两次**），实测 link UP 时 `0x000D`
- `0x11 SPEC`：`[15:14]` 速度（2=1000M）、`[13]` 主从（0=Slave）、实测 `0x8400`
- `0x1E/0x1F`：EXT 间接通道（先写 0x1E=地址，再读/写 0x1F=数据）
- `0x0D/0x0E`：MMD 间接通道（0x0D=dev，0x0E=地址；再写 0x0D=0x4000+dev 后经 0x0E 读写数据）

### 2.3 上电初始化序列（`IfxGeth_Phy_Yt8011an_init`，对照应用说明 §3）

1. 等待 clause-22 可响应（BMCR 非 0xFFFF/0x0000）
2. 基本配置（EXT）：降功耗 `0x1008/0x1092`、UnderVoltage `0x90BC/0x90B9`、send_s `0x2001`、
   100M template `0x1019/0x101A`、IOP `0x2005/0x2015`、LinkUp `0x2013`、
   100M training `0x3017/0x3027/0x3026/0x301E/0x3019/0x3014/0x301A`、
   ADC `0x1000`、PLL `0x1053/0x105E`、Sleep `0x1088/0x3008/0x3009`、CSD 阈值 `0x9095–0x9098`
3. RGMII 配置（VDDIO=3.3V）：`0x9000/0x0062` bank 切换、`0x9031=0xB200`、`0x903B=0x0040`、
   `0x903E=0x3B3B`、`0x903C=0x000F`、`0x903D=0x1000`、`0x9038=0x0000`
4. **RGMII TX delay**：`0x9001[7:4]`，125ps/step，置最大 `0xF`（≈1.875ns）。
   RGMII 要求 TXC 2ns skew；MAC 侧 SKEWCTL=0（同 tc387_2），必须由 PHY 加。
   注意 `0x9001[8]` 反映 strapping 的 RX delay（本板=1），RMW 时保留
5. 软复位：`MII 0x00 = 0x8140`（reset+AN enable），轮询复位位清零

### 2.4 RGMII delay（应用说明 §6.1，本工程血泪点）

- RX delay：strapping（RXCLK 上拉）→ 1000M 加 2ns；同步反映在 `EXT 0x9001[8]`；也可用寄存器开关
- TX delay：**必须配寄存器** `EXT 0x9001[7:4]`（默认仅 1step=125ps ≈ 无延迟）。
  不配则 MAC 发出的包到不了线（PC 侧 tcpdump 零包），配满后仍须结合 §5.2 的 SWR 时序才能真正通

---

## 3 目录结构（相对 tc397_uart_lettershell 的增量）

```
tc397_lwip_iperf/
├── Configurations/
│   ├── Configuration.h      # + RGMII 引脚定义（PinMap 符号）+ nRST/nINT + CPU0 服务以太网
│   ├── ConfigurationIsr.h   # + GETH_TX 100 / GETH_RX 101
│   └── lwipopts.h           # 新增：NO_SYS，MEM 76K（LMU heap），TCP_MSS 1460/WND 64K，
│                            #   MEMP 44 segs，PBUF_POOL 32，HW checksum 全开（GETH COE），debug 全关
├── Libraries/Ethernet/
│   ├── Phy_Yt8011an/        # 新增：YT8011AN 驱动（MDIO/EXT/MMD + §2.3 初始化）
│   └── lwip/                # 新增：lwIP（含 lwiperf）+ TriCore port（Ifx_Lwip.c/netif.c，见 §5）
├── Shell/shell_port.c       # + phy/phyr/phyw/ext/mmd/link/ytinit/ifconfig/ethstat/geth/clks/ping
├── Cpu0_Main.c              # + GETH 使能 + LwIP 初始化 + iperf TCP server(5001) + 诊断 UDP(5002/5003)
├── CMakeLists.txt           # + IFXGETH_MAX_RX_DESCRIPTORS=64 / TX=8（全局一致）
└── build.sh                 # + --freq（DAP 时钟，默认 1MHz；15MHz 在本环境不稳定）
```

---

## 4 构建 / 烧录 / 测试（Ubuntu 26.04）

```bash
export PATH=/opt/tricore-gcc/bin:$PATH
cd tc397_lwip_iperf
./build.sh build                        # Debug
./build.sh build --build-type Release   # Release（-O3，iperf 用这个，592Mbps）
./build.sh download                     # 烧录（需 TAS：systemctl status tas-server）
./build.sh download --build-type Release
```

PC 网卡（已配好，如重装对照）：

```bash
sudo ip addr add 192.168.0.1/24 dev enp6s0   # MCU 的 GW 指向它
ping -c3 192.168.0.100
iperf -c 192.168.0.100 -p 5001 -t 10 -w 256K  # 注意用 iperf2（lwiperf 是 iperf2 协议）
```

Shell（921600）：`link/phy/ifconfig/ethstat/geth/clks/ping 192.168.0.1`，
诊断 UDP：任意包发往 `192.168.0.100:5002` 即回统计（`5003` 为吸包 sink）。

---

## 5 移植与联调要点（vs tc387_2 / tc397_0）

### 5.1 启动挂死：`.lmudata` 奇地址 copy → Ssw 字拷贝 trap

现象：只要链接了 LwIP 相关目标文件（shell 的 ping/raw 引用把 lwIP core 拉进来），
整机无串口、无 tick、停在 main 之前；纯 UART 版正常。
根因：`Ifx_Lwip.c` 的多核自旋锁 `lwip_global_lock = 0` 经 `#pragma section ".lmudata"`
进入 PROGBITS，其 flash LMA 恰落在奇地址（Lcf 仅 `ALIGN(2)`），而 Ssw C-init
（`__copy_table`）按字拷贝 → 对齐 trap → 默认 trap 死循环。
修复：锁改为无显式初始化并放到 `.lmubss`（NOBITS，仅 Ssw 清零，无 copy 项），
copy 表恢复干净（仅 `.data` 一项）即启动。教训：凡进 copy 表的段，其 LMA 必须字对齐。

### 5.2 GETH SWR 要求 RXCLK：必须 link-up 之后再 initModule

现象：link UP、MDIO 全通，但 MAC 收发计数全零、TX DMA 描述符 OWN=1 永不取走、
无任何错误位（复位不完全，iLLD 的 SWR 超时不报错直接继续）。
根因：UM 要求 SWR 期间 GREFCLK/RXCLK 必须存在；本工程原顺序在 T1 建链（Slave
training 需几秒）之前就 SWR，此时 PHY 不驱动 RXCLK → DMA 引擎僵死。
修复：`low_level_init` 重排为 `nRST → YT 全初始化 → 轮询 link-up（15s 超时）→
initModule（SWR）→ start`。此后 ping 即通。

### 5.3 fGETH 只能 150MHz（TC39x 手册上限 100–150MHz）

tc387_2 的 300MHz 提速在本芯片超规格：DMA 静默停转而 CSR/MDIO 正常，极具迷惑性。
本工程固定 150MHz（GETHDIV=2），单流 TCP ~592Mbps（见 §6），稳定优先。

### 5.4 时钟核实

- GREFCLK（P11.5，125MHz 晶振）是 RGMII TXC 之源（UM Table 463：TXC 由 GREFCLK 分频产生）。
  曾用 `P11.IN` 采样法验证三路时钟：等间隔采样会被 shell 打印周期频闪，
  必须用 200 次背靠背采样统计（≈50% ones 即存活）。三路均存活后才排除时钟问题。
- fGETH/SRI/SPB 可用 `geth` 命令读回（150M/300M/100M）。

### 5.5 其他移植点

- 保留 tc397_0 的 iLLD（TC39xB）、Ssw、Lcf（6 核）、Cpu1..5（与 tc387_2 的 TC38x iLLD
  逐行对过，GETH 驱动一致，遂排除驱动差异）。
- GETH DMA 描述符/缓冲放 LMU（64×1536 RX + 8 TX），CPU 经 non-cached 视图（0xB）访问，
  RX 拷贝前按行作废 cache；lwip heap 放 DSPR0。
- `lwipopts.h` 关闭全部 `LWIP_DEBUGF`（UART 打印会把吞吐压到几 Mbps），开 GETH COE 硬件校验。
- 主循环纯轮询（RX 中断 SRC 已关，64 包/轮上限），Shell 限 1kHz 以内让路给线速。
- `lwiperf` 只支持单流（`-P4` 会掉到 ~2Mbps，属上游限制，非 bug）。

---

## 6 实测记录（2026-09-14，Release）

```
LINK   : UP   1000M full-duplex
YT8011AN: link UP after 1959 polls
LWIP   : MAC DE:AD:BE:EF:FE:ED IP 192.168.0.100 NM 255.255.255.0 GW 192.168.0.1
link      -> PHY link: UP (BMSR=0x000D) speed=1000M mode=Slave (SPEC=0x8400)
ping 192.168.0.1 3 -> 3 sent, 3 received, 0% loss, 0ms
PC ping 192.168.0.100 (100x) -> 100 received, 0% loss, avg 0.14ms
iperf -c 192.168.0.100 -p 5001 -t 30 -> 2.07 GBytes, 592 Mbits/sec, 零重传风暴
ethstat   -> rx_ok=2026896 rx_err=0 rx_nobuf=0 tx=1013570 rbu=0
geth      -> TX_GB=52042 RX_GB=60816 RX_CRC=0 RX_ALIGN=0 RX_OVF=0, TX_PAUSE=21512
diag UDP  -> gap1ms=0（主循环无 >1ms 停顿），hw_missed=0
```

---

## 7 Windows 11 + TASKING 构建与实测（2026-09-18）

### 7.1 测试背景

* 目标：Ubuntu GCC Release 已跑出 592Mbps（见 §6），现验证同一套源码在
  Windows 11 + TASKING v6.3r1 下的构建与功能，量化编译器优化差距。
* 约束：增量改动不得影响 Ubuntu GCC（`build.sh` 原样保留；源码改动包在
  `__TASKING__` 分支或工具链无关形式）。
* 环境：AURIX-Studio-1.10.36（AURIXFlasher v3.0.18），COM165（921600），
  DAP MiniWiggler；对端千兆车载以太网转换器（1000M Master）→ PC `以太网 5` = 192.168.0.1。
* iperf 工具：`tc397/tools/iperf.exe`（iperf 1.7.0 win32，iperf2 协议，已进 git）。

### 7.2 构建命令

```powershell
.\build.ps1 -Compiler tasking -Action download                 # Debug 编译并烧录
.\build.ps1 -Compiler tasking -Action rebuild -BuildType Release  # Release（测速用）
.\build.ps1 -Compiler tasking -Action download -BuildType Release # Release 烧录
```

注：`build\tasking` 目录同时只能存一种 BuildType，切 Debug/Release 必须用
`rebuild`（先清后建），否则 ninja 增量会报旧 depfile 路径错误（另见 §7.5 构建机注记）。

### 7.3 通用兼容改动（GCC 行为不变，详见 `tc397/temp/tasking_porting_log.md`）

与 lan8651 工程同 6 项（`+gcc` 语言扩展、shell.h/shell.c 的 `__TASKING__` 分支、
`SHELL_DSYNC()` 宏、LSL `shellCommand` 命名组、`static inline`）。
另修 `cmake/tricore-gcc-toolchain.cmake` 的 Windows 默认 GCC 路径
`1.10.28` → `1.10.36`（`try_compile` 子项目回退默认值问题；仅 Windows 分支）。

### 7.4 本工程实测日志（Tasking）

编译（387 obj，0 error，Debug/Release 均过）：

```
[386/387] Linking C executable tc397_lwip_iperf.elf
Done.
```

链路（`link` + `ifconfig`）：

```
PHY link: UP (BMSR=0x000D) speed=1000M mode=Slave (SPEC=0x8400)
MAC PHYIF: 0x000D0000 (LNKSTS bit0, LNKMOD bit1, LNKSPEED bit2)
netif 0: en0 IP 192.168.0.100 NM 255.255.255.0 GW 192.168.0.1
  HWaddr DE:AD:BE:EF:FE:ED MTU 1500 flags 0x0F
  link UP
```

ping（双向）：

```
# 板→PC：PING 192.168.0.1 : 32 bytes count 4 → 4 sent, 4 received, 0% loss（1/0/0/0 ms）
# PC→板：ping -n 4 192.168.0.100 → 4/4，<1ms，0% 丢失
```

iperf2 TCP（PC→板，`iperf.exe -c 192.168.0.100 -p 5001 -t 15 -w 256K`）：

```
# Tasking Debug（--tradeoff=4，-O0）：15.5s / 10.7MB / 5.83Mbps
# Tasking Release tradeoff=4（-O2，ADS 默认）：17.3s / 152MB / 73.7Mbps
# Tasking Release tradeoff=0（-O2，纯速度）：15.0s / 50.3MB / 28.1Mbps
# Tasking Release tradeoff=2（-O2，平衡）：17.3s / 40.6MB / 19.6Mbps
```

### 7.5 测试结果（第一轮）

* 功能全 PASS（编译/烧录/1000M 建链/双向 ping/iperf 连通）。
* 构建机注记：Python 自带 ninja 1.13.0（kitware 版）增量构建失败，
  WinGet 官方 1.13.2 正常；`rebuild` 可规避。AURIXFlasher 偶发连接失败
  （约 1/5），等 3 秒重试即好。

---

## 7.6 速率专项追查（2026-09-18 第二轮，深度）

> 结论先行：**上一轮"Tasking 73.7Mbps vs GCC 592Mbps，差 8 倍，属编译器优化差异"
> 的判断是错的。** 真正原因是 **iperf 客户端工具/参数** + **链路在 >300Mbps 时丢包**，
> 与编译器基本无关。下面按"发现顺序"记录证据。

### 7.6.1 关键发现一：iperf 客户端参数决定测量值（影响 >10 倍）

上一轮用的是 `iperf.exe`（**iperf 1.7.0 win32，2003 年版**），其 **TCP 默认块只有 8KB**
（`-l` 默认值很小），而 Linux 上的 iperf2 默认块大得多。同一块板、同一份固件，
只换客户端参数：

| iperf 客户端 / 参数 | 实测 TCP 速率 |
| --- | --- |
| iperf 1.7.0 `-w 256K`（默认块） | **15.4 Mbps** |
| iperf 1.7.0 `-w 256K -l 8K` | 10.4 Mbps |
| iperf 1.7.0 `-w 256K -l 64K` | 79.8 Mbps |
| iperf 1.7.0 `-w 1M -l 64K` | 199~285 Mbps |
| **iperf 2.2.1 `-w 1M`（默认块）** | **93~331 Mbps** |
| iperf 2.2.1 `-w 256K -l 64K` | 187 Mbps |

**➜ 所以"Tasking 比 Ubuntu 慢 8 倍"的第一层原因是 iperf 版本/参数，不是编译器。**
Ubuntu 上那 592Mbps 是用 Linux iperf2（大默认块）测的，而 Windows 侧最初用的是
2003 年的 iperf 1.7.0 默认 8KB 块 —— 两者不可直接比较。

补充：`iperf-2.2.1-win64.exe` 已随本轮放入 `tc397/tools/`。注意 **`-l` 指定大块
（如 128K/256K）反而会崩到 3 Mbps 量级，甚至让固件挂死**（见 §7.6.4），
建议就用 `-w 1M` 走默认块。

### 7.6.2 关键发现二：Release 用 `-O3 --tradeoff=0` 确实优于 `--tradeoff=4`

用 `iperf-2.2.1 -w 1M`、每次 30s、同板同链路，只切 CMake 的 Tasking 选项：

| Tasking Release 选项 | 30s 实测 | `input`（lwIP 协议处理） |
| --- | --- | --- |
| `--tradeoff=4`（`-O2` 默认级别） | 77.9 / 43.8 Mbps | 803 tk/pkt |
| **`-O3 --tradeoff=0`** | **90.9 / 279 Mbps** | **706 tk/pkt** |

`input` 每包快约 12%，峰值也明显更高，故 Release 固定 `-O3 --tradeoff=0`
（与 `tc387_lwip_iperf_gcc2` 的 Tasking 千兆配置一致）。

依据（cctc 手册 `ctc_user_guide.pdf`）：
* p.446-447：`-O2` 别名 `-OacefgIklMnoprsUvwy`（**大写字母=关闭**，`I`=不内联、
  `U`=不展开、`M`=不用 SIMD、`N`=不做 loop 对齐），而 `-O3` 为全小写（全开）；
* p.222-224：`--tradeoff` 会改变指令选择 —— 小实验实测 `tradeoff=0` 的循环用
  TriCore 硬件 `loop` 指令，`tradeoff=4` 退化为 `add + jlt.u` 软件循环。

### 7.6.3 关键发现三：瓶颈是"链路丢包"，不是 CPU / 不是编译器

用当前 Tasking `-O3` 固件做 UDP 灌流（打到 sink 端口 5003，不回复），
按目标速率扫描丢包率（PC 网卡 `txDiscard=0`、`txErr=0`，即 PC 侧没丢）：

| 目标速率 | PC 发出 | 板子收到 | 丢包率 |
| --- | --- | --- | --- |
| 100 M | 51185 | 51187 | **~0%** |
| 300 M | 152825 | 145775 | **4.6%** |
| 600 M | 311538 | 266324 | **14.5%** |
| 900 M | 441735 | 357165 | **19.1%** |

* 用 **GCC 固件**测，丢包率几乎相同（4.0% / 13.4% / 17.9%）➜ **与编译器无关**。
* 同一固件单流 TCP 20s 分区间报告：峰值 **331 Mbps**，但周期性掉到 **2~8 Mbps**
  并持续约 4s —— 正是 TCP RTO 退避（1s+2s+4s）的典型形态，即**丢包把客户端
  cwnd 打崩**。掉速期间板子 `busy` 仅 4~17%、`rx_nobuf=0`，**CPU 完全不是瓶颈**。
* 丢包随速率单调上升 ➜ 是"缓冲/瞬时排队"型丢包。板子侧 `hw_fifo_ovf` 确有增长，
  但 MTU 8KB 的 MTL RX FIFO 只够约 5 个满帧，链路突发时很容易溢出。

### 7.6.4 已排除项与"试过但失败"的改动（重要，别重踩）

* **DMA 突发配置已是最优且确认生效**（通过 UDP 诊断口 `rd` 读回寄存器）：
  `CH0_CONTROL.PBLX8=1`、`SYSBUS_MODE`：`AAL=1`、`MB=1`、`FB=0`、
  `CH0_RX/TX_CONTROL` 的 `RXPBL=TXPBL=32`，`MTL_RXQ0_OPERATION_MODE` 的
  `EHFC=1、RFA=1、RFD=4、RSF=1`。`fGETH=150MHz`（TC39x 手册上限 100–150MHz）。
  ➜ **不是配置没生效**，不要再怀疑这一层。
* **RX 缓存失效代码正常**：`netif.c` 的 `__TASKING__` 分支 `cachea.i` 生成的
  汇编是 `cachea.i [a2]0` + `loop`，无问题。
* **试过 TX 描述符 8→32（失败）**：想让 ACK 有更多缓冲，实测吞吐**崩到 ~4 Mbps**
  （ping 正常但 RX 几乎收不到包）。本工程对 DMA 描述符/缓冲的 LMU 布局很敏感，
  未定位根因前**保持 64/8 不要动**（已在 `CMakeLists.txt` 就地注释）。
* **试过开 RFC1323 窗口缩放（失败）**：`TCP_WND=64240` 是单流上限的硬约束
  （反推 RTT：Tasking 5.2ms、GCC 2.2ms，`吞吐≈窗口×8/RTT`），所以试过
  `LWIP_WND_SCALE=1 + TCP_RCV_SCALE=2 + TCP_WND=65535<<2`（256KB）：
  * 首次烧录后**整机启动挂死**（串口无 banner、ping 不通）；
  * 改成 128KB（`SCALE=1`）后能启动、ping 通，但 **UDP 诊断与 TCP 都不响应**，
    稍后彻底挂死。
  * 回滚后一切正常（`ping` 4/4）。两版 map 显示 `lwip_lmuram_heap` 只后移
    `0x64` 字节、DSPR0（240K）远未溢出、工程内只有一份 `lwipopts.h`，
    **根因未明**（疑与 lwIP memp 池布局变化 + 本工程的 Ssw 初始化/对齐敏感性有关，
    参见本工程 §5.1 曾出现的"`.lmudata` 奇地址 copy → Ssw 字拷贝 trap"）。
  * ➜ **要冲 >300Mbps 必须解决窗口问题，但当前不能直接用 `LWIP_WND_SCALE`。**

### 7.6.5 本轮净收益

| 项目 | 第一轮 | 本轮 |
| --- | --- | --- |
| 测量工具 | iperf 1.7.0，默认 8KB 块 | iperf 2.2.1 `-w 1M` |
| Tasking Release 配置 | `--tradeoff=4`（`-O2`） | `-O3 --tradeoff=0` |
| 最好成绩（单流 TCP） | 73.7 Mbps | **331 Mbps 峰值 / 279 Mbps 30s** |
| 与 GCC 的关系 | 误判为"编译器差距 8 倍" | 同参数下两者量级相当 |

**仍未达成 592Mbps 的原因**：不是编译器，而是
① 链路在 >300Mbps 丢包（4.6%→19.1%，打崩 cwnd）；
② 单流 TCP 的 64KB 窗口上限（需窗口缩放，但当前会挂死）。
下一步方向见 §7.7。

### 7.7 下一步（未完成，留给后会话）

1. **查丢包源头（优先）**：UDP 100M 丢 0% 但 300M 就丢 4.6%，说明链路中间
   （1000BASE-T1 转换盒 / T1 线缆 / YT8011AN RGMII 时序）在高负载下缓冲不足。
   本工程 `hw_crc=0`（收到的包 CRC 全对），说明不是 RGMII 采样错，而是**包没到 MAC**。
   建议：换一条 T1 线/换转换盒对照；或用 PC 侧抓包对照重传数。
2. **解决窗口缩放挂死**：这是冲 592Mbps 的必要条件。建议从"改变
   `struct tcp_pcb` 大小是否触发初始化 trap"入手，先只开 `LWIP_WND_SCALE=1`
   并保持 `TCP_WND=64240`（窗口不变、仅验证结构体变化是否挂死），再逐步加大。
3. **降低 RTT**：`input=706 tk/pkt` 里已含协议处理开销，可尝试把 lwIP 热代码
   放 PSPR（LSL 已有 `.text.text_cpu0` 归 PFLASH，可改用 `psram_text_cpu0` 组），
   但需同步启动拷贝表，风险较高。

---

## 7.8 大 socket 缓冲塌陷的根因与修复（2026-09-20，第三轮）

### 7.8.1 现象：PC 用 `-w 64K` 时吞吐塌成"整秒 0 字节"

PC 侧 iperf 只要把 socket 缓冲调大就崩，而调小反而跑满：

| PC 侧设置 | 结果 |
| --- | --- |
| 关中断裁决 + `-w 16K` | **578 Mbps 平稳**（Ubuntu 与 Windows 都能复现） |
| 开中断裁决 + `-w 16K` | 403 Mbps 平稳 |
| 开中断裁决 + `-w 64K` | **124 Mbps，且大量"整秒 0 字节"** |

"整秒 0 字节"是 TCP **零窗口 / persist 退避**（1s/2s/4s）的典型形态：
对端（板子）把窗口关到 0，PC 停在 persist 里等，平均吞吐被拖垮。

### 7.8.2 根因：板子通告的窗口与 PC 的 socket 缓冲同量级

板子原 `TCP_WND = 64240`（约 64K），恰好与 PC 的 `-w 64K` 同量级：

* PC 用 **16K** 缓冲时，未确认数据最多 16KB，**永远填不满**板子的 64KB 窗口
  → 板子侧窗口始终有余量 → 不触发"窗口关闭/重开"路径 → 平稳跑满；
* PC 用 **64K** 缓冲时，PC 会把板子的窗口填满，于是不断在
  "窗口填满 → 等板子释放窗口 → 再填满"之间震荡，一旦落在窗口更新空档
  就退化成零窗口 + persist 退避。

### 7.8.3 修复：把板子通告窗口收到 ~16K

`Configurations/lwipopts.h`：

```c
#define TCP_WND   (11 * TCP_MSS)   /* 16060 (~16K)，原 (44*TCP_MSS)=64240 */
```

PC 无论用多大 socket 缓冲都填不满它，塌陷消失。**实测（Tasking Release `-O3`）**：

| PC `-w` | 修复前 | 修复后 |
| --- | --- | --- |
| 16K | 39.6 Mbps | **359 Mbps** |
| 32K | 42.7 Mbps | **359 Mbps** |
| 48K | 5.79 Mbps | 328 Mbps |
| **64K** | **5.16 Mbps** | **359 Mbps** |
| 128K | 112 Mbps | **359 Mbps** |

20s 复测：`-w 16K` 357~359 Mbps、`-w 64K` 356~359 Mbps，**两者一致且平稳**。

吞吐上限 = `窗口×8/RTT`。取 16K 窗口仍够用，是因为实测 **RTT≈0.23~0.36 ms**
（早期按"窗口填满"反推出 5.2ms 是错的 —— 那时窗口并未真的满，是被 persist 卡住）。

### 7.8.4 交叉验证：GCC 与 Tasking 同速 ⇒ 瓶颈在 PC 侧

同一份源码、同样 `TCP_WND=16K`，两个 Release 固件都跑 **359 Mbps**：

| 固件 | 速率 | 板子 `busy` | `input` |
| --- | --- | --- | --- |
| Tasking Release `-O3` | 359 Mbps | **36.8%** | 656 tk/pkt |
| GCC Release `-O3` | 359 Mbps | **60.2%** | 611 tk/pkt |

两者的 `input`（板子处理速度）明显不同、CPU 占用也差很多，却得到**完全相同**的
吞吐 —— 说明板子侧不是限制项（都远未饱和），**当前上限由 PC 侧决定**。
用户实测"关中断裁决 578 vs 开中断裁决 403"也印证这一点：把 PC 网卡的
**中断裁决关掉**（设备管理器 → 网卡 → 高级 → 中断裁决 → 禁用），
359 应向 578 Mbps 靠拢。

> 注：本机当前**没有管理员权限**，无法用 `Set-NetAdapterAdvancedProperty`
> 关闭中断裁决，故 359 是在"中断裁决=启用"下测得的。

### 7.8.5 本轮又排除掉的两条路（别重踩）

* **`LWIP_WND_SCALE` 在本工程完全不可用**（已二分定位）：只定义
  `LWIP_WND_SCALE=1` 且 **`TCP_RCV_SCALE=0`、`TCP_WND` 保持 64240**（即仅让
  `struct tcp_pcb` 变大、窗口值不变）时，ping 与 shell 正常，但
  **UDP 诊断口与 TCP 都不响应** ⇒ 问题不在"结构体变大触发 Ssw trap"，
  而在该宏本身会让 lwIP 协议栈异常。**不要再用窗口缩放去放大窗口。**
* **降低 `TCP_WND_UPDATE_THRESHOLD` 到 `TCP_MSS`（失败）**：本意是让接收端每消费
  一个段就通告窗口进度、缓解零窗口。实测**反而更差**（16K~128K 全落到
  35~59 Mbps，`input` 从 706 涨到 956 tk/pkt）—— 每段都 `tcp_ack_now+tcp_output`，
  ACK/发送开销吃光了收益。已回滚为 lwIP 默认 `LWIP_MIN(TCP_WND/4, TCP_MSS*4)=5840`。
* **`TCP_WND = 32K`（失败）**：比 16K 更差（170~188 Mbps，`input` 涨到 ~1060 tk/pkt），
  说明该值处又落回"窗口被填满"的震荡区。**16K 是目前的甜点值。**

### 7.8.6 结论与建议

1. **板子侧**：`TCP_WND` 收到 16K 是解决"大 socket 缓冲塌陷"的关键，
   已修复并验证（64K 窗口 5.16 → 359 Mbps）。
2. **PC 侧（用户可做）**：把网卡 **中断裁决禁用**、关闭 LSO/大发送分载，
   吞吐应继续向 578 Mbps 逼近。
3. **若仍要更高**：需要在 PC 侧解决，而不是继续调板子 —— 因为
   Tasking/GCC 同速、`busy` 只有 37%/60%，板子侧还有余量。

## 8 2026-09-22 家族问题专项修复（串口 / lwIP）

> 背景：`tc397_sdmmc` §1.3 与 `tc397_selftest` §5.2/§5.3 定位出的几个"家族通用"问题
> （GCC 孤儿段、lwIP 定时器关中断、SPI 超时按循环计数、`frd_is_ready()` 空指针、
> `Ifx_Lwip_init*()` 重复 `initUART()`）。本次对 8 个 tc397 工程做了一轮横向排查。

### 8.1 改动

| 文件 | 改动 | 原因 |
| --- | --- | --- |
| `CMakeLists.txt`（GCC 分支） | **去掉 `-fdata-sections`**（保留 `-ffunction-sections`） | 家族通用孤儿段坑：Ubuntu gcc13 把静态变量放裸 `.<sym>` 段 → 启动不初始化 → `shellList[]` 野指针 → `shellGetCurrent()` Trap → **上电串口无输出**（ADS tricore-gcc11 不受影响） |
| `Libraries/Ethernet/lwip/port/src/Ifx_Lwip.c` | `Ifx_Lwip_pollTimerFlags()`：**只在取走并清 `timerFlags` 的那几行关中断**，`tcp_fasttmr()` / `tcp_slowtmr()` / `etharp_tmr()` / `dhcp_*_tmr()` 一律开着中断跑 | 原实现把**整个** lwIP 定时器处理放进关中断区，理由是"RX ISR 会调用 lwIP"。该理由在本工程已不成立：`ISR_Geth_Rx` 的注释和代码都明确不碰 lwIP（收包由主循环 `Ifx_Lwip_pollReceiveFlags()` 排空）。而关中断期间 `tcp_fasttmr()` 发出的延迟 ACK 要经 `low_level_output()` 做真实工作，同时 UART RX 字节会真的丢——这正是 `tc397_selftest` 里"串口失聪 + 首秒掉速"的根因 |
| 同上 | `Ifx_Lwip_init()` / `Ifx_Lwip_init_with_ip()` 删掉重复的 `initUART()` | `core0_main` 早已初始化 ASCLIN0 并打印了 banner，再跑一次 `IfxAsclin_Asc_initModule()` 会冲掉 TX FIFO、截断启动日志 |

注：本工程的 `lwiperf_report()` 用的是 `u64_t bytes_transferred` + `%llu`，与本仓库打过 64 位补丁的
`lwiperf.h` 一致（`tc397_selftest` 曾因抄成 `u32_t` 导致实参整体错位、板端 `kbits/s` 恒 0）。

### 8.2 验证（2026-09-22，TASKING Debug，COM168）

```
ifconfig : en0 192.168.0.100/24 link UP
ping 192.168.0.2 4 : 4 sent, 4 received, 0% loss
iperf -c 192.168.0.100 -p 5001 -w 64K -t 12 : 544 MBytes / 12 s = 380 Mbits/sec
板端 IPERF report : total bytes: 570425368, duration in ms: 12003, kbits/s: 380188
                    （与 PC 侧一致；也再次证明 64 位字节计数器路径正常）
全程无 tx_error / ERR_IF
```

GCC（ADS tricore-gcc11 11.3.1）与 TASKING v6.3r1 均 **0 error**。

---

## 9 许可

- iLLD/Libraries：Infineon Boost Software License 1.0
- Letter-Shell：MIT；lwIP：BSD
- 其余移植代码内部许可
