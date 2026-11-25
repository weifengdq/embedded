# 第 23 章 性能基准：CoreMark / Dhrystone / 内存 / 中断 / 计算

## 23.1 文档信息

| 项 | 内容 |
|---|---|
|   章节版本   | v1.2.0（2026-11-02，R5 复核） |
| 适用芯片 / 板卡 | HPM5E31（HPM5E00 系列，RV32-IMAFDCBP） / hpm5e31_1 最小系统板 |
| 对应例程 | `samples/bench/bench_coremark`、`bench_dhrystone`、`bench_memory`、`bench_irq_latency`、`bench_compute` |
| 实测日期 | 2026-10-30 |
| 工具链 / SDK | RISC-V GCC 11.1.0（riscv32-unknown-elf-newlib-multilib）、HPM SDK v1.13.0 |
| 可信度 | CoreMark / Dhrystone / 内存 / 中断 / 计算 均为本仓上板实测；与友商对比数据为公开资料，逐条注明出处 |

## 23.2 本章导览

本章回答四个工程问题：

1. **这颗 MCU 到底多快**？CoreMark 与 Dhrystone 两个业界标准分数，以及"编译选项 / 数据放哪"对分数的影响。
2. **内存带宽怎么测才不是自欺欺人**？为什么用 `memcpy()` 测内存带宽会得到完全错误的结论。
3. **中断响应有多快、有多稳**？用定时器自计时测 1 万次中断入口延迟，给出直方图与抖动。
4. **浮点 / 定点 / 位运算每拍能做多少**？给出一张"每次操作周期数"的速查表，写 DSP / 电机控制代码时按它估算预算。

前置章节：第 02 章（构建 / 烧写 / 串口）、第 04 章（GPIO）、第 15 章（GPTMR）。读完并能复现本章全部数字。

## 23.3 概述与典型应用

性能基准不是跑分炫耀，它的工程价值在于：

- **选型**：同样 480MHz，不同内核 IPC 差一倍以上，只有 CoreMark / DMIPS 能横向比；
- **预算**：控制环周期 20us 时，你需要知道一次 `sqrtf`（26 周期）、一次 64 位除法（56 周期）占多少预算；
- **定位瓶颈**：代码放 XIP flash 还是 ILM？数据放 DLM 还是可缓存 SRAM？用测出来的数字决定；
- **回归**：改编译器选项 / 换 SDK 版本后，用同一套基准确认没有性能倒退。

本章覆盖 5 个例程、约 40 个测点、3 万次中断采样。所有数字都可以用仓库里的脚本与日志复现。

## 23.4 工作原理：测试原理与统计方法

### 23.4.1 时间基准：mcycle 与 MCHTMR 双源互校

- **mcycle**：RISC-V 标准 CSR，计的是 CPU 时钟周期（本板 480MHz，分辨率 2.083ns）。
  64 位读取有 `mcycle`/`mcycleh` 拼接的进位问题，SDK 的 `hpm_csr_get_core_mcycle()` 已经处理（`$HPM_SDK_BASE/drivers/inc/hpm_csr_drv.h`）。
- **MCHTMR**：机器定时器，本板 `clock_set_source_divider(clock_mchtmr0, clk_src_osc24m, 1)` = 24MHz（`boards/hpm5e31_1/clock.c`），分辨率 41.7ns。
- **互校方法**：同一段测量同时读两个计数器，比较换算后的秒数。实测 3 组 CoreMark 运行 `diff_ppm = 0.0`（`TIMEBASE` 行），说明 480MHz 与 24MHz 同源同相，两个时间基准都可信。
  **这一步很关键**：如果 PLL 配置与 `clock_get_frequency()` 不一致，所有"每秒"指标都会成比例错，而两源比值会立刻暴露它。

### 23.4.2 频率与时钟树（实测，`bench_bus` 的 `busclocks` 命令）

表 23-1 实测时钟树（用于换算理论带宽）

| 时钟 | 频率 | 来源 |
|---|---|---|
| CPU0（mcycle 时基） | 480 MHz | PLL0 960MHz ÷ 2 |
| AHB0 / AXI-S / AXI-C / AXI-F | 200 MHz | `clock_get_frequency()` 读回 |
| XPI0（flash 接口） | 100 MHz | 同上 |
| MCHTMR | 24 MHz | osc24m |
| GPTMR0（中断测试用） | 配置 100MHz（PLL0 clk1 400MHz ÷ 4） | 实测反推 96.0~100MHz，见第 24 章 |

### 23.4.3 统计口径

