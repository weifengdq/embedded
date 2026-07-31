# TC387 + lwIP GETH 千兆 iperf 吞吐优化工程

在 Infineon **AURIX TC387** 上，基于 iLLD + lwIP（bare-metal，`NO_SYS=1`）实现的 **千兆以太网 iperf TCP server**。
原始 Infineon 示例实测约 **425 Mbit/s**，本项目通过一系列软硬件调优将其稳定提升到 **~945 Mbit/s**（千兆线速在 1460 字节 MSS 下的 TCP 理论上限），并修复了长时测速时 `iperf` 报告数据溢出的问题。

---

## 1. 硬件与环境

| 项目 | 说明 |
|------|------|
| MCU | Infineon AURIX TC387，4× TriCore @ 300 MHz（iperf 运行于 Core0） |
| MAC/PHY | 片内 GETH（千兆 MAC） + 外部 RTL8211F，RGMII，1000BASE-T |
| 编译器 | **TASKING v6.3r1** 与 **TriCore GCC 11.3.1** 均已验证可编译/下载/运行；TASKING 实测 ~945 Mbit/s，GCC 实测 ~660 Mbit/s（GCC 在 TriCore 上的代码生成效率略低，详见 §3.8） |
| 协议栈 | lwIP（raw API，`NO_SYS=1`，纯轮询数据通路） |
| 板端 IP | `192.168.0.100/24`，网关 `192.168.0.1`，MAC `DE:AD:BE:EF:FE:ED` |
| 服务端口 | iperf TCP server：**5001**；UDP 诊断服务：**5002**；UDP 黑洞（纯 RX 测速）：**5003** |

> 时钟关系：iperf 跑在 Core0 上，CPU 频率 300 MHz；`fGETH`（GETH 内核时钟，驱动 DMA/MTL 引擎）本工程被提升到 **300 MHz**（见 §3.2），`fSRI` 同为 300 MHz。

---

## 2. 构建与烧录

工程仅通过 `build.ps1` 构建（不手动修改 `build/` 目录）：

```powershell
# 编译（TASKING，Release）
.\build.ps1 -Compiler tasking -BuildType Release

# 烧录（通过调试器下载到板子）
.\build.ps1 -Compiler tasking -Action download

# 编译（TriCore GCC，Release；gcc 为默认编译器，可省略 -Compiler）
.\build.ps1 -BuildType Release

# 烧录（GCC）
.\build.ps1 -Action download
```

> **GCC 链接注意**：GCC 链接阶段会在 `%TEMP%` 写入响应文件；若 `TEMP` 指向
> `C:\WINDOWS\TEMP` 等受限路径，可能报 `cannot find @C:\WINDOWS\TEMP\...`。
> 可将 `TEMP`/`TMP` 指向普通路径后再构建，例如 `set TEMP=C:\tmp`。

构建产物（`.elf` / `.hex`）位于 `build/tasking/`。

### PC 端测速

使用 `tools/iperf.exe`（经典 iperf 2.x，与 lwIP `lwiperf` server 兼容）：

```text
iperf -c 192.168.0.100 -p 5001 -t 20
```

板端会在串口（115200，ASCLIN4 / P00_9 TX、P00_12 RX）打印：

- 启动后打印网卡 MAC 与静态 IP/掩码/网关：

```text
LWIP   : MAC 00:11:22:33:44:55
LWIP   : IP  192.168.0.100
LWIP   : NM  255.255.255.0
LWIP   : GW  192.168.0.1
```

- 网口 link 状态变化（插拔）时打印（首次轮询必报一次当前状态）：

```text
LINK   : UP   1000M full-duplex
LINK   : DOWN
```

- iperf 测速过程中打印（修复 uint32 溢出后）：

```text
IPERF report: type=0, remote: 192.168.0.2:9395, total bytes: 236380184, duration in ms: 2000, kbits/s: 945520
```

---

## 3. 关键修改与优化原理

### 3.1 纯轮询数据通路（无 RX/TX DMA 中断）

`Ifx_Lwip_pollReceiveFlags()` / `Ifx_Lwip_pollTimerFlags()` 在 `core0_main()` 的 `while(1)` 主循环中持续轮询 DMA 接收环与协议栈定时器。
- 避免中断上下文切换与栈帧开销，单核即可跑满千兆；
- 所有协议处理（TCP 收发、重传、流控）在主循环内顺序完成，确定性好。

### 3.2 将 `fGETH` 内核时钟从 150 MHz 提升到 300 MHz（决定性修复）

