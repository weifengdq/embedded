# TC387 + T30HM1TS2500 (S2500) 10BASE-T1S 交付说明

> **一句话**：Infineon AURIX TC387 TriBoard 通过 **QSPI2 @ 25 MHz + DMA** 驱动
> T30HM1TS2500（S2500）MAC-PHY，跑 lwIP 2.2（NO_SYS）+ letter-shell，
> 实测 **ping 100/100 0% 丢包（0.60 ms）**、**TCP 8.79~8.80 Mbit/s**、
> **纯发送 9.72 Mbit/s**（10BASE-T1S 线速约 9.8 Mbit/s）。
> PLCA **跟随者模式默认开启**（主机 USB-10T1S 是协调者）。
>
> 本文档面向第一次接触这块板的同学：装环境 → 编译 → 烧写 → 上电自检 →
> 性能数字 → 排错，全部按步骤写清楚。命令默认在
> `t30hm1ts2500/tc387` 下执行。

---

## 0. 三步跑通（环境已配好的机器）

```bash
cd t30hm1ts2500/tc387

./build.sh                 # 1) 编译 -> build/tc387_s2500{.elf,.hex}
./flash.sh                 # 2) 烧写 + 复位运行 + 串口自检（约 15 s）
ping -c 10 192.168.10.14   # 3) 主机侧验证
```

看到 `串口 /dev/ttyUSB0: 固件已运行 (tcqspi 有响应)` 且 ping 通，就齐了。

---

## 1. 硬件

### 1.1 组成

| 项目 | 内容 |
| --- | --- |
| MCU | Infineon AURIX TC387（TriBoard TC3X7 评估板，TriCore 1.6.2 三核） |
| MAC-PHY | T30HM1TS2500（S2500），OPEN Alliance 10BASE-T1x SPI 接口 |
| 调试器 | DAP MiniWiggler（USB `058b:0043`，DAS 识别为 `TriBoard TC2XX V2.0`） |
| 软件栈 | `common/s2500` 驱动 + lwIP 2.2.0（NO_SYS）+ letter-shell 3.2.4 |
| 多核 | 只启动 CPU0，CPU1~3 保持复位/空闲 |
| 网络 | IP 192.168.10.14/24，gw 192.168.10.10，MAC 44:9F:79:29:AC:86 |
| PLCA | 默认**开启跟随者**（主机为协调者 ID0）：ID=4 / NCNT=8 / TOTMR=32；`s2500 plca en 0` 退回 CSMA |

### 1.2 QSPI2 ↔ S2500 引脚表

| TC387 引脚 | S2500 信号 | 方向 | 说明 |
| --- | --- | --- | --- |
| P15.2 | CS_N | OUT | QSPI2 **SLSO0**，**软件驱动**：每 68 B 事务拉低，事务结束拉高 |
| P15.4 | RST_N | OUT | GPIO 低有效；初始化拉低 10 ms → 拉高 → 等 50 ms |
| P15.5 | IRQ_N | IN | GPIO 上拉输入；本移植**未使用中断线**（驱动轮询） |
| P15.6 | MOSI | OUT | QSPI2_MTSR，push-pull |
| P15.7 | MISO | IN | QSPI2_MRSTB，pull-down |
| P15.8 | SCLK | OUT | QSPI2_SCLK，push-pull |

SPI 参数（见 `src/board.c`）：

- 模式 0：`shiftClock = shiftTransmitDataOnTrailingEdge`，`clockPolarity = idleLow`
- MSB first，8 bit/word
- 时钟 **25.000 MHz**（`f_SPB=100 MHz` 分频 4；S2500 上限 25 MHz @ VIO=3.3 V）
- CS 软件控制（`autoCS=0`），一整块 68 B 传输期间保持低
- 每块格式：**4 B 头 + 64 B 数据**；接收时 footer 落在 `rx[64..67]`

### 1.3 调试串口（ASCLIN0）

| TC387 引脚 | 信号 | 说明 |
| --- | --- | --- |
| P14.0 | ASCLIN0_TX | 接 DAP MiniWiggler 虚拟串口 |
| P14.1 | ASCLIN0_RX | 同上 |