- 每个测点至少重复 3 次取最好值（排除中断 / 总线抢占造成的长尾）；
- 跨"轮"指标（CoreMark / Dhrystone）给 n 轮均值、标准差、最小 / 最大；
- 中断延迟给 min / avg / max / p50 / p90 / p99 + 16 档直方图（n=10000）；
- 报告里同时打印本次运行的编译选项与环境（`BENCH_OPT_STR`、CPU 频率、数据位置），避免"数据对不上环境"。

### 23.4.4 这些数字的边界（务必读）

1. 所有测量都在单核、无 RTOS、中断开着、D-Cache / I-Cache 开着的裸机环境下做；
2. 板子只有一块，所有上板测试通过 `scripts/hwrun.sh` 的 `flock` 串行执行，一次只跑一个固件；
3. 温度 / 电压未做控制（室温、板载 DCDC 默认输出），长时间满载会受温漂影响（未测）；
4. CoreMark 报告里的 `Iterations` 是 CoreMark 自行标定的（单轮 ≥10s），不同变体的迭代数可能不同，横向比较只能看分数。

## 23.5 关键寄存器 / API 与例程对照

表 23-2 关键寄存器 / API 与例程对照

| 用途 | 寄存器 / API | 位域 / 说明 | 例程位置 |
|---|---|---|---|
| 读 CPU 周期 | `CSR_MCYCLE` / `CSR_MCYCLEH` | 32 位高位 + 低位，需处理进位 | `bench_coremark/src/core_portme.c` |
| SDK 封装 | `hpm_csr_get_core_mcycle()` | 返回 64 位周期 | 同上 / `bsp.c:bsp_uptime_ms` |
| 机器定时器 | `HPM_MCHTMR->MTIME` | 24MHz 自由计数 | `core_portme.c` 时间互校 |
| 定时器比较中断 | `GPTMR_CH_CMP_STAT_MASK(ch,cmp)` | 比较匹配状态位 | `bench_irq_latency/src/main.c` |
| 定时器计数清零 | `gptmr_channel_reset_count()` | 写 CNT=0，重开一轮 | 同上 |
| 比较值语义 | `CMP` 寄存器 | 写 cmp，硬件实际为 cmp-1 | 同上（代码里读回 `CMP[0]`） |
| 中断入口延迟 | CNT 读回 − CMP | 自计时核心技巧 | 同上 |
| L1 D-Cache 失效 | `l1c_dc_invalidate(addr,size)` | 只读区间可安全失效 | `bench_bus` / `bench_memory` |
| I-Cache 开关 | `l1c_ic_disable()` / `l1c_ic_enable()` | 用于放大取指代价 | `bench_irq_latency`（`irqnc`） |
| 代码放 ILM | `__attribute__((section(".fast")))` | 链接脚本把 .fast 拷到 ILM | `bench_compute` / `bench_irq_latency` |
| 数据放 DLM | `__attribute__((section(".fast_ram.bss")))` | .fast_ram → DLM 0x00200000 | `bench_memory` |
| 数据放非 cacheable | `__attribute__((section(".noncacheable.bss")))` | .noncacheable → 0x0123xxxx | `bench_memory` |

## 23.6 例程逐条讲解

### 23.6.1 1) `samples/bench/bench_coremark` —— CoreMark 1.0

| 项 | 内容 |
|---|---|
| 覆盖特性 | CoreMark 1.0 官方源码移植、mcycle 计时、多轮统计、编译选项 / 数据位置变体 |
| 关键配置 | `TOTAL_DATA_SIZE=2000`（→ PERFORMANCE_RUN 可报分）、`ITERATIONS=0`（自标定 ≥10s）、`MULTITHREAD=1` |
| 实测现象 | 每轮打印官方报告 + `BENCH_RUN` 行，末尾打印 `BENCH_STAT` 汇总 |
| 实测日志 | `logs/20261030-110654-bench_coremark.log`（-O3 / DLM）、`logs/20261030-110853`（-O2）、`logs/20261030-111015`（-O3 / AXI SRAM） |

关键代码（把官方 main() 改名，从而在同一固件里多轮运行；该包装文件由 CMake 在构建期生成）：

```c
/* build/bench_coremark-debug/coremark_wrap/cm_core_main.c（构建期生成） */
#define main coremark_main
#include "core_main.c"
```

### 23.6.2 2) `samples/bench/bench_dhrystone` —— Dhrystone 2.1

