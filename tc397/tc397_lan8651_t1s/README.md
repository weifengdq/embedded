# tc397_lan8651_t1s — TC397 + LAN8651 10BASE-T1S (QSPI4) + LwIP + Letter-Shell

本工程由 `tc397_uart_lettershell` 复制，移植 `lan8651/tc387_lan8651_lwip_iperf_gcc`
的 LAN8651 驱动 + LwIP 胶水，合并 `tc397_lwip_iperf` 的 Shell/iperf 框架。
对端为 USB-10BASE-T1S（K2L `184f:0051`，网卡 `enx001ec0d1c337`）直连 PC。

* 工具链：tricore-gcc 13.4.1（`/opt/tricore-gcc`）+ CMake/Ninja，Ubuntu 26.04
* 下载：DAP MiniWiggler `058b:0043` + TAS（`tas-server.service`）+ `aurix_flasher`
 （复用 `/home/z/lz/tc387/ref/aurix_flasher_linux-master/linux/aurix_flasher`）
* 调试串口：`/dev/ttyACM0`（1a86:55d3），ASCLIN0 P14.0/P14.1，921600-8N1

---

## 1 硬件

| 信号 | TC397 Pin | 说明 |
| --- | --- | --- |
| nRST | P23.4 | GPIO 输出，复位低 10ms → 高 50ms |
| nINT | P33.7 | GPIO 输入上拉，`irq=idle` 常态 |
| MISO | P33.13 | `IfxQspi4_MRSTA_P33_13_IN`（RxSel_a） |
| MOSI | P22.0 | `IfxQspi4_MTSR_P22_0_OUT`（alt3） |
| SCLK | P22.3 | `IfxQspi4_SCLK_P22_3_OUT`（alt3） |
| nCS | P22.2 | `IfxQspi4_SLSO3_P22_2_OUT`（`autoCS=0`） |
| UART TX/RX | P14.0/P14.1 | ASCLIN0，921600 |
| LED | P13.0 | 低=亮，1Hz 心跳 |
| LAN8651 | B1-E/LMX 板 | 25MHz 晶振，3V3；T1S 双绞线接 USB 适配器 |
| PC | `enx001ec0d1c337` | `00:1e:c0:d1:c3:37`，配 `192.168.1.1/24` |

QSPI4：Mode0（trailing-edge），20MHz（芯片上限 25MHz），中断 TX100/RX101/ER102。
板端默认：MAC `02:00:00:10:BA:5E`，IP `192.168.1.100/24`，GW `192.168.1.1`，
PLCA 使能，NodeID=1（从），NodeCount=8，UDP echo 端口 9，lwiperf TCP 5001。

---

## 2 目录与移植要点

```
tc397_lan8651_t1s/
├── Libraries/LAN8651/lan8651.[hc]   # 自 tc387（739+199 行），QSPI2→QSPI4（P22.0/22.2/22.3+P33.13）
├── Libraries/Ethernet/lwip/         # 自 tc387：lwip 栈 + port（Ifx_Lwip/ethernetif_lan8651/netif）
├── Configurations/lwipopts.h        # 自 tc387：BOARDNAME→TC397，NETIF_DEBUG→OFF，
│                                    # + LWIP_RAW/ICMP（ping 用），TCP_WND/SND 8K，POOL 32，MEM 48K
├── Configurations/Configuration.h   # LAN8651 引脚/PLCA/IP/MAC/SPI 配置（见 §1）
├── Configurations/ConfigurationIsr.h# + QSPI4 TX100/RX101/ER102
├── Cpu0_Main.c                      # STM 1ms（兼 lwIP tick）+ LAN8651 init/start + UDP echo 9
│                                    # + lwiperf + link 轮询/gratuitous ARP + Shell
├── Shell/shell_port.c               # 保留 uart 基线命令 + 新增 ifconfig/ping/t1stat/t1r/t1w/plca/link
├── CMakeLists.txt/.project/.cproject/build.sh  # 重命名为 tc397_lan8651_t1s
└── build/gcc/tc397_lan8651_t1s.{elf,hex,map}
```