- 参数：**115200 8N1，无流控**
- 设备节点：`/dev/ttyUSB0`（VID:PID = `058b:0043`）。USB 重新枚举后编号可能
  变成 `ttyUSB1`，用下面命令确认：

```bash
ls -l /sys/bus/usb-serial/devices/            # ttyUSBx 属于哪个 USB 设备
ls /dev/serial/by-id/ | grep -i "IFX\|DAS"    # 按名字找最稳
```

- letter-shell **只认 CR（`\r`）**：发 `\r\n` 会得到 `Command not Found`。
  用 `tools/s25.py` 就不用管（它只发 `\r`）。

### 1.4 总线

单段 10BASE-T1S（P2MP），所有节点 + 主机 USB-10T1S 适配器挂同一根线：

- 主机 `enx001ec0d1c337` = `192.168.10.10/24`，PLCA **协调者**（ID0 / NCNT8 / TOTMR32）
- 本板 `192.168.10.14/24`，PLCA 默认**开启作跟随者**（id=4，需要主机 BEACON）

> **纪律**：这条总线是多块板共享的。任何吞吐/延迟测量前先抢锁：
> `flock /tmp/s2500_perf.lock -c '<测量命令>'`，否则互相干扰、数字全部作废。

---

## 2. 从零搭环境

### 2.1 主机依赖

```bash
sudo apt update
sudo apt install -y cmake ninja-build build-essential git python3-pip
pip3 install pyserial          # tools/s25.py 需要
```

### 2.2 TriCore GCC 13.4.1（命令行工具链）

工程用**开源 tricore-elf-gcc**（不是 HighTec），默认路径 `/opt/tricore-gcc-13.4.1`：

```bash
/opt/tricore-gcc-13.4.1/bin/tricore-elf-gcc --version
# tricore-elf-gcc (tricore-gcc-toolchain) 13.4.1 20260422
```

装到别处也可以，改 `cmake/tricore-gcc-toolchain.cmake` 的 `AURIX_TOOLCHAIN_BIN`，
或配置时传：

```bash
cmake -S . -B build -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=cmake/tricore-gcc-toolchain.cmake \
      -DAURIX_TOOLCHAIN_BIN=/your/path/bin -DCMAKE_BUILD_TYPE=Release
```

> 配置时若提示 `Manually-specified variables were not used by the project:
> CMAKE_TOOLCHAIN_FILE`，**可以忽略**——工具链文件已被 CMake 采用
> （`build/CMakeCache.txt` 里能看到 `AURIX_TOOLCHAIN_BIN` 和 tricore-elf-gcc 路径）。

### 2.3 DAS / TAS 8.3.0（烧写必需的驱动服务）

烧写走 Infineon **TAS（Tool Access Socket，DAS 的后继）**：

```bash
# 1) 安装（deb 在参考目录里，也可从 infineon.com/DAS 下载）
sudo dpkg -i <home>/tc387-tools/DAS_8.3.0_linux_x64.deb
#   依赖缺失时: sudo apt-get -f install

# 2) 确认安装位置
ls /opt/Tools/DAS/8.3.0/bin/          # 应看到 tas_server

# 3) 启动 TAS server（后台常驻，只需一次）
pgrep -x tas_server || \
  nohup /opt/Tools/DAS/8.3.0/bin/tas_server >/tmp/tas_server.log 2>&1 &
```

`flash.sh` 会自动检查并拉起 `tas_server`，一般不用手动起。

### 2.4 aurix_flasher（命令行烧写器）

Infineon 官方只有 Windows GUI，Linux 下用开源命令行版：

```bash
cd <home>/tc387-tools/aurix_flasher_linux-master/linux
make -f Makefile_linux                 # 产出 ./aurix_flasher
sudo cp aurix_flasher /usr/local/bin/  # 可选，放 PATH 里
```

本工程默认用
`<home>/tc387-tools/aurix_flasher`，可用环境变量覆盖：

```bash
AURIX_FLASHER=/path/to/aurix_flasher ./flash.sh
```

### 2.5 AURIX Development Studio（可选，仅调试/GUI 用）

**构建以 `build.sh`（CMake + tricore-elf-gcc）为准**，ADS 不是构建入口。ADS 用来：