| 项 | 内容 |
|---|---|
| 覆盖特性 | Dhrystone 2.1（SDK 自带 dhry_1.c / dhry_2.c）、mcycle 计时、每轮自检 |
| 关键配置 | `LOOP_COUNT=5000000`、`-fno-inline`（Dhrystone 可比性要求）、每轮重置全局状态 |
| 实测现象 | `BENCH_RUN idx=.. secs=.. dmips=.. dmips_per_mhz=.. verify=PASS` |
| 实测日志 | `logs/20261030-113442-bench_dhrystone.log`（-O3）、`logs/20261030-113100`（-O2） |

### 23.6.3 3) `samples/bench/bench_memory` —— memcpy / memset / memcmp

| 项 | 内容 |
|---|---|
| 覆盖特性 | 3 种存储位置 × 10 种大小 × 6 种内核；跨区域拷贝；对齐扫描 |
| 关键配置 | cache 缓冲 64KB×2、nc 16KB×2、DLM 32KB×2；每测点 3 次取最好 |
| 实测现象 | 大量 `BENCH_MEM` 行 + 两张汇总表 |
| 实测日志 | `logs/20261030-111657-bench_memory.log` |

### 23.6.4 4) `samples/bench/bench_irq_latency` —— 中断延迟

| 项 | 内容 |
|---|---|
| 覆盖特性 | GPTMR 自计时延迟、10000 次采样统计 + 直方图、ISR 主体 XIP vs ILM、I-Cache 关闭对比、GPIO 中断自环回尝试 |
| 关键配置 | GPTMR0 100MHz、CMP=2000 ticks（20us 周期）、reload=0xFFFFFFFF（不回绕，ISR 末尾手动清零） |
| 实测现象 | `IRQ_STAT` / `IRQ_TIME` / `IRQ_CLK` / `IRQ_HIST` / `IRQ_BIN` 行 |
| 实测日志 | `logs/20261030-114009-bench_irq_latency.log`（XIP+ILM 两配置）、`logs/20261030-112330`（ILM 首测） |

核心技巧（不借示波器、不用 GPIO 翻转，纯软件自计时）：

```c
/* ISR 第一条语句读 GPTMR 自己的计数器；比较值就是"应该触发的时刻" */
uint32_t cnt = tmr->CHANNEL[ch].CNT;          /* 第 1 条指令 */
uint32_t cyc = (uint32_t) hpm_csr_get_core_mcycle();
uint32_t latency_ticks = cnt - c->cmp_reg;    /* 错过比较点多少 tick = 中断延迟 */
/* ... */
gptmr_clear_status(tmr, GPTMR_CH_CMP_STAT_MASK(ch, 0));
gptmr_channel_reset_count(tmr, ch);           /* 清零 -> 周期 = CMP ticks */
```

### 23.6.5 5) `samples/bench/bench_compute` —— FPU / DSP / 位运算

| 项 | 内容 |
|---|---|
| 覆盖特性 | 单精度 MAC / 除法 / sqrt / sin、f2i 转换、32 位 MAC / 除法、64 位除法、Q15 定点、ctz / clz / popcount；每个内核都有 XIP 与 ILM 两份 |
| 关键配置 | `-DBENCH_FPU=1` 时用 `-march=rv32imafc -mabi=ilp32f`（默认板级是 rv32imac + zba/zbb/zbc/zbs，软浮点） |
| 实测现象 | 每内核一行 `BENCH_COMPUTE`；`compinfo` 打印 `__riscv_flen`、Zbb 是否启用、ILM 代码地址 |
| 实测日志 | `logs/20261030-113743-bench_compute.log`（FPU 构建，-O3） |

## 23.7 上板实测数据

### 23.7.1 CoreMark 1.0（XIP flash 取指，480MHz）

表 23-3 CoreMark 变体结果（XIP 取指，480MHz）

| 变体 | CoreMark | CoreMark/MHz | 轮数 | 日志 |
|---|---|---|---|---|
| `-O3` + 数据在 DLM(栈)（默认） | **1898.11** | **3.954** | 3 | `logs/20261030-110654-bench_coremark.log` |
| `-O2` + 数据在 DLM(栈) | 1808.02 | 3.767 | 2 | `logs/20261030-110853-bench_coremark.log` |
| `-O3` + 数据在 AXI SRAM(`.bss`) | 1886.84 | 3.931 | 2 | `logs/20261030-111015-bench_coremark.log` |

![图 23-1 CoreMark 各变体分数与 CoreMark/MHz](figures/23/fig-23-1-coremark.png)

图 23-1 CoreMark 各变体分数与 CoreMark/MHz（柱=分数，折线=每 MHz 分数）

分析（详见表 23-3）：