* **保留 tc397_0**：iLLD（TC39xB）、Ssw、Lcf（6 核）、Cpu1..5；勿用 tc387 iLLD 覆盖。
* **删掉的 GETH 残留**：tc387 `Ifx_Lwip.c` 尾部 `ISR_Geth_Tx/Rx`
  （TC397 无 `ISR_PRIORITY_GETH_*`，留之则汇编报错）。
* TC6：控制面 12B + 数据面 68B（4B header + 64B chunk），MSB-first，奇校验；
  `ETH_PAD_SIZE=2`；TX<60B 补零；MAC NCFGR=`MTIHEN|RFCS|EFRHD`；
  CONFIG0=`SYNC|RFA_ZARFE|BPS_64`（回读 `0x9006`）。
* 工程改名：`CMakeLists project`、`build.sh DEFAULT_TARGET`、
  `.project/.cproject` 内 5 处 `tc397_uart_lettershell`→`tc397_lan8651_t1s`。

构建产物（`build/` 不进 git）：

| 构建 | text | data | bss | hex |
| --- | --- | --- | --- | --- |
| Debug | 170398 | 64798 | 72156 | 649K |
| Release（板上即此版） | 188196 | 99713 | 72156 | 793K |

---

## 3 构建与下载

```bash
export PATH=/opt/tricore-gcc/bin:$PATH
cd tc397_lan8651_t1s
./build.sh build                        # Debug
./build.sh build --build-type Release   # 板上版本
./build.sh download                     # 需 TAS: systemctl status tas-server
./build.sh reset                        # RESET + Application Reset（-read 路径）
```

PC 组网（联调前必做，适配器本身是 `.2`，PC 用 `.1`）：

```bash
sudo ip addr add 192.168.1.1/24 dev enx001ec0d1c337
sudo ip link set enx001ec0d1c337 up
ping -c 3 192.168.1.100
```

---

## 4 启动日志与 Shell（921600）

```
TC397 QSPI4 + LAN8651 10BASE-T1S + LwIP + Letter-Shell
Board: TC397XX 292pin (ASCLIN0 P14.0 TX / P14.1 RX, 921600; QSPI4 P22.0/22.2/22.3+P33.13, nRST P23.4, nINT P33.7)
ChipID: 0xAF239793 CHREV=0x13
DTS raw=0x096E -> 48.50 C
Booting TC397 LAN8651 firmware (QSPI4 20MHz, PLCA ID=1 CNT=8)
LAN8651 DEVID=0x00086512 model=0x8651 rev=2
cfg readback ok (see t1stat)
LAN8651 started, MAC=02:00:00:10:BA:5E
Static IP=192.168.1.100 MASK=255.255.255.0 GW=192.168.1.1
UDP echo listening on port 9
lwIP iperf server ready (TCP 5001)
```

> 注：启动期紧随 10 次 TC6 读寄存器之后的一行打印偶发 1~2 乱码字节
> （已知 cosmetic quirk，功能无损）；寄存器回读以 `t1stat` 为准（见下）。

| 命令 | 说明 | 实测 |
| --- | --- | --- |
| `help/version/mcu/uid/uptime/reset/temp/sysinfo/mem/led` | uart 基线命令 | 正常 |
| `ifconfig` | netif（t1_0，IP/MAC/MTU/link） | `192.168.1.100 … link UP` |
| `ping <ip> [count] [size]` | 板端 RAW ICMP，Ctrl+C 中止 | `ping 192.168.1.1 3/4 32` 0% loss |
| `t1stat` | DEVID/SYNC/PLCA/PHY/MAC/BUF/IRQ 一览 | 见 §5 |
| `t1r <reg>` / `t1w <reg> <val>` | TC6 寄存器读写（hex） | `t1r 0x000A0094`=0x86512 |
| `plca [id] [count]` | 查看/ live 修改 PLCA（0=主/协调器） | `plca 0 8`→pst=1 |
| `link` | T1S 链路（sync/pst/phy_link/irq；判据见下文） | `link: UP (sync=0 pst=0 phy_link=1 …)` |
| `sqi [toid]` | SQI 信号质量 0~7（轮询模式，要 RX 流量） | `SQI=7 (SNR>=~18dB … best)` |
| `plcadiag` | PLCA 诊断 + BEACON/TO 速率 + STS1（RC） | `BEACON 1061/s, TO 1061/s`，全 0 |
| `pcsdiag` | PCS/MAC 错误 sticky 位（STS1/2/3 RC + TSR/RSR） | 全 0，`tsr=0x2A` 常态 |
| `evcnt` | TO/BCN 累计计数 | `TO_cnt≈BCN_cnt` |
| `cable` | 线缆健康综合（SQI + 错误位 + 结论） | `cable GOOD` |

