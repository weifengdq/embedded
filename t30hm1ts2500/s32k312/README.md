# S32K312 + T30HM1TS2500 (10BASE-T1S) 固件

> 目标板: NXP S32K312 (Cortex-M7 120MHz) + Onsemi T30HM1TS2500 10BASE-T1S MAC-PHY
> 调试器: 板载 J-Link OB (SWD)   调试串口: LPUART0 115200 (CH343P -> /dev/ttyACM2)
> 协议栈: lwIP 2.2.0 (NO_SYS) + letter-shell 3.2.4, 驱动在 `common/s2500/` (平台无关)

---

## 1. 快速开始 (3 条命令)

```bash
cd s32k312
./build.sh build          # CMake+ninja 编译
./flash.sh                # J-Link 烧写并运行
python3 ../tools/s25.py --auto s32k -c "s2500 info"
```

上电串口应打印:

```
==================================================
 S32K312 + T30HM1TS2500 10BASE-T1S
 CORE 120MHz  UART0 115200
 SPI : GPIO soft 3000kHz
==================================================
```

---

## 2. 硬件与接线

### 2.1 引脚表 (2026-11 用户纠正后的最终线序)

| 信号 | S32K312 引脚 | SIUL2 pinIndex | 复用/配置 | 方向 | 对接 S2500 |
|------|--------------|----------------|-----------|------|-----------|
| CS_N (白)  | PTB5  | 37  | MSCR37 / 软件 GPIO 输出 (硬件模式 SSS=4 = LPSPI0_PCS0) | 输出 | CS_N |
| MISO (灰)  | PTD16 | 112 | MSCR112 IBE=1, 输入经 **SIUL2_IMCR230 = 3** | 输入 | SDO |
| MOSI (蓝)  | PTB4  | 36  | MSCR36 SSS=3 = **LPSPI0_SOUT** | 输出 | SDI |
| SCLK (紫)  | PTD15 | 111 | MSCR111 SSS=4 = **LPSPI0_SCK** | 输出 | SCLK |
| IRQ_N (绿) | PTE9  | 137 | GPIO 输入, 内部上拉, 低有效 (eirq 8) | 输入 | IRQ_N |
| RST_N (黄) | PTD14 | 110 | GPIO 输出 | 输出 | RST_N |
| 调试 TX | PTC3 | — | LPUART0_TX ALT4 | 输出 | CH343P RX |
| 调试 RX | PTC2 | — | LPUART0_RX ALT4 | 输入 | CH343P TX |

复用依据: **NXP S32K3xx RM Rev.12 (2025-11-11) 内嵌附件 `S32K312_IOMUX.xlsx`**
(可用 `pdfdetach -saveall S32K3XXRM.pdf` 取出):

- `S32K312_IO Signal Table`: PTB4 = `MSCR36 SSS=011 LPSPI0_SOUT`,
  PTD16 = `MSCR112 SSS=100 LPSPI0_SIN`, PTD15 = `MSCR111 SSS=100 LPSPI0_SCK`,
  PTB5 = `MSCR37 SSS=100 LPSPI0_PCS0` (SSS=011 是 PCS1)
- `S32K312_Input Muxing`: `LPSPI0_SIN` 目的端 = `SIUL_IMCR742`,
  源 `SSS=0b0011 -> PTD16`。**注意该表编号是"输入通道全局编号", 实际寄存器
  索引要减 512** (RM 第 25 章 SIUL2 寄存器表只到 IMCR473): 742-512 = **230**,
  与 Zephyr `base100.h` 的 `PTD16_LPSPI0_SIN_I(230,3)` 完全一致。
  直接写 IMCR742 会触发总线错误 (HardFault)。

### 2.2 供电与终端

- VIO/VCC 3.3V; SPI 时钟上限 25MHz (VIO=3.3V) / 24MHz (VIO=2.5V)
- 总线两端各一个 100Ω 终端, 中间节点不要加终端
- 板号: 本板 MAC=44:9F:79:29:8E:B3, IP=192.168.10.13/24, gw=192.168.10.10

---

## 3. 环境搭建 A: 命令行 CMake (推荐, 本仓库默认)

### 3.1 依赖

