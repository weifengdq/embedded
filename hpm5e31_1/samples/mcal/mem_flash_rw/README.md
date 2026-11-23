# mem_flash_rw —— AUTOSAR Mem 驱动：擦-写-读-比较-空白检查（异步作业模型）

## 功能

用 AUTOSAR R25-11 Memory Driver（Mem，模块 ID 91，原 Fls）操作 HPM5E31 片内 1MB XPI NOR
的**保留扇区**，完整走一遍 `Mem_Init → Mem_Erase → Mem_BlankCheck → Mem_Write → Mem_Read
→ Mem_BlankCheck` 的作业流程，并验证 Det 错误路径与 `E_MEM_SERVICE_NOT_AVAIL`。

## 硬件要求

| 项 | 说明 |
|---|---|
| 板卡 | hpm5e31_1（HPM5E31IPB1，片内 1MB Flash，XPI0 映射到 0x80000000） |
| 外部接线 | 无，只需 USR 串口（CH343，/dev/ttyACM0，921600 8N1） |
| 保留扇区 | **0x800F0000 ~ 0x80100000（最后 64KB）**，应用镜像从 0x80000000 起约 80~92KB，不会被覆盖 |
| Flash 寿命 | 每次运行擦写 1 个 4KB 扇区，反复运行请留意（NOR 典型 10 万次擦除） |

安全窗口的核对方法（README 与第 47 章都写）：

1. **编译后看 map**：`grep etext build/mem_flash_rw-debug/output/demo.map`，本例程实测
   `etext = 0x80014428`（FLASH 用量 82984B），远小于 0x800F0000；
2. **运行时自证**：程序启动打印 `Mem: safe window 0x800f0000..0x80100000, app etext=0x80014428
   -> no overlap (OK)`；
3. **越界拒绝**：`memdet` 里对 0x80000000 发 `Mem_Erase`，返回 E_NOT_OK 并 Det 计数 +1
   （MEM_E_PARAM_ADDRESS），从设计上不可能擦到应用区。

## 编译与运行

```bash
cd hpm5e31_1
bash scripts/build.sh samples/mcal/mem_flash_rw
bash scripts/hwrun.sh mcal/mem_flash_rw --secs 25 --send "meminfo;memrw;memdet"
```

> BSP 已经占用了 shell 命令 `mem`（内存 hexdump），所以本模块的例程命令用
> `meminfo` / `memrw` / `memdet`，避免和 `boards/hpm5e31_1/bsp/bsp_cmds.c` 的
> `shellCmdmem` 重复定义（链接期 multiple definition）。

## 命令

| 命令 | 说明 |
|---|---|
| meminfo | 打印版本、实例几何（base/size/sector/page）、安全窗口与 etext 比对 |
| memrw | 擦除→空白检查→写 4KB→读回→逐字节比较→再空白检查（应 MEM_INCONSISTENT） |
| memdet | 9 条错误路径/边界测试：NULL 指针、窗口外擦除、未对齐写、零长度、非法实例、作业挂起、Suspend/Resume、DeInit 后调用、ECC 传播 |

## 实测现象

日志：`logs/20261102-140231-mem_flash_rw.log`（第二次运行 `logs/20261102-134132-mem_flash_rw.log`）

```text
Mem: module=91 vendor=0xffff sw=1.0.0 instances=1
Mem: instance0 base=0x80000000 size=1024 KB sector=4096 B page=256 B
Mem: safe window 0x800f0000..0x80100000, app etext=0x80014428 -> no overlap (OK)
--- mem rw @0x800f0000 (4096 B) ---
erase : OK, 1.909 ms, result=0
blank : blank OK (result=0)
write : OK, 15.281 ms, 261.8 KB/s, result=0
read  : OK, 0.101 ms, 39801.0 KB/s, result=0
cmp   : MATCH (first mismatch idx=4096)
blank : non-blank detected=YES (result=3)
sum   : 0x0007f800 (pattern seed 0x5A)
MEMRW: PASS
--- mem det: error paths ---
det   : before=0
read NULL ptr        : r=1 det=1
  last: mod=91 inst=0 api=0x05 err=0x02
erase outside window : r=1 det=2 (REFUSED, app safe)
write unaligned addr : r=1 det=3
read zero length     : r=1 det=4
get result bad inst  : det=5
read while pending   : r=1 det=6
suspend              : r=2 (expect 2 = E_MEM_SERVICE_NOT_AVAIL)
resume               : r=2 (expect 2)
read after DeInit    : r=1 det=7
re-init              : ok, jobResult=0
propagate ECC error  : result=4 (expect 4 = MEM_ECC_UNCORRECTED)
det   : after=7 (delta=7)
```

两次运行关键数据（稳定性）：

