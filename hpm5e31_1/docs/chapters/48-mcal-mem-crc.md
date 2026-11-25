# 第 48 章 Mem 与 Crc：片内 Flash 驱动与 CRC 库

## 48.1 文档信息

| 项 | 内容 |
|---|---|
| 章节 | 第 48 章 Mem 与 Crc：片内 Flash 驱动与 CRC 库 |
| 版本 | v1.3.0（2026-11-02，R6 MCAL） |
| 作者 | mcal-sys（R6 MCAL 成员） |
| 对应模块 | Mem（AUTOSAR Memory Driver，模块 ID 91，R25-11 起原 Fls 并入）与 Crc（CRC Library，模块 ID 201） |
| 对应例程 | samples/mcal/mem_flash_rw、mem_bench、crc_bench、crc_integrity |
| 实测日期 | 2026-11-02（hpm5e31_1，HPM5E31IPB1，CPU 480 MHz） |
| 可信度 | 高：8 个 CRC 算法的 SWS 标准向量全部对上，Mem 擦写读在保留扇区闭环，数据来自真实串口日志 |
| 数据来源 | logs/20261102-140231-mem_flash_rw.log、logs/20261102-140321-mem_bench.log（第二次 logs/20261102-141425-mem_bench.log）、logs/20261102-135730-crc_bench.log、logs/20261102-140431-crc_integrity.log |
| 依赖章节 | 第 21 章（XPI Flash 启动）、第 43 章（MCAL 基础设施与配置模型） |

## 48.2 本章导览

这一章解决两个"看起来简单、做对不容易"的模块：

1. **Mem**：AUTOSAR 里唯一负责"直接碰存储器"的驱动。它没有同步读写的幻觉——Write/Erase/Read
   都只是"受理作业"，真正的硬件动作在 `Mem_MainFunction` 里按单元推进。本章讲清作业状态机、
   为什么要把擦除/编程切成"一扇区/一页"一次、怎么保证不擦到应用镜像；
2. **Crc**：AUTOSAR 的纯计算库，但参数表（poly/init/refin/refout/xorout）一个都不能错。
   本章给出 8 个算法与 SWS 校验值逐个对上的证据，以及"用 HPM CRC 外设换 3 倍吞吐"的实测代价：
   外设寄存器约定没文档，必须在初始化时自校准。

读完本章你应该能回答：Flash 保留扇区怎么选、怎么自证安全；Mem 作业要调多少次 MainFunction；
CRC 该走硬件还是软件；以及把这套代码移植到别的板子要改哪几个文件。

## 48.3 概述

### 48.3.1 AUTOSAR 存储栈里的位置

AUTOSAR 的存储访问分三层：上层 NvM/Fee → 中间 MemAcc（Memory Access，负责跨段拆分、对齐、
作业排队）→ 底层 Mem（本模块，直接操作一种存储器技术）。Mem 只做"物理段访问"，
所有跨段、对齐、重试都由 MemAcc 处理（[SWS_Mem_00035]、SWS_MemoryDriver §7.1）。

本仓库没有实现 MemAcc，例程直接调 Mem——这是刻意的：MemAcc 是 BSW 层，不属于 MCAL 范围，
而 Mem 的 API 语义与作业模型已经足够独立可用。

### 48.3.2 模块与 API 覆盖

表 48-1 是本轮两个模块的 API 覆盖情况。Mem 的 13 个 SWS API 全部实现（含 Suspend/Resume 的
"无硬件能力"路径），Crc 的 8 个算法 + GetVersionInfo 全部实现。

表 48-1 Mem 与 Crc 的 SWS API 覆盖

| 模块 | SWS API | 本实现 | 说明 |
|---|---|---|---|
| Mem | Mem_Init / Mem_DeInit | 是 | 后者中止在途作业并置未初始化 |
| Mem | Mem_Read / Mem_Write / Mem_Erase | 是 | 异步受理，MainFunction 推进 |
| Mem | Mem_BlankCheck | 是 | 非空返回 MEM_INCONSISTENT（3） |
| Mem | Mem_GetJobResult | 是 | 返回 0/1/2/3/4/5 六态 |
| Mem | Mem_GetVersionInfo | 是 | 模块 ID 91 |
| Mem | Mem_Suspend / Mem_Resume | 是 | 无硬件挂起能力，返回 E_MEM_SERVICE_NOT_AVAIL |
| Mem | Mem_PropagateError | 是 | 置 MEM_ECC_UNCORRECTED 并中止作业 |
| Mem | Mem_HwSpecificService | 是 | 两个服务 ID：读几何、读作业进度 |
| Mem | Mem_MainFunction | 是 | 每次推进 1 个单元 |
| Crc | Crc_CalculateCRC8 / 8H2F / 16 / 16ARC / 32 / 32P4 / 32P76 / 64 | 是 | 8 个算法，含增量链式 |
| Crc | Crc_GetVersionInfo | 是 | 模块 ID 201 |

表 48-2 是模块的地址与资源分配，与 docs/MCAL开发约定.md §2 一致。

表 48-2 存储器与 CRC 外设的资源分配

| 资源 | 地址/实例 | 用途 | 归属 |
|---|---|---|---|
| 片内 XPI NOR | 0x80000000 ~ 0x800FFFFF（1 MB） | 代码镜像 + 保留数据区 | 全体（只读） |
| Mem 保留窗口 | 0x800F0000 ~ 0x80100000（64 KB） | Mem 例程擦写 | Mem 例程独占 |
| CRC 外设 | 0xF0080000，8 通道 | CRC 加速，使用通道 0 | Crc 模块独占 |
| L1 D-Cache | 写回 + 按地址失效 | 见 §48.11 的易错点 | 系统级 |

## 48.4 工作原理

### 48.4.1 Mem 的异步作业模型

SWS 的硬性要求是 [SWS_Mem_00066]：**所有异步服务的作业请求都必须在 Mem_MainFunction 中执行**。
于是驱动被拆成两半：