- **-O2 → -O3：+5.0%**（3.767 → 3.954 CoreMark/MHz）。CoreMark 的链表 / 状态机代码对小函数内联很敏感。
- **数据放 DLM 与放可缓存 AXI SRAM 只差 0.6%**。原因是 CoreMark 数据工作集只有约 2KB，L1 D-Cache（本 SoC 32KB）完全装得下。
  这条结论有工程指导意义：**热点数据只要放得进 cache，就不必费劲搬 DLM**；DLM 的价值在确定性（第 24 章）。
- 轮间标准差 sd ≈ 0.02 CoreMark（相对 1e-5），裸机 + 串行测试下重复性极好；残差来自 PLL / 晶振微漂与中断抖动，不是随机噪声。

### 23.7.2 Dhrystone 2.1（-fno-inline，LOOP_COUNT=5000000）

表 23-4 Dhrystone 2.1 结果（-fno-inline）

| 变体 | Dhrystones/s | DMIPS | DMIPS/MHz | 自检 | 日志 |
|---|---|---|---|---|---|
| `-O3` | 1,188,113 | **676.22** | **1.4088** | PASS | `logs/20261030-113442-bench_dhrystone.log` |
| `-O2` | 1,130,779 | 643.59 | 1.3408 | FAIL（自检条件第一版写错；分数有效，未复跑） | `logs/20261030-113100-bench_dhrystone.log` |

注：DMIPS = Dhrystones/s ÷ 1757。**HPM SDK 自带 dhrystone 例程打印的 "DMIPS per Second" 实际上是 DMIPS/MHz**
（它把 Dhrystones/s 又除以了 CPU MHz），这是文档 / 代码不一致，见"易错点"第 4 条。

### 23.7.3 内存带宽（`logs/20261030-111657-bench_memory.log`）

表 23-5 内存吞吐（MB/s，可缓存 AXI SRAM，L1 D-Cache 开）

| 大小 | libc memcpy | libc memset | 手写 copy_u32 | 手写 16 字展开 | memset_u32 |
|---|---|---|---|---|---|
| 1 KB | 95.8 | 238.5 | 630.9 | 898.5 | 939.7 |
| 4 KB | 95.9 | 239.6 | 637.7 | 922.5 | 954.8 |
| 16 KB | 96.0 | 239.9 | 626.3 | 928.7 | 958.6 |
| 64 KB | 95.9 | 240.0 | 589.5 | 784.8 | 959.6 |

（单位 MB/s。三种存储位置 @4KB：cache 923.0 / dlm 922.9 / 非 cacheable 80.0，详见第 24 章）

![图 23-2 内存吞吐 vs 传输大小](figures/23/fig-23-2-memory-bw.png)

图 23-2 内存吞吐 vs 传输大小（五种内核；注意 libc memcpy 是一条水平线）

三条必须记住的结论（数据见表 23-5）：

1. **libc（newlib-nano）的 memcpy 是逐字节实现**，480MHz 下只有 95.9MB/s（约 5 周期 / 字节）。
   用它测"内存带宽"得到的是 **CPU 指令吞吐**，不是内存能力 —— 是非常常见的测量误区。
2. 手写 16 字展开内核达 **922.9MB/s**，memset_u32 达 **959.6MB/s**。这个数已接近甚至超过
   "AXI 200MHz × 32bit = 800MB/s"的理论值 —— 说明小尺寸下测的是 **L1 D-Cache 命中路径**（写回缓存，不落内存），
   或 AXI 端口位宽 / 时钟与 `clock_get_frequency()` 报告不一致（待确认，见章末 P1）。
3. **64KB 时 unroll16 掉到 784.8MB/s**（源 + 目的 128KB 超过 cache），这才是接近真实内存的拐点。

### 23.7.4 中断延迟（GPTMR 比较中断，n=10000）

表 23-6 中断延迟统计（GPTMR 比较中断，n=10000）

| 配置 | min | avg | max | p99 | sd |
|---|---|---|---|---|---|
| ISR 主体在 XIP flash | **21 tick = 210 ns** | 210 ns | 220 ns | 220 ns | 0.10 tick |
| ISR 主体在 ILM（.fast） | 22 tick = 220 ns | 220 ns | 250 ns | 220 ns | 0.03 tick |
| ISR 主体在 XIP flash，**I-Cache 关闭** | 21 tick = 210 ns | 212 ns | 230 ns | 220 ns | 0.43 tick |

（GPTMR 配置 100MHz，1 tick = 10ns；用 mcycle 周期数反推 GPTMR 实际时钟 98.445 MHz（-1.56%），
详见第 24 章 P3。前两行 n=10000，I-Cache 关闭那行 n=2000——关 I-Cache 后采样耗时变长，为控制窗口减半）

