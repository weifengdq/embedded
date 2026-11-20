# HPM5361 + T30HM1TS2500 (S2500) 10BASE-T1S 固件

> 面向新接手同事的交付级文档。从零装环境 → 编译 → 烧写 → 跑通 → 排错, 一条龙。
> 所有数字都是本板 (HPM5361 + T30HM1TS2500, 节点 192.168.10.12) 实测。

## 0. 一页速览

| 项目 | 值 |
|------|-----|
| MCU | HPM5361 (RISC-V RV32, 480 MHz, 1 MB QSPI NOR, 128 KB ILM + 128 KB DLM) |
| PHY | T30HM1TS2500 (S2500) 10BASE-T1S MAC-PHY, OPEN Alliance TC6/TC10/TC14 |
| 接口 | SPI1 @ **25.000 MHz** + HDMA (ch0/ch1) + GPIO 片选/复位/中断 |
| 网络 | lwIP 2.2.0 (NO_SYS), IPv4 192.168.10.12/24, gw 192.168.10.10 |
| PLCA | 默认 follower `id=2 ncnt=8 totmr=32`; 主机 `enx001ec0d1c337` 是协调者 id=0 |
| TCP 吞吐 | **8.80 Mbit/s** (主机→板, iperf2, 8 s) / 7.0~8.0 Mbit/s (板→主机) |
| 纯发送 | **10.0 Mbit/s** (1500 B 帧, 833 fps; 线速上限 10 Mbit/s) |
| ping RTT | 平均 0.70 ms (min 0.62 / max 1.33) |
| 调试口 | 板载 FT2232H channel B = `/dev/ttyUSB1`, 115200 8N1 |

改前的历史基线 (20 MHz SPI, 无 DMA, 老 lwIP 参数): TCP 7.53 Mbit/s, bench 9.52-9.55 Mbit/s, ping 0.86 ms。

## 1. 硬件与接线

### 1.1 引脚表 (HPM5361 侧, 与 S2500 相连)

| S2500 信号 | HPM5361 引脚 | 方向 | 说明 |
|------------|--------------|------|------|
| CS_N | PA26 | OUT | GPIO 软件片选, 覆盖整个 OA-SPI 事务 |
| SCLK | PA27 | OUT | SPI1 SCLK, 模式 0 (CPOL=0/CPHA=0), 25 MHz |
| MISO | PA28 | IN | S2500 → MCU |
| MOSI | PA29 | OUT | MCU → S2500 |
| IRQ_N | PA12 | IN | 低有效; 驱动当前轮询, 未开中断 |
| RST_N | PA10 | OUT | 低有效, 上电拉低 5 ms 再释放 |
| UART0 TXD | PA00 | OUT | 调试串口 → FT2232H channel B RX |
| UART0 RXD | PA01 | IN | 调试串口 ← FT2232H channel B TX |
| UART2 TXD | PB08 | OUT | 镜像输出 (与 UART0 同内容) |
| UART2 RXD | PB09 | IN | 备用输入 (shell 会同时读两路) |

### 1.2 板载 FT2232H 双通道 (很关键, 新手最容易搞错)

`lsusb` 看到 `0403:6010 FT2232C/D/H Dual UART/FIFO IC`, 它有两个独立接口:

| 接口 | 用途 | 系统节点 |
|------|------|----------|
| if00 (channel A) | JTAG, 给 OpenOCD 烧写/调试 | 不产生 tty |
| if01 (channel B) | UART, 接 HPM5361 的 UART0 (PA00/PA01) | `/dev/ttyUSB1` |

注意: **`/dev/ttyUSB0` 不是本板的串口**(系统里还有别的 CH343/STLINK 设备时会错位),
`tools/s25.py --auto hpm` 是按 VID:PID=0403:6010 的第 2 个接口找的, 直接用这个命令最稳。

### 1.3 双路串口输出

固件把 shell 输出**同时**写到 UART0 和 UART2:

- UART0 (PA00/PA01) 是主调试口, 接 FT2232H channel B, 阻塞发送, 保证不丢字符;
- UART2 (PB08/PB09) 是镜像口, 按接线表可外接 USB-TTL; 发送前检查 TX FIFO,
  未接线/时钟未就绪时自动跳过, **不会拖住主串口**。

两路都是 115200 8N1, shell 输入两路都收 (谁先到用谁)。

### 1.4 终端电阻 / 总线注意 (现场经验)

- 每块自制板都在 TRX 对上带 49.9 Ω x2 终端, **多板并联会过终端**。本板 `s2500 hdd`
  长期报 `SHORT_OPEN=2 (SHORT/多余终端) 1W_SHORT=1`, 这是已知项, 不是故障;
- 过终端的直接后果是信号质量下降: `SQI` 只有 4/7 左右, `SQI+` 135~160/255;
- 不要动主机 `enx001ec0d1c337` 的 PLCA 配置和 IP (由 lead 统一管理);
- 同一时刻只允许一个人做吞吐测量, 否则数字互相干扰。本仓库统一用
  `flock /tmp/s2500_perf.lock -c '<测量命令>'` 串行化。

## 2. 环境安装 (从零开始)

### 2.1 需要的组件

| 组件 | 版本/路径 | 说明 |
|------|-----------|------|
| HPM SDK | v1.13.0, `<home>/hpm5e31/sdk_env_v1.13.0/hpm_sdk` | 只用到 drivers/soc/boards/components |
| RISC-V 工具链 | `riscv32-unknown-elf-newlib-multilib`, `<home>/hpm5e31/toolchains/...` | gcc/newlib, 带 multilib |
| cmake / ninja | cmake >= 3.20, ninja | 构建 |
| OpenOCD | **必须带 `hpm_xpi` flash 驱动** | 系统自带 0.12 不行 |
| python3 + pyserial | `pip3 install pyserial` | `tools/s25.py` 串口助手 |

### 2.2 HPM SDK + 工具链

```bash
# SDK (官方发布包解压即可, 目录名保持 hpm_sdk)
ls <home>/hpm5e31/sdk_env_v1.13.0/hpm_sdk   # 应能看到 drivers/ soc/ boards/ components/
export HPM_SDK_BASE=<home>/hpm5e31/sdk_env_v1.13.0/hpm_sdk

# 工具链
export GNURISCV_TOOLCHAIN_PATH=<home>/hpm5e31/toolchains/riscv32-unknown-elf-newlib-multilib
export PATH=${GNURISCV_TOOLCHAIN_PATH}/bin:${PATH}
riscv32-unknown-elf-gcc --version   # 能打印版本即可
```

`build.sh` / `flash.sh` 已经把这些路径写成默认值, 一般**不需要**手工 export。

### 2.3 OpenOCD (hpm_xpi) —— 新手最容易卡在这

HPMicro 的 flash 驱动 `hpm_xpi` 不在上游 OpenOCD 里。用 Debian/Ubuntu 自带的 openocd 会报:

```
Error: flash driver 'hpm_xpi' not found
```

两种拿到办法:

```bash
# 办法 A: 官方预编译包 (最快)
#   下载 https://github.com/hpmicro/riscv-openocd/releases 里的 hpm_xpi_v0.5.0 产物,
#   解压后形如 <dir>/bin/openocd
export OPENOCD=<dir>/bin/openocd

# 办法 B: 源码编译 (约 5 分钟)
git clone -b hpm_xpi_v0.5.0 https://github.com/hpmicro/riscv-openocd.git
cd riscv-openocd && ./bootstrap && ./configure --enable-ftdi --prefix=$PWD/out
make -j$(nproc) && make install
export OPENOCD=$PWD/out/bin/openocd
```

开发机上现成可用的一个（路径按实际安装位置替换）:

```
<home>/tools/ocd050/usr/local/bin/openocd
# Open On-Chip Debugger 0.12.0+dev-snapshot (2026-06-30-01:42)
```