### 诊断命令实测（2026-09-15，PLCA：适配器 ID0 + 板 ID1）

手册依据：datasheet §7.5 SQI（轮询流程）、§7.2.4 PLCA 诊断、
§11.5.2/3/4（STS1/2/3，**均为 RC 读清**）、§11.5.7 CTRCTRL、
§11.5.16 PRSSTS（MAXID）、§11.5.52~55（SQI 寄存器）。
寄存器（MMS4）：`SQICTL 0xA0 / SQISTS0 0xA1 / SQICFG0 0xAA（TOID 在 bit11:4）/
SQICFG2 0xAC`，`STS1 0xCA18 / STS2 0xCA19 / STS3 0xCA1A`，
`CTRCTRL 0x20 / TOCNT 0x24-25 / BCNCNT 0x26-27 / PRSSTS 0x36`
（即 OA `0x000400xx`；注意 CTRCTRL 在 `0x20` 区不是 `0xCA20`）。

```
letter:/$ plcadiag
PLCA en=1 id=1 ncnt=8 pst=1 tot=0x0020 burst=0x0080 maxid=8
BEACON 1061/s, TO 1061/s (1s window, 32-bit wrap-aware diff)
STS1(RC,read-clear)=0x0000: EMPCYC=0 RXINTO=0 UNEXPB=0 BCNBFTO=0 PSTC=0
letter:/$ pcsdiag
STS1(RC)=0x0000: DEC5B=0 ESDERR=0 PLCASYM=0 UNCRS=0 SQI=0 TXCOL=0 TXJAB=0 TSSI=0
STS2(RC)=0x0000: UV33=0 OT=0 IWDTO=0 WKEMDI=0 WKEWI=0; STS3(RC)=0x0000 ERRTOID=0
MAC ncr=0x0C ncfgr=0x02020040 nsr=0x04(IDLE=1) tsr=0x2A(COL=1 TXCOMP=1) rsr=0x02(REC=1)
letter:/$ evcnt
TO_cnt=7541 BCN_cnt=7540 (cumulative since counter enable)
letter:/$ sqi                       # iperf 背景流量下
SQI=7 (SNR>=~18dB BER<=~9.9E-16 (best))
letter:/$ cable                     # iperf 背景流量下
SQI=7 (SNR>=~18dB BER<=~9.9E-16 (best)) -> cable GOOD
phy_link=1 STS1err(DEC5B/ESDERR/PLCASYM/UNCRS)=0000 UV33=0 OT=0
```

* `tsr=0x2A`（COL+TXGO+TXCOMP）为常态：PLCA RS 为对齐 TO 会向 MAC 断言
  逻辑（假）碰撞，属正常现象（datasheet §4.6.1.5），非故障。
* SQI 需要持续 RX 流量：ping（100ms 间隔小包）下 6s 超时无结论，
  iperf 下一次即得 SQI=7；`cable` 同理（ping 下 inconclusive，iperf 下 GOOD）。
  SQI 判级：≥6 GOOD，4~5 MARGINAL，≤3 POOR（查线缆/终端/长度）。
* `TO_cnt≈BCN_cnt`：TOCNT 计的是**本地可用** TO（follower 每 cycle 1 个），
  故与 BEACON 数基本相等，属正常。
* Cable fault（HDD/TDR 类）：官方 harness 缺陷定位算法 **NDA 不公开**
  （datasheet §7.6），`cable` 命令以 SQI + 错误 sticky 位 + link 做健康综合
  结论，不能定位短路/开路点；Linux 侧 `ethtool --cable-test` 在 C2 上也不支持
  （仅 D0，见 §6 表）。

`t1stat`（从节点 ID1；下为 CSMA 回退旧值，PLCA 见 §5）：

