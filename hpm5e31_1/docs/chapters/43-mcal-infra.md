# 第 43 章 MCAL 基础设施、配置模型与集成

## 43.1 文档信息

| 项 | 内容 |
|---|---|
| 文档版本 | v1.3.0（2026-11-02，R6 MCAL） |
| 适用芯片/板卡 | HPM5E31IPB1 / hpm5e31_1 |
| 对应例程 | `samples/mcal/mcal_hello/`、`samples/mcal/ecu_integration/` |
| 实测日期 | 2026-11-02 |
| 可信度 | 高（类型/DET/构建/尺寸均实测）；"与 EB tresos 流程对比"为中（基于公开文档） |

## 43.2 本章导览

- 学到什么：MCAL 工程里"不写外设代码"的那一半——AUTOSAR 基础类型、Det/Dem 错误处理、预编译/链接期/post-build
  三种配置阶段、CMake 构建链、启动顺序与 MemMap；以及这些基础设施在 HPM5E31 上的实测开销。
- 前置章节：第 42 章（MCAL 总览）；第 02 章（工具链与烧写）。
- 配套：第 44~49 章每个模块的 `*_Cfg.h`/`*_PBcfg.c` 都按本章的模型写。
- 一句话结论：**MCAL 的"标准"主要体现在这些基础设施上**——同样的 API 名、同样的错误码、同样的配置阶段；
  把这个骨架搭对，剩下的外设驱动就是"填寄存器 + 填配置表"。

## 43.3 概述

MCAL 工程可以拆成"骨架"和"血肉"两部分。骨架就是本章讲的五件事：

| 组成 | 文件/目录 | 作用 | 谁维护 |
|---|---|---|---|
| 类型系统 | `mcal/common/Std_Types.h`、`Platform_Types.h`、`Compiler.h` | AUTOSAR 类型、编译抽象、32 位 RV32 映射 | Lead |
| 错误处理 | `mcal/common/Det.[ch]`、`Dem.[ch]` | 开发错误上报/记录/查询，生产错误桩 | Lead |
| 全局开关 | `mcal/common/Mcal_Cfg.h`、`Mcal_ModuleId.h`、`Mcal_Version.h` | DEV_ERROR_DETECT、Dem 开关、模块 ID、版本 | Lead |
| 内存映射 | `mcal/common/MemMap.h` | AUTOSAR 段宏的"空桩"实现 | Lead |
| 构建辅助 | `mcal/mcal.cmake` | 例程 CMakeLists 里 `mcal_add(模块...)` 一键接入 | Lead |

表 43-1 的五项合起来只有约 600 行代码，却是 15 个模块的共同底座；改动它们等于改动所有模块，所以本轮由
Lead 统一维护，模块负责人只读不改。

## 43.4 工作原理

### 43.4.1 类型系统：为什么不能直接用 SDK 的 `uint32_t`

AUTOSAR 要求 MCAL 公共 API 用 `uint8/uint16/uint32/sint8...`（来自 `Platform_Types.h`），而不是 C99 的
`uint32_t`。原因有三：① 标准只保证"至少 N 位"，AUTOSAR 类型把宽度钉死；② `boolean`/`Std_ReturnType`
是标准类型，跨厂商头文件才能对齐；③ 编译抽象（`P2VAR`/`FUNC` 等宏）让同一份代码能配不同编译器/内存保护。

HPM5E31 是 32 位 RV32、小端，映射关系见表 43-2。实测（`mcal_hello` 的 `mcal` 命令）：
`uint8/16/32/64 = 1/2/4/8 B`、`Std_ReturnType = 1 B`，与标准一致。

| AUTOSAR 类型 | C 类型（GCC for RISC-V） | 宽度 | 典型用途 |
|---|---|---|---|
| `boolean` | `unsigned char` | 1 B | TRUE/FALSE |
| `uint8` / `sint8` | `unsigned char` / `signed char` | 1 B | 通道号、错误码 |
| `uint16` / `sint16` | `unsigned short` / `short` | 2 B | 分频、计数值 |
| `uint32` / `sint32` | `unsigned int` / `int` | 4 B | 频率、地址、时间戳 |
| `uint64` | `unsigned long long` | 8 B | 64 位时间/CRC64 |
| `float32` / `float64` | `float` / `double` | 4/8 B | 仅测试/换算 |
| `Std_ReturnType` | `uint8` | 1 B | E_OK/E_NOT_OK |

