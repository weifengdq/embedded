# samples/bench —— 性能基准例程集

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

本目录是第 23/24 章（性能基准、总线吞吐与实时性）的**全部可复现实验**。每个例程都能编译、能上板、
串口有自证输出，README 里给出实测数据与日志路径。

| 例程 | 测什么 | 关键结论（详见各自 README 与第 23/24 章） | 实测日志 |
|---|---|---|---|
| `bench_coremark` | CoreMark 1.0（-O2/-O3、数据 DLM/AXI SRAM） | **3.954 CoreMark/MHz**（-O3，XIP）；-O2 3.767 | `logs/20261030-110654-bench_coremark.log` 等 3 个 |
| `bench_dhrystone` | Dhrystone 2.1（-O2/-O3，-fno-inline） | **1.4088 DMIPS/MHz**（-O3） | `logs/20261030-113442-bench_dhrystone.log` |
| `bench_memory` | memcpy/memset/memcmp × cache/nc/DLM × 1B~64KB | libc memcpy 只有 95.9MB/s（逐字节）；手写内核 **922.9MB/s**；非 cacheable 慢 11.5 倍 | `logs/20261030-111657-bench_memory.log` |
| `bench_irq_latency` | GPTMR 中断入口延迟（1 万次）+ 抖动 + ILM/XIP | **210~220ns**，抖动 <1 tick（10ns）；ILM 无改善 | `logs/20261030-114009-bench_irq_latency.log` |
| `bench_compute` | FPU/DSP/位运算 12 个内核，ILM vs XIP | mac_f32 4.03 cyc/flop、sin_f32 141 cyc、div_u64 56 cyc；ILM 无吞吐收益 | `logs/20261030-113743-bench_compute.log` |
| `bench_bus` | XPI flash 直读、UART 吞吐、总线时钟树 | **XPI 顺序读 40MB/s**（XPI0 100MHz×4bit 的 80%）；AXI 200MHz | `logs/20261030-113730-bench_bus.log` |

## 公共约定

- 统一 `bsp_init()` + `bsp_shell_poll()`，控制台 UART4 921600 8N1；
- 计时统一用 **mcycle（480MHz）**，并用 MCHTMR（24MHz）交叉校验；
- 每个样本都注册 letter-shell 调试命令，可反复测量而不必重新烧写；
- 上板一律走 `scripts/hwrun.sh`（内部 flock 串行，多人共用一块板安全）。

## 编译选项变体：run_variant.sh

基准需要"同一份代码、不同编译选项"多次上板，而 `scripts/build.sh` 只有一个 build 目录。用：

```bash
bash samples/bench/_bench_tools/run_variant.sh <sample相对路径> <变体名> '<额外 cmake -D 参数>' -- <hwrun 参数>
# 例：
bash samples/bench/_bench_tools/run_variant.sh bench/bench_coremark o2 '-DBENCH_OPT=-O2 -DBENCH_RUNS=2' -- --secs 55
```

它会：独立 build 目录配置 → 构建 → 把 ELF 复制到 `build/<name>-debug/output/` → 调 `hwrun.sh --no-build` 上板。

> **注意**：同一样本的两个变体**不要并发排队**，否则会互相覆盖 `build/<name>-debug/output/` 里的 ELF（踩过）。

## 通用坑（各样本 README 里有更详细的）

1. SDK 的 debug 配置**不带 -O**（默认 -O0），基准必须显式加 `-O2/-O3`；
2. `hwrun.sh` 烧写后 0.3s 才开串口抓包，自动测量的第一组输出要用 `board_delay_ms(1500)` 保护；
3. newlib-nano 的 `memcpy/memset` 是逐字节实现，不能用来测内存带宽；
4. 非 cacheable 段用 `.noncacheable.bss`，DLM 用 `.fast_ram.bss`，ILM 代码用 `.fast`（别混）；
5. 单板串行：所有上板测试排队执行，等待 1~3 分钟是正常的。