```
DEVID=0x00086512 SYNC=0 RESETC=0 oa_cfg=0x9006
PLCA en=1 id=1 ncnt=8 pst=0 tot=0x0020 burst=0x0080
PHY bmcr=0x0000 bmsr=0x0805(link=1) id=0x0007/0xC1B3
MAC ncr=0x0C(TXEN=1 RXEN=1) ncfgr=0x02020040 nsr=0x04
BUF rba=0 txc=48 irq=idle(1)
```

诊断寄存器：`STS1 (0x0004CA18)=0`（无 RXINTO/UNEXPB/BCNBFTO）。
BEACON/TO 计数器在 MMS4 `0x20+` 区（注意不是 `0xCA20+`），默认关闭，
` t1w 0x00040020 0x3` 使能后读 `0x00040026/27`（BCN）、`0x00040024/25`（TO）；
计数器读后不清零（只增，32 位回绕）。

`link`/`lan8651_link_up()` 判据为 `SYNC || PST || PHY_LINK`（BMSR link 位，
latch-low 故读两次取第二次）：K2L 适配器无 BEACON 时 `sync=0/pst=0` 但
`phy_link=1` 且业务正常，故必须纳入 PHY_LINK，否则 `link` 会误报 DOWN。
netif 侧另有 `LAN8651_FORCE_LINK_UP=1` 兜底。

---

## 5 实测数据

> 2026-09-14 的数据为 **CSMA 回退**下测得（当时适配器侧 PLCA 未使能，
> 板端恒 `pst=0`）；2026-09-15 起为 **PLCA 模式**（见 §5.3）。

### 5.1 CSMA 回退：从节点（PLCA ID1，默认）

* PC→板 `ping 192.168.1.100`：3/3，avg ~0.7ms，ttl=255
* 板→PC `ping 192.168.1.1 3/4 32`：0% loss，0~1ms
* UDP echo（端口 9）：22/64/200B 全部原样返回
* **iperf2 TCP（Release）：20s / 18.5MB / 7.69Mbps**（板端 server 报告 7688kbits/s，
  与客户端一致；接近 10M 理论极限，对标 tc387 的 8.3Mbps）
* Shell 自动化 `temp/test_lan8651_shell.py`：9/9 PASS

### 5.2 CSMA 回退：主节点（`plca 0 8` live 切换，ID0/协调器）

* `t1stat`：`pst=1`，串口 `LAN8651 link up` + `gratuitous_arp=sent`
* `STS1=0`（适配器不做协调器，无 BEACON 冲突）
* PC→板 ping：2/2、3/3，0% loss
* **iperf2 TCP：Debug 20s/11.6MB/4.79Mbps；Release 15s/14.3MB/7.89Mbps**

### 5.3 PLCA 模式（2026-09-15，适配器 ID0 协调器 + 板 ID1 跟随）

使能命令（见 §6 表 `set-plca-cfg` 行）：适配器 `ethtool --set-plca-cfg … node-id 0 …` 后，
板端 `t1stat` 从 `pst=0` 变 `pst=1`，`link: UP (sync=0 pst=1 phy_link=1 …)`；
BEACON 约 1061/s（bus cycle ~0.94ms，见 `plcadiag`），TO 计数同步涨。

* PC→板 ping 4/4（~0.8ms），UDP echo 5/5×100B
* **iperf2 TCP（Release）：20s / 20.9MB / 8.70Mbps**（8.60~8.81 各 interval，
  高于 CSMA 的 7.69——PLCA 免碰撞的收益）；测后 ping 3/3 存活（此前 CSMA 下
  iperf 后常 wedged，PLCA 下本次全程无 wedged，样本有限仅供参考）
* 角色对调（板 `plca 0 8` 协调器 + 适配器 `node-id 1` 跟随）：
  适配器 `Link detected: yes`（跟随侧 link 反映 PLCA 同步，即收到了板端 BEACON），
  ping 3/3，**iperf 15s/15.8MB/8.76Mbps**；板端 `STS1=0`（无 UNEXPB）；
  测完已恢复板 ID1 + 适配器 ID0（`t1stat` 回 `pst=1`，ping 2/2）

---

## 6 Linux 侧文档命令汇总（实测）

文档（已转 txt，均在 `tc397/ref/`，不进 git）：
`LAN867x-Linux-Driver-Install-Application-Note-00005992.txt`（AN5992），
`EVB-LAN8670-USB_Linux_Driver_3v0_README.txt`。
本机：内核 `7.0.0-31-generic`，ethtool `6.19`（≥6.7 ✓）。

