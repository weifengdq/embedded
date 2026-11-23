# mcal/ —— HPM5E31 AUTOSAR MCAL（R6）

> 本目录是《HPM5E31 实战手册》第六轮（R6）的 AUTOSAR CP MCAL 子集实现。
> 书稿见 `docs/chapters/42~49`；开发约定见 `docs/MCAL开发约定.md`（开工必读）。

## 是什么

- **不是** 芯片厂商认证的 MCAL，而是一套**照着 AUTOSAR SWS 从零写、能上板跑、能对比**的教学/工程参考实现；
- 13 个模块：`Mcu Port Dio Gpt Pwm Icu Adc Spi I2c Can Wdg Mem Crc`（Mem 即 R25-11 之前的 Fls）；
- 每个模块分三层：`include/<Mod>.h`（AUTOSAR API）、`src/<Mod>.c`（寄存器级实现）、
  `cfg/<Mod>_Cfg.h` + `cfg/<Mod>_PBcfg.c`（预编译 + post-build 配置）；
- 板级信息（引脚/实例/时钟/回调）**只出现在 `_PBcfg.c`**，换板只改配置。

## 目录

```text
common/       AUTOSAR 基础类型（Std_Types/Platform_Types/Compiler）、Det/Dem 桩、MemMap、模块 ID
<Mod>/        每个模块：include/ src/ cfg/
mcal.cmake    例程 CMakeLists 里 include 后 mcal_add(Mcu Port Dio ...) 即可
```

## 怎么用（例程侧）

```cmake
find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
project(hpm5e31_1_mcal_mcu_clock_init)
sdk_compile_definitions(-DBOARD_SHOW_CLOCK=0 -DBOARD_SHOW_BANNER=0)
include(${CMAKE_CURRENT_LIST_DIR}/../../../mcal/mcal.cmake)
mcal_add(Mcu Port Dio)
sdk_app_src(src/main.c)
```

```c
#include "bsp.h"
#include "Mcu.h"

int main(void)
{
    bsp_init();
    (void)Mcu_Init(&Mcu_Config);       /* post-build 配置：时钟/PLL/复位 */
    (void)Mcu_InitClock(0);
    while (Mcu_GetPllStatus() != MCU_PLL_LOCKED) { }
    (void)Mcu_DistributePllClock();
    ...
}
```

编译/上板：

```bash
cd hpm5e31_1 && source scripts/env.sh
bash scripts/build.sh samples/mcal/mcal_hello
bash scripts/hwrun.sh mcal/mcal_hello --secs 8 --send "mcal;det"
```

## 状态

见 `docs/generated/status.md`（`tools/gen_status.py` 生成）与各章书稿。截至定稿：

| 内容 | 位置 |
|---|---|
| 模块与例程清单 | `docs/chapters/42-mcal-overview.md`、`docs/MCAL开发约定.md` §1 |
| 配置与集成 | `docs/chapters/43-mcal-infra.md` |
| 各模块实现/实测/对比 | `docs/chapters/45~50` |
| 合规性与商用 MCAL 对比 | `docs/chapters/50-mcal-compliance.md` |

## 许可与边界

本目录代码为本仓库原创（AUTOSAR SWS 是公开规范，按规范实现 API 不构成侵权）。
`ref2/` 里 candleLight/roboparty 等 GPL 代码**未拷贝**进本目录，只在文档里作对比。