1. GUI 烧写 `build/tc387_s2500.hex`（等价于 `flash.sh`）；
2. 挂 DAP 单步调试：`Run > Debug Configurations > TriCore`，加载
   `build/tc387_s2500.elf`，勾选 *Download* 后 Start。

注意：ADS 自带 HighTec 编译器，**不要**用它重编本工程——链接脚本
`Lcf_Gnuc_Tricore_Tc.lsl` 是 GNU ld 语法，只配 tricore-elf-gcc。

### 2.6 串口助手

```bash
cd t30hm1ts2500
./tools/s25.py --auto tc387 -c "help"        # 按 VID:PID 自动找 DAP 串口
./tools/s25.py -p /dev/ttyUSB0 -c "s2500 info" -c "s2500 status"
```

`-c` 可重复；`--idle` 控制"多久没输出算结束"，读长日志调大（`--idle 12 -t 30`）。

### 2.7 udev 权限

DAP 串口属于 `plugdev` 组：

```bash
sudo usermod -aG plugdev $USER       # 重新登录生效
ls -l /dev/ttyUSB0                    # 应显示 crw-rw----+ root plugdev
```

---

## 3. 编译

```bash
cd t30hm1ts2500/tc387
./build.sh
```

产物：

| 文件 | 用途 |
| --- | --- |
| `build/tc387_s2500.elf` | 调试（带 `-g3` 符号） |
| `build/tc387_s2500.hex` | 烧写（`flash.sh` 用它） |
| `build/tc387_s2500.map` | 链接映射，查段大小/符号地址 |

编译时这两类 warning 属**正常**，不影响功能：

- `machine/endian.h: BYTE_ORDER redefined`（newlib 与 lwIP 宏冲突）
- `Manually-specified variables were not used: CMAKE_TOOLCHAIN_FILE`（见 2.2）

关键编译选项（`CMakeLists.txt`）：

```
-O2 -g3 -gdwarf-3 -Wall -fno-common -fstrict-volatile-bitfields
-ffunction-sections            # 注意: 没有 -fdata-sections! 见 9.2
-mcpu=tc38xx
-Wl,--gc-sections -T Lcf_Gnuc_Tricore_Tc.lsl -nocrt0
```

---

## 4. 烧写

```bash
./flash.sh
```

`flash.sh` 做四件事：

1. 检查 `build/tc387_s2500.hex`；
2. 没起 `tas_server` 就拉起来；
3. `aurix_flasher -hex ... -erase on -start on -ver on` 擦/写/校验；
4. **再发一次 TAS reset&run**，然后读串口自检。

### 4.1 为什么第 4 步必须有（重要坑）

`aurix_flasher` 用 **reset & halt（停核）** 方式连目标烧写。烧完写的"应用复位"
（OCTRL）在调试会话仍挂住内核时**不会让 CPU 真正跑起来**。现象是：

- 串口**一个字符都没有**（连 boot banner 都没有）
- ping 不通
- 但烧写过程全部 `Pass`（擦、写、校验都对）

`flash.sh` 里的 `"$FLASHER" -read 0xF003AF00` 就是借 `-read` 的
`device_connect(TAS_CLNT_DCO_RESET)` 路径做一次"复位并运行"。手动补救：

```bash
<home>/tc387-tools/aurix_flasher -read 0xF003AF00
```

### 4.2 烧写失败

| 现象 | 处理 |
| --- | --- |
| `Unknown Flash Size 00` / `Chip_id 0000000` | DAP/板子状态不对：拔插 DAP USB、板子断电再上电，重跑 `./flash.sh` |
| `Failed to reset the device` | `tas_server` 挂了：`pkill tas_server` 后重跑；或 USB 口被别的进程占用 |
| 烧完串口静默 | 见 4.1（`flash.sh` 已内置 reset&run） |

---

## 5. 上电自检

上电/复位后串口 115200 打印：

```
[TC387] board init: ASCLIN0 115200, QSPI2 25MHz+DMA
[TC387] QSPI2 + RST_N/IRQ_N ready

==============================================
 T30HM1TS2500 10BASE-T1S  TC387 TriBoard (DAP UART)
 MAC  : 44:9F:79:29:AC:86
 IPv4 : 192.168.10.14/24  gw 192.168.10.10
 PLCA : id=4 ncnt=8 en=1
 shell: 输入 help 查看命令, s2500 info 查看芯片
==============================================
root:/$
```