`flash.sh` 会自动按 `$OPENOCD` → `$HPM_OPENOCD` → 上述路径 → `PATH` 的顺序查找,
并且用 `grep -a hpm_xpi <openocd 二进制>` 确认驱动真的存在, 找不到会给出明确提示。

### 2.4 联网代理 (只在这台机器上需要)

```bash
export http_proxy=http://<proxy-host>:7890 https_proxy=$http_proxy
```

## 3. 编译

```bash
cd t30hm1ts2500/hpm5361
./build.sh                 # 默认 release + flash_xip
# 产物: build/output/demo.elf (以及 .bin/.map/.asm)
```

可覆盖的变量:

| 变量 | 默认 | 说明 |
|------|------|------|
| `HPM_SDK_BASE` | `<home>/hpm5e31/sdk_env_v1.13.0/hpm_sdk` | SDK 路径 |
| `GNURISCV_TOOLCHAIN_PATH` | `<home>/hpm5e31/toolchains/riscv32-unknown-elf-newlib-multilib` | 工具链 |
| `BUILD_DIR` | `build` | 构建目录 |
| `BUILD_TYPE` | `release` | `debug` 可加 -Og |

关键构建配置 (`CMakeLists.txt`):

- `BOARD=hpm5300evk`, `HPM_BUILD_TYPE=flash_xip` (XIP 直接从 QSPI NOR 跑);
- 自定义链接脚本: 在 SDK `flash_xip.ld` 的基础上把 letter-shell 的 `shellCommand` 段
  插进 `.text`, 并导出 `_shell_command_start/_shell_command_end`;
- `CONFIG_HPM_SPI=1` 打开 SDK 的 SPI 组件 (阻塞收发 + DMA 启动);
- **lwIP 调优参数用 `-D` 只作用于本板** (见 §6), 其它平台不受影响。

内存占用 (release, 都是实测):

```
FLASH    164560 B / 1 MB    (15.7%)
ILM         728 B / 128 KB  (0.6%)   只有中断向量
DLM      122240 B / 130304 B (93.8%) ← 很紧! 加缓冲前先看这里
AHB_SRAM    156 B / 32 KB   (0.5%)   SPI DMA 收发缓冲 (必须在 AHB SRAM)
```

## 4. 烧写与运行

### 4.1 烧写

```bash
cd t30hm1ts2500/hpm5361
./flash.sh                          # 烧 build/output/demo.elf
./flash.sh build/output/demo.elf    # 或指定 elf
OPENOCD=/path/to/openocd ./flash.sh # 或指定 openocd
```

脚本会先尝试 `reset halt` (走 FT2232H 的 nSRST) 烧写后 `reset run`;
失败会退回 `halt` + 直接 program + 写 PPOR 寄存器软复位 (`0xF410001C = 0x10`)。
看到 `烧写完成 (SRST reset run)` 或 `烧写完成 (无复位 + PPOR 软复位)` 都算成功。

### 4.2 看串口

```bash
cd t30hm1ts2500
python3 tools/s25.py --auto hpm -c "s2500 info"
python3 tools/s25.py --auto hpm -c "spi" -c "netif"
python3 tools/s25.py --auto hpm -i        # 交互模式, Ctrl-] 退出
```

**letter-shell 只认 CR (`\r`)**。手工用 minicom/picocom 时回车要发 CR;
发 `\r\n` 会被当成两条命令, 出现 `Command not Found`。

上电后会打印 banner, **等 banner 打完再发命令** (letter-shell 输入 FIFO 很浅,
banner 还在输出时灌命令会被截断, 表现为命令“没反应”)。

正常启动日志:

```
[BOOT] HPM5361 + T30HM1TS2500 (S2500) 10BASE-T1S
[BOOT] SPI1 25MHz  CS=PA26 SCLK=PA27 MISO=PA28 MOSI=PA29  HDMA ch0/1
[BOOT] RST=PA10 IRQ=PA12  UART0(PA00/01)+UART2(PB08/09) 115200
==============================================
 T30HM1TS2500 10BASE-T1S  HPM5361 + T30HM1TS2500
 MAC  : 44:9F:79:29:73:18
 IPv4 : 192.168.10.12/24  gw 192.168.10.10
 PLCA : id=2 ncnt=8 en=1
 shell: 输入 help 查看命令, s2500 info 查看芯片
==============================================
```