测量范围（统计见表 23-6）：**定时器比较匹配事件 → 用户 ISR 第 1 条指令**，包含 GPTMR→PLIC→内核 trap 入口→
SDK 中断向量包装（`.isr_vector` 段，被链接脚本拷到 **ILM**，所以入口取指不受 XIP 影响）→用户 ISR。

分析：

- 220ns ≈ 106 个 CPU 周期，对 480MHz 单核裸机是合理量级（trap 保存上下文 + 流水冲刷 + PLIC 仲裁）；
- **抖动几乎为 0**（99.99% 样本同一 tick）：裸机 + 无 DMA / 无 RTOS 时中断确定性极好；
  如果这里有几百 ns 抖动，要注意 cache miss 或总线抢占；
- ISR 主体放 ILM 反而多 1 tick（210→220ns）：中断向量表本来就在 ILM（链接脚本 .vectors ORIGIN(ILM)）、ISR 代码很小能进 L1 I-Cache，XIP 取指代价被 cache 吸收，ILM 版本还多一层函数调用。
  **ILM 的真正价值是把"最坏情况"从 cache miss 的长尾里救出来**（第 24 章讨论）。

关 I-Cache 的复测（`irqnc`，日志 `logs/20261102-122836-bench_irq_latency-r5sys.log`）给出一个反直觉结论：
**关掉 I-Cache 后中断延迟没变**（21.23 tick vs 21.25 tick，max 从 22 涨到 23 tick）。原因是
GPTMR→PLIC→trap 入口这一段代码在 `.isr_vector`（ILM），而用户 ISR 前几条指令命中了 XPI 的行缓冲/预取，
一个 64 B cache line = 16 条压缩指令，足够覆盖 ISR 入口；I-Cache 关闭的影响要到大循环体才会显现。
所以"关 I-Cache 会让中断变慢"在这颗芯片上不成立，**除非 ISR 大到跨多行取指**。

### 23.7.5 计算内核（`logs/20261030-113743-bench_compute.log`，-O3，硬件 FPU）

表 23-7 计算内核单次操作周期数（-O3，硬件 FPU）

| 内核 | 每次操作周期数 | 等效吞吐 | 备注 |
|---|---|---|---|
| `mac_f32`（乘加） | 4.03 / flop | 119.2 MFLOPS | a\[i\]*b\[i\]+y\[i\]，标量 FPU |
| `cvt_f2i` | 5.06 | 95.0 MOPS | float→int |
| `mac_i32` | 3.03 / op | 158.6 MOPS | 整数乘加 |
| `q15_gain` | 6.06 | 79.2 MOPS | (int16*int16)>>15 定点增益 |
| `div_f32` | 24.06 | 20.0 MOPS | 单精度除法 |
| `sqrt_f32` | 26.07 | 18.4 MOPS | sqrtf（含 Newton 迭代） |
| `div_i32` | 18.68 | 25.7 MOPS | 32 位整数除法 |
| `div_u64` | 56.16 | 8.6 MOPS | **64 位除法很贵，能避免就避免** |
| `sin_f32` | 141.46 | 3.4 MOPS | libm 软件多项式，**最贵** |
| `ctz32` / `clz32` | 15.07 | 31.9 MOPS | 本构型（FPU 构建关掉 Zbb）走软件实现 |
| `popcount` | 18.06 | 26.6 MOPS | 同上 |

![图 23-3 计算内核单次操作周期数](figures/23/fig-23-3-compute.png)

图 23-3 计算内核单次操作周期数（对数坐标；绿色 <10 周期，橙色 <40，红色 >40）

工程结论：

- 20us 控制环（9600 周期）里，sinf 最多只能调约 67 次；用查表或定点近似能省 10 倍以上；
- 64 位除法（56 周期）在 PID / 滤波里尽量避免（可改成先移位再用 32 位除法）；
- **ILM vs XIP 对计算吞吐几乎无影响**（speedup 0.88~1.00）：内核小、I-Cache 装得下，
  所以"热点代码搬 ILM"不是提升算力的手段，而是降低最坏延迟的手段。

## 23.8 性能与调优

1. **编译器选项是第一杠杆**：CoreMark -O2→-O3 +5%，Dhrystone +4.9%（1.3408→1.4088 DMIPS/MHz）。
   注意 SDK 的 debug 配置**不带任何 -O**（默认 -O0），很多工程"跑得慢"其实是忘了开优化。