`s2500 selftest`（**总线需静默**）：

```
===== S2500 上电自检 =====
[1] SPI/寄存器   : PASS (IDVER=0x00000011)
[2] PHY ID       : 0x180FF411  MAC=44:9F:79:29:AC:86
[3] PCS 环回收发 : PASS (rxlen=100 head=449F7929)
[4] PHY 环回收发 : PASS (rxlen=100 head=449F7929)
[5] PHY Status   : 0x082D link=1
[6] SQI/SQI+     : 5/7, 167/255
[7] HDD          : valid=1 SHORT_OPEN=2 (SHORT / 多余终端) 1W=1
===== 自检结束 =====
```

QSPI 时钟/DMA 自检：

```
root:/$ tcqspi
f_SPB=100000000 Hz  req=25000000 Hz  real=25000000 Hz  ch.baudrate=25000000
QSPI2 ECON0=0x00000240 BACON=0x03A00001 GLOBALCON=0x21003C01
DMA txCH=12 rxCH=13  irq tx=10644829 rx=10644829 er=0
```

判定标准：`real=25000000`、`er=0`、tx/rx 中断计数基本相等（一块数据一进一出）。

---

## 6. shell 命令手册

提示符 `root:/$`，命令**只发 CR**。

| 命令 | 作用 | 说明 |
| --- | --- | --- |
| `help` / `cmds` | 命令列表 | letter-shell 内建 |
| `s2500 info` | 芯片 ID/CAP/MAC/PHY/PLCA | 最常用 |
| `s2500 status` | link/PLCA/SQI/驱动统计 | 丢帧看 `drv stats` |
| `s2500 wd [on|off]` | 自愈看门狗统计/开关 | 默认 on，芯片自发复位后自动重初始化 |
| `s2500 selftest` | 寄存器 + 环回 + SQI + HDD 全检 | 总线静默时跑 |
| `s2500 counters [clear]` | MAC 计数（TX/RX 帧/字节/错误） | 读清 |
| `s2500 reg <mms> <addr> [值]` | 读/写 S2500 寄存器 | 调试神器 |
| `s2500 plca [id N] [ncnt N] [totmr N] [en 0/1]` | PLCA 配置 | 默认已开 (follower id4)；`en 0` 退回 CSMA |
| `s2500 sqi` | 信号质量 SQI/SQI+ | 0~7 / 0~255，越大越好 |
| `s2500 hdd` | 线束故障检测 | 需链路空闲 |
| `s2500 td <ref\|meas\|dly> [dur_ms]` | TC14 拓扑发现/距离 | 需 P2P 拓扑 |
| `s2500 ts <a\|b\|c>` | 读 TC6 硬件时间戳 | 64 bit |
| `s2500 time [set sec nsec]` | PTP 时间 | |
| `s2500 loop <0/1/2>` | 环回：0=off 1=PCS 2=PHY | 自检用 |
| `s2500 led [fn0 fn1\|off]` | DIO0/DIO1 LED 功能 | |
| `s2500 sleep [wake]` | TC10 睡眠/唤醒音 | |
| `s2500 analog` / `s2500 mac` / `s2500 irq` | 模拟/MAC/IRQ 诊断 | |
| `s2500 bench tx <count> [len]` | 纯发送压测 | 广播帧，**先上锁** |
| `s2500 rxdump` / `s2500 lbdump` | 最近收帧/环回解码 | |
| `ping <ip> [n]` | 板子主动 ping | |
| `iperf -c <ip> [-t s]` / `iperf -s` | TCP 吞吐（lwiperf） | **只兼容 iperf2** |
| `udpecho on\|off` | UDP 7 端口回显 | |
| `netif` | IP/MAC/link | |
| **`tcstat`** | P15 引脚 + QSPI2 关键寄存器 | 本板专用 |
| **`tcqspi`** | f_SPB/分频/实际波特率/DMA/中断计数 | 本板专用 |
| **`tcspeed [n]`** | n 次 68 B 原始 SPI 交换计时 | 定位总线瓶颈 |
| **`tcspi`** | 读 IDVER 的原始事务 | 查 MISO 通不通 |