## 5. 命令表

### 5.1 芯片/驱动命令 `s2500 <子命令>`

| 命令 | 作用 | 典型输出要点 |
|------|------|--------------|
| `s2500 info` | IDVER/PHYID/STDCAP/CONFIG0/MAC/PHY STATUS/PLCA/ONIPVER/DIOCFG/唤醒状态 | `IDVER=0x11` 才对 |
| `s2500 selftest` | 7 项自检: 寄存器/PHYID/PCS 环回/PHY 环回/PHY Status/SQI/HDD | 前 4 项必须 PASS |
| `s2500 status` | link/PLCA/SQI/PLCA 诊断/PCS 计数/驱动收发统计 | 看 `drv stats` 的 drop/fcs |
| `s2500 wd [on|off]` | 自愈看门狗统计/开关 (默认 on, 每 2 s 检查) | 芯片自发复位后自动重初始化, ping 中断 < 1.5 s |
| `s2500 counters [clear]` | MAC 硬件计数器 35 项 (含各长度帧、冲突、FCS 错) | 排查丢包/冲突 |
| `s2500 reg <mms> <addr> [值]` | 直接读写寄存器 (mms0=标准, mms2=MAC, mms3=PCS/PMA, mms4=PLCA, mms5=vendor) | 写时打印 W 行 |
| `s2500 plca [id N] [ncnt N] [totmr N] [en 0/1] [cr 0/1] [prec 0/1]` | PLCA 配置 | 默认 follower id=2 |
| `s2500 sqi` | SQI(0-7) / SQI+(0-255) / TOID | 本总线约 4/7, 135~160/255 |
| `s2500 hdd` | 线束故障检测 (开路/短路/多余终端) | 本板长期 `SHORT_OPEN=2 1W=1` (已知项) |
| `s2500 td <ref\|meas\|dly> [dur_ms]` | TC14 拓扑发现 (距离/延时) | 需要干净总线, 否则 `dly_err=1` |
| `s2500 time` / `s2500 ts <a\|b\|c>` | TC6 PTP 时间 / 发送时间戳 | |
| `s2500 loop <0\|1\|2>` | 环回开关: 0=关 1=PCS 2=PHY(模拟) | `selftest` 会自动用后关掉 |
| `s2500 led <fn0> [fn1]` / `s2500 irq` | DIO0/1 功能 / PHY 中断状态 | |
| `s2500 mac [xx:xx:..]` | 读/写 MAC | 默认用出厂 MAC |
| `s2500 analog <amp> <edth> <edthcd> <slw> <clko> <cmc>` | 模拟参数 (TWEAKS2) | 慎改 |
| `s2500 bench tx <count> [len]` | 纯发送压测 (广播 0x88B5) | 1500 B: 833 fps / 10.0 Mbit/s |

### 5.2 本板自带的 SPI 诊断 `spi`

| 命令 | 作用 |
|------|------|
| `spi` | SCLK/时钟源/DMA 状态 + 事务数/字节/周期统计 + CPU 等待占比 |
| `spi speed <MHz>` | 运行时改 SCLK (0=回到 25 MHz)。调速后立即 `spi reg 1000` + `spi loop 100` 验证 |
| `spi dma <0\|1>` | 数据事务走 DMA / 走阻塞式 FIFO |
| `spi bench <n>` | n 次 68 B 数据块事务, **不依赖 PHY/总线**, 纯测 SPI+驱动 |
| `spi reg <n>` | 连续读 n 次 IDVER, 统计错误数 (查 SPI 稳定性) |
| `spi loop <n>` | PCS 环回收发 n 次 (不依赖外部总线) |
| `spi reset` | 清零 SPI 统计 |
| `spi selftest` | 重新做 DMA/阻塞一致性自检 |

