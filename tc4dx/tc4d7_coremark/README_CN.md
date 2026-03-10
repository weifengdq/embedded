# tc4d7_coremark

本工程用于在 AURIX TC4D7 上运行 CoreMark，现已整理为干净的高性能 6 核版本。

当前版本支持：
- 1 核与 6 核运行
- `STACK` 与 `MALLOC` 两种最终可用的内存策略
- Release 模式下接近 6 倍的有效多核加速

## 最终正式结果

下表为最终确认的 Release 成绩，全部满足 CoreMark 有效性要求：运行时间超过 10 秒、CRC 正确、串口输出 `Correct operation validated`。

| 构建 | 线程数 | 内存策略 | 总时间 (s) | Iterations/Sec | 加速比 | 并行效率 |
| --- | ---: | --- | ---: | ---: | ---: | ---: |
| Release | 1 | STACK | 15.036789 | 1995.106750 | 1.0000x | 100.00% |
| Release | 6 | STACK | 15.049913 | 11960.201780 | 5.9948x | 99.91% |
| Release | 1 | MALLOC | 14.987649 | 2001.648112 | 1.0000x | 100.00% |
| Release | 6 | MALLOC | 15.058318 | 11953.526303 | 5.9718x | 99.53% |

结论：
- `STACK` 与 `MALLOC` 两种模式都已经实现接近线性的 6 核扩展
- 最终 6 核性能版在保证结果正确的前提下，基本跑满了 6 核并行效率
- 之前 `MALLOC` 六核不稳定的问题已经被消除

## 实现说明

最终性能版采用了保守但高效的多核执行模型：
- 主核只下发共享模板参数
- 每个 worker 核在本地内存中重建自己的 CoreMark 上下文
- 核间仅通过最小共享 mailbox 交换同步状态和 CRC 结果

这样避免了 worker 直接运行在共享链表/矩阵/状态数据上的一致性风险，同时保留了高性能 6 核路径。

## 构建与下载

在 `tc4d7_coremark` 目录执行：

```powershell
# 单核，Release，STACK
.\build.ps1 -Action rebuild -BuildType Release -CoremarkThreads 1 -CoremarkMemMethod STACK
.\build.ps1 -Action download -BuildType Release -CoremarkThreads 1 -CoremarkMemMethod STACK

# 六核，Release，STACK
.\build.ps1 -Action rebuild -BuildType Release -CoremarkThreads 6 -CoremarkMemMethod STACK -CoremarkIterations 30000
.\build.ps1 -Action download -BuildType Release -CoremarkThreads 6 -CoremarkMemMethod STACK

# 单核，Release，MALLOC
.\build.ps1 -Action rebuild -BuildType Release -CoremarkThreads 1 -CoremarkMemMethod MALLOC
.\build.ps1 -Action download -BuildType Release -CoremarkThreads 1 -CoremarkMemMethod MALLOC

# 六核，Release，MALLOC
.\build.ps1 -Action rebuild -BuildType Release -CoremarkThreads 6 -CoremarkMemMethod MALLOC -CoremarkIterations 30000
.\build.ps1 -Action download -BuildType Release -CoremarkThreads 6 -CoremarkMemMethod MALLOC
```

常用参数：
- `-BuildType`: `Debug | Release | RelWithDebInfo | MinSizeRel`
- `-CoremarkThreads`: `1..6`
- `-CoremarkMemMethod`: `STACK | MALLOC | STATIC`
- `-CoremarkIterations`: 固定 CoreMark 迭代种子，`0` 表示自动校准

说明：
- `STATIC` 仅适用于单上下文，CoreMark 上游不支持 `MULTITHREAD > 1` 时使用 `MEM_STATIC`

## 成绩有效性检查

记录分数前，应确认：
- 串口出现 `Correct operation validated`
- 所有 CRC 校验通过
- 运行时间不低于 10 秒
- 编译参数与目标测试模式一致