---

## 7. QSPI2 实现与优化过程

### 7.1 每块 68 B 的数据事务

S2500 的 OA-SPI 数据事务固定 **4 B 头 + 64 B 数据**：

```
TX: [31:0] 头(DNC/DV/SV/EV/EBO/TSC/奇校验) | 64 B payload
RX: 64 B payload                            | [67:64] 4 B footer(状态/校验)
```

一个 1514 B 以太网帧 = **24 块**，每块一次独立 CS 事务（CS 必须在块边界拉高）。

### 7.2 优化前（10 MHz + FIFO 中断）

| 项 | 值 |
| --- | --- |
| SCLK | 10.000 MHz（`QSPI_BAUDRATE=10000000`） |
| 数据搬运 | iLLD `IfxQspi_SpiMaster` **FIFO 中断**，每块若干次 TX/RX 中断 |
| 每块开销 | ≈54.4 µs 纯数据 + FIFO 中断 + 每块一次 12 B BUFSTS 控制读 |
| bench 1472 B | **7.28 Mbit/s**（618 fps） |
| iperf TCP | **6.45 Mbit/s** |

### 7.3 优化后（25 MHz + DMA）

改动都在 `src/board.c`：

```c
#define QSPI_BAUDRATE   25000000u
#define QSPI_TX_DMA_CH  IfxDma_ChannelId_12
#define QSPI_RX_DMA_CH  IfxDma_ChannelId_13

cfg.maximumBaudrate     = QSPI_BAUDRATE;
cfg.dma.useDma          = TRUE;          /* iLLD 自带 DMA 路径 */
cfg.dma.txDmaChannelId  = QSPI_TX_DMA_CH;
cfg.dma.rxDmaChannelId  = QSPI_RX_DMA_CH;
```

- **时钟**：`f_SPB=100 MHz` 分频 4，实测 **25.000 MHz**（`tcqspi` 可查）；
  这是 S2500 在 VIO=3.3 V 下的上限（`tclkp ≥ 40 ns`）。
- **DMA**：QSPI 的 TX/RX 服务请求直接路由到 DMA 通道
  （`SRC.TOS=DMA`, `SRPN=通道号`），**CPU 不再逐字节搬 FIFO**；
  每个 68 B 块只剩 **1 次 TX 完成中断 + 1 次 RX 完成中断**。
- **ISR**：`qspi2_tx_isr` → `IfxQspi_SpiMaster_isrDmaTransmit()`，
  `qspi2_rx_isr` → `IfxQspi_SpiMaster_isrDmaReceive()`，
  `qspi2_er_isr` → `IfxQspi_SpiMaster_isrError()`（优先级 110/111/112）。
- **CS 时序不变**：仍是软件控制 P15.2，一块 68 B 期间保持低；
  CS 在 DMA 的 RX 完成中断里释放（保证最后一个字节移完）。
- **忙等**：`hal_spi_xfer` 仍同步等 `IfxQspi_SpiMaster_getStatus()` 完成
  （SPI 事务本身是同步的），但等待期间不再有 FIFO 中断风暴。

**实测总线开销**：`tcspeed 2000` → **27.0 µs/块**（纯数据 21.76 µs，
HAL/DMA 开销 5.2 µs）→ 总线能力 **20.1 Mbit/s**，是 10BASE-T1S 线速的 2 倍，
所以纯发送能吃满线速。

### 7.4 为什么没有继续优化

- 纯发送 bench 1472 B 已 **9.85 Mbit/s，贴着线速**（10 Mbit/s 扣前导码/IFG），
  再快没有意义；
- TCP 瓶颈在**半双工总线 + 双向流量 + lwIP 处理**，不在 SPI；
- 试过把 `TCP_SND_BUF` 提到 8×MSS（`MEM_SIZE=32 K`），TCP 反而掉到
  0.3~0.8 Mbit/s（与 S2500 4 KB TX 缓冲配合变差），**已回退**，
  保持 `lwipopts.h` 默认值。

### 7.5 再加一档：PLCA 跟随者（TCP 7.95 → 8.80）

- 优化前 PLCA 默认关闭（CSMA/CD），而主机 USB-10T1S 是 PLCA **协调者**，
  两者混跑会互相碰撞：`s2500 status` 的 `pcs.collisions` 累计到 **9278**。