```bash
sudo apt install cmake ninja-build gcc-arm-none-eabi
# J-Link (SEGGER): 下载 JLink_Linux_x86_64.deb 安装, 默认路径 /opt/SEGGER/JLink
```

版本要求: CMake >= 3.22, arm-none-eabi-gcc >= 10 (实测 14.2.1)。

### 3.2 编译

```bash
cd s32k312
./build.sh build            # Debug
./build.sh build Release    # Release
./build.sh rebuild          # 清空后重编
```

产物: `build/Debug/s32k312_s2500.elf/.hex/.bin`

CMake 预设 (CMakePresets.json): `Debug` / `Release`, 工具链文件 `cmake/gcc-arm-none-eabi.cmake`。

### 3.3 编译期开关 (重要)

| 宏 | 默认 | 说明 |
|----|------|------|
| `S2500_SPI_BACKEND` | **1 (硬件 LPSPI0)** | 1=`lpspi_hw.c` LPSPI0 硬件 SPI(默认, 2026-11-20 打通); 0=`spi_soft.c` GPIO 位操作 |
| `LPSPI_HW_TARGET_HZ` | 15000000 | 硬件后端目标 SCK, 按实测功能时钟(60.55MHz)自动选分频, 上限 `LPSPI_HW_MAX_HZ`=25MHz |

两个后端都编译进固件, **运行时**可切换(不用重新烧写): `s32k hw off` 退回软件 SPI,
`s32k hw on` 切回硬件。

编译期指定后端 (默认 `S2500_SPI_BACKEND=1`; 想编译期就退回软件 SPI 用 0):

```bash
cmake -S . -B build/hw -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake \
      -DCMAKE_C_FLAGS=-DS2500_SPI_BACKEND=0
cmake --build build/hw
```

---

## 4. 环境搭建 B: S32 Design Studio (图形化)

1. 安装 **S32 Design Studio 3.5+** (含 S32K3xx 器件支持包)。
2. `File -> New -> S32DS Project from Example` **不需要**: 本工程是 CMake 工程,
   用 `File -> New -> C Project` 建**空工程**后手工加文件也可以, 但更简单的方式是:
   - `File -> Import -> General -> Existing Projects into Workspace` 选择
     `s32k312` (S32DS 3.5 支持 CMake 工程导入, 自动读取 CMakePresets.json)
   - 若提示找不到工具链: `Window -> Preferences -> C/C++ -> Build -> Global Toolchain`,
     选择 S32DS 自带 `GNU Arm Embedded` 或系统 arm-none-eabi。
3. Build: 右键工程 `Build Project`, 产物在 `build/Debug/`。
4. 调试配置: `Run -> Debug Configurations -> GDB SEGGER J-Link Debugging`,
   新建配置, 设备选 **S32K312**, 接口 SWD, 速度 4000kHz,
   Executable 选 `build/Debug/s32k312_s2500.elf`。
5. 启动调试后可打断点、看外设寄存器 (S32K3xx 的 SVD 由器件包提供)。

> 说明: 本仓库的 `Project_Settings/Startup_Code`、`RTD/`、`generate/`、
> `board/` 都来自 S32 Design Studio 的 RTD 配置工具, 改引脚/时钟建议在
> S32DS 的 `Pins`/`Clocks` 工具里改后重新生成, 但 **SPI 引脚由
> `lpspi_hw_init()` 直接写 MSCR/IMCR 覆盖**, 不依赖生成配置。

---

## 5. J-Link 驱动与烧写

### 5.1 安装

```bash
sudo dpkg -i JLink_Linux_V*.deb      # 安装到 /opt/SEGGER/JLink
lsusb | grep -i segger               # 应看到 1366:0105 (J-Link OB)
```

非 root 用户需要 udev 规则 (安装包已带), 否则 `JLinkExe` 报 `Cannot connect to probe`。

### 5.2 烧写

```bash
./flash.sh              # 默认 Debug
./flash.sh Release
JLINK_SPEED=1000 ./flash.sh     # 降低 SWD 速度
```

`flash.sh` 实际执行:

```
device S32K312
si SWD
speed 4000
connect
r ; h
loadfile build/Debug/s32k312_s2500.hex
r ; g
```

### 5.3 串口

