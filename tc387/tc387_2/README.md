# tc387_2 — TC387 + LwIP GETH 千兆 iperf + Letter-Shell (921600)

本工程在 `ref/tc387_lwip_iperf_gcc2` 基础上，合并 `tc387_1` 的 **Letter-Shell 921600 高速串口** 与 **LwIP GETH 千兆 iperf**，实现 **Ubuntu 26.04 + tricore-gcc 13.4.1 + CMake/Ninja**  下的 `tc387_2` 移植。板端静态 IP `192.168.0.100/24`，网关 `192.168.0.1`，MAC `DE:AD:BE:EF:FE:ED`，串口 `ASCLIN4 P00.9 TX / P00.12 RX 921600-8N1`。

* 原 iperf 工程见 `ref/tc387_lwip_iperf_gcc2`（fGETH 300MHz、PBLX8、流控等千兆优化，见 §3）
* Shell 移植见 `tc387_1/Shell`（1024B FIFO、RX 32>TX 31>STM 10、批量 ISR 优化，见 tc387_1/README §5.4）
* 合并后主循环同时轮询 **LwIP (pollTimer/ReceiveFlags)** 与 **Shell (UART_Poll/Shell_Process)**，并新增 `ping` 原生命令（RAW ICMP，支持 Ctrl+C 中止）

---

## 1 硬件与环境

| 项目 | 说明 |
|------|------|
| MCU | TC387 4×TriCore @300MHz（iperf+Shell 运行于 Core0） |
| MAC/PHY | 片内 GETH + RTL8211F RGMII 1000BASE-T |
| 网口 | 板端直连 PC `enp6s0`，PC 配置 `192.168.0.1/24`（见 §4.4） |
| 串口 | ASCLIN4 P00.9(TX)/P00.12(RX) → `/dev/ttyACM0` (1a86:55d3 CH340) 921600-8N1 |
| DAP | 058b:0043 DAS JDS TriBoard → `/dev/ttyUSB0` FTDI，仅 TAS 下载 |
| 编译器 | tricore-gcc 13.4.1 (`/opt/tricore-gcc/bin`) + CMake 4.2 + Ninja 1.13 |
| 产物 | `build/gcc/tc387_2.elf` (text 156k) / `tc387_2.hex` (21k) / `tc387_2.map` |

---

## 2 目录结构

```
tc387_2/
├── cmake/tricore-gcc-toolchain.cmake  # Linux /opt/tricore-gcc
├── Configurations/lwipopts.h          # + LWIP_RAW/LWIP_ICMP for ping
├── Configurations/ConfigurationIsr.h  # OS_TICK 10, ASCLIN4 TX31 RX32, GETH 100/101
├── Libraries/
│   ├── iLLD/TC3xx/...                 # iLLD (Asclin/Stm/Scu/Dts/Port/Geth...)
│   ├── UART/UART_Logging.c/h          # 921600 优化版 (1024 FIFO, RX HAL 中断)
│   ├── Ethernet/lwip/...              # lwIP NO_SYS=1 + iperf + RTL8211F
│   └── Infra/Service                  # Bsp, Ssw
├── Shell/
│   ├── letter-shell/src/              # 3.2.4
│   ├── shell_cfg_user.h               # 1024 Shell缓冲, 8历史, tick=g_TickCount_1ms
│   └── shell_port.c/h                 # + ifconfig/ethstat/ping (RAW ICMP)
├── Lcf_Gnuc_Tricore_Tc.lsl            # + .shellCommand/.shellVar (KEEP), lmuram_nc
├── Cpu0_Main.c                        # STM 1ms + DTS + UART/Shell + GETH/LwIP/iperf/UDP diag
├── build.sh / serial_monitor.py       # 一键构建/烧录/监控 (替代 build.ps1)
└── build/gcc/tc387_2.{elf,hex,map}
```

---

## 3 关键移植点

### 3.1 工程名与构建
* `project(tc387_2)` → `tc387_2.hex`，`AURIX_TARGET_NAME` 同名，`DEFAULT_TARGET=tc387_2` in `build.sh`
* `Configurations/ConfigurationIsr.h`: `OS_TICK 10`（原 99，降低让 UART 抢占），新增 `ASCLIN4_TX 31 / RX 32`
* `cmake/tricore-gcc-toolchain.cmake`: Linux 默认 `/opt/tricore-gcc/bin`
* `CMakeLists.txt`: 增 `SHELL_CFG_USER="shell_cfg_user.h"`，显式 `target_sources(IfxDts_Dts.c)`，`IFXGETH_MAX_RX 64 / TX 8`，`Lcf` 的 `.shellCommand/.shellVar`
* `Lcf_Gnuc_Tricore_Tc.lsl`: 保留 `lmuram_nc` (0xB0040000) 的 `.lmudata/.lmubss` 映射（DMA 非缓存），末尾追加 `.shellCommand/.shellVar` (KEEP, PROVIDE)