`spi reg` / `spi loop` / `spi bench` 是**拔掉总线也能跑**的三件套: 只要它们全过,
说明 MCU 侧 SPI/时序/驱动没问题, 问题一定在 PHY/总线/配置。

### 5.3 网络命令

| 命令 | 作用 |
|------|------|
| `netif` | 板名/MAC/IP/链路/PLCA ID |
| `ping <ip> [n]` | 从板子发 ICMP echo (raw API, 阻塞式) |
| `iperf -s` | 板上起 iperf2 TCP server (端口 5001, lwiperf) |
| `iperf -c <ip>` | 板子做 iperf2 client |
| `udpecho on\|off` | UDP 回显 (端口 7) |
| `tcpstats` | lwIP 内存/pbuf/收发统计 |
| `reboot` | 软件复位 (写 PPOR) |

## 6. 实现要点 (接手要改代码时先看这里)

### 6.1 SPI1 25 MHz 是怎么来的

- SPI1 时钟源选 PLL0 clk1 = 600 MHz; SCLK = 600 / ((11+1) x 2) = **25.000 MHz**
  (整数分频, 没有取整误差); SDK 在 >20 MHz 时会把 SCLK_DIV 设成 0xFF 直通,
  实际是 `clock_set_source_divider(clock_spi1, 600MHz/24)` → 源 25 MHz 直通 SCLK。
- 运行时可用 `spi` 确认: `SPI1 : SCLK=25000000 Hz src=25000000 Hz`;
- 实测 20/21/22/24/25 MHz 各档: 1000 次 IDVER 读 0 错 + 100 次 PCS/PHY 环回 100/100,
  **25 MHz 稳定, 定在 25 MHz** (注意: `spi speed 20` 实际会落到 24 MHz, 因为 SDK 对
  <=20 MHz 会优先选 24 MHz 晶振源, 这是 SDK 行为, 不是 bug);
- CS 建立/保持用 mcycle 精确延时 200 ns (原来是 1 us, 白白浪费 ~9% 事务时间)。

### 6.2 DMA

- 数据事务 (68 B) 走 HDMA ch0(TX)/ch1(RX), DMAMUX 源 `HPM_DMA_SRC_SPI1_TX/RX`,
  字节宽度 (SPI DATALEN=8 且未开 data_merge, 只能按字节搬);
- **DMA 缓冲必须放 AHB SRAM** (`.ahb_sram` 段, 0xF0400000), 核内 DLM 不是 DMA 的可达内存;
- 上电自检: 阻塞读 IDVER 与 DMA 读 IDVER 比较, 不一致自动回退阻塞式并打印告警;
- `spi dma 0/1` 可随时切换对比。

### 6.3 驱动层的两个关键优化 (common/s2500, 平台无关)

1. **TX chunk 信用缓存**: `BUFSTS.TXC` 只会增加, 本地递减即可, 1500 B 帧 24 块能省掉
   23 次 BUFSTS 控制事务 (每次 12 B SPI), 单帧 SPI 时间 -15%;
2. **收包预算 8 → 16**: 一次进入 netif 最多取 16 帧, 减少主循环往返。
   可用 `-DS2500_NETIF_RX_BUDGET=n` 覆盖。

### 6.4 lwIP 调优 (只作用于本板)

`common/lwip-port/lwipopts.h` 里这些宏都加了 `#ifndef` 保护, 本板在
`CMakeLists.txt` 用 `-D` 覆盖, **其它平台不传这些 -D, 取值与原来完全一致**:

| 宏 | 原值 | 本板 |
|----|------|------|
| `MEM_SIZE` | 24 KB | 32 KB |
| `TCP_WND` | 2*MSS | 4*MSS (5840) |
| `TCP_SND_BUF` | 4*MSS | 6*MSS (8760) |
| `TCP_SND_QUEUELEN` | 8*SND_BUF/MSS | 16 |
| `PBUF_POOL_SIZE` | 24 | 28 |
| `MEMP_NUM_TCP_SEG` | 48 | 32 |
| `MEMP_NUM_PBUF` | 16 | 8 |