* API 侧只做四件事——检查初始化、检查实例号、检查地址/长度/对齐、置 `MEM_JOB_PENDING` 返回 E_OK；
* MainFunction 侧按"单元"推进：擦除一次一个扇区（4 KB）、编程一次一页（256 B）、
  读取一次一个 chunk（512 B）、空白检查一次 512 B。单元大小在 Mem_PBcfg.c 的实例表里。

图 48-1 是这个状态机，以及实测的单元阻塞时间。

![图 48-1 Mem 作业状态机与 MainFunction 分块推进](figures/48-mcal-mem-crc/fig-48-1-mem-job-fsm.png)

这样做的好处是：擦一个扇区要 1.9 ms，如果一次作业擦 16 KB 就是 7.6 ms 的阻塞，
放进 1 ms 任务会直接超时；切成"一扇区一次"后单次 MainFunction 最长 1.908 ms，
写一页 0.978 ms，读一个 chunk 只要 0.020 ms（表 48-5）。代价是作业期间要多次调用 MainFunction，
例程里的 `mem_wait_job()` 就是这个循环（带 2~3 s 超时，不允许死等）。

### 48.4.2 结果语义与失败分类

作业结果用 `Mem_GetJobResult` 读，六态取值来自 [SWS_MemAcc_91016]：
0=MEM_JOB_OK、1=MEM_JOB_PENDING、2=MEM_JOB_FAILED、3=MEM_INCONSISTENT、
4=MEM_ECC_UNCORRECTED、5=MEM_ECC_CORRECTED。其中 3 专门给空白检查：
擦完读到全 0xFF 是 0，读到非 0xFF 是 3——这不是"失败"，而是"结果与预期不符"，
上层 NvM 要靠它判断某个块是否已写过（[SWS_Mem_00076]）。

### 48.4.3 Flash 窗口保护

片内 1 MB Flash 从 0x80000000 起，应用镜像约 80~92 KB。Mem 驱动如果对整片开放，
一个地址算错就会擦掉正在执行的代码（砖）。因此本实现在 PBcfg 里给每个实例配了
`eraseWindowStart/eraseWindowEnd`，写/擦/空白检查必须完全落在窗口内，
否则 Det 报 `MEM_E_PARAM_ADDRESS` 并返回 E_NOT_OK。图 48-2 是布局。

![图 48-2 片内 1MB Flash 布局与 Mem 保留窗口](figures/48-mcal-mem-crc/fig-48-2-flash-window.png)

窗口不是 SWS 要求的，是工程安全措施；它把"驱动参数错误"从"砖"降级成"Det 计数 +1"。
SWS_Mem_00035 说驱动不做对齐/分段，那是 MemAcc 的活；本实现只额外做了"越界即拒绝"。

### 48.4.4 Crc 的软件表与硬件加速

Crc 是纯计算库：8 个算法各有固定的 poly/init/refin/refout/xorout（SWS_Crc §7.2）。
两种实现路径：

1. **软件表**：每个算法 256 项查表，合计 6656 B（Crc.c 里的 8 张表）；
   首次调用 `reg = init`，后续调用 `reg = StartValue ⊕ xorout`（[SWS_Crc_00014]/[SWS_Crc_00041]），
   结果 `= reg ⊕ xorout`。反射算法用反射表，语义等价；
2. **硬件路径**：CRC 外设支持自定义 poly/init/refin/refout/xorout 与 poly 宽度 4~32 位，
   因此 7 个 ≤32 位算法都能上硬件，CRC64 只能软件（外设位宽不够）。

外设的寄存器约定没有文档：INIT_DATA 是否要按位反序、XOROUT 由硬件还是软件加、
数据按字节喂还是按 32 位字喂、poly 是否要左对齐，这四个维度组合出 16 种可能。
本驱动在首次计算时用"16 字节整段 + 24 字节整段 + 7/9 字节分段链"三重复核逐组标定，
把能对上软件表的组合记下来；标不上的算法永久回落软件表。这个自校准是本章最实用的设计：
换了芯片或 SDK 版本，不用改代码，最多损失加速比。

## 48.5 寄存器

表 48-3 是 CRC 外设的寄存器与驱动对应关系（基址 0xF0080000，通道 0，寄存器定义见
hpm_sdk/soc/HPM5E00/ip/hpm_crc_regs.h）。

表 48-3 CRC 外设寄存器对照

| 寄存器 | 位域 | 驱动 API | 例程位置 |
|---|---|---|---|
| CHN0.PRE_SET | PRE_SET[7:0] | crc_setup_channel_config（preset=none 时走自定义） | Crc.c: crc_hw_run_conv |
| CHN0.CLR | CLR[0] | 写 1 清通道配置与结果，每次计算前重装 | Crc.c: crc_hw_run_conv |
| CHN0.POLY | POLY[31:0] | 生成多项式，polyShift 时左对齐 | Crc.c: crc_hw_run_conv |
| CHN0.INIT_DATA | INIT_DATA[31:0] | 起始寄存器值，链式时来自上次结果 | Crc.c: crc_hw_run_conv |
| CHN0.XOROUT | XOROUT[31:0] | 结果异或值 | Crc.c: crc_hw_run_conv |
| CHN0.MISC_SETTING | REV_IN[8] / REV_OUT[16] | refin / refout | Crc.c: crc_hw_run_conv |
| CHN0.MISC_SETTING | BYTE_REV[24] | 按字喂数时的字节序（实测用 lsb） | Crc.c: crc_hw_run_conv |
| CHN0.MISC_SETTING | POLY_WIDTH[4:0] | 位宽：8/16/32 对应 CRC_POLY_WIDTH_x | Crc.c: 算法参数表 |
| CHN0.DATA | DATA[31:0] | 写数据：crc_calc_block_bytes / large_block_fast | Crc.c: crc_hw_run_conv |
| CHN0.RESULT | RESULT[31:0] | crc_get_result，按位宽掩码后返回 | Crc.c: crc_hw_run_conv |