### 3.2 UART 921600 优化 (来自 tc387_1)
* `SERIAL_BAUDRATE 921600`, `oversampling 16`, `medianFilter three`, `samplePoint 12`, `prescaler 1`, `PadDriver cmosAutomotiveSpeed4`
* `ASC_TX/RX_BUFFER_SIZE 1024` (原 256)，`RX FIFO Level 1` (原 8，确保每字节即中断)，`TX Level 8`
* 中断优先级 `RX 32 > TX 31 > STM 10`（原 STM 99 抢占 UART 导致溢出）
* `asclin4RxISR`: `isrReceive` 后 `getReadCount` 批量 64B `read` 再 `Shell_RxPushBulk`，`UART_Poll` 仅作 `RFL` 丢失回退（关中断再 `isrReceive`）并单包排空

### 3.3 Shell 合并与主循环
* `Shell_Init` + `Shell_PrintBanner` 在 `Dts` 初始化后、`GETH` 初始化前完成，复用 `tc387_1` 的 1024B `gShellBuffer` 与 `SHELL_RX_RING 1024`、`_write` 经 `IfxAsclin_Asc_write` 分片 128B
* `Cpu0_Main.c` 合并版：STM 1ms → P00.5 高 → `initUART` → `DTS` → `Shell_Init` → 打印 banner/ChipID/STM/DTS → `IfxGeth_enableModule` → `Ifx_Lwip_init_with_ip` → `lwiperf` + `diag UDP`
* 主循环为 **LwIP 优先、Shell 限频**：
  ```c
  uint32_t lastShellTick=0;
  while(1){
    Ifx_Lwip_pollTimerFlags();
    Ifx_Lwip_pollReceiveFlags();
    if (Shell_HasPending() || g_TickCount_1ms != lastShellTick){
      UART_Poll(); Shell_Process(); lastShellTick=g_TickCount_1ms;
    }
  }
  ```
  避免 Shell 每迭代开销拖慢 GETH 线速（实测 Debug 538 Mbit/s，见 §7）

### 3.4 lwIP 选项与 ping
* `Configurations/lwipopts.h`: 增 `LWIP_RAW 1 / LWIP_ICMP 1 / LWIP_RAW_WITH_ICMP 1` 以支持板端主动 ping（`NO_SYS` 下用 RAW PCB）
* `Shell/shell_port.c` 新增：
  * `ifconfig`: 遍历 `netif_list` 打印 IP/NM/GW、HWaddr、MTU、link
  * `ethstat`: `g_diag_rx_ok/err/nobuf`, `g_diag_rbu`, `isrRx/Tx`, `SYSBUS/RXCTL/TXCTL`
  * `ping <ip> [count] [size]`: RAW PCB `IP_PROTO_ICMP`，`inet_chksum`，`raw_sendto`，`raw_recv` 解析 IP 头 + ICMP 回显（`ICMP_ER`、`id/seq` 匹配），等待 1s 超时、`Ctrl+C` (0x03) 扫描 `gRxRing` 中止，统计 `sent/recv/loss`
  * `mem`: 显示 `MEM_SIZE` (LMURAM)
  * 保留 `mcu/uid/uptime/reset/temp/sysinfo/version`
* 复用 `tc387_1` 的 `shell_cfg_user.h` 的 `extern volatile uint32 g_TickCount_1ms`（实际定义在 `Ifx_Lwip.c`）

### 3.5 GETH 千兆关键（继承自 ref）
* `fGETH 300MHz` (`IfxScuCcu_setGethFrequency(300000000)` 在 `IfxGeth_enableModule` 前)，`PBLX8=1 RXPBL/TXPBL=32`，`802.3x EHFC RFA1 RFD4 PAUSE 0x1000`，LMU 非缓存视图，TSO，`TCP_MSS 1460` 窗口 64k，`PBUF_POOL 32` 等（详见 `ref/tc387_lwip_iperf_gcc2/README.md` §3）
* `lwiperf` 的 `bytes_transferred` 已为 `u64_t`，长时测速无溢出

---

## 4 构建与烧录 (Ubuntu 26.04)

### 4.1 工具链
```bash
sudo apt update && sudo apt install -y cmake ninja-build python3-serial usbutils iperf
export PATH=/opt/tricore-gcc/bin:$PATH
tricore-elf-gcc --version  # 13.4.1
```

安装见 `tc387_1/README.md` §3.2–3.3（13.4.1 tar.gz + DAS 8.3.0 + libftd2xx + tas-server).

### 4.2 一键脚本
```bash
cd tc387_2
./build.sh build                        # Debug
./build.sh build --build-type Release   # Release (O3, 无 LTO)
./build.sh download                     # 需 TAS: systemctl status tas-server; aurix_flasher -id list
./build.sh download --build-type Release --id 0
./build.sh clean
```
产物 `build/gcc/tc387_2.{elf,hex,map}`。

注意：`aurix_flasher` 默认 `-start on` 后板子可能仍处 halt，需 TAS 额外 `RESET` (本脚本的 `check` 后 `read` 会触发)。若串口无输出，执行 `aurix_flasher -id 0 -read 0xF003A000` 或按板载 RESET 按钮，`P00.5` 应变高 (`P00_OUT 0x120`)。