实测: 再往上加 `TCP_SND_BUF` 到 8*MSS (11680) 反而让板→主机方向掉到 5.84 Mbit/s,
所以停在 6*MSS。**这个参数不是越大越好**, 与 DLM 余量和半双工 ACK 时序有关。

### 6.5 时基与计数器溢出

两个 32 位计数器在 480 MHz 下都会 8.95 s 回绕, 踩过的坑:

- sys_now/bsp_millis: 用 MCHTMR (24 MHz, 64 位), **不要用 mcycle**, 否则 lwIP 定时器异常;
- SPI 统计: 累计周期用 64 位 (bsp_spi_stats_t.cyc/wait_cyc), 32 位回绕后
  “平均周期/事务”会掉成 810 这种假数字, 会把 CPU 占用算错。

`bsp_millis()` 用 MCHTMR (24 MHz, 64 位)。**不要改回 mcycle**: 32 位 mcycle 在 480 MHz
下 8.95 s 就回绕一次, 会让 lwIP 的 `sys_now()` 在 9 秒内“倒退”, 引发 TCP 定时器异常。

### 6.6 代码地图

```
hpm5361/
  src/main.c        应用配置 (IP/PLCA/板名) + 主循环 + DMA 自检
  src/bsp.c/.h      SPI1(25MHz/DMA)/GPIO/UART0+UART2/MCHTMR 时基/S2500 HAL
  src/diag.c        spi 诊断命令 (speed/dma/bench/reg/loop/reset/selftest)
  src/port.c        common/s2500 的串口/复位移植层
  CMakeLists.txt    构建 + lwIP 调优 -D + 链接脚本改造
  build.sh          编译   flash.sh 烧写
common/s2500/       S2500 通用驱动 (平台无关)
common/lwip-port/   lwIP 配置 (宏都有 #ifndef, 可被各平台覆盖)
common/letter-shell 命令 shell
```

## 7. 实测数据

### 7.1 改造前后

| 项目 | 改前 (20 MHz, 无 DMA) | 改后 (25 MHz + DMA + 调优) |
|------|----------------------|---------------------------|
| SPI SCLK | 20.000 MHz | **25.000 MHz** |
| 68 B 事务吞吐 | 9.5 Mbit/s 量级的 SPI 供给 | 阻塞 **20.92 Mbit/s** / DMA 16.48 Mbit/s |
| 68 B 事务 CPU | ~全阻塞 | 阻塞 11818 周期; DMA 15349 周期其中 69% 是纯等待 |
| 纯发送 1500 B | 9.52-9.55 Mbit/s | **10.0 Mbit/s (833 fps)** |
| TCP 主机→板 | 7.53 Mbit/s | **8.80 Mbit/s** |
| TCP 板→主机 | (未通过) | 8.02 Mbit/s |
| ping 平均 RTT | 0.86 ms | **0.70 ms** (min 0.62) |

### 7.2 这版的关键读数

```
spi
  CPU  : 480 MHz
  SPI1 : SCLK=25000000 Hz  src=25000000 Hz  DMA=on (自检通过)
       事务=2000 字节=136000  平均 15372.3 周期/事务  平均 226.10 周期/字节
       CPU: 总周期=30698545  其中等待=21312101 (69%)  有效=9386444 (31%)

spi dma 0 / spi bench 2000
  吞吐=20.923 Mbit/s   平均 11781.3 周期/事务  有效 100%

s2500 bench tx 300 1500   -> 833 fps, 10000 kbit/s
s2500 bench tx 500 300    -> 3703 fps,  8888 kbit/s
s2500 bench tx 3000 64    -> 11494 fps, 5885 kbit/s
spi reg 1000              -> 错误 0
spi loop 100              -> 成功 100 / 失败 0
s2500 sqi                 -> SQI=4/7  SQI+=135~160/255
s2500 hdd                 -> valid=1 SHORT_OPEN=2 (SHORT/多余终端) 1W_SHORT=1
```