表 48-4 是 Flash 侧用到的寄存器与函数。Mem 驱动本身不直接写 XPI 寄存器，而是调 ROM API；
ROM API 的配置头来自板级宏 BOARD_APP_XPI_NOR_CFG_OPT_*。

表 48-4 Flash/XPI 与外设时钟相关寄存器与接口

| 寄存器/接口 | 位域/参数 | 驱动 API | 例程位置 |
|---|---|---|---|
| XPI0 基址 0xF3000000 | — | rom_xpi_nor_auto_config | Mem.c: Mem_Init |
| xpi_nor_config_t | 通道/时序/命令表 | ROM API 内部填充并缓存 | Mem.c: s_norCfg[] |
| xpi_nor_config_option_t | header/option0/option1 | 板级宏 0xFCF90002/0x6/0x1000 | Mem_PBcfg.c |
| 擦除 | addr + 4 KB | rom_xpi_nor_erase | Mem.c: MEM_JOB_ERASE |
| 编程 | addr + 256 B | rom_xpi_nor_program | Mem.c: MEM_JOB_WRITE |
| 读 | addr + len | XIP 直读 + l1c_dc_invalidate | Mem.c: MEM_JOB_READ |
| 存储器属性 | total/sector/page size | rom_xpi_nor_get_property（例程侧参考） | samples/flash/flash_rw_erase |
| 时钟 SYSCTL | 资源 CRC0 | clock_add_to_group(clock_crc0, 0) | Crc.c: crc_hw_ensure_ready |
| L1 D-Cache | 按地址失效 | l1c_dc_invalidate | Mem.c: mem_dcache_invalidate |

## 48.6 配置

三个配置阶段都演示到了：

* **Pre-compile**：Mem_Cfg.h 定 `MEM_DEV_ERROR_DETECT`、`MEM_MAX_INSTANCES`、
  `MEM_UNITS_PER_MAIN_FUNCTION`；Crc_Cfg.h 定 `CRC_USE_HW_ACCEL`、`CRC_HW_MIN_LENGTH`；
* **Post-build**：Mem_PBcfg.c 给实例表与窗口，Crc_PBcfg.c 给外设基址/通道/时钟；
* **Link-time**：本工程统一用 post-build 指针，不做单独 link-time 配置（第 43 章说明）。

改板时只需要动 PBcfg。下面是 Mem_PBcfg.c 的核心（节选，完整文件见 mcal/Mem/cfg/）：

```c
/* 板级常量：与 docs/MCAL开发约定.md §2 "片内 Flash" 一致 */
#define MEM_CFG_FLASH_BASE          (0x80000000UL)
#define MEM_CFG_FLASH_SIZE          (1024UL * 1024UL)
#define MEM_CFG_SAFE_WINDOW_START   (0x800F0000UL)  /* 最后 64KB */
#define MEM_CFG_SAFE_WINDOW_END     (0x80100000UL)
#define MEM_CFG_SECTOR_SIZE         (4096UL)
#define MEM_CFG_PAGE_SIZE           (256UL)

const Mem_InstanceConfigType Mem_InstanceConfigTable[] = {
    { MEM_CFG_FLASH_BASE, MEM_CFG_FLASH_SIZE,
      MEM_CFG_SAFE_WINDOW_START, MEM_CFG_SAFE_WINDOW_END,
      MEM_CFG_SECTOR_SIZE, MEM_CFG_PAGE_SIZE, MEM_CFG_READ_CHUNK, FALSE }
};

const Mem_ConfigType Mem_Config = {
    1U,                                  /* numInstances */
    (uint32)(uintptr_t)HPM_XPI0,          /* hwBaseAddress */
    (uint32)BOARD_APP_XPI_NOR_CFG_OPT_HDR,
    (uint32)BOARD_APP_XPI_NOR_CFG_OPT_OPT0,
    (uint32)BOARD_APP_XPI_NOR_CFG_OPT_OPT1,
    Mem_InstanceConfigTable
};
```

注意 Mem.h 只 include Std_Types/Mcal_Cfg/Mcal_ModuleId/Mcal_Version，**没有任何 HPM SDK 头**；
HPM 类型只出现在 Mem.c 与 Mem_PBcfg.c（契约 §3.2）。

## 48.7 例程

四个例程各自解决一个问题，命令表见表 48-5（BSP 已占用 `mem` 做 hexdump，所以 Mem 的命令改为
`meminfo/memrw/memdet`，这条易错点记在 §48.11）。

表 48-5 四个例程的命令与验证目标

| 例程 | 命令 | 验证目标 |
|---|---|---|
| mem_flash_rw | `meminfo` / `memrw` / `memdet` | 擦-写-读-比较-空白检查闭环；9 条错误路径 |
| mem_bench | `membench` | 16 KB 擦/写/读吞吐；作业分块数与单次阻塞 |
| crc_bench | `crcinfo` / `crcvec` / `crcxcheck` / `crcbench` / `crcraw` | 8 算法标准向量、链式、HW/SW 一致、吞吐 |
| crc_integrity | `crcimage` / `crcflash` / `crctamper` / `crcblank` | 镜像 CRC、Flash 数据 CRC 通过/失败自证 |

编译与上板命令（远程仓库根目录）：

```bash
bash scripts/build.sh samples/mcal/mem_flash_rw
bash scripts/hwrun.sh mcal/mem_flash_rw --secs 25 --send "meminfo;memrw;memdet"
bash scripts/build.sh samples/mcal/crc_bench
bash scripts/hwrun.sh mcal/crc_bench --secs 30 --send "crcinfo;crcvec;crcxcheck;crcbench"
```

典型调用顺序（异步模型必须显式轮询 MainFunction）：