```bash
./build.sh monitor                 # picocom -b 115200 /dev/ttyACM2
python3 ../tools/s25.py --auto s32k -c "s2500 info"
```

`s25.py --auto s32k` 按 VID:PID (1a86:55d3) 自动找 CH343P 第 1 个口。
若系统里有两块 CH343P 板子 (rk3506 也用), 用 `-p /dev/ttyACM2` 指定。
**注意 letter-shell 只认 CR (`\r`), 发 `\r\n` 会得到 "Command not Found"**。

---

## 6. 固件结构

| 文件 | 说明 |
|------|------|
| `src/main.c` | 初始化顺序: 时钟 -> 引脚 -> 中断/SysTick -> UART -> board_init -> app -> 主循环 |
| `src/s2500_port.c` | 平台层: UART0 轮询收发、SPI HAL、延时、复位、`s32k` 诊断命令 |
| `src/s2500_spi.c/.h` | SPI 后端分发 (软件/硬件), 编译期 `S2500_SPI_BACKEND` 选择 |
| `src/spi_soft.c` | 软件 SPI (GPIO 位操作, 模式0, MSB first, ~3MHz) |
| `src/lpspi_hw.c` | 硬件 LPSPI0 驱动 (PIO/eDMA, 引脚 MSCR/IMCR, 时钟自动标定) |
| `src/systick.c` | 1ms SysTick |
| `board/` | S32DS 生成的引脚/端口配置 (SPI 引脚会被 lpspi_hw 覆盖) |
| `RTD/` `generate/` `PlatformSDK/` | NXP RTD 6.0.0 驱动与 S32K312 头文件 |
| `../common/s2500/` | 平台无关 S2500 驱动 / lwIP 胶水 / shell 命令 |

---

## 7. 命令手册 (串口 shell)

### 7.1 S2500 芯片命令 (`s2500 <子命令>`)

| 命令 | 说明 |
|------|------|
| `s2500 info` | IDVER/PHYID/STDCAP/CONFIG0/MAC/PHY 状态/PLCA/ONIPVER 等 |
| `s2500 selftest` | 7 项上电自检 (寄存器/PCS 环回/PHY 环回/链路/SQI/HDD) |
| `s2500 status` | 链路/PLCA/SQI/冲突/驱动收发统计 |
| `s2500 wd [on|off]` | 自愈看门狗统计/开关（默认 on，芯片自发复位后自动重新初始化） |
| `s2500 counters [clear]` | MAC 计数器 (TX/RX 帧/字节/错误) |
| `s2500 reg <addr> [val]` | 读/写寄存器 (addr 16 进制) |
| `s2500 plca ...` | PLCA 配置 (en/id/ncnt/totmr) |
| `s2500 sqi` / `s2500 hdd` | 信号质量 / 线束故障检测 |
| `s2500 bench tx <n> [len]` | 纯发送压测 (不经 TCP) |
| `s2500 mac [xx:..]` | 读/写 MAC |
| `s2500 td/ts/time/loop/led/sleep/analog/irq` | 拓扑发现/时间戳/环回等 |

### 7.2 网络命令

| 命令 | 说明 |
|------|------|
| `netif` | 显示 MAC/IP/网关/链路/PLCA |
| `ping <ip> [n]` | 板端发起 ICMP |
| `iperf -s` / `iperf -c <ip> [-t n]` | lwiperf TCP 吞吐 (服务端/客户端) |
| `udpecho on\|off` | UDP 7 端口回显 |
| `tcpstats` | lwIP 统计 |
| `reboot` | 软复位 |

### 7.3 平台诊断命令 (`s32k`, 本板新增)