iperf 期间 (主机→板 8 s, 8.50 MB / 8.80 Mbit/s) 的 CPU 账:

```
事务=266644 字节=11746840 平均 11175.9 周期/事务
CPU 总周期=2979993736 其中等待 62% 有效 38%
→ 8.1 s x 480 MHz = 3888 M 周期, SPI 路径“有效”1120 M = **整核 29%**
  (如果把 DMA 等待也算进去是 77%, 这段时间 CPU 实际在空转轮询)
```

### 7.3 测量方法 (可复现)

```bash
# 纯发送
flock /tmp/s2500_perf.lock -c 'cd t30hm1ts2500 && python3 tools/s25.py --auto hpm -t 8 -c "s2500 bench tx 300 1500"'

# TCP: 板做 server, 主机做 client (主机侧数才有意义)
flock /tmp/s2500_perf.lock -c 'cd t30hm1ts2500 && python3 tools/s25.py --auto hpm -c "iperf -s" >/dev/null; sleep 1; iperf -c 192.168.10.12 -t 8 -i 8'

# TCP 反方向: 主机做 server, 板做 client
iperf -s -B 192.168.10.10 -t 12 &
python3 tools/s25.py --auto hpm -c "iperf -c 192.168.10.10"
```

注意:

- iperf3 连不上 lwiperf, **必须用 iperf2** (`iperf` 命令);
- `iperf -r` (反向) 不被 lwiperf 支持, 反方向要按上面第二个办法测;
- 测量必须串行化, 否则同一总线上的其它板会污染结果。

## 8. 排错表

| 现象 | 可能原因 | 定位/解决 |
|------|----------|-----------|
| `Error: flash driver 'hpm_xpi' not found` | 用了系统 openocd | 按 §2.3 换 hpm 版 openocd, 或 `OPENOCD=... ./flash.sh` |
| `Hart 0 is unavailable` / `Algorithm timed out` | openocd 版本不对 (0.11 老包) | 换 0.12+dev-snapshot 的 hpm_xpi 版本 |
| 串口无输出 | 串口选错 (选了 ttyUSB0) / 波特率不是 115200 | 用 `tools/s25.py --auto hpm`; 确认 FT2232H if01 |
| `Command not Found` | 回车发了 `\r\n` | letter-shell 只认 CR |
| 发命令没反应 | banner 还在输出, 输入 FIFO 被截断 | 等 banner 打完再发; 或 `reboot` 后 sleep 2 |
| `[BOOT] s2500_app_init failed ... retry in 2s` 死循环 | SPI1 接线/3V3/复位/S2500 供电 | 量 3V3, 查 PA26-PA29/PA10/PA12; `s2500 info` 看 `IDVER=0x11` |
| `s2500 selftest` 第 3/4 项环回 FAIL | SPI 层问题 (与总线无关) | `spi reg 1000`、`spi loop 100`; 降 `spi speed 20` 试; 查地回流/接线长度 |
| `spi reg` 有错或 `spi loop` 失败 | SCLK 太高 / DMA 缓冲不在 AHB SRAM / 接线长 | `spi dma 0` 对比; 降频; 缩短接线 |
| `link : DOWN` 但 ping 通 | LKST 在空闲时掉 0 (本总线常见) | 以 ping/吞吐为准; 有流量后 `s2500 info` 会看到 link=1 |
| SQI 显示 0/7 | 空闲无流量 | 先发几帧再看 |
| SQI 只有 3~4/7 | 过终端 (多板并联 49.9Ωx2) | 已知项, 见 §1.4 |
| `s2500 hdd` = SHORT_OPEN=2 / 1W=1 | 多余终端/单线短 | 已知项, 不是故障; 真要整改需减终端 |
| ping 通但 iperf 只有几 Mbit/s | 总线上还有别人在测 / PLCA 配置冲突 | 用 flock 串行; 确认本板 `en=1 id=2`, 主机 id=0 ncnt=8 |
| `plca diag RXINTO=1` | 有节点在别人的时隙发 (PLCA 配置冲突) | 查各板 PLCA ID 是否重复 (§9) |
| `bench tx` 卡很久 | 上电后 PHY 未锁定, TX 信用不再回收, 每块等 200 ms 超时 | 先 ping 一次或等链路起来再压测 |
| `s2500 td` 报 `dly_err=1` | 总线上有 BEACON/流量干扰 | 停 PLCA 协调者后再测, 或只做记录 |
| 编译报 `BYTE_ORDER redefined` | lwIP arch/cc.h 常见告警 | 可忽略 |
| 链接报 DLM 溢出 | lwIP 缓冲调大了 | 看 `DLM: x/130304`; 减 `PBUF_POOL_SIZE` 或 `MEM_SIZE` |