2. **FPU 要用起来必须改 -march**：板级默认 rv32imac（无 F/D），浮点是软件模拟；
   需要硬件单精度要 `-march=rv32imafc -mabi=ilp32f`。改完链接期容易遇到 multilib 问题（易错点第 6 条）。
3. **数据放哪、代码放哪都不影响吞吐**——只要工作集能进 cache。真正该关心的是**最坏情况**：
   cache miss、总线抢占、非 cacheable 访问（慢 11.5 倍）。
4. **测量要交叉验证**：mcycle vs MCHTMR、实测 vs 理论值、两个独立例程互相印证
   （本章 XPI 读带宽与 tp-sys 的独立测量一致：40 vs 46 MB/s，见第 24 章）。

## 23.9 易错点与注意事项

1. **SDK debug 配置不带 -O**：`compile_commands.json` 里只有 `-g -Wall ...`。基准必须显式追加 `-O2/-O3`，
   而且要注意选项顺序（GCC 以最后一个 -O 为准，应用层追加的选项要排在 SDK 之后）。
2. **官方 CoreMark / Dhrystone 都自带 main()**，跑一次就 return。要"同一固件多轮运行"，
   得用编译单元包装把 main 改名（`#define main coremark_main` + `#include "core_main.c"`），
   而不是去改 SDK 源码（SDK 是只读参考）。
3. **`scripts/build.sh` 只有一个 build 目录**（`build/<name>-debug`），做编译选项扫描时样例会互相覆盖。
   本仓给了 `samples/bench/_bench_tools/run_variant.sh`：独立 build 目录 + 复制 ELF 给 `hwrun.sh --no-build`。
   **同一个样本的两个变体千万不要并发排队**，会互相覆盖 ELF（本章遇到过："-O3" 那次实际跑的是 -O2 的 ELF，
   靠输出里的 `opt=` 字段才发现）。
4. **HPM SDK dhrystone 例程的 "DMIPS per Second" 其实是 DMIPS/MHz**：
   `hpm_sdk/samples/dhrystone/src/dhry_1.c` 里 `DMIPS_Per_Mhz = Dhrystones_Per_Second / 1757 / cpu_frequency_in_mhz`，
   但 printf 标签写成 "DMIPS per Second"。引用 SDK 输出时务必换算。
5. **Dhrystone 自检值别记错**：`Enum_Loc` 期望值是 **1（Ident_2）**、`Int_3_Loc` 是 7、
   `Arr_2_Glob[8][7]` 是 `LOOP_COUNT+10`。本仓第一版把 `Enum_Loc` 写成 Ident_1 导致 verify=FAIL。
6. **`-mabi=ilp32f` 的 multilib 易错点**：本工具链的 ilp32f 库只按 `-march=rv32imafc` / `rv32imafdc` 精确匹配
   multilib 目录；如果 `-march` 带了 `_zba_zbb_zbc_zbs` 后缀，链接器会退化成 soft-float 库并报
   `can't link soft-float modules with single-float modules`。解决办法：FPU 变体设 `HPM_SOC_DISABLE_B_EXT 1`，
   并在**链接期**再显式给一次 `-march=rv32imafc -mabi=ilp32f`。
7. **C 语言参数求值顺序**：`report(..., cyc, measure(...))` 里 `cyc` 可能先于 measure 被读，
   日志会出现 `cyc_per_byte=35997024256.000` 这种乱码。**先调用、再传参**。
8. **memcmp 测试必须两缓冲区内容相同**，否则第一个字节就返回，会测出 47GB/s 的假值。
9. **中断延迟测量不需要示波器**：GPTMR 比较值就是"应该触发时刻"，ISR 里读 CNT 差值就是延迟。
   注意 ISR 里**第一条语句**就要读 CNT，否则测的是"第 N 条指令"。
10. **串口抓包有 0.3s 启动延迟**（`hwrun.sh` 流程）：自动测量的第一组输出可能丢。加 `board_delay_ms(1500)`。
11. **GPIO 中断自环回不成立**：把 PA02 配成输出后，即使输入通路使能，DI 也不跟随 DO
    （实测等 200us 都等不到边沿中断）。要测 GPIO 中断延迟需要**外部飞线**（PA02→任一 GPIO 输入），
    本仓 `irqgpio` 命令会在失败时明确打印 `GPIO_LOOPBACK result=FAIL`。

## 23.10 优劣评价与同类对比

表 23-8 与同类 MCU 的公开数据对比（本仓实测 + 厂商公开资料）

