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

## 7 许可

- iLLD/Libraries：Infineon Boost Software License 1.0
- Letter-Shell：MIT；lwIP：BSD
- 其余移植代码内部许可
