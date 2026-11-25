# bench_compute —— FPU / DSP / 位运算吞吐（含 ILM vs XIP 对比）

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

测 12 个计算内核的**单次操作周期数 / 吞吐**，每个内核都有两份代码：
默认（XIP flash）与 ILM（`__attribute__((section(".fast")))`），同一固件内对比取指路径影响：

| 类别 | 内核 |
|---|---|
| 单精度 FPU | `mac_f32`（乘加）、`div_f32`、`sqrt_f32`、`sin_f32`、`cvt_f2i` |
| 整数 | `mac_i32`、`div_i32`、`div_u64` |
| 定点 DSP | `q15_gain`（(int16*int16)>>15） |
| 位操作 | `ctz32`、`clz32`、`popcount` |

计时：mcycle，每测点 3 次取最好；函数指针调用避免内联。

## 接线

无。

## 编译与运行

```bash
# 板级默认构型：rv32imac + zba/zbb/zbc/zbs（无 F/D，软浮点）
bash scripts/build.sh bench/bench_compute

# 硬件单精度 FPU 构型（必须改 -march/-mabi）
source scripts/env.sh
cmake -GNinja -B build/bench_compute-fpu -S samples/bench/bench_compute \\
  -DBOARD=hpm5e31_1 -DBOARD_SEARCH_PATH=$PWD/boards -DHPM_BUILD_TYPE=flash_xip \\
  -DCMAKE_BUILD_TYPE=debug -DBENCH_FPU=1 -DBENCH_OPT=-O3
cmake --build build/bench_compute-fpu
```

CMake 变量：`BENCH_FPU`（0/1）、`BENCH_OPT`（默认 -O3）。

## 串口命令

| 命令 | 说明 |
|---|---|
| `comp` | 跑全部内核（约 2s） |
| `compinfo` | 打印 `__riscv_flen`、Zbb 是否启用、ILM/XIP 代码地址 |

## 实测数据（FPU 构型，-O3，`logs/20261030-113743-bench_compute.log`）

| 内核 | cyc/op | 吞吐 | XIP→ILM speedup |
|---|---|---|---|
| `mac_f32` | 4.03 /flop | 119.2 MFLOPS | 0.999 |
| `cvt_f2i` | 5.06 | 95.0 MOPS | 0.998 |
| `mac_i32` | 3.03 /op | 158.6 MOPS | 0.999 |
| `q15_gain` | 6.06 | 79.2 MOPS | 0.999 |
| `div_f32` | 24.06 | 20.0 MOPS | 1.000 |
| `sqrt_f32` | 26.07 | 18.4 MOPS | 1.000 |
| `div_i32` | 18.68 | 25.7 MOPS | 1.000 |
| `div_u64` | 56.16 | 8.6 MOPS | 0.949 |
| `sin_f32` | 141.46 | 3.4 MOPS | 0.988 |
| `ctz32` / `clz32` | 15.07 | 31.9 MOPS | 0.938 / 0.883 |
| `popcount` | 18.06 | 26.6 MOPS | 0.947 |

串口自证输出：

```
board=hpm5e31_1 cpu=480 MHz opt=-O3 BENCH_FPU=1
BENCH_COMPUTE kernel=mac_f32     unit=MFLOPS xip_mops=119.18 ilm_mops=119.07 xip_cyc_op=4.027 ilm_cyc_op=4.031 speedup=0.999
BENCH_COMPUTE kernel=sin_f32     unit=MOPS   xip_mops=3.39   ilm_mops=3.35   xip_cyc_op=141.457 ilm_cyc_op=143.207 speedup=0.988
```

结论：

1. **ILM 对计算吞吐没有帮助**（speedup 0.88~1.00，多数是 ILM 略慢 1~2%）：内核代码很小，L1 I-Cache 已能覆盖；
   搬 ILM 的收益在"最坏情况延迟"（避免 cache miss），不在平均吞吐。
2. `sin_f32`（141 周期）与 `div_u64`（56 周期）是最贵的两个操作，控制环里要避开；
3. 硬件 FPU 的 `mac_f32` 是 4 周期/flop（fmul+fadd 流水），`sqrt_f32` 26 周期（含 Newton 修正）。

## 易错点与注意事项

1. **FPU 构型的 multilib 易错点**（最重要）：本工具链的 ilp32f 库只按 `-march=rv32imafc` / `rv32imafdc` 精确匹配；
   若 `-march` 带 `_zba_zbb_zbc_zbs` 后缀，链接期会报
   `can't link soft-float modules with single-float modules`。解决：
   `set(HPM_SOC_DISABLE_B_EXT 1)` 关掉 B 扩展追加，并在**链接期**再显式给 `-march=rv32imafc -mabi=ilp32f`。
2. **`sinf/sqrtf` 需要 `-lm`**：newlib-nano 默认不链数学库，会 undefined reference。
3. **FPU 构型关掉 Zbb 后，位操作变软件实现**（15~18 周期）：所以位运算数据要以默认构型（带 zbb）为准，
   两个构型的表不能混着看。
4. **`SHELL_CMD_RETURN` 不存在**：letter-shell 只有 `SHELL_CMD_DISABLE_RETURN`，写错会编译报错。
5. **串口抓包 0.3s 延迟**会让最先打印的 kernel（mac_f32/div_f32）丢失，已加 `board_delay_ms(1500)`。

## 参考

- FPU/ABI：`$HPM_SDK_BASE/cmake/toolchain.cmake`（RV_ARCH/RV_ABI 与 multilib 探测）
- 缓存：`$HPM_SDK_BASE/arch/riscv/l1c/hpm_l1c_drv.h`；链接脚本 `flash_xip.ld` 的 `.fast > ILM` 段