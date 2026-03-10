# tc4d7_coremark（中文说明）

本工程用于在 AURIX TC4D7 上运行 CoreMark，支持单核与六核、Debug/Release、多种内存策略对比。

## 1. 功能概览

当前支持：
- 线程数切换：`CoremarkThreads=1..6`
- 构建模式切换：`Debug/Release/RelWithDebInfo/MinSizeRel`
- 内存策略切换：`STACK/MALLOC/STATIC`

## 2. 常用命令

在 `tc4d7_coremark` 目录执行：

```powershell
# 单核，Release，STACK
.\build.ps1 -Action rebuild -BuildType Release -CoremarkThreads 1 -CoremarkMemMethod STACK
.\build.ps1 -Action download -BuildType Release -CoremarkThreads 1 -CoremarkMemMethod STACK

# 六核，Release，STACK
.\build.ps1 -Action rebuild -BuildType Release -CoremarkThreads 6 -CoremarkMemMethod STACK
.\build.ps1 -Action download -BuildType Release -CoremarkThreads 6 -CoremarkMemMethod STACK

# 单核，Release，MALLOC
.\build.ps1 -Action rebuild -BuildType Release -CoremarkThreads 1 -CoremarkMemMethod MALLOC
.\build.ps1 -Action download -BuildType Release -CoremarkThreads 1 -CoremarkMemMethod MALLOC

# 六核，Release，MALLOC
.\build.ps1 -Action rebuild -BuildType Release -CoremarkThreads 6 -CoremarkMemMethod MALLOC
.\build.ps1 -Action download -BuildType Release -CoremarkThreads 6 -CoremarkMemMethod MALLOC
```

可选固定迭代数（便于严格对比）：

```powershell
-CoremarkIterations 30000
```

## 3. 日志解读

关键字段：
- `Iterations/Sec`：核心性能指标
- `Compiler flags`：确认是否是 Release（`-O3 -DNDEBUG`）
- `Parallel ... : 6`：六核并行模式已启用
- `crc*`：结果校验，必须全部正确
- `Correct operation validated`：结果有效

## 4. 为什么 6 核不是 6 倍

这是嵌入式多核常见现象，不是异常：
- CoreMark 含较多内存访问和链表操作，容易受内存/互联瓶颈限制
- 多核有同步与共享资源争用开销
- 当前实现下数据放置方式会显著影响扩展效率

建议用下列指标评估：
- 加速比 = `6核 Iter/s / 1核 Iter/s`
- 并行效率 = `加速比 / 6`

## 5. 本轮多核优化改动

### 5.1 修复 MALLOC 六核卡住

已做两项修复：
- 链接脚本 `Lcf_Gcc_Tricore_Tc.lsl`：`LCF_HEAP_SIZE` 从 `4k` 提升到 `64k`
- `core_main.c`：增加 `portable_malloc` 失败检查，失败时打印错误并退出，避免“无日志卡死”
- `core_portme.c`：修复 worker 晚启动时可能错过首轮 generation 的竞态（会导致 timeout 和非主核 CRC 全 0），并在超时时打印每个 slot 的 `seen/done/target` 诊断信息

### 5.2 新增 LMU 专用池（用于多核 MALLOC）

为减少多核情况下普通堆分配的不确定性，在 `core_portme.c` 增加了：
- 多核 + `MEM_MALLOC` 时使用 `.coremark_lmu` 段上的固定池（bump allocator）
- `portable_free` 在该模式下为 no-op
- 池大小默认 `256KB`

### 5.3 链接脚本新增段

在 `Lcf_Gcc_Tricore_Tc.lsl` 新增：
- `.coremark_lmu (NOLOAD)`，放到 `lmuram`

## 6. 内存策略建议

- `STACK`：当前基线，单核稳定、六核可运行
- `MALLOC`：现在已可用于六核对比，且具备 LMU 池加持
- `STATIC`：仅适合单上下文，不适合六核（CoreMark 上游逻辑限制）

## 7. 推荐测试矩阵

建议固定迭代数后，执行以下 4 组并记录串口：

1. `Release + 1核 + STACK + Iter=30000`
2. `Release + 6核 + STACK + Iter=30000`
3. `Release + 1核 + MALLOC + Iter=30000`
4. `Release + 6核 + MALLOC + Iter=30000`

建议输出对比表：
- BuildType
- Threads
- MemMethod
- Iterations
- Total time
- Iterations/Sec
- Speedup（相对 1核同策略）
- Efficiency

## 8. 后续可继续优化方向

- 做“按核本地内存分片”分配（每个 context 尽量落在对应核更优内存）
- 降低并行同步共享变量争用
- 在保持结果有效前提下尝试编译器增强（如 LTO）

如果你把 4 组日志贴出来，我可以直接给你生成完整的性能评估报告和下一步改造优先级。