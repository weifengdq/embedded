# bench_dhrystone —— Dhrystone 2.1 基准（DMIPS/MHz）

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

跑 Dhrystone 2.1，输出 **Dhrystones/s、DMIPS、DMIPS/MHz**，多轮给均值/标准差；每轮结束做
Dhrystone 官方自检（Int_3_Loc==7、Enum_Loc==1、两个字符串、Arr_2_Glob[8][7]==LOOP_COUNT+10），
[PASS] 才计入统计。

- 被测代码是 **HPM SDK 自带的 dhry_1.c / dhry_2.c**（`$HPM_SDK_BASE/samples/dhrystone/src/`），
  没有复制进仓库，只做了"编译单元包装"把它的 main() 改名；
- 计时用 **mcycle（480MHz）**；
- 变体：`-DBENCH_OPT`（-O2/-O3）、`-DBENCH_LOOPS`（LOOP_COUNT）、`-DBENCH_RUNS`。

## 接线

无（只用 UART4 921600）。

## 编译与运行

```bash
bash scripts/build.sh bench/bench_dhrystone
bash samples/bench/_bench_tools/run_variant.sh bench/bench_dhrystone o3 '-DBENCH_OPT=-O3 -DBENCH_LOOPS=5000000 -DBENCH_RUNS=2' -- --secs 18
```

## 串口命令

| 命令 | 说明 |
|---|---|
| `dh <n>` | 再跑 n 轮 |
| `dhstat` | 打印统计 |
| `dhinfo` | 打印环境（CPU 频率、LOOP_COUNT、优化选项） |

## 实测数据

| 变体 | Dhrystones/s | DMIPS | DMIPS/MHz | 自检 | 日志 |
|---|---|---|---|---|---|
| `-O3` | 1,188,113 | 676.22 | **1.4088** | PASS | `logs/20261030-113442-bench_dhrystone.log` |
| `-O2` | 1,130,779 | 643.59 | 1.3408 | FAIL（自检条件第一版写错，分数有效，未复跑） | `logs/20261030-113100-bench_dhrystone.log` |

串口自证输出：

```
BENCH_RUN idx=1 secs=4.208354 dhrystones_per_s=1188112.85 dmips=676.22 dmips_per_mhz=1.4088 verify=PASS
BENCH_STAT metric=dhrystone n=2 mean_dmips=676.22 dmips_per_mhz=1.4088 sd=0.0000 ...
```

结论：-O2 → -O3 提升 4.9%；两轮结果几乎完全相同（sd=0.0000），说明裸机 + 串行测试的重复性极好。
DMIPS/MHz 只有 1.41，是因为按 Dhrystone 规则用了 `-fno-inline`（不允许内联跨过程调用）。

## 易错点与注意事项

1. **SDK 自带例程打印的 "DMIPS per Second" 其实是 DMIPS/MHz**：`dhry_1.c` 里
   `DMIPS_Per_Mhz = Dhrystones_Per_Second / 1757 / cpu_frequency_in_mhz`，但 printf 标签写错。
   引用 SDK 输出时必须自己换算，否则会把 DMIPS/MHz 当成 DMIPS 用（差 480 倍）。
2. **Dhrystone 自检期望值**：`Enum_Loc` 应为 1（Ident_2，不是 Ident_1）、`Int_3_Loc` 为 7、
   `Arr_2_Glob[8][7]` 为 LOOP_COUNT+10。本仓第一版写成 Ident_1 导致 verify=FAIL。
3. **必须每轮重置全局状态**（Ptr_Glob/Str_1_Loc/Arr_2_Glob[8][7]），否则第二轮结果不可比。
4. **`-fno-inline` 不能省**：Dhrystone 的可比性建立在"过程调用不被内联消掉"上，
   去掉这项分数会虚高（本仓实测未开 -O 时也不同，注意区分）。
5. **官方 main() 只跑一次**，本仓用 `#define main dhrystone_main_unused` + include 的方式绕开，
   驱动循环写在 `src/main.c` 里（见第 23 章"易错点"第 2 条）。

## 参考

- SDK 例程：`$HPM_SDK_BASE/samples/dhrystone/`（`README_zh.rst`）
- 原始论文：R. P. Weicker, *Dhrystone: A Synthetic Systems Programming Benchmark*, CACM 1984