编译抽象由 `Compiler.h` 提供（表 43-3）。本工程不做内存保护，所以 `memclass/ptrclass` 参数都展开为空；
保留宏形态是为了将来接 MPU/OS 时只改一个文件。

| 宏 | 展开 | 用途 |
|---|---|---|
| `FUNC(ret, memclass)` | `ret` | 函数定义 |
| `P2VAR(t, memclass, ptrclass)` | `t *` | 输出指针参数 |
| `P2CONST(t, memclass, ptrclass)` | `const t *` | 输入指针参数 |
| `CONSTP2VAR`/`CONSTP2CONST` | `t * const`/`const t * const` | 常量指针 |
| `VAR(t, memclass)` | `t` | 变量定义 |
| `INLINE` | `static inline` | 内联函数 |
| `MCAL_SECTION(x)` | `__attribute__((section(x)))` | 指定段（可选） |

### 43.4.2 Det/Dem：错误处理的两条路

AUTOSAR 把错误分两类：

- **开发错误（Development Error）**：参数为空、模块未初始化、配置非法——只可能出现在开发阶段，上报 `Det`；
- **生产错误（Production Error）**：总线错误、硬件故障——量产也要处理，上报 `Dem`（本仓库只给计数桩）。

Det 的实现很简单（`mcal/common/Det.c`，558 B 代码 + 54 B RAM）：8 条环形记录 + 计数 + 查询接口。
真正要注意的是**调用规范**：每个 API 入口先检查初始化状态，再检查参数，错误时用
`MCAL_DET_REPORT(<MOD>_MODULE_ID, instance, <MOD>_API_ID_XXX, <MOD>_E_XXX)`，然后返回 `E_NOT_OK`。
`MCAL_DEV_ERROR_DETECT=STD_OFF` 时这个宏展开为空，零开销。

| 错误码（`Det.h`） | 值 | 典型场景 |
|---|---|---|
| `DET_E_PARAM_POINTER` | 0x01 | 传了 `NULL_PTR` |
| `DET_E_PARAM_VALUE` | 0x02 | 通道号越界、频率非法 |
| `DET_E_PARAM_CONFIG` | 0x03 | 配置表非法（通道数与数组不匹配） |
| `DET_E_UNINIT` | 0x04 | 未 `Init` 就调用其他 API |
| `DET_E_INIT_FAILED` | 0x05 | 时钟/PLL 未锁定 |
| `DET_E_BUSY` | 0x07 | 异步作业未完成又发起新作业 |

表 43-4 的错误码与 SWS 的通用错误码对齐；各模块自己的 `*_API_ID_*` 常量写在自己的头文件里。

### 43.4.3 配置模型：预编译、链接期与 post-build

AUTOSAR 允许三种配置阶段，本仓库三种都"落"了，但只用两种（图 43-1）：

![图 43-1 MCAL 三种配置阶段与代码/数据的落点](figures/43/fig-43-1-config-stages.png)

| 阶段 | 载体 | 何时生效 | 本仓库用法 |
|---|---|---|---|
| Pre-compile | `mcal/<Mod>/cfg/<Mod>_Cfg.h` | 编译时（`#if`） | 模块开关、通道数、DEV_ERROR_DETECT、可选 API |
| Link-time | 外部 `const` 指针 | 链接时 | 未单独实现（用 post-build 替代，保留扩展点） |
| Post-build | `mcal/<Mod>/cfg/<Mod>_PBcfg.c` | 运行前（加载配置） | 引脚/实例/时钟/回调/时序参数 |

表 43-5 是三种阶段的对比。为什么本仓库跳过 link-time？因为 link-time 配置（多个配置集链接期选择）在裸机
单配置场景没有收益，反而多一层指针；post-build 的 `const <Mod>_ConfigType <Mod>_Config` 已经能实现
"换板只换 PBcfg"。

| 维度 | Pre-compile | Link-time | Post-build |
|---|---|---|---|
| 配置粒度 | 编译单元 | 链接单元 | 运行时配置集 |
| 修改代价 | 重新编译 | 重新链接 | 只改配置/重新烧写 |
| 代码尺寸 | 最小（可裁掉分支） | 中 | 最大（查表） |
| 本仓库 | 使用 | 未用 | 使用 |

Post-build 配置长这样（以 Port 为例，真实代码见第 44 章）：