iLLD 默认 `GETHDIV=2`，即 `fGETH = 150 MHz`。GETH 内核时钟驱动 DMA 与 MTL FIFO 引擎。
实测在 150 MHz 下，即使 CPU 几乎空闲（busy≈50%），**RX DMA 排空 MTL FIFO 的速率仍被钳制在 ~730 Mbps**，并触发 802.3x PAUSE 反压。

在 `netif.c` 的 `low_level_init()` 中，于 `IfxGeth_enableModule()` 之前调用：

```c
/* Raise fGETH from the iLLD default 150MHz (GETHDIV=2) to
 * 300MHz (GETHDIV=1, synchronous to fSRI/fSOURCE0). The GETH
 * kernel clock feeds the DMA/MTL engines; at 150MHz the RX DMA
 * drain rate capped iperf at ~730Mbps (with PBLX8) even though
 * the CPU was idle. At 300MHz the board sustains ~945Mbps
 * (TCP theoretical max for 1460-byte MSS on GigE) with zero
 * FIFO overflows and zero PAUSE frames. */
{
    extern float32 IfxScuCcu_setGethFrequency(float32 gethFreq);
    IfxScuCcu_setGethFrequency(300000000.0f);
}
```

**这一改动使吞吐从 ~729 Mbps 跃升到 ~945 Mbps，是达成目标的根本原因。**
（注：fGETH 与 fSRI 同频 300 MHz 在本板实测长期稳定；如需绝对保守，可改回默认 150 MHz，此时配合下述 PBLX8 约 729 Mbps。）

### 3.3 DMA 突发加长：`PBLX8 = 1`，`RXPBL = TXPBL = 32`

普通 `PBL=32`（128 B 突发）下，RX DMA 因每次 SRI 事务延迟主导而封顶约 620 Mbps，同时 MTL FIFO 溢出、CPU 空闲。
`PBLX8` 将编程 PBL 放大 8 倍，显著拉长突发、摊薄总线事务开销：

```c
#ifndef GETH_DMA_PBLX8
#define GETH_DMA_PBLX8      1
#endif
#ifndef GETH_DMA_RXPBL
#define GETH_DMA_RXPBL      32
#endif
#ifndef GETH_DMA_TXPBL
#define GETH_DMA_TXPBL      32
#endif
GETH_DMA_CH0_CONTROL.B.PBLX8    = GETH_DMA_PBLX8;
GETH_DMA_CH0_RX_CONTROL.B.RXPBL = GETH_DMA_RXPBL;
GETH_DMA_CH0_TX_CONTROL.B.TXPBL = GETH_DMA_TXPBL;
```

此改动使吞吐从 ~620/425 Mbps 提升到 ~729 Mbps，并清零 FIFO 溢出。

> **重要坑位**：`SYSBUS_MODE.FB = 1`（固定 256-beat 突发，Infineon 千兆应用笔记推荐）
> 在本板（LMU 作为 DMA 缓冲）下会**直接锁死 GETH DMA**——已用 FB+MB / FB 单独 / FB+PBLX8 / 运行时写四种组合验证均挂死。
> 因此保留 `AAL=1 + MB=1`，用 `PBLX8` 而非 `FB` 来加长突发。

### 3.4 802.3x 硬件流控（避免 RX 丢包反压限速）

MTL RX FIFO 仅 8 KB，峰值流量下需要硬件流控让对端暂停发送，否则 FIFO 溢出丢包会让 TCP 重传雪崩。
使能 EHFC 并选用**短** XOFF/XON 阈值与**短** pause 时间，避免反压过度拖慢整条链路：

```c
#define GETH_FC_ENABLE      1       /* 使能 802.3x flow control */
#define GETH_FC_RFA         1       /* XON 触发空闲水位 */
#define GETH_FC_RFD         4       /* XOFF 触发空闲水位 */
#define GETH_FC_PAUSE_TIME  0x1000u /* 512-bit 时间量子，保持较短 */
```

关闭流控实测会从 945 Mbps 崩到个位数 Mbps，证实其必要性。阈值可在运行时通过 UDP 诊断服务调整（见 §5）。

### 3.5 LMU 非缓存视图保证 DMA 一致性

描述符环与 DMA 收发缓冲均放在 **LMU 的非缓存视图（地址前缀 `0xB…`）**，CPU 访存绕过 D-Cache，与 GETH DMA 自然一致，无需手动维护缓存。
收包采用**复制式 RX**：DMA 写入后，lwIP 用 `memcpy` 拷出到协议栈 pbuf，并对源缓冲做 `dcache invalidate` 让突发填充有效。