- 让板子加入 PLCA 当**跟随者**（id=4）后，每个节点在自己的时隙发送，不再碰撞：
  **TCP 7.95 → 8.79~8.80 Mbit/s（3 次 8.79/8.80/8.80）**，碰撞计数降到 **5**。
- 代价：纯广播小帧略降（60 B：7.27 → 5.93 Mbit/s，每帧要等时隙），
  1472 B 大帧基本不变（9.83 → 9.72 Mbit/s）。
- 默认值在 `src/main.c` 的 `s2500_app_cfg_t.plca_en`；
  退回 CSMA：shell 里 `s2500 plca en 0`（立即生效，掉电恢复默认）。

---

## 8. 实测数据（2026-11-20；主机 192.168.10.10 / 板 192.168.10.14）

| 项目 | ① 优化前 10 MHz / FIFO 中断 | ② 25 MHz + DMA（CSMA） | ③ 最终：25 MHz + DMA + PLCA 跟随者 |
| --- | --- | --- | --- |
| `tcqspi` 实际 SCLK | 10.000 MHz | **25.000 MHz** | **25.000 MHz** |
| PLCA | off | off | **en=1 id=4 PST=1** |
| ping 20 次 | 0% 丢包，0.694/0.782/0.887 ms | 0.589/0.639/0.720 ms | **0.579/0.611/0.631 ms** |
| **ping 100 次** | — | **0% 丢包，0.548/0.601/0.685 ms** | — |
| `bench tx 500 1472` | 7.28 Mbit/s（618 fps） | **9.83~9.85 Mbit/s（834~836 fps）** | **9.72 Mbit/s（825 fps）** |
| `bench tx 500 1000` | — | 9.78~9.80 Mbit/s（1222~1225 fps） | 9.62 Mbit/s（1201 fps） |
| `bench tx 1000 60` | — | 7.16~7.38 Mbit/s（14900~15400 fps） | 5.93 Mbit/s（12345 fps，小帧受 PLCA 时隙开销限制） |
| **iperf TCP 10 s** | **6.45 Mbit/s**（7.69 MB） | **7.95~7.98 Mbit/s**（9.5 MB，3 次） | **8.79~8.80 Mbit/s**（10.5 MB，3 次） |
| PCS 碰撞计数 | — | 9278（CSMA 与协调者 BEACON 互撞） | **5** |
| `tcspeed 2000`（68 B） | — | 27.0 µs/块 = 20.1 Mbit/s | 27.0 µs/块 = 20.1 Mbit/s |
| `s2500 selftest` | 全 PASS | **全 PASS**（PCS/PHY 环回 rxlen=100） | **全 PASS** |
| SQI / SQI+ | 1/7、63/255 | **4~5/7、151~167/255** | 4/7、151/255 |
| HDD | SHORT_OPEN=2 | SHORT_OPEN=2、1W_SHORT=1 | 同左（见 9.4） |
| TC14 TD | — | dist_mr=0、dly_mr=0、dly 报错 | 同左（见 9.5） |
| 驱动统计 | — | 10 s TCP 期间 `drop=0 fcs=0` | `drop=0 fcs=0` |

复现命令（**先上锁**）：

```bash
flock /tmp/s2500_perf.lock bash -c '
  cd t30hm1ts2500
  # 纯发送
  tools/s25.py -p /dev/ttyUSB0 -c "s2500 bench tx 500 1472" -t 60 --idle 6
  # TCP（板子做客户端，主机做服务端；iperf2）
  iperf -s -f m & sleep 1
  tools/s25.py -p /dev/ttyUSB0 -c "iperf -c 192.168.10.10 -t 10" -t 40 --idle 14
  kill %1
'
```

> 关 PLCA（纯 CSMA）时 3 次采样 7.95~7.98，偶尔会掉到 ~7.1（总线上有别的节点发流量时会碰撞）；
> 开 PLCA 跟随者后 3 次采样 8.79/8.80/8.80，抖动基本消失（碰撞计数从 9278 降到 5）。
> 如果看到 6~7 Mbit/s 的异常值，先确认上一轮 `iperf -s` 退干净了（日志里
> `Address already in use` 说明板子连到了旧 server）：`pkill -f "iperf -s"` 后再测。