## 构建系统清理

本次还同时清理了构建脚本：
- `build.ps1` 已去掉 `build`、`rebuild`、`all` 中重复的 configure 流程，因此不会再出现无意义的 `CMAKE_TOOLCHAIN_FILE` warning
- `cmake/AurixProject.cmake` 已改为递归扫描有效源码目录，不再对排除的构建输出目录做全树 glob，因此可消除之前的 `GLOB mismatch!` 噪音

## 测试日志

```bash
# 单核，Release，STACK
.\build.ps1 -Action rebuild -BuildType Release -CoremarkThreads 1 -CoremarkMemMethod STACK
.\build.ps1 -Action download -BuildType Release -CoremarkThreads 1 -CoremarkMemMethod STACK
## 串口日志
=== TC4D7 CoreMark (1 thread) ===
2K performance run parameters for coremark.
CoreMark Size    : 666
Total ticks      : 3223427395
Total time (secs): 15.036789
Iterations/Sec   : 1995.106750
Iterations       : 30000
Compiler version : 11.3.1 20221230
Compiler flags   : -O3 -DNDEBUG
Memory location  : STACK
seedcrc          : 0xe9f5
[0]crclist       : 0xe714
[0]crcmatrix     : 0x1fd7
[0]crcstate      : 0x8e3a
[0]crcfinal      : 0x5275
Correct operation validated. See README.md for run and reporting rules.
CoreMark 1.0 : 1995.106750 / 11.3.1 20221230 -O3 -DNDEBUG / STACK

# 六核，Release，STACK
.\build.ps1 -Action rebuild -BuildType Release -CoremarkThreads 6 -CoremarkMemMethod STACK -CoremarkIterations 30000
.\build.ps1 -Action download -BuildType Release -CoremarkThreads 6 -CoremarkMemMethod STACK
## 串口日志
=== TC4D7 CoreMark (6 thread) ===
2K performance run parameters for coremark.
CoreMark Size    : 666
Total ticks      : 3229989361
Total time (secs): 15.049913
Iterations/Sec   : 11960.201780
Iterations       : 180000
Compiler version : 11.3.1 20221230
Compiler flags   : -O3 -DNDEBUG
Parallel AURIX_TC4DX_CORES : 6
Memory location  : STACK
seedcrc          : 0xe9f5
[0]crclist       : 0xe714
[1]crclist       : 0xe714
[2]crclist       : 0xe714
[3]crclist       : 0xe714
[4]crclist       : 0xe714
[5]crclist       : 0xe714
[0]crcmatrix     : 0x1fd7
[1]crcmatrix     : 0x1fd7
[2]crcmatrix     : 0x1fd7
[3]crcmatrix     : 0x1fd7
[4]crcmatrix     : 0x1fd7
[5]crcmatrix     : 0x1fd7
[0]crcstate      : 0x8e3a
[1]crcstate      : 0x8e3a
[2]crcstate      : 0x8e3a
[3]crcstate      : 0x8e3a
[4]crcstate      : 0x8e3a
[5]crcstate      : 0x8e3a
[0]crcfinal      : 0x5275
[1]crcfinal      : 0x5275
[2]crcfinal      : 0x5275
[3]crcfinal      : 0x5275
[4]crcfinal      : 0x5275
[5]crcfinal      : 0x5275
Correct operation validated. See README.md for run and reporting rules.
CoreMark 1.0 : 11960.201780 / 11.3.1 20221230 -O3 -DNDEBUG / STACK / 6:AURIX_TC4DX_CORES

# 单核，Release，MALLOC
.\build.ps1 -Action rebuild -BuildType Release -CoremarkThreads 1 -CoremarkMemMethod MALLOC
.\build.ps1 -Action download -BuildType Release -CoremarkThreads 1 -CoremarkMemMethod MALLOC
## 串口日志
=== TC4D7 CoreMark (1 thread) ===
2K performance run parameters for coremark.
CoreMark Size    : 666
Total ticks      : 3198857374
Total time (secs): 14.987649
Iterations/Sec   : 2001.648112
Iterations       : 30000
Compiler version : 11.3.1 20221230
Compiler flags   : -O3 -DNDEBUG
Memory location  : MALLOC
seedcrc          : 0xe9f5
[0]crclist       : 0xe714
[0]crcmatrix     : 0x1fd7
[0]crcstate      : 0x8e3a
[0]crcfinal      : 0x5275
Correct operation validated. See README.md for run and reporting rules.
CoreMark 1.0 : 2001.648112 / 11.3.1 20221230 -O3 -DNDEBUG / MALLOC

# 六核，Release，MALLOC
.\build.ps1 -Action rebuild -BuildType Release -CoremarkThreads 6 -CoremarkMemMethod MALLOC -CoremarkIterations 30000
.\build.ps1 -Action download -BuildType Release -CoremarkThreads 6 -CoremarkMemMethod MALLOC
# 串口日志
=== TC4D7 CoreMark (6 thread) ===
2K performance run parameters for coremark.
CoreMark Size    : 666
Total ticks      : 3234191692
Total time (secs): 15.058318
Iterations/Sec   : 11953.526303
Iterations       : 180000
Compiler version : 11.3.1 20221230
Compiler flags   : -O3 -DNDEBUG
Parallel AURIX_TC4DX_CORES : 6
Memory location  : MALLOC
seedcrc          : 0xe9f5
[0]crclist       : 0xe714
[1]crclist       : 0xe714
[2]crclist       : 0xe714
[3]crclist       : 0xe714
[4]crclist       : 0xe714
[5]crclist       : 0xe714
[0]crcmatrix     : 0x1fd7
[1]crcmatrix     : 0x1fd7
[2]crcmatrix     : 0x1fd7
[3]crcmatrix     : 0x1fd7
[4]crcmatrix     : 0x1fd7
[5]crcmatrix     : 0x1fd7
[0]crcstate      : 0x8e3a
[1]crcstate      : 0x8e3a
[2]crcstate      : 0x8e3a
[3]crcstate      : 0x8e3a
[4]crcstate      : 0x8e3a
[5]crcstate      : 0x8e3a
[0]crcfinal      : 0x5275
[1]crcfinal      : 0x5275
[2]crcfinal      : 0x5275
[3]crcfinal      : 0x5275
[4]crcfinal      : 0x5275
[5]crcfinal      : 0x5275
Correct operation validated. See README.md for run and reporting rules.
CoreMark 1.0 : 11953.526303 / 11.3.1 20221230 -O3 -DNDEBUG / MALLOC / 6:AURIX_TC4DX_CORES
```