### 3.6 TX TSO（TCP Segmentation Offload）路径

对大段写（`tcp_write` 长缓冲），`low_level_output()` 走 TSO 路径，由 MAC 硬件按 MSS 自动切分，减少 per-segment 的软件开销，抬升 TX 方向效率。

### 3.7 lwIP 协议栈参数（`Configurations/lwipopts.h`）

| 宏 | 值 | 说明 |
|----|-----|------|
| `TCP_MSS` | 1460 | 千兆下每帧承载最大净荷（MTU 1500 - IP/TCP 头） |
| `TCP_WND` / `TCP_SND_BUF` | 44 × MSS = 64 240 | 大窗口，匹配千兆 BDP，未启用窗口缩放（规避某版崩溃） |
| `PBUF_POOL_SIZE` | 32 | 收包 pbuf 池 |
| `MEMP_NUM_TCP_SEG` | 44 | 并发 TCP 段，匹配窗口 |
| `MEM_SIZE` | 76 KB | lwIP 堆（置于 LMURAM） |
| `CHECKSUM_*`（IP/TCP/UDP） | 0 | 关软件校验和，由 MAC 硬件 COE 完成 |
| `ETH_PAD_SIZE` | 0 | 不填充，省一次拷贝 |
| `LWIP_NOASSERT` / `LWIP_STATS` | 1 / 0 | 发布态去除断言与统计开销 |

### 3.8 GCC（TriCore GCC 11.3.1）移植要点

TASKING 与 GCC 都能产出行得通、跑得满的固件。把原 TASKING 工程迁到 GCC 时修了以下几处：

1. **`#pragma section` 语法差异**：TASKING 允许连续 `fardata/farbss` 不关闭；GCC 要求
   每个命名段用 `#pragma section` 显式关闭后再开下一个。已在 `Ifx_Lwip.c` /
   `IfxGeth_Phy_Rtl8211f.c` 改为「开段 → 声明 → 关段」配对，段名用 GCC 习惯的
   `.lmudata`/`.lmubss`/`.bss_cpu0`/`.text_cpu0`（文件末尾另有 `#pragma section` 关闭 `.text_cpu0`）。

2. **链接脚本段名映射**：GCC 链接脚本 `Lcf_Gnuc_Tricore_Tc.lsl` 按 `.lmudata`/`.lmubss`
   而非 `.data.lmudata`/`.bss.lmubss` 把 GETH DMA 缓冲/描述符映射到 **LMU 非缓存视图**
   （`lmuram_nc`，`0xB…`）。最初误用 `.data.lmudata`/`.bss.lmubss` 导致这些变量落入默认
   `.bss`（DSPR0），`dsram0` 溢出链接失败；改为正确段名后 DMA 一致性得以保证。

3. **Release 开启 LTO**：`CMakeLists.txt` 对 GCC Release 增加 `-flto`，让 lwIP 热路径跨
   编译单元内联。

4. **`%TEMP%` 响应文件蹊跷**：GCC `ld` 在链接时会把超长命令行写入 `%TEMP%` 下的响应文件；
   若 `TEMP` 指向受限目录会报 `cannot find @C:\WINDOWS\TEMP\...`，将 `TEMP`/`TMP` 指向普通
   路径（如 `C:\tmp`）即可。

> GCC 实测 ~660 Mbit/s（20s 稳定），低于 TASKING 的 ~945 Mbit/s。二者共享全部硬件优化
> （fGETH 300MHz、PBLX8、流控等），差距来自 GCC 在 TriCore 上的代码生成/取指效率；
> 若要进一步逼近线速，可将以太网热代码（lwIP/iperf 路径）放到 PSPR/DSPR 零等待内存，
> 但需同步修改启动拷贝表，风险较高，此处未做。

---

## 4. iperf 长时测速 `uint32` 溢出修复（本轮）

### 问题

`lwiperf` 用 `u32_t bytes_transferred` 累计收/发的字节数。在 945 Mbps 下，约 **36 秒**字节数即超过 `uint32_t` 上限（4 294 967 296 ≈ 4.29 GiB），计数器回绕；
随后 `kbits/s = (bytes_transferred / duration_ms) * 8` 用回绕后的值计算，导致报告速率骤降（实际并未下降）。

实测故障样张（修复前）：