```c
/* mcal/Port/cfg/Port_PBcfg.c —— 板级映射只出现在这里 */
const Port_ConfigType Port_Config = {
    .pinConfigs = Port_PinConfig,
    .numPins    = (uint16)(sizeof(Port_PinConfig) / sizeof(Port_PinConfig[0])),
};
```

关键约定：**驱动 `.c` 里不许出现 `HPM_MCAN0`、`PA02` 这种板级常量**，实例/引脚/时钟全部从 `_PBcfg.c` 进来。
有了这条约定，换板（例如把 SPI1/SPI3 换成 SPI0/SPI2）只需改配置，驱动一行不动。

### 43.4.4 构建链：一个 `mcal_add` 把模块接进例程

![图 43-2 从例程 CMakeLists 到上板日志再到书稿的构建链](figures/43/fig-43-2-build-chain.png)

图 43-2 的左边是例程，右边是日志与书稿。例程的 `CMakeLists.txt` 只有五行（第 42 章例程模板），
关键是 `include(mcal/mcal.cmake)` 与 `mcal_add(...)`：

```cmake
include(${CMAKE_CURRENT_LIST_DIR}/../../../mcal/mcal.cmake)
mcal_add(Mcu Port Dio)          # 只编这三块 + common
sdk_app_src(src/main.c)
```

`mcal_add` 做三件事：① 加 `mcal/common` 头文件与 `Det.c/Dem.c`；② 对每个模块加 `include/`、`cfg/` 头文件目录；
③ 把 `src/*.c`、`cfg/*.c` 交给 `sdk_app_src()` 编译。好处是例程不用关心模块依赖，也不会把 15 个模块全编进去
（`mcal_hello` 只编 common，尺寸最小）。

构建链的实测命令与输出（`mcal_hello`）：

```text
$ bash scripts/build.sh samples/mcal/mcal_hello
[71/71] Linking C executable output/demo.elf
Memory region         Used Size  Region Size  %age Used
           FLASH:       75728 B         1 MB      7.22%
ELF: build/mcal_hello-debug/output/demo.elf
```

### 43.4.5 启动与集成顺序

![图 43-3 MCAL 初始化顺序与主循环/喂狗的关系](figures/43/fig-43-3-init-sequence.png)

图 43-3 是"最小 ECU"的初始化顺序，也是 `ecu_integration` 例程的主干：

1. `bsp_init()`：板级时钟/串口/调试控制台（SDK 提供，MCAL 不接管）；
2. `Det_Init()`：清错误计数，越早越好，这样后续 Init 的错误都能被记录；
3. `Mcu_Init()` → `Mcu_InitClock()` → 等 `Mcu_GetPllStatus()==MCU_PLL_LOCKED` → `Mcu_DistributePllClock()`：
   时钟是所有外设的前提；
4. `Port_Init()`：引脚复用/方向/上下拉，必须在所有外设 Init 之前（否则外设引脚可能是模拟态）；
5. 各外设 `Init()`：Gpt/Pwm/Icu/Adc/Spi/I2c/Can/Mem/Crc；
6. `Wdg_Init()` + 启动喂狗：看门狗要**最后**使能，否则前面初始化时间长会误复位；
7. 主循环：`bsp_shell_poll()` + 周期任务 + `Wdg_SetTriggerCondition()`。

这个顺序在 AUTOSAR 里由 EcuM 编排；本仓库没有 EcuM，用 `ecu_integration` 的 `main()` 手工实现同样的顺序，
并在文档里标注"接 AUTOSAR 时应由 EcuM_Init/EcuM_StartupTwo 调用"。

### 43.4.6 MemMap：为什么是空桩

AUTOSAR 驱动源码通常长这样：`#define MCU_START_SEC_CODE` / `#include "MemMap.h"` / 函数 / `#define MCU_STOP_SEC_CODE`。
MemMap 负责把这些段映射到链接脚本里的不同区域，配合 MPU 实现"驱动代码不可写、配置数据只读"等保护。

本仓库 `mcal/common/MemMap.h` 把所有段宏定义成空宏（第 42 章坑 3 提到过），原因：① 本工程没有 MPU/OS-Application；
② HPM SDK 的链接脚本是统一 `.text/.data/.bss`，强行分段会和 SDK 冲突。这样做的代价是**没有内存保护**，
属于第 50 章"量产差距清单"里的一条；如果将来要接 OS，需要按 OS 的 MemMap 规则重新落段。

## 43.5 关键"寄存器"与 API 对照

