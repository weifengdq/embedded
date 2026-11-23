# bench_coremark —— CoreMark 1.0 基准（HPM5E31 @480MHz）

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

跑 EEMBC CoreMark 1.0 官方基准，输出 CoreMark 分数与 CoreMark/MHz，并做**多轮统计**
（每轮分数、均值、标准差、最小/最大）。同一固件内可反复运行，不需要重新烧写。

- 官方 CoreMark 源码**直接来自 HPM SDK**（`$HPM_SDK_BASE/middleware/coremark/`），没有复制进本仓库；
  本目录只提供移植层（`core_portme.c/.h`）、驱动（`main.c`）和 CMake 变体开关。
- 计时源：**mcycle（= CPU 时钟 480MHz，分辨率 ~2ns）**，同时用 MCHTMR(24MHz) 交叉校验时基。
- 数据块位置：默认 CoreMark 用 `MEM_METHOD=MEM_STACK`，而本 SoC 链接脚本把**栈放在 DLM**
  （`flash_xip.ld`：`.stack > DLM`，0x00200000），可用 `-DBENCH_DATA=sram` 改成 `.bss`(AXI SRAM, 走 L1 D-Cache)。

## 接线

无。只用板载调试串口 UART4（921600 8N1）。

## 编译与运行

```bash
# 默认变体：-O3 + 数据在 DLM(栈)
bash scripts/build.sh bench/bench_coremark
bash scripts/hwrun.sh bench/bench_coremark --secs 75

# 其它变体（编译选项/数据位置不同）
bash samples/bench/_bench_tools/run_variant.sh bench/bench_coremark o2   '-DBENCH_OPT=-O2 -DBENCH_RUNS=2' -- --secs 55
bash samples/bench/_bench_tools/run_variant.sh bench/bench_coremark sram '-DBENCH_OPT=-O3 -DBENCH_DATA=sram -DBENCH_RUNS=2' -- --secs 55
```

CMake 变量：

| 变量 | 默认 | 说明 |
|---|---|---|
| `BENCH_OPT` | `-O3` | 优化级别（追加在 SDK 默认选项之后；SDK 的 debug 配置本身**不带任何 -O**） |
| `BENCH_EXTRA` | 空 | 追加选项，例如 HPM SDK coremark 例程的激进参数串 |
| `BENCH_DATA` | `dlm` | `dlm`=栈(DLM) / `sram`=MEM_STATIC，数据进 `.bss`(AXI SRAM) |
| `BENCH_RUNS` | `3` | 每次上电自动跑几轮（每轮约 21.7s，其中约 5.9s 是 CoreMark 自标定） |

## 串口命令

| 命令 | 说明 |
|---|---|
| `cm <n>` | 再跑 n 轮 |
| `cmstat` | 打印已跑轮次的统计（均值/标准差/最小/最大/CoreMark/MHz） |
| `cminfo` | 打印环境（CPU 频率、优化选项、数据位置、计时源） |

## 实测现象与数据

上电后自动跑 N 轮，串口每轮打印 CoreMark 标准报告 + 一行自证数据：

```
BENCH_RUN idx=1 score=1898.1099 iters=30000 secs=15.805197 wall=21.668394 mtime_secs=15.805197 cmhz=3.9544
TIMEBASE mcycle_secs=15.805197 mchtmr_secs=15.805197 diff_ppm=0.0 cpu_hz=480000000 mchtmr_hz=24000000
===== CoreMark summary (n=3) =====
BENCH_STAT metric=coremark n=3 mean=1898.1099 sd=0.02 ... cmhz=3.9544
```

| 变体 | CoreMark | CoreMark/MHz | 轮数 | 日志 |
|---|---|---|---|---|
| `-O3` + 数据在 DLM(栈)（默认） | 1898.11 | 3.954 | 3 | `logs/20261030-110654-bench_coremark.log` |
| `-O2` + 数据在 DLM(栈) | 1808.02 | 3.767 | 2 | `logs/20261030-110853-bench_coremark.log` |
| `-O3` + 数据在 AXI SRAM(`.bss`) | 1886.84 | 3.931 | 2 | `logs/20261030-111015-bench_coremark.log` |

几点结论：

1. **-O2 → -O3 提升 5.0%**（1808 → 1898），说明 CoreMark 对编译优化敏感；
2. **数据放 DLM 与放可缓存 AXI SRAM 只差 0.6%**（1898.1 vs 1886.8）：CoreMark 工作集约 2KB，
   能完整装进 L1 D-Cache，所以"数据放哪块 RAM"影响很小 —— 这点常被误解；
3. **mcycle 与 MCHTMR 时间完全一致（diff_ppm=0.0）**：480MHz 与 24MHz 都来自同一 PLL/晶振，
   实测时基可信；CoreMark 报告里 Iterations=30000（自标定结果，单轮 ≥10s，符合官方规则）。

## 坑与注意事项

1. **CoreMark 官方 `core_main.c` 自带 `main()` 只跑一次**，跑完就 return（嵌入式里会跑飞）。
   本仓在构建期生成一个"编译单元包装" `cm_core_main.c`：
   `#define main coremark_main` + `#include "core_main.c"`，把官方 main 改名后由我们的
   `main()` 循环调用，才能同一固件多轮运行。源码仍在 SDK 里，未复制。
2. **SDK 的 debug 配置不带 `-O`**（默认 -O0）。看 `scripts/build.sh` 生成的
   `compile_commands.json` 会发现只有 `-g -Wall ...`，所以**必须显式**加 `-O2/-O3`，
   否则测出来的是 -O0 的分数（本仓 `sdk_compile_options` 追加在最后，GCC 以最后一个 -O 为准）。
3. **`scripts/build.sh` 只支持一个固定配置**（`build/<name>-debug`），而基准要同一份代码换编译选项多次上板。
   用 `samples/bench/_bench_tools/run_variant.sh`：在临时 build 目录用额外 `-D` 配置 → 构建 →
   复制 ELF 到 `build/<name>-debug/output/` → `hwrun.sh --no-build` 上板（仍走整机 flock 串行）。
   **注意：同一样本的两个变体不要并发排队**，否则它们会互相覆盖 `build/<name>-debug/output/` 里的 ELF。
4. **串口抓包有 ~0.3s 启动延迟**（`hwrun.sh` 烧写后 sleep 0.3 才开始抓）。自动跑的第 1 组数据可能丢，
   解决办法：`main()` 里先打印 banner，并在自动测量前 `board_delay_ms(1500)`。
5. **CoreMark 单轮耗时约 21.7s**：15.8s 是计时区间，另外约 5.9s 是 CoreMark 自己的迭代次数标定（不计分）。
   想缩短就 `-DBENCH_RUNS=1` 并把 `--secs` 调到 30。
6. `ee_printf` 本仓自己实现（`HAS_PRINTF=0`），既输出到串口又顺手抓出
   `Iterations/Sec` 与 `Iterations`，这样固件自己能算统计，不依赖离线解析日志。
   （注意行首字符串是 `Iterations` 后 7 个空格再冒号，长度容易数错 —— 第一版就数错了。）

## 参考

- EEMBC CoreMark：`$HPM_SDK_BASE/middleware/coremark/README.md`
- HPM SDK coremark 例程（移植层参考）：`$HPM_SDK_BASE/samples/coremark/`