```text
... total bytes: 2364719128, duration in ms: 20001, kbits/s: 945840   <- 正确
... total bytes:  433864728, duration in ms: 40001, kbits/s:  86768   <- 溢出，错误
... total bytes: 2217058328, duration in ms:1000007, kbits/s:  17736  <- 溢出，错误
```

（正确值应为持续的 ~945520 kbit/s；回绕后的假字节数被公式当成“全程平均”，所以越长越假。）

### 修复

1. `port/include/arch/cc.h`：新增 64 位整型别名
   ```c
   typedef uint64 u64_t;
   typedef sint64 s64_t;
   ```
2. `src/apps/lwiperf/lwiperf.c`：结构体字段 `u32_t bytes_transferred` → `u64_t bytes_transferred`；
   带宽计算改为 64 位、先乘后除以保精度并避免溢出：
   ```c
   bandwidth_kbitpsec = (u32_t)((conn->bytes_transferred * 8ULL) / duration_ms);
   ```
3. `src/include/lwip/apps/lwiperf.h`：报告回调 `lwiperf_report_fn` 的 `bytes_transferred` 参数改为 `u64_t`。
4. `Cpu0_Main.c`：对应报告函数签名改为 `u64_t`，打印字节数用 `%llu`：
   ```c
   Ifx_Lwip_printf("IPERF report: type=%d, remote: %s:%d, total bytes: %llu, "
                  "duration in ms: %"U32_F", kbits/s: %"U32_F"\n",
                  ..., (unsigned long long)bytes_transferred, ms_duration, bandwidth_kbitpsec);
   ```

修复后，40s / 200s / 1000s 等长时测试均稳定报告 **~945520 kbit/s**，与短时测试一致。

---

## 5. UDP 诊断服务（端口 5002）

向板端 `192.168.0.100:5002` 发送命令，板端回显一行实时计数器（无需重新烧录即可调参/观测）：

| 命令 | 作用 |
|------|------|
| `fc <rfa> <rfd> <pauseTime>` | 运行时改写 802.3x 流控阈值；`pauseTime=0` 关闭流控 |
| `rd <offset_hex>` | 读取 GETH 寄存器（基地址 `0xF001D000`，offset < 0x2000），结果在回显 `rdval=` |
| `wr <offset_hex> <value_hex>` | 写 GETH 寄存器（同地址范围） |
| `z` | 清零软件性能计数器 |

回显字段示例（部分）：`rdval`、`rx_ok`、`rx_err`、`hw_fifo_ovf`、`hw_missed`、`txpause`、`rbu`、`dma_miss`、
`busy_t`/`idle_p`（CPU 占用）、`fcpu`/`fstm`/`fsri`（各时钟频率）、`sysbus`/`rxctl`/`txctl`（GETH 寄存器现场）。

此外，**端口 5003** 是一个 UDP 黑洞（仅 `pbuf_free`），可用于在不引入 TCP 开销的前提下，单独测量“DMA + 拷贝 + IP/UDP 解复用”的纯 RX 能力，以定位瓶颈在硬件 DMA 还是软件。

---

## 6. RAM / Flash 使用情况

来自 TASKING Release 构建的链接映射（`build/tasking/tc387_lwip_iperf_gcc2.map`）：

| 区域 | 用途 | 已用 | 容量 |
|------|------|------|------|
| **PFlash（代码 + 只读数据）** | 固件代码 + lwIP 常量 | **≈ 63.6 KiB**（Code 56.9 + RO 6.7） | 12 MiB（4×3 MiB） |
| **DSPR0（CPU0 RAM）** | 栈、BSS、data、CSA 等 | **≈ 141.8 KiB** | 240 KiB |
| **LMU RAM** | lwIP 堆（76 KiB）+ GETH 描述符 + RX/TX 缓冲 | **≈ 116.3 KiB** | 128 KiB |
| UCB | 配置字节 | ≈ 3.9 KiB | — |
| **RAM 合计** | | **≈ 261 KiB** | — |

> 绝大部分动态内存消耗来自 lwIP 协议栈堆（`MEM_SIZE = 76 KiB`，置于 LMURAM）与 GETH DMA 缓冲/描述符；
> 代码镜像极小（<64 KiB），Flash 余量充足，可进一步扩容协议栈堆或增加 pbuf 池。

---

## 7. 测试结果汇总

### 7.1 优化各阶段吞吐（iperf TCP，`-t 20`，Core0）