| 文档命令 | 本机实测 | 结果 |
| --- | --- | --- |
| `uname -r` | `7.0.0-31-generic` | ✓ |
| `ip link show`（§7 找 `enx<MAC>`） | `enx001ec0d1c337`（`00:1e:c0:d1:c3:37`） | ✓ |
| `lsmod \| grep microchip_t1s` | 已加载（Used by 1），免编译驱动 | ✓ |
| `dmesg` 绑定证据 | `LAN867X Rev.C2 usb-001:018:00: attached PHY driver` | ✓（需 sudo） |
| `/sys/bus/mdio_bus/devices/usb-001:018:00/driver` | → `LAN867X Rev.C2`，`phy_id 0x0007c165` | ✓ |
| `ethtool --version`（文档要 ≥6.7） | `6.19` | ✓ |
| `ethtool --get-plca-cfg enx…` | 初始 `Enabled: No, node 255`（即之前全跑 CSMA 的原因） | ✓ |
| `ethtool --set-plca-cfg enx… enable on node-id 0 node-cnt 8 to-tmr 0x20 burst-cnt 0x0 burst-tmr 0x80` | 回读 `Enabled: Yes, node 0 (coordinator)`，8/32/0/128 | ✓，见 §5.3 |
| `ethtool enx…` 链路 | `10Mb/s Half, Link detected: yes` | ✓ |
| `ip addr add 192.168.1.1/24` + `ip link set up` | 替代文档 nmcli，`nmcli device` 显示 connected (externally) | ✓ |
| `iperf3 -s` / `iperf3 -c … -u -b 10M`（文档 §10，UDP 9.43M） | 板端无 UDP iperf server（仅 TCP lwiperf + UDP echo 9）：`iperf3 -c 192.168.1.100 -u -b 10M` 报 `control socket has closed unexpectedly`；UDP 能力由 echo 覆盖（§5） | 按预期不适用，已记录 |
| `ethtool --cable-test`（文档 §12，仅 D0） | `PHY driver does not support cable testing`（本机 C2） | 按预期不支持，已记录 |
| `insmod microchip_t1s.ko …` / `load.sh` | 本机驱动已内置绑定，无需编译加载 | 未执行（不需要） |
| PLCA Configurator TUI | 需求简单，直接 ethtool 命令即可 | 未用 |

---

## 7 已知问题

1. **K2L USB-10BASE-T1S 适配器会 wedged**（双向无包，`ip -s` TX 涨 RX 停，
   板端 Shell/SPI 一切正常；板复位无效，复位适配器即恢复）：
   CSMA 回退下轻载几十秒~几分钟或 iperf 后偶发；**PLCA 使能后本次全程
   （ping+UDP+两轮 iperf+主从对调）未再 wedged**（样本有限，仅供参考，
   workaround 照旧有效）。 workaround（屡试屡爽）：
   ```bash
   echo -n 0 | sudo tee /sys/bus/usb/devices/1-7/authorized > /dev/null; sleep 2
   echo -n 1 | sudo tee /sys/bus/usb/devices/1-7/authorized > /dev/null; sleep 3
   sudo ip addr add 192.168.1.1/24 dev enx001ec0d1c337
   ```
   iperf 测试方法：复位适配器后立即跑（`iperf -c 192.168.1.100 -p 5001 -t 15 -w 32K -M 1024`）。
2. **启动期一行打印偶发乱码字节**（§4 注）：单发 `Asc_write` 长行在 QSPI
   burst 后偶发，功能无损；`t1stat`（分块写）始终正常，以它为准。
3. 板端默认从节点 ID1；主节点用 `plca 0 8` live 切换（掉电/复位恢复 ID1，
   如需默认主节点改 `Configuration.h` 的 `LAN8651_PLCA_NODE_ID` 重编）。
4. `lwiperf` 只支持单流 TCP；测速用 iperf2（非 iperf3）。

---

## 8 许可

* iLLD/Libraries：Infineon Boost Software License 1.0
* Letter-Shell：MIT；`aurix_flasher`：MIT + Apache 2.0
* 其余移植代码内部许可
