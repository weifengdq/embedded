# STM32H723ZGT6 + T30HM1TS2500 (S2500) 10BASE-T1S 移植工程

> 平台: STM32H723ZGT6 (Cortex-M7 @550MHz, 1MB Flash / 564KB RAM, HSE 25MHz)
> 收发器: T30HM1TS2500 (S2500, OPEN Alliance 10BASE-T1x MAC-PHY, OA-SPI 模式0, 最高 25MHz)
> 调试器: STLINK-V3 (USB 0483:3754, SWD + VCP)
> 实测日期: 2026-11-20, 固件 build/stm32h723_s2500.elf (96284 B)

这个目录是"把 F407 的 S2500 工程原样搬到一个全新的 H723 板上"的完整实现。
如果你只想知道怎么跑起来, 直接跳到 [3. 三分钟跑起来](#3-三分钟跑起来)。
如果你是第一次用 H7, 请一定读 [6. D-Cache / MPU / DMA](#6-d-cache--mpu--dma-h7-最容易翻车的地方),
里面列了 H7 与 F4 的 5 个致命差异, 全部已经有可用的写法在 `Core/Src/board.c` 里。

目录:

1. [硬件与接线](#1-硬件与接线)
2. [环境搭建](#2-环境搭建-ubuntu-22042404)
3. [三分钟跑起来](#3-三分钟跑起来)
4. [时钟树](#4-时钟树)
5. [工程结构 / 编译产物](#5-工程结构--编译产物)
6. [D-Cache / MPU / DMA](#6-d-cache--mpu--dma-h7-最容易翻车的地方)
7. [命令手册](#7-命令手册-letter-shell)
8. [实测数据](#8-实测数据-2026-11-20)
9. [PLCA 协调者 (ID=0) 测试](#9-plca-协调者-id0-测试)
10. [排错表](#10-排错表)
11. [与 STM32F407 工程的差异 / 移植清单](#11-与-stm32f407-工程的差异--移植清单)

---

## 1. 硬件与接线

### 1.1 接线表 (必须严格按这张表, 和 F407 那块板完全不同)

| S2500 / 调试口 | STM32H723 引脚 | 复用功能 | 说明 |
|---|---|---|---|
| CS_N  | **PG10** | GPIO 推挽输出 | 软件片选, 低有效, **覆盖整帧 SPI 事务** (12B 控制 / 68B 数据) |
| SCLK  | **PG11** | SPI1_SCK  (AF5) | 模式 0 (CPOL=0, CPHA=0), MSB first |
| MISO  | **PG9**  | SPI1_MISO (AF5) | |
| MOSI  | **PD7**  | SPI1_MOSI (AF5) | |
| IRQ_N | **PD5**  | EXTI5, 下降沿 | 模块侧上拉; H7 里 EXTI5..9 **共用 EXTI9_5 向量** |
| RST_N | **PD6**  | GPIO 推挽输出 | 高 = 释放; 上电时打 2ms 低脉冲 |
| USART1_TX | **PA9**  | USART1_TX (AF7) | 115200 8N1 → STLINK-V3 的 VCP = `/dev/ttyACM0` |
| USART1_RX | **PA10** | USART1_RX (AF7) | |
| SWD | PA13/PA14 (+GND, 3V3) | SWDIO/SWCLK | STLINK-V3, 目标电压实测 3.38V |

要点:

* **CS_N 走 GPIO, 不用硬件 NSS**。S2500 要求 CS_N 在整个事务期间拉低, 事务之间必须抬高,
  由 `Core/Src/port.c` 的 `st_spi_xfer` 负责 (拉低 → 传输 → 抬高的时间也被统计下来)。
* 每次上电 `board_init()` 会对 RST_N 打一个 2ms 低脉冲, 再等 5ms 让芯片 boot;
  如果模块已经被别的固件搞进奇怪状态, 断电重上电比软件复位可靠。
* 这块板 (以及仓库里其它平台的自制板) 都焊了 **两组 49.9Ω 终端**, 多板并联 = 终端过量,
  `s2500 hdd` 会报 `SHORT_OPEN=2`, SQI 约 4/7。这是已知硬件现状, 不影响 10Mbit/s 跑满,
  但会降低信号余量 (所以下面所有"降速 A/B"的手段都值得记住)。

### 1.2 板子上的网络身份

| 项 | 值 |
|---|---|
| IP | 192.168.10.11/24 |
| 网关 / 主机 | 192.168.10.10 (Ubuntu 上的 USB-10T1S, 接口 `enx001ec0d1c337`) |
| PLCA | follower, node-id = **1**, NCNT=8, TOTMR=32, en=1 |
| MAC | 芯片出厂 MAC, 实测 44:9F:79:29:F9:8E |
| 主机角色 | 主机是唯一协调者 (node-id 0), 全网只能有一个协调者 |

---

## 2. 环境搭建 (Ubuntu 22.04/24.04)

```bash
# 交叉编译 + 构建
sudo apt update
sudo apt install -y gcc-arm-none-eabi binutils-arm-none-eabi \
                    cmake ninja-build make git

# 烧写 / 调试
sudo apt install -y openocd stlink-tools

# 串口 (任选其一)
sudo apt install -y picocom minicom python3-serial

# 当前用户需要能开 /dev/ttyACM* (STLINK-V3 VCP)
sudo usermod -aG plugdev,dialout "$USER"   # 重新登录生效
```

版本参考 (本工程实测通过的版本):

```
arm-none-eabi-gcc (15:14.2.rel1-1) 14.2.1   # >= 10 均可
cmake 4.2.3                                # >= 3.20
ninja 1.13.2
Open On-Chip Debugger 0.12.0
```

需要联网时 (本仓库所在内网):

```bash
export http_proxy=http://<proxy-host>:7890 https_proxy=$http_proxy
```

---

## 3. 三分钟跑起来

```bash
cd stm32h723
./build.sh          # 编译, 产物在 build/
./flash.sh          # 用 STLINK-V3 SWD 烧写并复位

# 串口 (letter-shell 只认 CR = 回车符 0x0D, 不能只发 \n)
picocom -b 115200 /dev/ttyACM0
# 或者用仓库里的工具 (自动按 VID:PID 找口, 自动补 CR)
../tools/s25.py --auto h723 -c "s2500 info"
```

正常启动应该是这样 (`s25.py` 抓的开机横幅):

```
[BOOT] STM32H723ZGT6 + T30HM1TS2500 (S2500) 10BASE-T1S
[BOOT] SYSCLK 550 MHz / HCLK 275 MHz / SPI1 25000 kHz
[BOOT] HSE 25MHz OK, VOS0, D-Cache ON
[BOOT] USART1 115200 8N1 / PLCA id=1

==============================================
 T30HM1TS2500 10BASE-T1S  STM32H723 + T30HM1TS2500
 MAC  : 44:9F:79:29:F9:8E
 IPv4 : 192.168.10.11/24  gw 192.168.10.10
 PLCA : id=1 ncnt=8 en=1
 shell: 输入 help 查看命令, s2500 info 查看芯片
==============================================
```

然后:

```bash
../tools/s25.py --auto h723 -c "s2500 status"     # link UP / PST=1
ping -c 5 192.168.10.11                              # 0% 丢包, RTT ~0.6ms
```

---

## 4. 时钟树

```
HSE 25MHz ──┬─ PLL1: M=2 -> 12.5MHz, N=44 -> VCO 550MHz, P=1 ── SYSCLK 550MHz
            │                                    ├─ Q=4 -> 137.5MHz
            │                                    └─ R=2 -> 275MHz
            │        SYSCLK/1 = 550MHz (CPU)
            │        HCLK  = /2 = 275MHz  (AXI/AHB 上限)
            │        APB1/2/3/4 = /2 = 137.5MHz
            │        VOS0 (必须! VOS1 最高 400MHz), FLASH latency 3WS
            │
            └─ PLL2: M=5 -> 5MHz, N=80 -> VCO 400MHz, P=2 ── 200MHz = SPI1 内核时钟
                                            └─ Q=4 -> 100MHz
                 SPI1 分频 (CFG1.MBR): /8 = 25MHz (默认)   /16 = 12.5MHz   /32 = 6.25MHz ...
```

| 时钟 | 频率 | 用途 |
|---|---|---|
| SYSCLK / CPU | 550 MHz | 内核 |
| HCLK (AXI/AHB) | 275 MHz | AXI SRAM / DMA |
| APB2 | 137.5 MHz | USART1 (kernel = PCLK2) |
| PLL2P | 200 MHz | SPI1 内核时钟 |
| SCLK | **25 MHz** | S2500 上限 25MHz; `spi speed 12` 可退到 12.5MHz |
| SysTick | 1 kHz | HAL 时基, DWT->CYCCNT @550MHz 做 us 级计时 |

HSE 起振失败会自动退回 HSI 64MHz (`[BOOT] HSE FAIL -> HSI fallback`), 频率略低但能进 shell 排错,
此时 `spi` 命令里 `SYSCLK 544 MHz (HSE falls back to HSI)` 一眼就能看出来。

---

## 5. 工程结构 / 编译产物

```
stm32h723/
├── CMakeLists.txt              # 只编译用得到的 HAL 模块 (16 个 .c)
├── cmake/arm-none-eabi.cmake   # cortex-m7 + fpv5-d16 + hard-float
├── STM32H723XG_FLASH.ld        # 内存布局: DTCM 栈 / RAM_D1 数据 / RAM_D2 DMA 缓冲
├── startup_stm32h723xx.s
├── build.sh / flash.sh / .gitignore
├── Core/Inc/board.h            # 引脚、统计结构、对外 API
├── Core/Inc/stm32h7xx_hal_conf.h
├── Core/Src/
│   ├── main.c                  # 配置 + 主循环 (s2500_app_poll)
│   ├── board.c                 # 时钟/D-Cache/MPU/SPI1+DMA/USART1/DWT  ← 移植时主要改这里
│   ├── port.c                  # common/s2500 移植层 (SPI 事务 / 延时 / 串口 / 复位)
│   ├── bench.c                 # 板级命令: bench / spi (含 spi speed / spi dma / spi bench)
│   ├── stm32h7xx_it.c          # SysTick / USART1 / EXTI9_5
│   ├── system_stm32h7xx.c / syscalls.c / sysmem.c
├── Drivers/                    # STM32H7xx_HAL_Driver + CMSIS (从参考工程拷入, 未改动)
└── build/                      # 编译产物 (git 忽略)
```

通用代码全部来自仓库的 `common/`, **本工程没有任何复制粘贴的 lwIP / letter-shell**:

| 目录 | 作用 |
|---|---|
| `common/s2500` | S2500 驱动 + lwIP netif + shell 命令, 平台无关 |
| `common/lwip`, `common/lwip-port` | lwIP (NO_SYS) + lwipopts |
| `common/letter-shell`, `common/letter-shell-port` | 串口 shell |

编译 (内存占用, 实测):

```
$ ./build.sh
   text    data     bss     dec     hex  filename
  96072     160   86024  182256   2c7f0  build/stm32h723_s2500.elf
   DTCMRAM:      0 B    128 KB   0.00%     <- 只放 MSP 栈 (128KB 全给栈)
   RAM_D1 :  85992 B    320 KB  26.24%     <- .data/.bss/heap
   RAM_D2 :    192 B     32 KB   0.59%     <- .dma_buffer (SPI DMA 缓冲)
   FLASH  :  96284 B      1 MB   9.18%
```

可调构建选项 (CMake cache):

| 选项 | 默认 | 说明 |
|---|---|---|
| `S2500_TCP_WND` | 8760 | lwIP TCP 接收窗口 (字节)。**下行吞吐的关键**, 见第 8 节。传 0/空 = 用 `common/lwip-port/lwipopts.h` 的 2*MSS |
| `BUILD_TYPE` | Release | `-Os -g0`; Debug = `-O0 -g3` |

```bash
# 想回到 common 的默认窗口 (跨平台一致性优先时):
cmake -S . -B build -DS2500_TCP_WND=0 && ninja -C build
```

---

## 6. D-Cache / MPU / DMA (H7 最容易翻车的地方)

### 6.1 内存布局 (由链接脚本决定, 不是随手放的)

| 区域 | 地址 | 大小 | 放什么 | Cache | DMA 能访问? |
|---|---|---|---|---|---|
| DTCMRAM | 0x20000000 | 128 KB | **只有 MSP 栈** | 不经过 cache | ❌ 不行 |
| RAM_D1 (AXI) | 0x24000000 | 320 KB | .data/.bss/heap | 走 D-Cache | ✅ |
| RAM_D2 | 0x30000000 | 32 KB | **.dma_buffer** (SPI DMA 缓冲) | MPU 配成 **Normal non-cacheable** | ✅ |
| FLASH | 0x08000000 | 1 MB | 代码/常量 | I-Cache | — |

为什么这么分:

* **DMA 访问不到 DTCM**。栈放 DTCM 是 H7 的标准做法 (最快), 但千万别把 DMA 缓冲放栈上。
* **SPI DMA 缓冲放在 RAM_D2 并让 MPU 声明为"非缓存"**。这样 CPU 写完缓冲立刻对 DMA 可见,
  DMA 写完立刻对 CPU 可见, **不需要 clean/invalidate 也能正确工作**。
  `board.c` 里还在传输前后各做一次 `SCB_CleanDCache_by_Addr` / `SCB_InvalidateDCache_by_Addr`
  作为"MPU 万一配错"的兜底 (对非缓存区是无操作, 代价可忽略)。

MPU 配置见 `board.c: mpu_config()`:

```c
mpu.BaseAddress = 0x30000000; mpu.Size = MPU_REGION_SIZE_32KB;
mpu.TypeExtField = MPU_TEX_LEVEL1;       /* TEX=1, C=0, B=0 => Normal, non-cacheable */
mpu.IsCacheable  = MPU_ACCESS_NOT_CACHEABLE;
mpu.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
mpu.IsShareable  = MPU_ACCESS_SHAREABLE;
```

顺序也很重要: **先配 MPU, 再 `SCB_EnableICache()` / `SCB_EnableDCache()`**, 最后才 `HAL_Init()`。

### 6.2 H7 与 F4 的 5 个致命差异 (这次移植全部踩过)

| # | H7 的行为 | 现象 | 本工程的处理 |
|---|---|---|---|
| 1 | **新 SPI IP 只有 `SPE=1` 不会产生时钟, 必须再置 `CR1.CSTART`** | 第一次 DMA 传输: TX DMA 立刻"完成"(12B 灌进 16B FIFO), RX DMA 的 NDTR 一动不动 → 1ms 超时 → 永久回退轮询 | `board.c: spi_dma_xfer()` 里 `SPI1->CR1 |= SPI_CR1_CSTART;` |
| 2 | 传输结束标志是 `SR.EOT` / `SR.TXC`, **没有 F4 的 BSY**; 而且 HAL 轮询函数返回前 `SPI_CloseTransfer()` 已经把 EOT/TXC 清掉了 | 照抄 F4 的 "CS 抬高前等 BSY" → 每次事务白等 1ms (实测平均 1010us/次, SPI 占用 97.8%) | DMA 路径在 `board_spi_transfer` 内部等 EOT+TXC; `port.c` 里**不再等** |
| 3 | **波特率分频在 `CFG1.MBR`**, 不在 `CR1.BR`; DMA 使能在 `CFG1.RXDMAEN/TXDMAEN`, 长度在 `CR2.TSIZE`, TX/RX 是分开的 `TXDR/RXDR` | 编译报错/时钟不对 | `board_spi_clock_hz()` / `board_spi_set_hz()` 都读写 CFG1.MBR |
| 4 | **DMA 请求号不在 `SxCR.CHSEL`, 而是写 `DMAMUX1_Channelx->CCR`** (SPI1_RX=37, SPI1_TX=38) | RX/TX 一条流永远不触发 | `board.c: spi_dma_init()`: `SPI_DMA_RX_MUX->CCR = DMA_REQUEST_SPI1_RX;` |
| 5 | **DWT->CYCCNT 必须先置 `DEMCR.TRCENA`** | us 延时/bench 全是 0 | `dwt_init()`: `CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;` |

另外还有两个"平台细节":

* `HAL_SPI_Init()` 之后 SPE 状态随 HAL 版本不同, 本工程在初始化末尾**明确 `__HAL_SPI_DISABLE`**,
  每次事务自己开、自己关 (和 F407 工程行为一致)。
* H7 的 `EXTI5..9` 共用一个中断向量 → `EXTI9_5_IRQHandler`, 挂起位是 `EXTI_PR1_PR5`。
* H7 的 PWR 没有独立时钟门控, 不需要 `__HAL_RCC_PWR_CLK_ENABLE()`。

### 6.3 一次 SPI 事务到底做了什么 (`board_spi_transfer`)

```
CS 已经在外面拉低 (port.c)
 ├─ __HAL_SPI_DISABLE                 // 配置只能在 SPE=0 时改
 ├─ 清空 RX FIFO 残留 (SR.RXP + RXDR)  // 否则 RX 会整体错位一字节
 ├─ IFCR 清 EOT/TXTF/UDR/OVR/MODF/CRCERR...
 ├─ CR2.TSIZE = len                   // 不写这个主机不产生时钟
 ├─ CFG1 |= RXDMAEN|TXDMAEN
 ├─ memcpy -> s_tx_stage (.dma_buffer, 非缓存); clean (兜底)
 ├─ DMA1_Stream0 (RX, req37) / Stream1 (TX, req38): M0AR/NDTR/清标志/使能
 ├─ __HAL_SPI_ENABLE + CR1.CSTART      // 时钟从这里开始
 ├─ 轮询 DMA TC (1ms 超时 -> 记录 LISR/HISR/SR/CFG1/NDTR/CR 到 g_spi_dma_dbg 并回退轮询)
 ├─ 等 SR.EOT|SR.TXC (1ms 超时)
 ├─ CR1 &= ~CSTART; __HAL_SPI_DISABLE; CFG1 清 DMA 位; 关两条流
 ├─ invalidate (兜底) + memcpy -> 调用者缓冲
CS 抬高 (port.c), 统计 cycles
```

DMA 出错时**只回退一次并永久改走 HAL 轮询** (现场宁可慢也不要挂), 这个行为可以用
`spi dma on` 重新打开。

---

## 7. 命令手册 (letter-shell)

> **letter-shell 只认 CR (0x0D)**。用 `picocom`/`minicom` 手打没问题;
> 用脚本发命令时用 `tools/s25.py` (它会自动补 `\r`), 长命令加 `-t 25 --idle 12`:
> `./tools/s25.py --auto h723 -c "bench 500 1428" -t 25 --idle 12`

### 7.1 板级命令 (本工程 `Core/Src/bench.c`)

| 命令 | 说明 |
|---|---|
| `spi` / `spi stat` | SPI1 配置 + 运行统计: 时钟分频、DMA 开关、事务数/字节数、平均/最大耗时、DMA 占比/回退/超时、SPI 占用、IRQ 次数、超时时打印 DMA 寄存器快照 |
| `spi dma on\|off` | 开/关 DMA (off = 走 HAL 轮询, 排错用) |
| `spi speed <MHz>` | 运行期改 SCLK, 例如 `spi speed 12` / `spi speed 25`; 不带参数则打印当前 |
| `spi bench [n]` | n 次 12B 控制事务 (读 IDVER) 的微基准, 给出 cycles/us 和理论线速 |
| `spi reset` | 清零 SPI 统计 |
| `bench [帧数] [payload]` | UDP 连续发送 (默认 1000 帧 × payload 1000B, 广播目的 MAC, 不需要 ARP), 打印 fps / L2 Mbit/s / UDP Mbit/s / 链路状态 |

### 7.2 S2500 / 网络命令 (来自 `common/s2500`)

| 命令 | 说明 |
|---|---|
| `s2500 info` | IDVER / PHYID / STDCAP / CONFIG0 / MAC / PHY STATUS / PLCA / ONIPVER / DIOCFG0 / WS_STATUS1 |
| `s2500 status` | link / plca(id,ncnt,PST,CRPS) / SQI / SQI+ / plca diag / PCS 碰撞 / 驱动统计 |
| `s2500 wd [on\|off]` | 自愈看门狗统计/开关（默认 on，芯片自发复位后 ≤2 s 自动重新初始化） |
| `s2500 selftest` | 上电自检: SPI 寄存器、PHY ID、PCS 环回、PHY 环回、SQI、HDD。**环回测试会短暂扰动链路**, 之后 link 需要几秒恢复 |
| `s2500 reg <mms> <addr> [value]` | 直接读写寄存器, 例如 `s2500 reg 0 0` → `R mms0[0x0000] = 0x00000011` |
| `s2500 sqi` | SQI / SQI+ / TOID |
| `s2500 hdd` | 线束故障检测 (本板 `SHORT_OPEN=2` = 终端过量) |
| `s2500 counters` | MAC 收发计数器 (TX/RX octets, frames, 各长度档, underflow, collision, fcserr...) |
| `s2500 plca id <id> ncnt <n> totmr <n> en <0\|1>` | 运行期改 PLCA (协调者测试就是用它) |
| `netif` | 板名 / MAC / IP / mask / gw / link / plca id |
| `ping <ip> [count]` | 板子发 ICMP echo |
| `iperf -s` / `iperf -c <ip>` | lwiperf TCP 服务端 / 客户端 (端口 5001) |
| `udpecho on\|off` | UDP 回显 (端口 7) |
| `tcpstats` | lwIP 统计 |
| `reboot` | 软件复位 |

---

## 8. 实测数据 (2026-11-20)

测试机: 主机 192.168.10.10 (USB-10T1S, PLCA 协调者 0, NCNT=8, TOTMR=32),
被测板 192.168.10.11 (follower id=1), 总线空闲, 测量用 `flock /tmp/s2500_perf.lock` 串行。

| 项 | 结果 | 备注 |
|---|---|---|
| 编译 | FLASH 96284 B (9.18%), RAM_D1 85992 B (26.24%), RAM_D2 192 B | `./build.sh` Release |
| SWD 烧写 | openocd 0.12 + `stlink.cfg` + `stm32h7x.cfg` @4MHz, Verify OK | 目标电压 3.38V, Cortex-M7 r1p2 |
| 芯片识别 | IDVER=0x00000011, PHYID=0x180FF411, MAC 44:9F:79:29:F9:8E | `s2500 info` |
| 自检 | SPI/PCS 环回/PHY 环回 全 PASS, SQI 4/7, SQI+ 151/255, HDD SHORT_OPEN=2 | `s2500 selftest` |
| PLCA (follower) | en=1 id=1 ncnt=8 PST=1, plca diag=0 | |
| **ping 主机→板** | **10/10, 0% 丢包, RTT min/avg/max = 0.529/0.621/0.934 ms** | `ping -c 10 192.168.10.11` |
| **iperf2 下行** (主机→板) | **9.07 Mbit/s** (8.75 MB / 8.09 s), 两次完全一致 | 板 `iperf -s`, 主机 `iperf -c 192.168.10.11 -t 8` |
| **iperf2 上行** (板→主机) | **8.87 Mbit/s** (10.6 MB / 10.0 s) | 主机 `iperf -s`, 板 `iperf -c 192.168.10.10` |
| **bench 纯发送** | **826.4 fps, L2 9.71 Mbit/s, UDP(payload) 9.44 Mbit/s** (500 × 1470B, 604ms) | `bench 500 1428`, 链路 UP, tx_dropped +0 |
| bench 短包 | 100 × 1470B → L2 9.83 Mbit/s | 线速上限 10 |
| SPI 事务 | 12B 控制事务 **6.00 us** (25MHz, 理论 3.84us); DMA 占比 100%, 回退 0, 超时 0 | `spi bench 1000` / `spi` |
| SPI 25MHz vs 12.5MHz | 默认 TCP 窗口下下行吞吐两者都是 7.44 Mbit/s → **SPI 不是瓶颈**; 25MHz 的价值是把 68B 数据块的 SPI 时间从 43.5us 压到 21.8us (理论), 给 RX 留更多余量 | |
| SPI 占用率 | 空闲时约 88% | 驱动在主循环里连续读 `BUFSTS` 轮询收包 (一次 12B 控制事务 ~6us), 所以 SPI 几乎一直在忙; 这是为了拿到最低收包时延, 属正常 |
| 缓存 | I-Cache/D-Cache 全开, DMA 缓冲在 RAM_D2 非缓存区 | `spi` 命令第 2 行 |
| 复测 (冷启动后) | ping 10/10 0% (0.561/0.627/1.016 ms), 下行 9.07, 上行 8.87, bench 9.71 Mbit/s, `fcserr=0 overflow=0 underflow=0` | 交付固件 (`c689b60`) 同一份二进制 |

### 8.1 下行吞吐为什么要调 TCP 窗口 (重要)

下行一开始只有 **7.44 Mbit/s** (上行 8.87)。把 SPI 从 12.5MHz 提到 25MHz **完全没变化**,
说明瓶颈不在 SPI, 而在 TCP: lwIP 默认 `TCP_WND = 2*MSS = 2920 B`,
一次只能在途两段, 主机每发 2 段就要等一个 ACK 往返 (总线上 ~0.3-0.6ms),
于是吞吐 ≈ 窗口 / RTT, 被压在 7.4。

本工程的 CMake 选项 `S2500_TCP_WND` 实测:

| TCP_WND | 下行 Mbit/s |
|---|---|
| 2920 (2*MSS, common 默认) | 7.44 |
| 5840 (4*MSS) | 8.80 |
| **8760 (6*MSS) ← 默认** | **9.07** |
| 11680 (8*MSS) | 9.07 (无进一步收益) |

> 这个覆盖只影响 H723 工程 (通过 `target_compile_definitions`), 没有改 `common/lwip-port/lwipopts.h`,
> 其它平台行为不变。若要跨平台完全一致, 用 `-DS2500_TCP_WND=0` 关掉, 代价是下行回到 7.44。

---

## 9. PLCA 协调者 (ID=0) 测试

```bash
cd t30hm1ts2500
sudo ./host/coord_test.sh h723     # 主机自动降为 follower 7, 板子当 ID=0 协调者, 结束自动恢复
```

2026-11-20 15:37 实测 (`<workdir>/coord_h723_20261120-153735.log`):

| 步骤 | 结果 |
|---|---|
| 主机降为 follower 7 | `local node ID: 7 (follower)`, PLCA status: off |
| 板子切协调者 | `s2500 plca id 0 ncnt 8 totmr 32 en 1` → `PLCA id=0 ncnt=8 totmr=32 en=1` |
| 主机 → 板 ping ×20 | **20/20, 0% 丢包**, RTT 0.544/0.574/0.605 ms |
| 板 → 主机 ping ×20 | **20/20 全回**, 0-1 ms |
| 下行 TCP (板作协调者) | **9.07 Mbit/s** (8.74 MB / 8.09 s) |
| 上行 TCP | **8.87 Mbit/s** (10.6 MB / 10.0 s) |
| 板端计数器 | `single_col=10741`, `fcserr=0`, `overflow=0`, `underflow=0` (单板协调者 + 过量终端时碰撞计数上升属正常) |
| 恢复 | 主机回到 `node ID 0 (coordinator)`, 板子回 id=1, ping 3/3 0% |

注意: 协调者模式下 `s2500 status` 里的 `link` 可能仍显示 DOWN
(S2500 在协调者模式主要靠自己的 BEACON 工作, 不依赖收到对端信号),
**判据以 ping/iperf 为准**, 这一点在协调者测试里已被 20/20 ping + 9.07 Mbit/s 证实。

---

## 10. 排错表

### 10.1 编译 / 烧写

| 现象 | 原因 | 处理 |
|---|---|---|
| `arm-none-eabi-gcc: not found` | 没装工具链 | `sudo apt install gcc-arm-none-eabi` |
| `error: 'DMA_SxCR_CHSEL_Pos' undeclared` | 在抄 F4 的 DMA 代码 | H7 的请求号写 `DMAMUX1_Channelx->CCR`, SxCR 里没有 CHSEL |
| `error: 'EXTI5_IRQn' undeclared` | H7 的 EXTI5..9 共用向量 | 用 `EXTI9_5_IRQn` / `EXTI9_5_IRQHandler` / `EXTI_PR1_PR5` |
| `error: 'UART_InitTypeDef' has no member 'Prescaler'` | H7 的字段叫 `ClockPrescaler`, FIFO 在 handle 里 | `huart.Init.ClockPrescaler`, `huart.FifoMode` |
| **SWD 连不上** (`Error: open failed` / `target not halted`) | ① USB 被别的进程占用 ② 板子没供电 ③ 固件把 SWD 引脚复用了 ④ 芯片进了低功耗 | ① `lsusb \| grep 0483` 看是否只有 1 个 STLINK-V3, 关掉其它 openocd ② 量 3V3, 目标电压应 3.3V 左右 ③ `openocd ... -c "init; reset halt"` 用复位拉回; 实在不行接 BOOT0=1 上电再连 ④ `./flash.sh -s 1000` 降速再试 |
| `Unable to match requested speed 4000 kHz, using 3300 kHz` | STLINK-V3 的档位限制 | 只是提示, 不影响; 想消除就 `./flash.sh -s 3300` |
| openocd 报 `Adding extra erase range` | 镜像末尾不是扇区边界 | 正常, 忽略 |
| 烧完没反应 | 忘了复位 / 串口接错 | `./flash.sh` 自带 `reset run`; 确认串口是 STLINK-V3 的 VCP (`/dev/ttyACM0`) |

### 10.2 S2500 / 链路

| 现象 | 原因 | 处理 |
|---|---|---|
| `s2500 info` 读不到 / IDVER ≠ 0x00000011 | SPI 线序、CS、供电、模块复位 | ① 对照第 1 节接线表逐根查 (尤其 CS=PG10, MISO=PG9) ② 量模块 3V3 ③ `reboot` 再看 ④ `spi dma off` 走轮询排查 DMA ⑤ `spi speed 6` 降到 6.25MHz 试 |
| `s2500 info` 全 0xFFFFFFFF | MISO 悬空 / 没供电 / CS 没拉低 | 查 MISO 焊接、模块供电、PG10 |
| 数据偶尔错位一字节 | RX FIFO 里上一次的残留 | 本工程每次事务前都会清 RX FIFO; 若自己改驱动别漏掉这步 |
| `link: DOWN` | ① 主机不是协调者 ② 别的板子在做协调者测试 (主机被降级) ③ 物理断线/终端问题 ④ 刚做完 `s2500 selftest` | ① `sudo ethtool --get-plca-cfg enx001ec0d1c337` 看 node ID 是不是 0 ② 等 30-60s 或问一下谁在测 ③ `s2500 hdd` 看 SHORT_OPEN/1W_SHORT ④ 等 2-5s 自动恢复 |
| `ping 不通` (link 却 UP) | IP 冲突 / 目的 MAC 学不到 / PLCA id 冲突 | ① `netif` 确认 IP 是 192.168.10.11 ② 全网 id 必须唯一 (本板 1) ③ 主机 `arp -n` 看是否有条目 ④ 板端 `ping 192.168.10.10` 反向验证 |
| `ping 通但丢包` | 信号完整性 (终端过量), 或别家板子在刷流量 | `s2500 counters` 看 `fcserr/align/symbol` 是否增长; 降 `spi speed 12` 做 A/B; 错开测量时间 |
| `s2500 counters` 里 `underflow` 增长 | 驱动写 TX 太快, MAC 来不及 | 本工程 `underflow=0`; 若增长, 检查是不是关掉了 `wait tx credits` |
| `HDD SHORT_OPEN=2` | 每块板都焊了两组 49.9Ω, 多板并联 = 终端过量 | **已知硬件现状**, 不修; 影响是信号余量变小 |
| SQI 读到 0/7 | SQI 是"最近一次收包"的质量, 没流量时读 0 | 先 ping 一下再读; 自检里测到 4/7 |
| `link: DOWN` 但 ping 通 / iperf 满速 | 观测到的现象: ① 上电/复位后 LKST 需要约 5-10s 才 UP; ② 连续高速发送 (bench) 期间 LKST 会瞬时变 DOWN (终端过量 → `single_col` 计数上升), 发送结束又恢复 | **判据用 ping + `s2500 counters`**: 只要 `fcserr/overflow/underflow` 不涨、ping 0% 丢包, 就是正常的; 不要只看 LKST 一个位 |
| 上电后头几秒 ping 不通 | PLCA follower 需要先收到若干 BEACON 才 LKST=UP | 等 5-10s; 生产上用 `ping` 重试或 `link watchdog` (本工程故意不做自动重配, 见下) |

### 10.3 吞吐低

| 现象 | 排查顺序 |
|---|---|
| 下行 < 8 Mbit/s | ① `spi` 看 SCLK 是否 25MHz、DMA 占比是否 100%、回退/超时是否 0 ② 把 `S2500_TCP_WND` 调到 8760 (第 8.1 节) ③ `spi speed 25` vs `12` 做 A/B (若完全不变, 瓶颈不在 SPI) ④ `s2500 counters` 看 fcserr/overflow ⑤ 确认主机侧没有别家在跑测试 (`flock /tmp/s2500_perf.lock`) |
| 上行 < 8 Mbit/s | ① `bench` 能到 9.7 说明发送通路没问题, 看 TCP_SND_BUF/`tcpstats` ② 主机 `iperf -s` 的窗口 (Linux 默认够) ③ PLCA 配置 (NCNT/TOTMR 不要乱改) |
| `spi` 里 平均耗时 明显 > 理论值 | ① 是否 `DMA 占比 100%` ② 若 12B 事务 > 12us, 检查是不是在 port.c 里又等了 EOT (见 6.2 #2) ③ 超时次数非 0 时看 `DMA DBG` 那行的 LISR/HISR/SR/CFG1 |
| 一开始能跑, 一段时间后变慢 | 看 `spi` 的 `回退` 次数是否增加 (DMA 出错后永久回退轮询, 这是有意的保护) |

### 10.3.1 为什么固件里没有"链路掉线自动重配"看门狗

协调者测试里板子会临时被设成 `id=0`, 而协调者模式下 LKST 本身就可能是 DOWN (见第 9 节)。
如果固件因为 "link DOWN 超过 N 秒" 就自动把 PLCA 重配回 `id=1`, 会**直接破坏协调者测试和现场调试**。
所以这里刻意只做"状态报告", 恢复动作留给人和 `host/coord_test.sh` 的 `restore()`。
需要用的时候有现成命令 (见下面的恢复清单)。

### 10.4 现场恢复清单

```bash
# 1) 板子卡住 / 状态不明
../tools/s25.py --auto h723 -c "reboot"

# 2) 链路/PLCA 状态混乱 -> 恢复默认 follower
../tools/s25.py --auto h723 -c "s2500 plca id 1 ncnt 8 totmr 32 en 1"

# 3) 主机被误设为 follower -> 恢复协调者 (别人做协调者测试后)
sudo ethtool --set-plca-cfg enx001ec0d1c337 enable on node-id 0 node-cnt 8 to-tmr 32

# 4) 完全重来
cd stm32h723 && ./build.sh && ./flash.sh
```

---

## 11. 与 STM32F407 工程的差异 / 移植清单

STM32F407 历史工程仅保留记录（板子损坏已停用, 未随本目录发布）；两个工程的 **目录结构、shell 命令、s2500 驱动接口完全一致**,
差异集中在 `Core/Src/board.c` + 链接脚本 + 工具链文件:

| 项 | STM32F407 | STM32H723 (本工程) |
|---|---|---|
| 内核 / 主频 | Cortex-M4F 168MHz | Cortex-M7 550MHz (VOS0) |
| SPI | SPI3 @21MHz (APB1/2) | SPI1 @25MHz (内核时钟 = PLL2P 200MHz / 8) |
| 片选 / 中断 | PA15 / PD0 (EXTI0) | PG10 / PD5 (EXTI9_5) |
| Cache | 无 | I-Cache + D-Cache, MPU 非缓存区 (第 6 节) |
| 内存布局 | 单块 128KB SRAM | DTCM 128K(栈) + RAM_D1 320K(数据) + RAM_D2 32K(DMA) |
| DMA 请求号 | `SxCR.CHSEL` | `DMAMUX1_Channelx->CCR` |
| SPI 启动传输 | `SPE=1` 即可 | `SPE=1` + `CR1.CSTART=1` (**必踩**) |
| 传输结束判断 | `SR.BSY` | `SR.EOT` / `SR.TXC` |
| 波特率分频 | `CR1.BR` | `CFG1.MBR` |
| 时钟配置 | 8MHz HSE → PLL | 25MHz HSE → PLL1/PLL2 + VOS0 |
| 下行吞吐 | — | 9.07 Mbit/s (TCP_WND=8760) |

移植到另一块 H7 板 (比如 H743/H750) 时要改的只有:
`Core/Inc/board.h` 的引脚宏、`board.c` 的 PLL 参数/VOS 档位 (H743 是 480MHz 主频)、
`STM32H723XG_FLASH.ld` 的容量、`CMakeLists.txt` 的 `STM32H723xx` 型号宏。

---

## 12. 许可与出处

* S2500 驱动 / lwIP / letter-shell: 见仓库根目录 LICENSE 与各子目录说明。
* HAL/CMSIS: STMicroelectronics, 从 STM32CubeMX 生成的 H723 工程拷入, 未改动。
* `STM32H723XG_FLASH.ld` 基于 ST 模板改写 (内存布局 + `.dma_buffer` 段), 见文件头注释。