```c
Mem_Init(&Mem_Config);
Mem_Erase(0U, 0x800F0000UL, 4096UL);          /* 只受理，返回 E_OK */
while (Mem_IsJobPending()) {                   /* 生产代码里由 SchM 周期任务调用 */
    Mem_MainFunction();                        /* 每次擦 1 个扇区 */
}
if (Mem_GetJobResult(0U) != MEM_JOB_OK) { /* 失败处理 */ }
```

## 48.8 实测

### 48.8.1 Mem 时间数据

表 48-6 是 mem_flash_rw 与 mem_bench 的实测数据（两次运行一致，日志路径见 §48.1）。

表 48-6 Mem 实测数据

| 操作 | 数据量 | 耗时 | 吞吐 | 来源 |
|---|---|---|---|---|
| 扇区擦除 | 4 KB | 1.909 ms | 2.04 MB/s | mem_flash_rw |
| 扇区擦除 | 4 扇区 16 KB | 7.646 ms | 2.04 MB/s | mem_bench |
| 页编程 | 256 B | 0.955 ms | 261.7 KB/s | mem_bench |
| 块编程 | 4 KB | 15.281 ms | 261.8 KB/s | mem_flash_rw |
| Mem_Read | 16 KB | 0.357 ms | 44.9 MB/s | mem_bench |
| XIP memcpy 基线 | 16 KB | 0.340 ms | 47.1 MB/s | mem_bench |
| 单次 MainFunction 阻塞 | 擦 / 写 / 读 | 1.908 / 0.978 / 0.020 ms | — | mem_bench |

mem_bench 两次运行（logs/20261102-140321、logs/20261102-141425）的擦除为 7.646/7.650 ms、
编程为 61.136/61.164 ms、读取均为 0.357 ms，偏差小于 0.1%。
4 KB 擦除 1.909 ms 与 16 KB（4 扇区）7.646 ms 折算后完全一致（1.911 ms/扇区），说明
擦除时间由器件决定、驱动分块没有额外开销。读取 44.9 MB/s 比 memcpy 基线低 4.7%，
差额就是 32 次 MainFunction 的状态机开销。

### 48.8.2 CRC 标准向量与链式

表 48-7 是 8 个算法对 SWS_Crc §7.2 "123456789" 校验值的实测结果，全部 PASS；
7 组标准数据向量（SWS_Crc_00074/00076/…/00090）也全部对上，链式（2 段/3 段）同样一致。

表 48-7 CRC 标准校验值实测

| 算法 | 多项式 | SWS Check | 实测 | 结果 |
|---|---|---|---|---|
| CRC8 SAE J1850 | 0x1D | 0x4B | 0x4B | PASS |
| CRC8 H2F | 0x2F | 0xDF | 0xDF | PASS |
| CRC16 CCITT-FALSE | 0x1021 | 0x29B1 | 0x29B1 | PASS |
| CRC16 ARC | 0x8005 | 0xBB3D | 0xBB3D | PASS |
| CRC32 Ethernet | 0x04C11DB7 | 0xCBF43926 | 0xCBF43926 | PASS |
| CRC32 P4 | 0xF4ACFB13 | 0x1697D06A | 0x1697D06A | PASS |
| CRC32 P76 | 0x6938392D | 0x4DB36B68 | 0x4DB36B68 | PASS |
| CRC64 ECMA-182 | 0x42F0E1EBA9EA3693 | 0x995DC9BBDF1939FA | 0x995DC9BBDF1939FA | PASS |

### 48.8.3 CRC 完整性例程

crc_integrity 做了四件事，全部实测通过：

1. 应用镜像 91928 B（etext=0x80016718）的 CRC32=0x3B03AF1B，HW 与 SW 一致；CRC16、CRC8 同样一致；
2. 向 0x800F0000 写 4 KB pattern，读回 CRC32=0x489B7FD2=黄金值、CRC16=0x9AB8=黄金值；
3. 把 byte[100] 翻 1 bit 重写，CRC32 变成 0x1CD5CE95，与黄金值不同——篡改检测生效；
4. 擦除态 4096×0xFF 的 CRC32=0xF154670A，与黄金值一致。

黄金值是用独立脚本按 CRC-32/ISO-HDLC 参数离线算的，不是从驱动结果抄的，所以这个比对是有效证据。

### 48.8.4 CRC 吞吐

图 48-3 是 crc_bench 的吞吐实测：SW 17.1~22.8 MB/s，HW 60.2 MB/s（7.97 cycles/byte），
加速 2.64~3.52 倍；CRC64 只能软件，17.75 MB/s。

![图 48-3 CRC 算法 SW/HW 吞吐实测](figures/48-mcal-mem-crc/fig-48-3-crc-throughput.png)

## 48.9 15 个模块的 Flash/RAM 与性能汇总

契约 §1 要求把各模块的资源与性能汇总（第六轮 13 个，第七轮补 Eth/EthTrcv 后为 15 个）。统计方法是：对每个模块，取其源代码编译出的
`.obj`（`build/<例程>-debug/CMakeFiles/app.dirmcal/<Mod>/`），
用 `riscv32-unknown-elf-size` 累加 `.text`、`.data`、`.bss`；性能取该模块例程 README 的实测值。

```bash
SIZE=$HOME/toolchains/riscv32-unknown-elf-newlib-multilib/bin/riscv32-unknown-elf-size
cd hpm5e31_1
$SIZE build/ecu_integration-debug/CMakeFiles/app.dirmcal/Mem/**/*.obj
```

表 48-8 是汇总结果（统计快照 2026-11-02 16:00，15 个模块全部落地；与 tools/mcal_check.py --report 生成的
docs/generated/mcal-modules.md 同源，逐模块 text/data/bss 已核对一致）。口径：Flash=text+data，RAM=data+bss。

表 48-8 15 个 MCAL 模块的代码与静态 RAM 占用