| 命令 | 说明 |
|------|------|
| `s32k` | SPI 后端/SCK、UART 错误计数、IRQ_N/RST_N 电平 |
| `s32k spi` | LPSPI0 全寄存器转储 (含 PARAM FIFO 深度) + SCK/func/超时计数 |
| `s32k probe` | 直接读 S2500 IDVER (扫描复用用) |
| `s32k hw on\|off` | 运行时切换硬件 LPSPI0 / 软件 GPIO 后端 |
| `s32k speed <hz>` | 运行时改 SCK 并实测 |
| `s32k selftest <n>` | 连续 n 次读 IDVER, 统计 bad/timeout (判定硬件 SPI 是否可靠) |
| `s32k bench <n>` | n 笔 68 字节事务的 SPI 层吞吐 (Mbit/s) |
| `s32k poll <us>` | 主循环接收轮询退避周期 (0 = 每轮都轮询) |
| `s32k stats` | 驱动 TX/RX 统计 (帧/chunk/溢出/重同步/校验错) |
| `s32k xcmp` | 同一笔事务软/硬件 SPI 逐字节对照 |
| `s32k dma on\|off` | 打开/关闭 LPSPI0 的 eDMA 搬运 (默认 off, 见 9.7) |
| `s32k dmareg` | eDMA 通道/TCD/DMAMUX 寄存器转储 |
| `s32k dmamem` | eDMA 引擎自检 (RAM->RAM 软件触发) |
| `s32k dmatest <tx> <rx>` | 单笔 DMA 事务分阶段诊断 |
| `s32k meclk` | MC_ME 分区时钟门状态 (eDMA/DMAMUX 是否使能) |
| `s32k clear` | 清 UART 计数 |

另有 `sdiag` (LPSPI0 深度诊断, 见第 9.4 节)。

---

## 8. 实测数据 (2026-11-20, 3 节点总线: 本板 + HPM5361 + TC387, 主机协调者)