## 9. 总线节点约定 (PLCA ID)

| 节点 | PLCA ID | IP |
|------|---------|-----|
| 主机 enx001ec0d1c337 (协调者) | 0 | 192.168.10.10 |
| STM32F407 | 1 | 192.168.10.11 |
| **HPM5361 (本板)** | **2** | 192.168.10.12 |
| S32K312 | 3 | 192.168.10.13 |
| TC387 | 4 | 192.168.10.14 |
| RK3506 | 5 | 192.168.10.15 |

主机参数: node count 8, TO 32 BT, burst disabled, PLCA status on。
本板默认 `en=1 id=2 ncnt=8 totmr=32` (follower)。要本板当协调者:

```
s2500 plca id 0 ncnt 8 en 1
```

## 10. 已知限制与风险

1. **HDD 报 SHORT/多余终端, SQI 只有 4/7**: 多板并联终端过量, 属硬件设计问题。
   它限制了链路裕量, 也让 LKST 在空闲时掉 0; 不整改也能跑到 8.8 Mbit/s;
2. **DMA 比阻塞式慢**: 68 B 粒度下每次 DMA 要 68 次字节级握手, 事务时间从 24.6 us
   涨到 31.9 us (吞吐 20.9 → 16.5 Mbit/s)。但 25 MHz 下即使 DMA 的 16.5 Mbit/s 也
   远高于 10 Mbit/s 线速, 对 TCP 无影响; 换来的是 CPU 有效周期下降 (31% vs 100%)。
   若将来要压 64 B 小包极限, 应改 `data_merge` + 32 位 DMA 或直接关 DMA;
3. **DMA 等待期 CPU 仍在空转轮询** (占 69% 事务时间)。要真正省电/省 CPU, 下一步应改成
   DMA 完成中断 + WFI, 而不是忙等; 当前实现没做, 因为没有其它任务可跑;
4. **RX 中断未使用, 空闲轮询本身就吃掉约一半 CPU**: IRQ_N (PA12) 已接但驱动仍轮询.
   空闲时主循环不停地读一次 BUFSTS 判断有没有帧, 实测 **约 47k 次 SPI 控制事务/秒**,
   每次约 10.9 us (含 DMA 启动开销) → 约 **51% 的单核占用**。满载时这份开销是隐含的收包
   工作, 但纯空闲时就是纯浪费。下一步: 打开 S2500 中断 (IMSK0) + PLIC + IRQ_N 做中断
   驱动收包, 或至少把空闲轮询降到 kHz 量级;
5. **PLCA 诊断位 RXINTO 偶发置 1**: 说明总线上存在时隙相位冲突 (节点数/ID/终端),
   目前不影响吞吐, 但建议统一各节点的 ncnt 与 ID 后再复测;
6. **TCP 反方向 (板→主机) 7.0~8.0 Mbit/s** (多次实测 8.02 最佳), 略低于主机→板。lwiperf 发送窗口和半双工 ACK
   时序是主因, 不是 SPI/驱动瓶颈 (SPI 侧还有 2 倍余量);
7. **DLM 93.8% 已很紧**, 再加缓冲会链接失败, 需要先动 `lwipopts.h` 或把大数组移到 AHB SRAM;
8. **`s2500 bench tx` 在链路未起时很慢** (每块 200 ms 超时), 压测前先 `ping` 确认链路。