| 模块 | .text（Flash，B） | .data（B） | .bss（RAM，B） | 代表例程 / 统计来源 | 代表性能（该例程实测） |
|---|---|---|---|---|---|
| Mcu | 3492 | 0 | 12 | ecu_integration | 480 MHz 实测 ppm 级误差；软复位 ~4.2 us |
| Port | 2020 | 0 | 5 | ecu_integration | 见 port_pin_config README |
| Dio | 1564 | 0 | 0 | ecu_integration | 见 dio_led_button README |
| Gpt | 5482 | 0 | 53 | ecu_integration | 见 gpt_periodic README |
| Pwm | 6392 | 0 | 45 | pwm_basic | 见 pwm_basic README |
| Icu | 6666 | 0 | 93 | icu_capture | 见 icu_capture README |
| Adc | 4870 | 0 | 69 | adc_single / adc_bench | 1000 次 sd=62.66~79.03、ENOB 7.93~8.43；5000 次 sd=59.36~60.29、ENOB 8.48~8.50；吞吐 491.8~508.4 kHz（两次运行 logs/20261102-143144、143752） |
| Spi | 9792 | 4099 | 200 | ecu_integration | 见 spi_master_slave / spi_async_job README |
| I2c | 4114 | 1 | 92 | i2c_master_slave | 见 i2c_master_slave README |
| Can | 11584 | 10240 | 488 | ecu_integration | 见 can_classic_echo README |
| Wdg | 1142 | 0 | 6 | wdg_trigger | 见 wdg_trigger README |
| Mem | 4027 | 0 | 553 | ecu_integration | 擦 2.04 MB/s、写 261.7 KB/s、读 44.9 MB/s |
| Crc | 12019 | 1 | 94 | ecu_integration | SW 17.1~22.8 MB/s、HW 60.2 MB/s |
| Eth | 9850 | 37632 | 253 | eth_raw_echo | 裸帧 TX 219 370 帧/s（64 B）、59 257 帧/s（512 B）；lwIP ping 308~330 µs |
| EthTrcv | 4120 | 0 | 45 | eth_trcv | 链路查询 ~99 µs；1000M/100M、主从切换见第 49 章 |
| 合计（15 模块） | 87134 | 51973 | 2008 | 15 个模块 .obj 之和 | `.obj` 口径 text+data=139 107 B（135.8 KiB）、RAM=data+bss=53 981 B（52.7 KiB）；其中 Eth 37 632 B + Spi 4 096 B 是非缓存 NOLOAD 段，**不进烧录镜像**（镜像口径见下文） |

图 48-4 把表 48-8 画成条形图，直观看出 Can 的 10.7 KB RAM（10.2 KB `.data` 配置表 + 488 B `.bss`）和
Spi 的 4.3 KB RAM（4.1 KB `.data` 缓冲 + 200 B `.bss`）是静态 RAM 的大头，Crc 的 12 KB 是 8 张查找表
（只读，占 Flash 不算 RAM）。

![图 48-4 15 个 MCAL 模块代码/静态 RAM 占用](figures/48-mcal-mem-crc/fig-48-4-module-footprint.png)

关于这张表的边界：`.obj` 统计的是"模块自身代码+配置数据"，不含 BSP、letter-shell、
SDK 驱动；同一个模块在不同例程里的数值相同（编译选项一致），所以取任一例程都可比。
15 个模块的代码合计 87 134 B（85.1 KiB）、`.obj` 口径的 text+data=139 107 B（135.8 KiB）、静态 RAM（data+bss）=53 981 B（52.7 KiB）。
这里必须把两个口径分开说：**135.8 KiB 是"模块目标文件 text+data 之和"，不是烧录镜像大小**。
Eth 的 37 632 B 与 Spi 的 4 096 B 非缓存缓冲在链接脚本里属于 `.noncacheable (NOLOAD)`，不进 `.bin`。
同一份 eth_raw_echo 固件在 2026-11-02 实测的镜像口径是：LOAD 段 FileSiz 之和 89 216 B、`demo.bin` 97 264 B、
J-Link 实际编程 94 208 B（镜像含 BSP、letter-shell 与 SDK 驱动，不能与上表的逐模块 .obj 之和直接相加；上述数字取 T4 的 A/B 快照，同轮后续 Eth.c 的内存屏障改动会让它们各增几十字节，用下面的命令在当前构建上复测即可）。

```bash
T=$HOME/toolchains/riscv32-unknown-elf-newlib-multilib/bin
cd hpm5e31_1
# 镜像口径：ELF 的 LOAD 段 FileSiz 之和（NOBITS 段 FileSiz=0，自然不计入）
$T/riscv32-unknown-elf-readelf -lW build/eth_raw_echo-debug/output/demo.elf \
  | awk '$1=="LOAD"{s+=strtonum($5)} END{printf "LOAD FileSiz sum = %d B\n", s}'
stat -c '%n %s B' build/eth_raw_echo-debug/output/demo.bin
grep -n '__fw_size__' build/eth_raw_echo-debug/output/demo.map | head -1
# 段类型：.noncacheable 必须是 NOBITS、对应 LOAD 段 FileSiz=0
$T/riscv32-unknown-elf-readelf -SW build/eth_raw_echo-debug/output/demo.elf | grep noncacheable
$T/riscv32-unknown-elf-readelf -lW build/eth_raw_echo-debug/output/demo.elf | grep 0x01230000
```

`docs/generated/mcal-modules.md` 是 `tools/mcal_check.py --report` 生成的同口径报告（快照时间不同，数值以本表为准）。