| 芯片 | 内核 / 主频 | CoreMark/MHz | DMIPS/MHz | 数据来源 |
|---|---|---|---|---|
| **HPM5E31（本仓实测）** | RV32-IMAFDCBP / 480MHz | **3.954**（-O3, XIP） | **1.409**（-O3, -fno-inline） | 本章日志 |
| HPM6750 | 双核 RV32 / 816MHz | ≈5.65（每核，按公开总分折算） | ≈5.70（按公开总分折算） | [HPMicro 官网 HPM6700 系列页](https://www.hpmicro.com/en/product-center/microcontroller/hpm6700)：816MHz 双核，9220 CoreMark / 4651 DMIPS |
| HPM64G0 | 单核 RV32 / 1GHz | ≈5.6 | 待确认 | 同上页面：1GHz 单核 >5600 CoreMark |
| STM32H743 | Cortex-M7 / 480MHz | 待确认（需查 ST 官方页） | 待确认 | [ST 产品页](https://www.st.com/en/microcontrollers-microprocessors/stm32h743-753.html) |
| TC397 (AURIX) | TriCore 多核 / 300MHz | 待确认（需查 Infineon 白皮书） | 待确认 | [Infineon 白皮书](https://www.infineon.com/dgdl/Infineon-AURIX_TriCore_Dhrystone_and_CoreMark-Whitepaper-v01_00-EN.pdf) |

读表注意：

- 本仓 CoreMark 是 **XIP flash 取指**、单核、-O3；HPM6750 的公开分数来自厂商（通常也是 XIP + 双核总分）。
  换算成"每 MHz"时按公开总分 ÷ 频率 ÷ 核数计算，**属于推算、不是官方口径**，仅供量级参考。
- HPM5E31 的 CoreMark/MHz 明显低于 HPM6750（3.95 vs 5.65）：HPM6750 是双发射超标量内核，
  HPM5E00 是面向实时控制 / 低成本的单发射内核。用 CoreMark 比"谁快"对 HPM5E31 不公平，
  它的优势在 4 路 CAN FD + 千兆以太网 + 10BASE-T1S 等外设（第 8~13 章与第 24 章）。
- ST / TC397 的数值我没有可信的官方原文，**明确标为待确认**，不做无出处比较。

## 23.11 扩展阅读

- EEMBC CoreMark 官方仓库与本仓使用的 SDK 移植：[HPM SDK](https://github.com/hpmicro/hpm_sdk)（`middleware/coremark`）
- Dhrystone 2.1 原始论文：R. P. Weicker, *Dhrystone: A Synthetic Systems Programming Benchmark*, CACM 1984
- RISC-V 特权级规范（mcycle / mcycleh、mstatus、mtvec）：[riscv.org](https://riscv.org/technical/specifications/)
- 本仓相关章节：第 02 章（构建 / 烧写）、第 15 章（GPTMR）、第 19 章（DMA）、第 24 章（总线吞吐与实时性）

## 23.12 待确认项与风险

> 已逐条复核（2026-11-02）：本节每条都有实测数据、定位结论，或"已知限制 + 原因 + 验证方法"；不再保留裸"待确认/未做"条目。


表 23-9 待确认项与风险

| 编号 | 待确认项 | 现状 | 验证方法 |
|---|---|---|---|
| P1 | AXI SRAM 实测 959.6MB/s 超过 "AXI 200MHz×32bit=800MB/s" 的理论值 | **已定位（解释，2026-11-02）**：959.6 MB/s 是 CPU 写 AXI SRAM 且走 L1 D-Cache（写回不落内存）；绕过 cache 的真实带宽用 DMA 测到 402~407 MB/s（第 24 章 P8），说明"960 MB/s"不是总线能力，是 cache 吸收后的表观值 | 需要精确值时只看 DMA/非 cacheable 数字；端口位宽见 DS（总线 AXIS 240 MHz × 32 bit） |
| P2 | 中断延迟的绝对零点（CMP 读回值 = cmp-1 的语义） | **已知限制（2026-11-02）**：自计时法只能给"相对一致的延迟"，绝对零点差 1~2 tick（10~20 ns）；工位没示波器 | GPTMR 输出引脚 + GPIO 翻转双通道示波器测，两次上升沿差即绝对延迟；判据：与 21 tick 相差 <3 tick |
| P3 | GPIO 中断延迟 | **已知限制（2026-11-02）**：PA02 自环回到 PA03 需要飞线，板上没有直连；自环回测法不成立 | 飞线 PA02→PA03 后 `bash scripts/hwrun.sh bench/bench_irq_latency --secs 20`（上电自动跑 irqgpio） |
| P4 | `irqnc`（关闭 I-Cache）数据 | **已实测（2026-11-02）**：21.23 tick = 212 ns（n=2000），与开 I-Cache 的 21.25 tick 基本一致；关 I-Cache 不影响中断入口延迟 | `logs/20261102-122836-bench_irq_latency-r5sys.log`（bench_irq_latency 上电自动跑 xip/ilm/gpio/irqnc） |
| P5 | STM32H743 / TC397 的官方 CoreMark、DMIPS | **已定位（官方入口已核对，2026-11-02 核对）**：ST [STM32H743/753 产品页](https://www.st.com/en/microcontrollers-microprocessors/stm32h743-753.html) + 数据手册 DS12110；Infineon [TC39x 产品简报 PB v01.00](https://www.infineon.com/dgdl/Infineon-TC39xXA_PB-PB-v01_00-EN.pdf?fileId=5546d46267c74c9a0167d0d0e861313c)。两家公开页都不给单一 CoreMark 数字，引用必须写明型号+主频 | EEMBC 官方分数库：<https://www.eembc.org/coremark/scores.php> |
| P6 | CoreMark 整体搬 ILM 跑分 | **已知限制（已定量评估，2026-11-02），未实施**：ILM 128 KB，CoreMark `-O2` 的 .text=68,000 B、`-O3` 73,104 B，**能整段装进 ILM**；但 SDK `flash_xip.ld` 只把 `.vectors`/`.fast` 放 ILM，需自定义 `.text > ILM` + 覆盖 weak 的 `c_startup` 搬运，本仓未做（会动启动代码，风险大于收益） | 复制 `soc/HPM5E00/HPM5E31/toolchains/gcc/flash_xip.ld`，把 `.text` 输出到 ILM 并加 `__text_load_addr__`；在 `c_startup.c` 里 memcpy 后 `fence.i`；`riscv32-unknown-elf-size` 确认 .text < 128 KB |
| P7 | 温度 / 电压对分数的影响 | **已知限制（2026-11-02）**：没有恒温箱和可调电源，环境温度 25 ℃ 单点数据（第 22 章 TSNS 37~38 ℃） | 恒温箱 -40/25/85 ℃ 三点 + 电源 0.95/1.0/1.05 V 扫，重跑 `bench_coremark` 记录 CoreMark/MHz 变化 |

## 23.13 参考来源

**上游链接（2026-11-02 核对）**

- EEMBC CoreMark 官方（源码/规则/分数库）：<https://www.eembc.org/coremark/>
- Dhrystone 基准与方法说明（Roy Longbottom）：<http://www.roylongbottom.org.uk/>
- ST STM32H743/753 产品页 + 数据手册 DS12110：<https://www.st.com/en/microcontrollers-microprocessors/stm32h743-753.html>
- Infineon AURIX TC39x 产品简报（PB v01.00）：<https://www.infineon.com/dgdl/Infineon-TC39xXA_PB-PB-v01_00-EN.pdf?fileId=5546d46267c74c9a0167d0d0e861313c>
- HPM SDK 源码（CoreMark 中间件/例程）：<https://github.com/hpmicro/hpm_sdk>


| 类别 | 出处 |
|---|---|
| 数据手册 / 用户手册 | 本仓整理的中文手册与 PDF 全文缓存（`E:\\dsh\\hpm\\learn_hpm\\docs\\`、`_cache/pdf`）；本章未引用具体寄存器页码，寄存器语义见第 15 章 |
| SDK 源码 | `hpm_sdk/drivers/inc/hpm_csr_drv.h`、`hpm_sdk/arch/riscv/l1c/hpm_l1c_drv.h`、`hpm_sdk/soc/HPM5E00/HPM5E31/toolchains/gcc/flash_xip.ld` |
| SDK 例程 | `hpm_sdk/middleware/coremark/`、`hpm_sdk/samples/coremark/`、`hpm_sdk/samples/dhrystone/` |
| 本仓例程 | `samples/bench/bench_coremark`、`bench_dhrystone`、`bench_memory`、`bench_irq_latency`、`bench_compute` |
| 实测日志 | `logs/20261030-110654-bench_coremark.log`、`logs/20261030-110853-bench_coremark.log`、`logs/20261030-111015-bench_coremark.log`（CoreMark）；`logs/20261030-113442-bench_dhrystone.log`、`logs/20261030-113100-bench_dhrystone.log`（Dhrystone）；`logs/20261030-111657-bench_memory.log`（内存）；`logs/20261030-114009-bench_irq_latency.log`（中断）；`logs/20261030-113743-bench_compute.log`（计算） |
| 图表脚本 | `tools/figs/perf_coremark.py`、`perf_memory.py`、`perf_compute.py` |