---

## 9. 排错表（按现象查）

### 9.1 烧完串口全静默 / ping 不通

- **原因**：`aurix_flasher` 以 reset&halt 连接，烧完 CPU 停在 halt。
- **判据**：烧写全 `Pass` 但串口连 banner 都没有；调试器读 `P15_IOCR4` 是复位
  默认值 `0x10101010`（不是 `0x08981080`），说明固件没跑。
- **处理**：`flash.sh` 已内置 reset&run；手动执行
  `aurix_flasher -read 0xF003AF00`。

### 9.2 热复位后 SPI 偶发失效（`-fdata-sections` 坑）

- **现象**：冷启动正常；复位后偶尔 `s2500 reg` 读回 0、`tcspi rc=-1`、
  SPI 时好时坏，而串口一直正常。
- **根因**：`-fdata-sections` 把每个变量放进独立的 `.data.<var>` 段，
  iLLD 链接脚本的 `__copy_table`/`__clear_table` 只覆盖 `.data`/`.bss`，
  这些"孤儿段"复位后**不会被初始化**，`g_qspi` 等驱动结构体里是随机值。
- **处理（本工程已做）**：
  1. `CMakeLists.txt` **只保留 `-ffunction-sections`，绝不加 `-fdata-sections`**；
  2. 驱动结构体在初始化函数里显式 `memset`（`board.c` 里 `g_qspi`、`g_qspi_ch`、
     `g_asc` 都清零），不依赖 `.data` 初值。
- **自检**：`tcstat` 看 `P15_OUT` bit4=1；`tcqspi` 的 `real` 非 0。

### 9.3 `s2500 status` 显示 `link : DOWN` 但能 ping

- 10BASE-T1S 的 `PHY STATUS.LKST` 依赖**线路上有活动**，安静总线会读到 0。
- **判据**：以 `PLCA.PST`（能看到 BEACON 为 1）和**实际 ping** 为准。
- `selftest` 里刚做完环回常看到 `link=1`，正是这个原因。

### 9.4 `s2500 hdd` 报 `SHORT_OPEN=2`、`1W_SHORT=1`

- `SHORT_OPEN=2` = 检出"短路/多余终端"。自制板每块都焊 49.9 Ω×2 终端，
  多块并联后总线阻抗过低，HDD 判成 SHORT。
- **不是芯片坏**（本板 ping/iperf 正常）：建议总线上只保留两端终端，
  或只插需要测的板；`1W_SHORT=1` 建议用万用表现场复核。

### 9.5 TC14 `s2500 td` 全 0 / `dly` 报错

- `TD: dist_mr=0 dly_mr=0 ... err(dm=0 dly=1 auto=0)`
- TC14 的距离/时域测量按 **P2P（点对点）** 设计；本环境是 P2MP 共享总线 +
  PLCA 协调者持续发 BEACON，测不到回波是**预期行为**，不代表 PHY 故障。
- 要验证 TD：拔掉其它节点，主机与板子**单独点对点**连接后再测。

### 9.6 `s2500 selftest` 环回 FAIL、`rxlen=1514`

- 环回期间总线上有真实帧（别人的 ping / 广播）被收进来。
- 处理：等总线静默再测；不是本板故障。

### 9.7 ping 不通

按顺序查：

1. `netif` 看 IP/MAC 是否 192.168.10.14 / 44:9F:79:29:AC:86；
2. 主机 `enx001ec0d1c337` 是否 UP、是否有 192.168.10.10（**不要改主机 IP/PLCA**，
   由 lead 统一管理）；
3. `s2500 status` 看 TX/RX 计数是否变化、`drop/fcs` 是否增长；
4. `s2500 counters` 看 TX 是否真发出去了；
5. 总线终端/线序（10BASE-T1S 单对差分，P/N 反接不通）。

### 9.8 iperf 数字异常 / 连不上

- 必须用 **iperf2**（`iperf`），**iperf3 不兼容** lwiperf；
- 板子做客户端：`iperf -c <主机IP>`；做服务端：`iperf -s`（监听 5001）；
- 测量前 `flock /tmp/s2500_perf.lock`，否则别的板在跑流量，数字没意义。