## 复测

```bash
# 6 stack
=== TC4D7 CoreMark (6 thread) ===
2K performance run parameters for coremark.
CoreMark Size    : 666
Total ticks      : 3225019284
Total time (secs): 15.039973
Iterations/Sec   : 11968.106464
Iterations       : 180000
Compiler version : 11.3.1 20221230
Compiler flags   : -O3 -DNDEBUG
Parallel AURIX_TC4DX_CORES : 6
Memory location  : STACK
seedcrc          : 0xe9f5
[0]crclist       : 0xe714
[1]crclist       : 0xe714
[2]crclist       : 0xe714
[3]crclist       : 0xe714
[4]crclist       : 0xe714
[5]crclist       : 0xe714
[0]crcmatrix     : 0x1fd7
[1]crcmatrix     : 0x1fd7
[2]crcmatrix     : 0x1fd7
[3]crcmatrix     : 0x1fd7
[4]crcmatrix     : 0x1fd7
[5]crcmatrix     : 0x1fd7
[0]crcstate      : 0x8e3a
[1]crcstate      : 0x8e3a
[2]crcstate      : 0x8e3a
[3]crcstate      : 0x8e3a
[4]crcstate      : 0x8e3a
[5]crcstate      : 0x8e3a
[0]crcfinal      : 0x5275
[1]crcfinal      : 0x5275
[2]crcfinal      : 0x5275
[3]crcfinal      : 0x5275
[4]crcfinal      : 0x5275
[5]crcfinal      : 0x5275
Correct operation validated. See README.md for run and reporting rules.
CoreMark 1.0 : 11968.106464 / 11.3.1 20221230 -O3 -DNDEBUG / STACK / 6:AURIX_TC4DX_CORES


# 6 malloc
=== TC4D7 CoreMark (6 thread) ===
2K performance run parameters for coremark.
CoreMark Size    : 666
Total ticks      : 3235204444
Total time (secs): 15.060343
Iterations/Sec   : 11951.918642
Iterations       : 180000
Compiler version : 11.3.1 20221230
Compiler flags   : -O3 -DNDEBUG
Parallel AURIX_TC4DX_CORES : 6
Memory location  : MALLOC
seedcrc          : 0xe9f5
[0]crclist       : 0xe714
[1]crclist       : 0xe714
[2]crclist       : 0xe714
[3]crclist       : 0xe714
[4]crclist       : 0xe714
[5]crclist       : 0xe714
[0]crcmatrix     : 0x1fd7
[1]crcmatrix     : 0x1fd7
[2]crcmatrix     : 0x1fd7
[3]crcmatrix     : 0x1fd7
[4]crcmatrix     : 0x1fd7
[5]crcmatrix     : 0x1fd7
[0]crcstate      : 0x8e3a
[1]crcstate      : 0x8e3a
[2]crcstate      : 0x8e3a
[3]crcstate      : 0x8e3a
[4]crcstate      : 0x8e3a
[5]crcstate      : 0x8e3a
[0]crcfinal      : 0x5275
[1]crcfinal      : 0x5275
[2]crcfinal      : 0x5275
[3]crcfinal      : 0x5275
[4]crcfinal      : 0x5275
[5]crcfinal      : 0x5275
Correct operation validated. See README.md for run and reporting rules.
CoreMark 1.0 : 11951.918642 / 11.3.1 20221230 -O3 -DNDEBUG / MALLOC / 6:AURIX_TC4DX_CORES

# 单 malloc
=== TC4D7 CoreMark (1 thread) ===
2K performance run parameters for coremark.
CoreMark Size    : 666
Total ticks      : 3223187381
Total time (secs): 15.036309
Iterations/Sec   : 1995.170443
Iterations       : 30000
Compiler version : 11.3.1 20221230
Compiler flags   : -O3 -DNDEBUG
Memory location  : MALLOC
seedcrc          : 0xe9f5
[0]crclist       : 0xe714
[0]crcmatrix     : 0x1fd7
[0]crcstate      : 0x8e3a
[0]crcfinal      : 0x5275
Correct operation validated. See README.md for run and reporting rules.
CoreMark 1.0 : 1995.170443 / 11.3.1 20221230 -O3 -DNDEBUG / MALLOC

# 单 stack
=== TC4D7 CoreMark (1 thread) ===
2K performance run parameters for coremark.
CoreMark Size    : 666
Total ticks      : 3222481712
Total time (secs): 15.034898
Iterations/Sec   : 1995.357732
Iterations       : 30000
Compiler version : 11.3.1 20221230
Compiler flags   : -O3 -DNDEBUG
Memory location  : STACK
seedcrc          : 0xe9f5
[0]crclist       : 0xe714
[0]crcmatrix     : 0x1fd7
[0]crcstate      : 0x8e3a
[0]crcfinal      : 0x5275
Correct operation validated. See README.md for run and reporting rules.
CoreMark 1.0 : 1995.357732 / 11.3.1 20221230 -O3 -DNDEBUG / STACK
```