基础设施不直接操作外设寄存器，但每一层都有对应的"验证点"（表 43-6）：

| 基础设施 | 头文件/实现 | 关键 API/宏 | 验证例程与命令 | 实测证据 |
|---|---|---|---|---|
| 类型系统 | `Platform_Types.h` | `uint8..uint64`、`boolean` | `mcal_hello` 的 `mcal` | sizeof 1/2/4/8 |
| 编译抽象 | `Compiler.h` | `FUNC/P2VAR/INLINE` | 所有模块编译 | 0 warning |
| 版本信息 | `Mcal_Version.h` | `MCAL_SW_MAJOR_VERSION` | `mcal` | 打印 1.0.0 |
| 模块 ID | `Mcal_ModuleId.h` | `MCU_MODULE_ID` 等 13 个 | `mcal` | 与 SWS_BSWGeneral §11.2 一致 |
| 开发错误 | `Det.[ch]` | `Det_ReportError/GetLastError` | `det`、`detbench` | 62.09 cyc / 73.11 cyc |
| 生产错误 | `Dem.[ch]` | `Dem_ReportErrorStatus` | 各模块错误路径 | 计数增加 |
| 全局开关 | `Mcal_Cfg.h` | `MCAL_DEV_ERROR_DETECT` | 编译期 | 尺寸/周期对比 |
| 配置阶段 | `<Mod>_Cfg.h`/`_PBcfg.c` | `<Mod>_Init(&<Mod>_Config)` | 各模块例程 | 见 44~48 章 |
| 构建辅助 | `mcal.cmake` | `mcal_add(...)` | 每个例程 | 编译通过 |
| 内存映射 | `MemMap.h` | `*_START_SEC_CODE` | 全模块 | 空桩，链接通过 |

## 43.6 例程逐条讲解

本章对应的例程是 `samples/mcal/mcal_hello`（无外设依赖）与 `samples/mcal/ecu_integration`（13 模块集成）。
（第 7 轮新增的 Eth/EthTrcv 有意不进集成固件，原因见第 42 章 §42.7.1。）

`mcal_hello` 的 `detbench` 命令是本仓库"把 MCAL 开销量化"的起点，代码如下（节选）：

```c
c0 = hpm_csr_get_core_mcycle();
for (i = 0; i < 1000u; i++) {
    (void)Det_ReportError((uint16)MCU_MODULE_ID, 0u, 0x5Au, (uint8)DET_E_PARAM_VALUE);
}
c1 = hpm_csr_get_core_mcycle();
printf("Det_ReportError = %u cyc total\r\n", (unsigned)(c1 - c0));
```

这段代码解决两个问题：① 证明 Det 环路真的被执行（count 增加）；② 给出"每上报一次错误要多少纳秒"的硬数据，
供第 42 章表 42-7 与第 48 章资源表引用。

`ecu_integration` 把 13 个模块按图 43-3 的顺序初始化，用 1 ms GPT 通知驱动最小任务调度器，
再用 shell 命令逐项观测。初始化骨架（节选，完整代码见 `samples/mcal/ecu_integration/src/main.c`）：

```c
Det_Init();
Mcu_Init(&Mcu_Config);                      /* 时钟：480 MHz（PLL0 960/2） */
if (Mcu_InitClock(MCU_CLOCK_SETTING_0) == E_OK) {
    while (Mcu_GetPllStatus() != MCU_PLL_LOCKED) { }
    (void)Mcu_DistributePllClock();
}
Port_Init(&Port_Config);                    /* 引脚复用必须早于所有外设 */
board_can_init_pins();                      /* 板级：CAN 引脚 + TCAN1044 STB 使能 */
board_can_transceivers_enable();
Gpt_Init(&Gpt_Config); Gpt_StartTimer(0, 100000u); Gpt_EnableNotification(0);  /* 1 ms */
Spi_Init(&Spi_Config);  Can_Init(&Can_Config);
(void)Can_SetControllerMode(0, CAN_CS_STARTED);
Mem_Init(&Mem_Config);  Pwm_Init(&Pwm_Config);  Icu_Init(&Icu_Config);
Wdg_Init(&Wdg_Config);  Wdg_SetTriggerCondition(Wdg_Config.slowTimeoutMs);   /* 看门狗最后使能 */
```

这段代码把"顺序"这件事变成了可验证的：如果 Port 在 Gpt 之后初始化、或 Wdg 在启动时就使能，
现象分别是"定时器引脚不输出"和"初始化途中被复位"——两种错误都在本仓库开发过程中真实出现过。
集成实测数据见表 43-7 的后半部分。

