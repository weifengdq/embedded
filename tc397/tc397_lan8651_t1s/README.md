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
| `link` | T1S 链路（sync/pst/phy_link/irq） | 见 §5 |

`t1stat`（从节点 ID1，常态）：

```
DEVID=0x00086512 SYNC=0 RESETC=0 oa_cfg=0x9006
PLCA en=1 id=1 ncnt=8 pst=0 tot=0x0020 burst=0x0080
PHY bmcr=0x0000 bmsr=0x0805(link=1) id=0x0007/0xC1B3
MAC ncr=0x0C(TXEN=1 RXEN=1) ncfgr=0x02020040 nsr=0x04
BUF rba=0 txc=48 irq=idle(1)
```

诊断寄存器：`STS1 (0x0004CA18)=0`（无 RXINTO/UNEXPB/BCNBFTO），
`BCNCNT (0xCA26/27)=0`（适配器不发 BEACON，纯 CSMA 回退）。

---

## 5 实测数据（2026-09-14）

从节点（PLCA ID1，默认）：

* PC→板 `ping 192.168.1.100`：3/3，avg ~0.7ms，ttl=255
* 板→PC `ping 192.168.1.1 3/4 32`：0% loss，0~1ms
* UDP echo（端口 9）：22/64/200B 全部原样返回
* **iperf2 TCP（Release）：20s / 18.5MB / 7.69Mbps**（板端 server 报告 7688kbits/s，
  与客户端一致；接近 10M 理论极限，对标 tc387 的 8.3Mbps）
* Shell 自动化 `temp/test_lan8651_shell.py`：9/9 PASS

主节点（`plca 0 8` live 切换，ID0/协调器）：

* `t1stat`：`pst=1`，串口 `LAN8651 link up` + `gratuitous_arp=sent`
* `STS1=0`（适配器不做协调器，无 BEACON 冲突）
* PC→板 ping：2/2、3/3，0% loss
* **iperf2 TCP：Debug 20s/11.6MB/4.79Mbps；Release 15s/14.3MB/7.89Mbps**

---

## 6 已知问题

1. **K2L USB-10BASE-T1S 适配器会 wedged**（双向无包，`ip -s` TX 涨 RX 停，
   板端 Shell/SPI 一切正常；板复位无效，复位适配器即恢复）：
   轻载几十秒~几分钟或 iperf 后偶发。 workaround（屡试屡爽）：
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

## 7 许可

* iLLD/Libraries：Infineon Boost Software License 1.0
* Letter-Shell：MIT；`aurix_flasher`：MIT + Apache 2.0
* 其余移植代码内部许可