### 4.3 手动 CMake
```bash
cmake -S . -B build/gcc -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/tricore-gcc-toolchain.cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build build/gcc -j$(nproc)
tricore-elf-size --format=berkeley build/gcc/tc387_2.elf
```

### 4.4 网络环境 (enp6s0 直连)
```bash
sudo ip addr flush dev enp6s0
sudo ip addr add 192.168.0.1/24 dev enp6s0
sudo ip link set enp6s0 up
ip addr show enp6s0  # inet 192.168.0.1/24
ping -I enp6s0 -c 3 192.168.0.100   # <1ms
iperf -c 192.168.0.100 -p 5001 -t 10  # ~538 Mbit/s (Debug, Shell 合并后)
# 板端 Shell 内亦可
# letter:/$ ping 192.168.0.1 3 32
# letter:/$ ifconfig; ethstat; sysinfo
```

---

## 5 串口与 Shell

```bash
python3 -m serial.tools.miniterm /dev/ttyACM0 921600 --raw
python3 serial_monitor.py --port /dev/ttyACM0 --baud 921600
```

启动日志（921600）：
```
LWIP   : MAC DE:AD:BE:EF:FE:ED
LWIP   : IP  192.168.0.100
...
iperf TCP server on 5001, diag UDP 5002, sink 5003
LINK   : UP   1000M full-duplex
letter:/$ 
```

| 命令 | 说明 |
|------|------|
| `help` | 列出全部命令 |
| `mcu` | ChipID/SCU_ID/RSTSTAT/CCUCON/STM |
| `uid` | CHIPID + DTSSTAT |
| `temp` | DTS 温度 |
| `uptime` | g_TickCount 天时分秒 |
| `sysinfo` | mcu+temp+uptime |
| `ifconfig` | netif IP/NM/GW/HWaddr/link |
| `ethstat` | DMA/诊断计数器 |
| `ping <ip> [count] [size]` | RAW ICMP echo，`Ctrl+C` 中止 |
| `mem` | LWIP 堆大小 |
| `reset/reboot` | 软件复位 |

示例：
```
letter:/$ ifconfig
netif 0: en0 IP 192.168.0.100 NM 255.255.255.0 GW 192.168.0.1
  HWaddr DE:AD:BE:EF:FE:ED MTU 1500 flags 0x0F  link UP
letter:/$ ping 192.168.0.1 3 32
PING 192.168.0.1 : 32 bytes count 3
Reply from 192.168.0.1: bytes=32 seq=0 time=0 ms
...
PING statistics: 3 sent, 3 received, 0% loss
```

`Ctrl+C` 在 ping 等待循环中扫描 `gRxRing` 的 `0x03` 实现中止。

---

## 6 常见问题

* **Q: 烧录后串口无输出**  
  A: `aurix_flasher` 的 `-start on` 后偶尔仍 halt，执行 `aurix_flasher -id 0 -read 0xF003A000` 观察 `P00_OUT bit5` 是否为 1；若为 0，按 RESET 或 `read` 触发的 `RESET (run)`，再 `stty -F /dev/ttyACM0 921600 raw -echo` 后重读。

* **Q: `ping: Destination Host Unreachable` / ARP incomplete**  
  A: 确认 PC `enp6s0` 为 `192.168.0.1/24` 且 `link UP 1000M`（`ip addr` + `ethtool enp6s0`），板端 `ifconfig` 的 `link UP`，`GETH` 已使能；`tcpdump -i enp6s0 arp` 应见请求与应答。

* **Q: iperf 带宽低于预期 (~660 → 538)**  
  A: 合并 Shell 后主循环多了 `Shell_Process`，Debug `-Og` 下约 538 Mbit/s（Release O3 约 540，无 LTO）。纯轮询版（无 Shell）GCC 约 660。已将 Shell 限频至 1kHz/有数据时才调度，仍略有开销，属正常。

* **Q: Release LTO 链接报 `undefined reference to updateLwIPStackISR`**  
  A: 已在 `CMakeLists.txt` 去除 `-flto`（Release 仅 `-O3`），并给 ISR 加 `__attribute__((used))`。若需 LTO，需对 ISR 所在编译单元加 `-fno-lto` 或 `KEEP` 向量表。

* **Q: 串口乱码**  
  A: 确认 `921600` 且端口为 `ttyACM0` (CH340) 非 `ttyUSB0` (DAP)，`dmesg | grep tty`，Fifo 已扩大至 1024，`RX Level 1`。

---

## 7 性能

| 环境 | 吞吐 | 说明 |
|------|------|------|
| tc387_2 Debug (本合并版, Shell 限频) | **~538 Mbit/s** | `iperf -t 10`, 主循环含 Shell |
| tc387_2 Release O3 (无 LTO) | **~540 Mbit/s** | 同上 |
| ref 纯 iperf GCC 11.3.1 Debug | **~660 Mbit/s** | 无 Shell，见 ref README |
| ref TASKING 6.3 Release | **~945 Mbit/s** | 千兆线速 |

`ping` 往返 <1ms，`ifconfig` 链路 `UP 1000M`。

---

## 8 许可

* iLLD: Boost Software License 1.0
* Letter-Shell: MIT
* aurix_flasher_linux: MIT + Apache 2.0