| 指标 | 第 1 次 | 第 2 次 |
|---|---|---|
| 4KB 擦除 | 1.910 ms | 1.909 ms |
| 4KB 编程 | 15.288 ms（261.6 KB/s） | 15.281 ms（261.8 KB/s） |
| 4KB 读回 | 0.101 ms | 0.101 ms |
| Det 错误条数 | 7 | 7 |

## 关键点

1. **异步作业模型**（SWS_Mem_00066）：`Mem_Write/Mem_Erase/Mem_Read/Mem_BlankCheck` 只做参数检查、
   置 `MEM_JOB_PENDING` 并返回 E_OK；真正动作在 `Mem_MainFunction` 里按"单元"推进——
   一次 MainFunction 擦 1 个扇区 / 编 1 页 / 读 1 个 chunk（512B）。例程用 `mem_wait_job()`
   显式轮询 MainFunction 并带 2~3 s 超时，生产代码里由 SchM 周期任务调用。
2. **作业结果**（`Mem_GetJobResult`）：0=MEM_JOB_OK、1=PENDING、2=FAILED、3=MEM_INCONSISTENT、
   4=ECC_UNCORRECTED、5=ECC_CORRECTED，数值与 SWS_MemAcc_91016 一致。
3. **D-Cache 坑**：ROM API 写完 Flash 后，XIP 直读可能命中 L1 D-Cache 的旧行。驱动在每次
   编程/擦除后对受影响范围做 `l1c_dc_invalidate`，读作业开始前也会失效一次；不这样做会
   出现"写成功但读回是旧数据"的假象（`samples/flash/flash_rw_erase` 也记录了同一个坑）。
4. **窗口保护**：SWS 本身允许驱动操作整片存储器，本实现按板级安全需求把擦/写/空白检查
   限制在 PBcfg 的 `eraseWindowStart..eraseWindowEnd`（最后 64KB），越界报 `MEM_E_PARAM_ADDRESS`。
   这属于工程约束，已写进第 47 章"待确认项"。
5. **与 SDK 直调的区别**：SDK 例程直接调 `rom_xpi_nor_erase/program` 并自己轮询；Mem 驱动把
   参数检查/作业状态机/结果语义（MEM_INCONSISTENT、MEM_ECC_*）标准化，上层（MemAcc/NvM）
   不用关心是片内 NOR 还是外部 Flash。
6. `Mem_Suspend/Mem_Resume`：HPM5E31 的 XPI NOR 擦/编由 ROM API 阻塞完成，无硬件挂起能力，
   按 [SWS_Mem_00082] 返回 `E_MEM_SERVICE_NOT_AVAIL`（=2，本工程定义值）。

## 待确认项

- `E_MEM_SERVICE_NOT_AVAIL` 数值：SWS 只给符号未给数值，本工程取 `0x02`（见第 47 章）。
- 本工程 `Mem_Init(configPtr)` 使用 post-build 配置指针（契约 §5 要求），与 SWS_Mem_00087
  "precompile 变体应传 NULL" 不同，属配置模型差异，已在第 47 章说明。

## mcal_check 兼容项

`python3 tools/mcal_check.py`（契约 §3/§4/§9 的静态检查）对本模块与本例程的检查结果：

| 检查项 | 要求 | 本模块/例程情况 |
|---|---|---|
| C1 公共头不得 include HPM SDK 头 | include/*.h 与 cfg/*.h 干净 | Mem.h 只 include Std_Types/Mcal_Cfg/Mcal_ModuleId/Mcal_Version；Mem_Cfg.h、Mem_PBcfg.h 同样干净 |
| C2 驱动 .c 不得硬编码引脚/实例 | 实例宏只能出现在 *_PBcfg.c | Mem.c 无 HPM_XPI0、无 Pxxx 引脚；XPI 基址与 ROM API 配置头全部来自 Mem_PBcfg.c |
| C3 五件套与版本/API_ID/模块 ID/开关/include guard | 齐全 | include/Mem.h、src/Mem.c、cfg/Mem_Cfg.h、cfg/Mem_PBcfg.{h,c}；含 MEM_SW_*_VERSION、MEM_API_ID_*、MEM_MODULE_ID=91、MEM_DEV_ERROR_DETECT |
| C4 模块 ID 与 Det 上报一致 | Det 用自己的模块 ID | `MCAL_DET_REPORT(MEM_MODULE_ID, ...)`，实测 det 记录 mod=91 |
| C5 例程 README | 含 hwrun 命令与 logs/ 真实日志 | 本文档含 `bash scripts/hwrun.sh mcal/mem_flash_rw ...` 与 logs/20261102-140231-mem_flash_rw.log |

复跑：`python3 tools/mcal_check.py`（当前对 mcal/Mem 与本例程 0 错 0 警）。