| 阶段 | 配置 | 吞吐 | 备注 |
|------|------|------|------|
| 基线 | Infineon 原始示例（fGETH 150 MHz，PBL=32） | **~425 Mbps** | 起点 |
| + PBLX8=1 | RXPBL/TXPBL=32 | **~729 Mbps** | FIFO 溢出清零 |
| + fGETH 300 MHz | 内核时钟翻倍 | **~945 Mbps** | TASKING 达成千兆线速（目标 ≥900） |
| 同上述优化（GCC 11.3.1） | `-O3 -flto` | **~660 Mbps** | 与 TASKING 共享全部硬件优化；GCC 代码生成效率略低 |

### 7.2 长时稳定性（修复 uint32 溢出后）

| 时长 | 字节数 | kbits/s | 结论 |
|------|--------|---------|------|
| 2 s | 236 380 184 | 945 520 | 正常 |
| 5 s | 591 241 240 | 945 792 | 正常 |
| 10 s | 1 182 261 272 | 945 808 | 正常 |
| 20 s | 2 364 719 128 | 945 840 | 正常 |
| 40 s | 4.7+ GiB | **~945 520** | 修复前回绕为 86 768，现已正确 |
| 200 s | 23+ GiB | **~945 520** | 修复前回绕为 86 696，现已正确 |
| 1000 s | 118+ GiB | **~945 520** | 修复前回绕为 17 736，现已正确 |

### 7.3 满速下诊断计数器（iperf 跑满时）

| 计数器 | 值 | 含义 |
|--------|-----|------|
| `hw_fifo_ovf` | 0 | MTL FIFO 无溢出 |
| `txpause` | 0 | 未发出 PAUSE 帧（链路未反压） |
| `rx_err` | 0 | 无描述符错误丢包 |
| `rbu` | 0 | 无接收 buffer 不可用 |
| `dma_miss` | 0 | 无 DMA 漏帧 |
| CPU busy | ≈ 97% | 单核接近跑满，无空闲瓶颈 |
| ping 延迟 | < 1 ms | 链路正常 |

**结论**：在单核 300 MHz 上，TC387 + lwIP 千兆 iperf server 可稳定跑满 ~945 Mbps（千兆 TCP 理论上限），
长时测速在修复 `uint32` 溢出后数据一致可信。

---

## 8. 修改文件清单

| 文件 | 修改要点 |
|------|----------|
| `Libraries/Ethernet/lwip/port/src/netif.c` | `fGETH` 150→300 MHz；`PBLX8=1`+`RXPBL/TXPBL=32`；802.3x 流控；LMU 非缓存一致性；FB=1 锁死的规避说明 |
| `Libraries/Ethernet/lwip/port/src/Ifx_Lwip.c` | 纯轮询 RX、复制式收包 + dcache invalidate、TSO TX 路径；GCC `#pragma section` 开/关配对与 `.lmudata`/`.lmubss`/`.bss_cpu0`/`.text_cpu0` 段名 |
| `Libraries/Ethernet/lwip/port/src/IfxGeth_Phy_Rtl8211f.c` | GCC `#pragma section` 配对（`.text_cpu0`） |
| `Libraries/Ethernet/lwip/port/include/arch/cc.h` | 新增 `u64_t` / `s64_t` 别名 |
| `Libraries/Ethernet/lwip/src/apps/lwiperf/lwiperf.c` | `bytes_transferred` 改 `u64_t`；64 位带宽计算（先乘后除） |
| `Libraries/Ethernet/lwip/src/include/lwip/apps/lwiperf.h` | 报告回调字节数参数改 `u64_t` |
| `Configurations/lwipopts.h` | `TCP_MSS=1460`、窗口/段数/pbuf 池/堆、硬件校验和 |
| `Cpu0_Main.c` | iperf TCP server（5001）、UDP 诊断服务（5002）、UDP 黑洞（5003）、64 位报告打印；诊断增加 `fgeth` 字段 |
| `Lcf_Gnuc_Tricore_Tc.lsl` | `.lmudata`/`.lmubss` 映射到 LMU 非缓存视图 `lmuram_nc`（DMA 一致性） |
| `CMakeLists.txt` | GCC Release 增加 `-flto`（链接与编译） |
| `cmake/tricore-gcc-toolchain.cmake` | TriCore GCC 工具链定义（已含，未改） |

> 其他文件（如 `Cpu1/2/3_Main.c`、`Lcf_*.lsl`、链接脚本与 iLLD 库）为 Infineon 官方示例原样保留，无需改动。