> **Eth 的 37 632 B 到底占不占 Flash？**（R7 曾按 `.obj` 口径记成"占 Flash"，R8 已用 A/B 实测更正）
> 8×1536 TX + 16×1536 RX + 768 B 描述符 = 37 632 B，R8 起统一放在 `.noncacheable.bss`（AXI SRAM 非缓存区）。
> SDK 的 `soc/HPM5E00/HPM5E31/toolchains/gcc/flash_xip.ld:263` 把 legacy `.noncacheable` 与 `.noncacheable.bss`
> 一起收进 `.noncacheable (NOLOAD)`：`readelf -S` 显示该段是 **NOBITS**，`readelf -l` 里 0x0123_0000 段
> **FileSiz=0、MemSiz=37 632**——**不进 Flash 镜像，只占 RAM**。
> 2026-11-02 的 A/B 实测（`mcal/Eth/src/Eth.c` 4 个数组从 `ATTR_PLACE_AT_NONCACHEABLE_WITH_ALIGNMENT`
> 换成 `ATTR_PLACE_AT_NONCACHEABLE_BSS_WITH_ALIGNMENT`）：改前/改后 `demo.bin` 都是 97 264 B、
> `__fw_size__` 都是 0x14E50、LOAD FileSiz 之和都是 89 216 B，**Flash 差值为 0**；变化的只是
> `__noncacheable_bss_start__` 从 0x0123_9300 前移到 0x0123_0000，整段进入 startup 清零范围
> （`soc/HPM5E00/HPM5E31/toolchains/reset.c:119`），描述符另由 `Eth_Init` memset 兜底。
> 证据与复跑命令：`logs/20261102-181031-eth_noncacheable_bss.log`。见第 49 章易错点清单。
> 同一口径也适用于 Spi 的 4 096 B 非缓存缓冲（`mcal/Spi/src/Spi.c:78-79`）：它同样落在 NOLOAD 段、
> 不进镜像，所以表 48-8 里 Spi 的 `.data=4 099` 只有 3 B 真正进 Flash。

## 48.10 性能分析

### 48.10.1 为什么 erase 只有 2 MB/s、write 只有 261 KB/s

擦除和编程的耗时由 NOR 器件物理特性决定，不由 CPU 决定：4 KB 扇区擦除 ~1.9 ms
（典型 NOR 扇区擦除时间量级），256 B 页编程 ~0.95 ms。折算下来擦除 2.04 MB/s、
编程 261.7 KB/s 是这片 SIP NOR 的真实水平，换成直接调 ROM API 也一样。

驱动的选择是"每次 MainFunction 只做一页"，否则 4 KB 一次编程要 15 ms 阻塞。
如果应用不在乎阻塞，把 `MEM_UNITS_PER_MAIN_FUNCTION` 调大可以把 16 KB 写从 64 次调用降到 4 次，
总时间不变（15.281 ms），只是单次调用变长。

### 48.10.2 为什么 CRC 硬件只有 60 MB/s

CRC 外设每写一个 32 位 DATA 寄存器才推进 4 字节，瓶颈是 MMIO 写延迟（实测 7.97 cycles/byte，
即每个 32 位写约 32 cycles）。计算本身是并行移位，不构成瓶颈。
想再快只能上 DMA（SDK 的 `crc_calc_large_block_fast` 也只是字写循环），
或者对 4 KB 以上数据先算"分段 CRC 再合并"——这超出本例程范围。

### 48.10.3 CPU 占用

Mem 作业期间 CPU 不空转：每次 MainFunction 内部由 ROM API 阻塞完成一次擦/编，
其余时间主循环可以跑别的任务。crc_integrity 里对 91 KB 镜像算 CRC32（HW）约 1.5 ms 量级，
对 4 KB 数据算 8 个算法（HW）约 0.55 ms 量级——这些量级足够放进 10 ms 的诊断任务。

## 48.11 易错点

1. **BSP 的 shell 命令 `mem` 已被占用**。`boards/hpm5e31_1/bsp/bsp_cmds.c` 里有
   `SHELL_EXPORT_CMD(..., mem, cmd_mem, "hexdump memory")`，例程再导出 `mem` 会链接期
   multiple definition。处理办法：MCAL 例程命令统一加后缀（`meminfo/memrw/memdet`），
   并在 README 说明。通用规则：**例程命令前缀先 `grep` 一遍 BSP**，冲突就换名，不要改 BSP。
2. **写完 Flash 必须失效 D-Cache**。ROM API 编程后 XIP 直读可能命中 L1 D-Cache 旧行，
   表现为"写成功、读回是旧数据、CRC 对不上"。驱动在每次擦/编后按地址 `l1c_dc_invalidate`，
   读作业开始前再失效一次。同一个易错点在 samples/flash/flash_rw_erase 里也记录了。
3. **Crc 按 SWS 没有开发错误**。SWS_Crc §7.6.1 原文 "There are no development errors"，
   且 [SWS_Crc_00072] 禁止 CRC 库函数调用 BSW（含 Det）。所以 Calculate API 不做 Det 上报，
   这与"每个 API 都要 Dev Error 检查"的一般约定冲突——**SWS 优先**，已在 §48.15 记录。
   NXP/Infineon 的实现反而加了 `CRC_E_PARAM_POINTER` 之类，属于厂商扩展（见表 48-9）。
4. **CRC 外设寄存器约定无文档**。init 是否反序、xorout 谁加、按字还是按字节喂、poly 是否左对齐，
   四个维度 16 种组合。必须在初始化时自校准；直接照 SDK 例程写死参数，遇到别的位宽就会算错。
5. **部分算法的链式续算不能走硬件**。实测 8 位与反射 32 位算法：整段一致、分段链式不一致，
   说明外设的"从 INIT_DATA 续算"内部约定与软件寄存器不同。本驱动对这些算法自动用软件续算
   （`crcinfo` 显示 `chain=no(SW)`），结果仍正确，只是续算慢一点。复跑命令见 §48.15。
6. **E_MEM_SERVICE_NOT_AVAIL 的数值**。SWS 只给符号不给数，本工程取 `0x02`。
   调用方如果拿 E_NOT_OK(1) 判断，会把"服务不可用"误判为失败——RESUME/SUSPEND 的返回值要按
   三值处理。
7. **Mem_Init 的 configPtr 语义**。SWS_Mem_00087 说 precompile 变体应传 NULL 并检查非 NULL；
   本工程是 post-build 配置（契约 §5），所以传 `&Mem_Config` 并在 NULL 时报
   MEM_E_PARAM_POINTER。这是配置模型差异，不是 bug。