### 9.9 串口问题

| 现象 | 原因/处理 |
| --- | --- |
| 命令回 `Command not Found` | 发了 `\r\n`；letter-shell 只认 `\r` |
| 换 USB 口后没反应 | 设备号变了：`ls -l /sys/bus/usb-serial/devices/`，或用 `--auto tc387` |
| 乱码 | 波特率不对，必须 115200 8N1 |
| `Permission denied` | 用户不在 `plugdev` 组（见 2.7） |

### 9.10 编译告警

- `BYTE_ORDER redefined`：newlib 与 lwIP 宏冲突，无害；
- `CMAKE_TOOLCHAIN_FILE was not used`：CMake 提示措辞，工具链实际生效；
- `Wunused-parameter` 等：iLLD 代码风格，不影响。

### 9.11 PLCA 开了却完全发不出去（`PST=0`）

- 跟随者必须有协调者的 BEACON 才能发。若主机侧没在当协调者，或总线上只有本板，
  `PLCA.PST=0`，ping/iperf 全部失败，但本地 `s2500 info`/`reg` 都正常。
- 判据：`s2500 status` 里 `plca ... PST=0`（正常应为 1）。
- 处理：恢复主机协调者配置（**由 lead 统一管理**），或本板临时退回 CSMA：
  `s2500 plca en 0`。


---

## 10. 已知问题 / 风险

1. **TCP 8.79~8.80 Mbit/s（PLCA 跟随者）/ 7.95（CSMA）**：离 10 Mbit/s 线速还有约 12%
   余量；再提升需改 `common/`（批量收发）或独占总线。
2. **PLCA 默认开启意味着依赖主机协调者**：主机若恢复成纯 CSMA，本板会发不出去
   （`PST=0`），需 `s2500 plca en 0`（见 9.11）。
3. **HDD 报 SHORT_OPEN=2**：板上终端电阻偏多，建议硬件侧确认（见 9.4）。
4. **TC14 TD 不可用**：P2MP 拓扑下的预期行为（见 9.5）。
5. **IRQ_N 未使用**：驱动轮询；接中断可进一步降 CPU 占用（当前已够用）。
6. **烧写依赖第三方 flasher**（`<home>/...`），不在本仓库；换机按 2.4 重编。
7. **总线是共享资源**：压测必须 `flock /tmp/s2500_perf.lock`；
   禁止改主机 `enx001ec0d1c337` 的 IP/PLCA。

---

## 11. 目录结构与关键文件

```
tc387/
├── CMakeLists.txt              # 构建: iLLD + common/(S2500+lwIP+shell)
├── build.sh                    # 一键编译
├── flash.sh                    # 一键烧写 + reset&run + 串口自检
├── cmake/
│   ├── AurixProject.cmake      # 源文件/头文件递归收集
│   └── tricore-gcc-toolchain.cmake   # 编译器路径 (AURIX_TOOLCHAIN_BIN)
├── Configurations/             # iLLD 启动配置 (Ifx_Cfg*.h/c)
├── Lcf_Gnuc_Tricore_Tc.lsl     # GNU ld 链接脚本（含 letter-shell 命令段）
├── Libraries/                  # iLLD TC3xx + Infra（Infineon, Boost License）
├── src/
│   ├── board.h / board.c       # ASCLIN0 / QSPI2(25MHz+DMA) / STM0 / GPIO + HAL
│   └── main.c                  # core0_main: board_init -> s2500_app_init -> poll
├── Cpu1_Main.c ... Cpu3_Main.c # 未启动（保留符号）
└── README.md                   # 本文档
```

| 关键文件 | 说明 |
| --- | --- |
| `src/board.c` | QSPI2 25 MHz + DMA、CS 时序、UART、STM、`s2500_hal_t` |
| `src/main.c` | IP/MAC/PLCA 配置（`s2500_app_cfg_t`） |
| `../common/s2500/` | 共享驱动（寄存器/PLCA/HDD/SQI/TC14），**平台无关**，改动需全平台验证 |
| `../common/lwip-port/lwipopts.h` | lwIP 配置（TCP_MSS=1460、TCP_SND_BUF=4×MSS） |
| `../tools/s25.py` | 串口助手（`--auto tc387`） |