## 43.7 上板实测数据

| 项 | 实测值 | 方法/日志 |
|---|---|---|
| `Det.c` 代码/数据 | 558 B text + 54 B bss | `riscv32-unknown-elf-size` |
| `Dem.c` 代码/数据 | 12 B text + 4 B bss | 同上 |
| `mcal_hello` 整机 Flash | 75 728 B（含 SDK/shell/board） | 构建输出 |
| Det 上报单次开销 | 62.09 cyc ≈ 0.129 µs @480 MHz | `detbench` 1000 次 |
| Det 查询单次开销 | 73.11 cyc ≈ 0.152 µs | 同上 |
| 循环基线 | 9.71 cyc/iter | 同上 |
| Det 注入/回读正确性 | count 0→1，last mod=101 err=2 | `det` 命令 |
| 集成固件：13 模块同时运行 30 s | Det=0、无复位、1 ms tick 连续 | `logs/20261102-143218-ecu_integration.log` |
| 集成固件：CAN0→CAN1 10 帧 | accepted=9、rx=9、tx_cfm=9、busoff=0 | 同上，`ecu can send 10` |
| 集成固件：PWM 50%→30% + ICU 捕获 | ICU period=0x1389（5001 tick，50 µs） | 同上，`ecu pwm 30;ecu icu` |
| 集成固件：Wdg 100 ms 喂狗/1000 ms 超时 | 25 s 存活，状态 state=1 | 同上，`ecu wdg` |
| 集成固件：Mem 几何查询 | base=0x80000000 size=1 MB sector=4096 page=256 | 同上，`ecu mem` |
| 集成固件：CRC32 SW/HW | 0xCBF43926 PASS/PASS | 同上，`ecu crc` |
| 集成固件：ADC 200 次 | mean=35590、min=34896、max=35755 | 同上，`ecu adc 200` |
| 集成固件：I2C 写序列 | setupEB=0、rc=0、result=0 | 同上，`ecu i2c` |

表 43-7 前七行来自 `logs/20261102-133416-mcal_hello.log`，后八行来自
`logs/20261102-143218-ecu_integration.log`，可分别用下面两条命令复跑：

```bash
bash scripts/hwrun.sh mcal/mcal_hello --secs 8 --send "det;detbench"
bash scripts/hwrun.sh mcal/ecu_integration --secs 30 \
  --send "ecu;ecu crc;ecu adc 200;ecu i2c;ecu pwm 30;ecu icu;ecu wdg;ecu can send 10;ecu can stat"
```

## 43.8 性能与调优

基础设施的性能问题只有两个：**Det 的开销**和**配置查表的开销**。

- Det：62 cyc 一次，正常路径为零（宏在 `STD_OFF` 时展开为空）。量产建议：开发阶段 `STD_ON`，
  发布版本 `STD_OFF`，并在 CI 里跑一遍 `STD_ON` 的错误路径测试；
- 配置查表：post-build 表是 `const`，放在 Flash（XIP），访问一次约 3~10 cyc（命中 Cache 时）；如果某模块在
  1 µs 级中断里频繁查表，可把热点表复制到 ILM 或改成预编译常量。

还有一个"隐形成本"是 **API 入口的参数检查**：一个典型的 `if ((ch >= NUM) || (ptr == NULL_PTR)) return E_NOT_OK;`
在 -O2 下是 2~4 条指令；15 个模块 × 每个模块 10 个 API，合计不到 1 KB 代码。第 48 章表 48-8 会给出
各模块的 Flash/RAM 实测，可以对照本章的"骨架"成本。

## 43.9 坑与注意事项

1. **`uint32` 与 `uint32_t` 不能混着用**：MCAL 公共头用 AUTOSAR 的 `uint32`，SDK 头用 `uint32_t`；同一文件
   同时 include 两者时类型其实相同（都是 unsigned int），但混写会让读者困惑，也容易在别的编译器上出问题。
2. **不要在 `.h` 里 include `hpm_*` 头**：一旦公共头依赖 SDK，PC 侧静态检查、单元测试、换 SDK 版本都会崩。
3. **`MCAL_DEV_ERROR_DETECT` 要能"一处关"**：如果各模块自己写 `#if`，量产时很难统一关；本仓库把它集中在
   `Mcal_Cfg.h`。