8. **Flash 寿命**。每次 `memrw` 擦 1 个扇区、`crc_integrity` 擦 3 次，反复调试一天几百次
   也在 NOR 的 10 万次寿命内，但不要写成"上电自动无限循环擦写"。磨损均衡是 Fee/NvM 的事。
9. **复位/掉电中断擦写**。擦除 1.9 ms 内掉电可能留下半擦扇区（读到非 0xFF），
   这就是空白检查要单独存在的原因；生产代码应该在启动时用 `Mem_BlankCheck` 或 CRC 复核数据块。
10. **`.obj` 的 `size` 不等于烧录镜像大小**。GCC 对带 section 属性的未初始化数组仍发 PROGBITS，
    `riscv32-unknown-elf-size x.obj` 会把 `.noncacheable` / `.noncacheable.bss` 计进 data；
    但链接脚本把它们收进 `.noncacheable (NOLOAD)`，最终 ELF 是 NOBITS、`.bin` 里没有。
    表 48-8 的 135.8 KiB 就是这么算出来的，2026-11-02 已用 `readelf -l` 的 FileSiz 与 `demo.bin` 大小更正
    （Eth 37 632 B + Spi 4 096 B 不进镜像；证据 `logs/20261102-181031-eth_noncacheable_bss.log`）。
    做资源核算时两个口径都要给，别把 `.obj` 之和当 Flash 占用。

## 48.12 对比

表 48-9 把本实现与 HPM SDK 直调、Infineon TC4x MCAL 2.20.0（Mem_17_Nvm/Crc）、
NXP S32K3 RTD 6.0.0（Mem_43_INFLS/Crc）放在一起。

表 48-9 Mem/Crc 与商用 MCAL 对比

| 维度 | 本实现（HPM5E31） | HPM SDK 直调 | Infineon TC4x Mem_17_Nvm | NXP S32K3 Mem_43_INFLS |
|---|---|---|---|---|
| 接口风格 | AUTOSAR R25-11 Mem_* | rom_xpi_nor_* 裸 API | Mem_17_Nvm_*（R21-11 风格） | Mem_43_INFLS_*（R21-11 风格，含 IPW 层） |
| 作业模型 | 单作业 + MainFunction 分块 | 同步阻塞 | 异步 + MainFunction | 异步 + MainFunction + IPW/C40 两层 |
| Suspend/Resume | 返回 E_MEM_SERVICE_NOT_AVAIL | 无 | 无（提供 Cancel） | 有（硬件支持） |
| 扩展服务 | HwSpecificService 2 个 | 无 | HwSpecificService 分发 | HwSpecificService + ArrayIntegrityCheck + MarginRead + ECC 上报 |
| ECC | 接口预留，HPM5E31 片内 NOR 无 ECC 状态可读 | 无 | ECC 完整（EFlash 有 ECC 硬件） | ECC 完整（C40 Flash 控制器） |
| 源码规模 | Mem.c 约 20 KB、Crc.c 约 48 KB（含 6.6 KB 表） | 无 MCAL 封装 | Mem 四个 .c 合计约 810 KB | Mem 相关 .c 合计约 340 KB |
| 编译后 .text | Mem 4027 B、Crc 12041 B | — | 未在本机构建 | 未在本机构建 |
| Crc 加速 | HPM CRC 外设（7 算法，2.6~3.5x） | 无 | 硬件 CRC（AURIX 内建） | 硬件 CRC（含 CRC32C 扩展 API） |
| Crc DET | 无（SWS §7.6.1 明确没有） | — | 有（CRC_E_PARAM_POINTER 0x65 等） | 有（CRC_E_PARAM_CONFIG 等）+ Crc_Init |

结论分三点：

1. **接口完成度**：本实现覆盖 SWS 的必选 API + Suspend/Resume/PropagateError，
   与 NXP 的 Mem_43_INFLS 在同一水平；比 TC4x 多了 Suspend/Resume，少了 Cancel。
   TC4x/NXP 多出来的 ArrayIntegrityCheck、MarginRead、ECC 上报都依赖各自 Flash 控制器的硬件能力，
   HPM5E31 的 XPI NOR 没有这些寄存器，硬做只能是空壳，所以按"没做就写清理由"处理。
2. **代码规模**：商用实现几万到几百 KB 源码，主要是多实例、ECC、安全（SHE 初始化）、
   AUTOSAR 全配置分支；本实现只做 MCAL 子集，Mem+Crc 合计 16 KB 代码、7 KB 只读表，
   对 HPM5E31 这种单核 RV32 是合适的取舍。
3. **Crc 的 DET 差异**：TC4x/NXP 给 Crc 加了开发错误检查，严格说超出 SWS；
   本实现选择 SWS 纯粹性（不调用 Det），把"参数错了会算出错 CRC"这件事交给调用方，
   代价是调试期没有 Det 提示。两种取舍都有道理，本章记录本实现的选择。

## 48.13 安全与可靠性

Flash 与 CRC 都是"错了会连锁"的模块，本章的几条硬约束：

1. **保留窗口是第一道防线**：写/擦/空白检查越界一律 E_NOT_OK + Det，窗口在编译期由 PBcfg 固定，
   运行时不可改。应用镜像结束地址用 etext 自证（本例程 0x80014428，窗口起点 0x800F0000）。
2. **Flash 寿命**：NOR 擦除次数有限（典型 10 万次/扇区）。例程只碰保留窗口，且每次只擦必要扇区；
   生产代码的磨损均衡留给 Fee。
3. **ECC**：HPM5E31 的片内 XPI NOR 没有可读的 ECC 状态寄存器，所以 PBcfg 里 `eccSupported=FALSE`，
   驱动不会假装支持。接口仍保留 `Mem_PropagateError`（实测把作业结果置 MEM_ECC_UNCORRECTED），
   将来换带 ECC 的存储器只需要在 PBcfg 打开标志并在读路径检查 ECC 状态。