> **注意: 本章是"软件 SPI 时代"的历史基线 (TCP 1.17/1.40 Mbit/s), 仅用于前后对比。**
> 当前默认是 LPSPI0 硬件 SPI, 最终数字见 [第 9 章](#9-lpspi0-硬件-spi-已打通默认启用)。

| 项目 | 结果 | 备注 |
|------|------|------|
| `s2500 selftest` | **7/7 PASS** | IDVER=0x11, PCS/PHY 环回 100B OK |
| PHY | link UP, speed10=1 | PHYID=0x180FF411 |
| SQI / SQI+ | 0/7, 7/255 | TOID=255 |
| HDD | valid=1, SHORT_OPEN=2 | 1W_SHORT=0 (SHORT: 总线上有多余终端) |
| ping 主机 (板->主机) | 5/5, **0% 丢包**, RTT 2-3ms | |
| ping 板 (主机->板) | 20/20, 0% 丢包, RTT 1.8ms | 协调者测试 |
| **TCP 上行 (板->主机)** | **1.40 Mbit/s** | iperf2, 8s |
| **TCP 下行 (主机->板)** | **1.17 Mbit/s** | iperf2, 8s |
| 旧版本 (交叉线序 + 软件 SPI) | 1.16 Mbit/s | 见 git 历史 |

**PLCA 协调者测试 (`host/coord_test.sh s32k`)**: 主机降为 follower7 -> 板子成为
协调者 ID=0 -> 双向 ping 20/20 0% 丢包 (RTT 1.80~1.90ms) -> TCP 上行 1.40 /
下行 1.17 Mbit/s -> 自动恢复主机协调者 (0% 丢包)。
日志: `<workdir>/coord_s32k_20261120-113629.log`

### 8.1 与旧版本对比

| | 旧 (交叉线序, 软件 SPI) | 新 (纠正线序, 软件 SPI) |
|---|---|---|
| SPI 后端 | GPIO 位操作 | GPIO 位操作 (~3MHz) |
| TCP | 1.16 Mbit/s | **1.40 Mbit/s (+21%)** |

提升原因: 旧固件按"MOSI=PTD16 / MISO=PTB4"配置 GPIO 方向, 与新线序相反,
读回字节全 0 靠重试; 纠正后 `spi_soft.c` 自己写 MSCR 方向, 发送一次成功。

---

## 9. LPSPI0 硬件 SPI (已打通, 默认启用)

**2026-11-20 结论: 硬件 SPI 可用, 默认后端已切到 LPSPI0。** 上一轮"每 4 字节重复首字"
的根因不是信号完整性、不是帧长、不是 RDF/RXCOUNT 判据、不是上下拉、也不是 CONT,
而是 **FIFO 节拍**:

- S32K312 的 LPSPI0 **FIFO 只有 2 个字深** (`PARAM=0x00080202`, TXFIFO=2 / RXFIFO=2);
- 旧代码把 `FCR.TXWATER` 设成 3, 而 RM 定义 `SR.TDF = (TXCOUNT <= TXWATER)`,
  于是 TDF 恒为 1, 写循环从不等 FIFO 空位, **第 3 个字以后的所有 TDR 写被硬件静默丢弃**
  → S2500 只收到半个包头, 回显字里 HDRB=1 变成 `0x40000000`,
  于是每个字都读回 `0x40000000`(看起来像"重复首字")。

### 9.1 最终配置

| 项目 | 值 |
|---|---|
| 帧格式 | **32 位帧** (`TCR.FRAMESZ=31`, BYSW=0, MSB first, SPI mode 0) |
| SCK | **15.137 MHz** (功能时钟 60.55MHz, `CCR1.SCKSET=SCKHLD=1`) |
| CS | PTB5 软件 GPIO, 覆盖整笔事务; `TCR.CONT=1` 连续传输, 收尾写 `CONT=0` 命令字结束 |
| FIFO 节拍 | 深度从 `PARAM` 现读; `FCR = TXWATER(depth-1) / RXWATER(0)`; 每次 TX 写入都以 `FSR.TXCOUNT < depth` 判空位 |
| TCR 写入 | 写 TCR 也压入 tx/command FIFO, 写完必须等 `TXCOUNT==0` (同 NXP `fsl_lpspi.c`) |
| 引脚 | PTB4=MSCR36 SSS=3 SOUT / PTD15=MSCR111 SSS=4 SCK / PTD16=MSCR112 IBE + IMCR230=3 SIN / PTB5=MSCR37 GPIO |
| 后端 | 默认硬件; `s32k hw off` 运行时退回软件 SPI, `s32k hw on` 切回 |

### 9.2 实测数字 (2026-11-20, 同一块板同一笔事务)

| 项目 | 软件 SPI | 硬件 LPSPI0 |
|---|---|---|
| IDVER 读回 | `00 00 3c 00  10 00 00 00  00 00 00 11` | **完全相同** |
| 1000 次 IDVER 自检 | — | **bad=0 timeout=0, 18ms → PASS** |
| SPI 层吞吐 (68B 事务) | ~2.9 Mbit/s | **13.27 Mbit/s** (2000×68B / 82ms) |
| TCP 下行 (主机→板) | 1.17 Mbit/s | **6.26 Mbit/s** |
| TCP 上行 (板→主机) | 1.40 Mbit/s | **5.55 ~ 7.45 Mbit/s** |
| ping RTT | 2–3 ms | **0.88 ~ 0.96 ms** |
| PLCA 协调者 (ID=0) 测试 | 通过 | **通过** (双向 ping 20/20, 下行 3.55 / 上行 7.47) |
| SPI 超时 | — | 0 (累计 63 万笔事务) |
| 驱动丢包 | — | rx drop/ovf/resync/fcs/parity 全 0 |

### 9.3 第二个瓶颈: 接收路径的软件 CRC32 (已修)

硬件 SPI 打通后下行仍只有 3.55 Mbit/s, 而 SPI 层能跑 13.27 Mbit/s; 主机侧
`ss -ti` 显示 `rwnd_limited:100%`、`rtt 6.5ms`(minrtt 0.88ms) —— 瓶颈在接收侧 ACK 时延。
逐帧排查发现 `s2500_recv()` 每帧用**逐位 CRC32** 复核 FCS (1460 字节 ≈ 60k 周期 ≈ 0.5ms),
这段串在"收完 → 交给 lwIP → 回 ACK"之间, 直接把 TCP 窗口卡死。
改成半字节查表 (结果完全一致, 已用 200 组随机数据比对) 后 **下行 3.55 → 6.26 Mbit/s**。

同时试过 `TCP_WND`: 2×MSS(默认) 与 4×MSS 无差别, 8×MSS 反而掉到 1.07 Mbit/s
(接收路径顶不住 8 帧突发), 因此保持 lwIP 默认窗口, 不改公共配置。

### 9.4 诊断工具 (新增 `sdiag`)

| 命令 | 说明 |
|---|---|
| `sdiag regs` | VERID/PARAM(FIFO 深度)/CR/SR/CFGR/CCR/CCR1/FCR/FSR/TCR/RSR + 焊盘 MSCR/GPDI |
| `sdiag hw lock\|burst <hex>` | 逐帧 trace: 每帧 TXCOUNT/RXCOUNT/SR/RDR 原值 |
| `sdiag hw wave <hex>` | **自带"逻辑分析仪"**: 用 GPDI + DWT 周期计数器在一次真实硬件传输中逐 SCK 上升沿采样 MOSI/MISO 焊盘, 事后还原实际位流 (可直接回答"SCK 打了多少拍 / MOSI 发出去的是什么") |
| `sdiag soft <hex>` | 同一笔事务走软件 SPI 做对照 |
| `sdiag idver <n>` | 批量读 IDVER 统计 |

### 9.5 顺带修掉的两个坑

- **LPUART RX FIFO 没开**: 硬件 SPI 下一帧要连做 24 笔 SPI (≈1ms), 这期间主循环进不来,
  而 RX 只有 1 字节缓冲 → 每 87us 不取就 OR 溢出丢字符。表现为 shell 命令被打断
  (`s2500 counters` → `s2500 couters`, 导致协调者测试里改 ID 的命令失败)。
  已开 TX/RX FIFO, 并在每笔 SPI 事务前、以及 printf 等 TDRE 时收串口; 现在 `or=0 drop=0`。
- **主循环空转轮询**: 空闲时每轮都发 12 字节 BUFSTS 控制事务会吃掉近一半 SPI 带宽,
  改成"有包就立刻继续轮询, 空轮询退避 150us"(`s32k poll <us>` 可调)。

### 9.6 仍未做到 / 下一步

- 下行 6.26 Mbit/s 与 RK3506 (Linux, 8.97 Mbit/s) 还有差距。剩余开销: 每帧两次内存拷贝
  (`rx->buf → frame → pbuf`)、每 chunk 一笔 68B 事务 (24 笔/帧)。
  下一步: ① `s2500_recv` 直接组包进 pbuf 省掉两次拷贝; ② 上 eDMA (TCD 32 位元素,
  DMAMUX LPSPI0 TX/RX) 把 chunk 搬运交给 DMA; ③ 试 `CFGR1.SAMPLE=1`(延迟 SCK 采样)
  把 SCK 提到 20MHz 以上 (需同时确认 S2500 25MHz 上限与信号完整性)。
- 硬件 SPI 是共享总线上的本板资源, 多人同时压测时下行数字会掉到 ~3.5 Mbit/s, 属正常。

---

### 9.7 eDMA 现状 (TX 方向已打通; RX 受 IP 请求语义限制; 默认关闭)

本轮把 eDMA 从头调通了一半, 并查清了几个**关键机理** (全部有实测):

**机理 1: 一次 DMAMUX 请求脉冲只驱动一个 eDMA minor loop**
用 DMAMUX 的 **"Always On" 源 (源号 62/63)** 单独验证: 即使请求线一直有效,
通道也只搬一个 minor loop 就停 (`CITER 4→3`)。所以 "NBYTES=4 + CITER=n" 的
**逐字 TCD 永远走不通** —— 这解释了之前所有"只搬一个字就卡住"的现象。

**机理 2: TX 要用 TDBR 突发别名区, 一个 minor loop 推整块**
RM 70.3.6.1: `TDBR0..127`(0x400) 是 128 字突发别名区, DMA 用地址递增一次写完整块,
LPSPI 自己从突发区喂 2 字深的 FIFO, 不会溢出。配置:
`SADDR=数据, SOFF=4, DADDR=&TDBR[0], DOFF=4, NBYTES=整块字节数, CITER=1, DLAST=-len`,
`CH_CSR.ERQ` 不置位(置了等于立刻做一次软件请求), 只写 `TCD_CSR=DREQ|START`。

**机理 3: TDBR 的字节序与 TDR 完全一致**
用不可解析的包头 0x12345678 探针 (`s32k dmabyte`): 不预交换时 S2500 回显
`12345678`(完全一致), 预交换后回显 `78563412`。**不需要任何字节交换**。

**实测结果 (TX 走 eDMA, RX 仍由 CPU 从 FIFO 读)**
- 12 字节事务: `rx = 00 00 3c 00 10 00 00 00 00 00 00 11`, 与 PIO 路径**逐字节一致**;
  `s32k dma on` 后 `s32k selftest 1000` → **bad=0 timeout=0 PASS**。

**RX 为什么还不行**
RDF 是**电平** (RXCOUNT>RXWATER), 一次累积只给一个脉冲; 而 `RDBR` 读空 FIFO
**不会等待**(实测读出 0), 所以"一次读整块"只能拿到已在 FIFO 里的那几个字
(FIFO 只有 2~4 字深), 拿不到还在移位中的字; 逐字链又因为 FIFO 累积而只有第一拍。
要让 RX 也走 DMA, 需要块长 ≤ FIFO 深度, 或者干脆用 RX 中断侧收。
(另: 68 字节事务下打开 DMA 会挂死板子, 尚未定位, 所以 **默认 `s_dma_on = 0`**。)

**给后来者的命令**
`s32k dmareg` (寄存器) / `s32k dmamem` (引擎自检 RAM→RAM, 必须用静态 SRAM 缓冲,
栈在 DTCM 里 eDMA 访问不到, 会报 `ES=0x80000002` 源总线错) /
`s32k dmabyte` (字节序探针) / `s32k dmabest` (TX 突发 + RX 逐字组合) /
`s32k dmatest <txsrc> <rxsrc>` (单笔分阶段)。

**结论**: 吞吐瓶颈不在搬运方式 (PIO 已达 SCK 线速 88%), 所以 eDMA 暂不作为默认;
代码与诊断命令保留, `s32k dma on` 可现场复现 TX-DMA 路径。

---

## 10. 排错表

| 现象 | 原因 | 处理 |
|------|------|------|
| 串口无输出 | LPUART CTRL.TE=0 (RTD `Hw_Init` 会清 0) | 已修: `s2500_port_uart_init()` 补 TE/RE |
| 串口能收不能发 / 只在压测后死 | RTD `AsyncReceive` 回调链在 OR 溢出后停摆 (R13) | 已修: 改主循环轮询 + 每轮清 OR/NF/FE/PF |
| `Command not Found` | 发送了 `\r\n` 而不是 `\r` | 用 `s25.py` 或只发 CR |
| `s2500 info` 全 0 | SPI 线序/方向错 (MISO 被配成输出) | 按第 2.1 节核对; `s32k spi` 看 MSCR/IMCR |
| 全 0 且 `s32k spi` 里 mscr112 是 0x00200000 | PTD16 被当输出 | 改为 IBE (`0x00080000`) |
| `s2500 selftest` HDD SHORT_OPEN=2 | 总线上多余 100Ω 终端 | 只在两端保留终端 |
| 板子 IP 不通但串口正常 | PLCA 主机/协调者未就绪 | `netif` 看 link/plca; 与主机 PLCA 配置对齐 |
| J-Link `Could not connect` | 探针被占用 / 目标复位中 | 重新插拔; `JLINK_SPEED=1000` |
| 编译 `undefined reference to Mcu_schm_read_msr` | RTD SchM 需要读 PRIMASK | 已由 `src/schm_stub.c` 提供 |
| 编译 `shell_ext.h: unknown type name size_t` | letter-shell 少 include | CMakeLists 已对该文件加 `-include stddef.h` |
| 后台上电后 shell 卡死 | 见 R13 | 用 `s32k` 看 `or=/drop=` 计数 |

---

## 11. 已知问题 / 后续工作

1. **LPSPI0 硬件 SPI 已打通** (第 9 节), 默认后端已是硬件 SPI:
   SPI 层 13.27 Mbit/s, TCP 下行 6.26 / 上行 7.45 Mbit/s (软件 SPI 时 1.17 / 1.40)。
   剩余优化项见 9.6 (去掉两次内存拷贝、试 SAMPLE=1 提 SCK、eDMA)。
2. 若现场需要回退, `s32k hw off` 即可退回软件 SPI (~3MHz), 功能与上一轮一致。
3. HDD 报 SHORT (总线上有多余终端), 属总线物理层问题, 非本板固件问题。
4. 硬件后端已重跑 selftest(IDVER x1000 bad=0)/ping/iperf/协调者测试, 数据见 9.2。