4. **Det 的实例号/ API 号不要瞎填**：AUTOSAR 要求 `ApiId` 与 SWS 附录一致；本仓库每个模块头文件里有
   `<MOD>_API_ID_*` 常量，写驱动时直接引用。
5. **配置结构体不要放栈上**：post-build 配置是 `const` 全局量，别在函数里构造大结构体（RV32 栈只有几 KB）。
6. **`mcal_add()` 的模块名大小写敏感**：Linux 文件系统区分大小写，`mcal_add(Mcu)` 不能写成 `mcal_add(mcu)`。
7. **CMake 缓存**：改了 `mcal.cmake` 或增删源文件后，偶尔需要删 `build/<name>-debug` 重新配置；
   本仓库的 `build.sh` 只在没有 `CMakeCache.txt` 时 configure，所以换模块列表后要手动删缓存。
8. **阻塞命令要喂狗**：集成例程里一个 1 s 的 busy-wait（`ecu tick 1000`）就把 1000 ms 的看门狗等超时了，
   板子复位（`raw=0x00010010` = WDG0）。凡是可能超过看门狗超时的循环（长传输、擦写、等待、轮询），
   都要在循环里周期性调用 `Wdg_SetTriggerCondition()`，或者把超时配置得比最长阻塞时间更长。
   这条在单模块例程里几乎发现不了，是集成测试的典型收获。

## 43.10 优劣评价与同类对比

商用 MCAL 的"基础设施"通常由配置工具生成，本仓库是手写。两者流程对比如表 43-8：

| 环节 | EB tresos / Infineon Tresos 流程 | 本仓库手写流程 |
|---|---|---|
| 配置输入 | 图形化参数树（.xdm/arxml） | `*_Cfg.h` + `*_PBcfg.c` 源码 |
| 变更生效 | 工具重新生成 C 代码 | 手改源码，重新编译 |
| 一致性检查 | 工具校验（参数依赖/范围） | 编译期 `#if` + 运行期 Det |
| 学习成本 | 高（工具 + 模板） | 低（就是 C） |
| 适合场景 | 量产项目、多人协作 | 学习/迁移/小项目 |

表 43-8 没有"谁更好"的结论：工具链的价值在量产一致性，手写的价值在透明可控。本仓库的目标是后者。

## 43.11 参考来源

- AUTOSAR CP《SWS Standard Types》《SWS Platform Types》《SWS BSW General》：
  <https://www.autosar.org/standards/classic-platform/>（本地文本在 `autosar_txt/`）
- AUTOSAR《SWS Default Error Tracer》《SWS Diagnostic Event Manager》同上
- HPM SDK v1.13.0 构建系统：<https://github.com/hpmicro/hpm_sdk>（`cmake/cmake-ext.cmake`）
- Elektrobit EB tresos：<https://www.elektrobit.com/products/ecu/eb-tresos/studio/>
- Infineon AURIX TC4x MCAL（配置工具与用户手册）：<https://www.infineon.com/>
- NXP S32K3 RTD：<https://www.nxp.com/design/software/development-software/s32k3-standard-software>
- 本仓库第 02 章（构建/烧写）、第 26 章（寄存器速查）、第 42 章（MCAL 总览）、第 48 章（资源与性能）

## 43.12 待确认项与风险

| # | 项 | 状态 | 说明 / 复跑方法 |
|---|---|---|---|
| 1 | link-time 配置未实现 | 已知限制 | 单配置裸机场景无收益；扩展点在 `<Mod>_PBcfg.h`，接多配置时按 SWS 加配置指针数组 |
| 2 | MemMap 为空桩 | 已知限制 | 无 MPU/OS 分区，段宏不生效；接 OS 时按 OS MemMap 规则重做，见 43.2.6 |
| 3 | Det 记录深度 8 条 | 已知限制 | 深挖错误现场时可改 `DET_NUM_RECORDS`；或接调试器看 RAM |
| 4 | 未实现 Dem 完整状态机 | 已知限制 | Dem 属于 BSW，本仓库只给计数桩（`MCAL_DEM_ENABLE` 默认 OFF） |
| 5 | 配置生成工具缺失 | 已知限制 | 手写配置，没有参数树校验；可用脚本做静态检查（第 50 章差距清单） |
| 6 | 启动顺序依赖手工维护 | 已定位 | 没有 EcuM 编排；`ecu_integration` 用注释标注各步的 AUTOSAR 对应关系 |