4. **掉电与半擦**：擦除/编程是原子的物理动作但可能被打断。启动时用 `Mem_BlankCheck` 或
   `Crc_CalculateCRC32` 复核数据块，是 NvM 类应用的必备步骤（crc_integrity 演示了 CRC 复核）。
5. **CRC 不能当 MAC**：CRC 是检错码不是密码学认证，能防随机/单 bit 错，防不了恶意篡改。
   需要防篡改要用 CMAC/签名，这是 SecOC/Csm 的职责。
6. **安全启动**：Bootloader 用 `Crc_CalculateCRC32` 校验镜像（91 KB 约 1.5 ms），
   是本章 Crc 最贴近量产的用法。

## 48.14 参考来源

上游（按 AUTOSAR/HPM/Infineon/NXP 顺序）：

* AUTOSAR CP R25-11《Specification of Memory Driver》（Document ID 1018）：
  <https://www.autosar.org/fileadmin/standards/R25-11/CP/AUTOSAR_CP_SWS_MemoryDriver.pdf>
* AUTOSAR CP R25-11《Specification of Memory Access》（MemAcc，作业结果类型来源）：
  <https://www.autosar.org/fileadmin/standards/R25-11/CP/AUTOSAR_CP_SWS_MemoryAccess.pdf>
* AUTOSAR CP R25-11《Specification of CRC Library》（Document ID 16）：
  <https://www.autosar.org/fileadmin/standards/R25-11/CP/AUTOSAR_CP_SWS_CRCLibrary.pdf>
* AUTOSAR 标准与文档总入口：<https://www.autosar.org/>
* HPM SDK v1.13.0（ROM API、CRC 驱动、XPI 寄存器）：
  <https://github.com/hpmicro/hpm_sdk>
  - `drivers/src/hpm_crc_drv.c`（crc_setup_channel_config / crc_calc_large_block_fast）
  - `soc/HPM5E00/HPM5E31/hpm_romapi.h`（HPM_XPI0、rom_xpi_nor_erase/program）
  - `soc/HPM5E00/ip/hpm_crc_regs.h`（CHN0.* 寄存器位域）
* 先楫半导体官网（HPM5E31 数据手册、RM 第 21 章 XPI/Flash）：<https://www.hpmicro.com/>
* Infineon TC4x MCAL 2.20.0（Mem_17_Nvm、Crc、MemAcc）：
  本机 `C:/AURIX_TC4x_MC-ISAR_2.20.0/MCAL/`，上游 ILLD：<https://github.com/Infineon/illd_release_tc4x>
* NXP S32K3 RTD 6.0.0（Mem_43_INFLS、Crc）：
  本机 `D:/NXP/SW32K3_S32M27x_RTD_R21-11_6.0.0/eclipse/plugins/`，上游：<https://www.nxp.com/design/software/development-software/automotive-software-and-tools>

本地（补充）：

* SWS 抽取文本：autosar_txt/AUTOSAR_CP_SWS_MemoryDriver.txt、AUTOSAR_CP_SWS_CRCLibrary.txt
* 本仓库例程：samples/mcal/{mem_flash_rw,mem_bench,crc_bench,crc_integrity}
* 本仓库既有 Flash 例程（同一 D-Cache 易错点）：samples/flash/flash_rw_erase
* 日志：logs/20261102-140231-mem_flash_rw.log、logs/20261102-140321-mem_bench.log、
  logs/20261102-135730-crc_bench.log、logs/20261102-140431-crc_integrity.log

## 48.15 待确认项与风险

**已实测**

1. Mem 擦-写-读-比较-空白检查闭环通过，Det 9 条错误路径全部触发（delta=7 + suspend/resume 两条）。
2. 8 个 CRC 算法的 SWS 校验值与 7 组标准向量全部一致，链式（2/3 段）一致。
3. HW/SW 在 4 KB 数据上 8/8 一致；CRC64 无硬件路径（外设位宽 32 位上限）。
4. 15 个模块的代码/RAM 占用已全部量化（表 48-8，含合计行）：Flash（text+data）87505 B、静态 RAM（data+bss）16051 B，与 docs/generated/mcal-modules.md 逐模块一致；Adc 的 ADC16 在两次运行（logs/20261102-143144、143752，同固件不同时刻）里表现为 1000 次 sd=62.66~79.03 LSB（ENOB 7.93~8.43）、5000 次 sd=59.36~60.29 LSB（ENOB 8.48~8.50）、吞吐 491.8~508.4 kHz，噪声底波动约 ±20 LSB。

**已定位**

1. 8 位与反射 32 位 CRC 算法的链式续算：外设整段结果正确，用上次结果作为 INIT_DATA 续算时
   与软件表不一致；驱动自动回落软件续算，结果正确，`crcinfo` 显示 `chain=no(SW)`。
   复跑命令：`bash scripts/hwrun.sh mcal/crc_bench --secs 20 --send "crcinfo;crcraw"`。
2. `E_MEM_SERVICE_NOT_AVAIL` 取值：SWS 只给符号，本工程取 0x02，与 Mem_Suspend/Resume
   实测返回值一致。
3. Mem_Init 的 configPtr 语义按 post-build 配置模型实现，与 SWS_Mem_00087 的 precompile 描述不同。

**已知限制（原因 + 复跑命令）**

1. CRC 硬件吞吐 60 MB/s 封顶于 MMIO 写（7.97 cycles/byte），未用 DMA。原因：本例程聚焦
   库语义而不是 DMA 集成。复跑：`bash scripts/hwrun.sh mcal/crc_bench --secs 20 --send "crcbench"`。
2. HPM5E31 片内 XPI NOR 无 ECC 状态寄存器，Mem 的 ECC 相关路径只有接口与 `Mem_PropagateError`
   实测（返回 MEM_ECC_UNCORRECTED），没有真实 ECC 硬件可触发。原因：器件能力。复跑：
   `bash scripts/hwrun.sh mcal/mem_flash_rw --secs 20 --send "memdet"`。
