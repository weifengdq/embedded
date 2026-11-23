# MCAL API 追溯矩阵（AUTOSAR CP SWS R25-11 × HPM5E31 实现）

> 本文件是第 50 章《AUTOSAR 合规性与商用 MCAL 对比》的支撑材料，由 `docs/mcal/gen_traceability.py` 从 SWS 抽取文本与 `mcal/` 源码生成；人工口径（部分实现判据、偏差、验证状态）在 `docs/mcal/traceability_manual.json`。

## 0. 口径与免责声明

| 项 | 说明 |
|---|---|
| SWS 基准 | AUTOSAR CP R25-11 抽取文本（31 个 SWS，本地 `autosar_txt/`），逐模块取 §8.5 函数定义 + §8.6/8.5 回调与调度函数 + §7.x 错误码 + §10 配置容器 |
| 实现基准 | 本仓库 `mcal/<Mod>/include/<Mod>.h` 的公开声明 + `src/*.c` 的函数体 |
| 状态定义 | `已实现`＝头文件声明且源文件有函数体；`部分`＝声明与主路径可用但受限（见备注）；`未实现`＝本子集未提供 |
| DET 检查 | 扫描函数体是否出现 `MCAL_DET_REPORT`/`Det_ReportError`；有打勾，不代表每条 SWS 错误路径都覆盖 |
| 覆盖率 | （已实现 + 0.5×部分）/ SWS API 总数，仅作进度指标，**不是**认证证据 |
| 生成日期 | 2026-11-02（R6 MCAL）|

**这不是 AUTOSAR 合规证书。** 本表只说明“SWS 里写了什么、本实现有没有对应函数、有没有 DET 检查”，不覆盖：时序/并发行为、错误路径完整性、配置参数语义、MISRA/WCET、ASIL 安全论证。商用 MCAL 的合规性靠“需求追溯 + 静态分析 + 单元/集成测试 + 安全手册 + 认证报告”共同支撑，本表只对应其中第一项的一个子集。

## 1. 总览

表 0-1 13 个模块的 SWS API 覆盖（机器统计）

| 模块 | 模块 ID | SWS API | 已实现 | 部分 | 未实现 | 覆盖率 | API ID 定义 | DET 错误码 | 有 DET 的 API | 代码行数 |
|---|---|---|---|---|---|---|---|---|---|---|
| Mcu | 101 | 11 | 11 | 0 | 0 | 100% | 11 | 8 | 11 | 857 |
| Port | 124 | 5 | 5 | 0 | 0 | 100% | 5 | 8 | 5 | 568 |
| Dio | 120 | 9 | 9 | 0 | 0 | 100% | 9 | 4 | 9 | 470 |
| Gpt | 100 | 14 | 11 | 0 | 3 | 79% | 14 | 10 | 11 | 902 |
| Pwm | 121 | 13 | 9 | 0 | 4 | 69% | 10 | 10 | 9 | 806 |
| Icu | 122 | 26 | 17 | 0 | 9 | 65% | 20 | 12 | 17 | 960 |
| Wdg | 102 | 4 | 4 | 0 | 0 | 100% | 4 | 8 | 4 | 379 |
| Adc | 123 | 17 | 13 | 0 | 4 | 76% | 13 | 11 | 13 | 990 |
| Spi | 83 | 14 | 14 | 0 | 0 | 100% | 15 | 10 | 13 | 1367 |
| I2c | 113 | 8 | 8 | 0 | 0 | 100% | 9 | 9 | 8 | 735 |
| Can | 80 | 18 | 13 | 0 | 5 | 72% | 18 | 10 | 13 | 2205 |
| Mem | 91 | 12 | 12 | 0 | 0 | 100% | 13 | 7 | 0 | 821 |
| Crc | 201 | 9 | 9 | 0 | 0 | 100% | 9 | 0 | 0 | 1005 |
| Eth | 88 | 42 | 20 | 0 | 22 | 48% | 25 | 15 | 19 | 1941 |
| EthTrcv | 73 | 31 | 17 | 0 | 14 | 55% | 21 | 5 | 15 | 953 |
| **合计** | | **233** | **172** | **0** | **61** | **74%** | | | **147** | **14959** |

表 0-2 分组覆盖率（行＝模块，列＝API 组；格子＝已实现/该组 SWS API 数）

| 模块 | 初始化/去初始化 | 时钟/复位/模式/低功耗 | 数据通路/作业 | 状态/查询 | 通知/中断/唤醒控制 | 版本信息 | 调度函数 | 其他 |
|---|---|---|---|---|---|---|---|---|
| Mcu | 2/2 | 8/8 | – | – | – | 1/1 | – | – |
| Port | 1/1 | – | 3/3 | – | – | 1/1 | – | – |
| Dio | – | – | 8/8 | – | – | 1/1 | – | – |
| Gpt | 2/2 | 1/1 | 2/2 | 3/3 | 4/8 | 1/1 | – | – |
| Pwm | 2/2 | 0/4 | 3/3 | 1/1 | 4/5 | 1/1 | – | – |
| Icu | 2/2 | 1/1 | 5/5 | 4/5 | 6/18 | 1/1 | – | – |
| Wdg | 1/1 | 1/1 | 1/1 | – | – | 1/1 | – | – |
| Adc | 2/2 | 0/4 | 4/4 | 2/2 | 6/7 | 1/1 | – | – |
| Spi | 2/2 | – | 7/7 | 4/4 | – | 1/1 | 2/2 | – |
| I2c | 2/2 | – | 3/3 | 1/1 | – | 1/1 | 2/2 | 1/1 |
| Can | 2/2 | 2/2 | 3/4 | 4/5 | 1/4 | 1/1 | 10/10 | – |
| Mem | 2/2 | 3/3 | 5/5 | 1/1 | – | 1/1 | 2/2 | – |
| Crc | – | – | – | – | – | 1/1 | – | 8/8 |
| Eth | 1/1 | 1/1 | – | 1/1 | 0/3 | 1/1 | – | 16/35 |
| EthTrcv | 1/1 | – | – | – | 1/1 | 1/1 | – | 14/28 |

## 2. 逐模块 API 追溯

### 2.1 Mcu（模块 ID 101）

- 头文件：`mcal/Mcu/include/Mcu.h`；源文件：`mcal/Mcu/src/Mcu.c`；配置：`mcal/Mcu/cfg/Mcu_Cfg.h`、`mcal/Mcu/cfg/Mcu_PBcfg.c`
- 版本宏：AR (4u).(4u).(0u) / SW (1u).(0u).(0u)；API ID 常量 11 个；DET 错误码 8 个
- 配置：pre-compile 开关 10 个，post-build 结构字段 33 个（`Mcu_ConfigType`）
- SWS 要求：API 11 个、回调/调度 0 个、DET 错误码 9 个、配置容器 10 个

| SWS API | Service ID | Sync/Async | 本实现 | 实现状态 | DET | 备注 |
|---|---|---|---|---|---|---|
| `Mcu_Init` | 0x00 | Synchronous | `Mcu_Init` | 已实现 | √ |  |
| `Mcu_InitRamSection` | 0x01 | Synchronous | `Mcu_InitRamSection` | 已实现 | √ |  |
| `Mcu_InitClock` | 0x02 | Synchronous | `Mcu_InitClock` | 已实现 | √ |  |
| `Mcu_DistributePllClock` | 0x03 | Synchronous | `Mcu_DistributePllClock` | 已实现 | √ |  |
| `Mcu_GetPllStatus` | 0x04 | Synchronous | `Mcu_GetPllStatus` | 已实现 | √ |  |
| `Mcu_GetResetReason` | 0x05 | Synchronous | `Mcu_GetResetReason` | 已实现 | √ |  |
| `Mcu_GetResetRawValue` | 0x06 | Synchronous | `Mcu_GetResetRawValue` | 已实现 | √ |  |
| `Mcu_PerformReset` | 0x07 | Synchronous | `Mcu_PerformReset` | 已实现 | √ |  |
| `Mcu_SetMode` | 0x08 | Synchronous | `Mcu_SetMode` | 已实现 | √ |  |
| `Mcu_GetVersionInfo` | 0x09 | Synchronous | `Mcu_GetVersionInfo` | 已实现 | √ |  |
| `Mcu_GetRamState` | 0x0a | Synchronous | `Mcu_GetRamState` | 已实现 | √ |  |

**状态机/生命周期（SWS §7.1 摘要）**

上电为 UNINIT。`Mcu_Init()` 成功后进入 INIT；`Mcu_InitClock()` 只启动 PLL 锁定流程、不等待，`Mcu_GetPllStatus()` 轮询；`Mcu_DistributePllClock()` 把 PLL 切到系统时钟。未初始化调用任意 API 报 `MCU_E_UNINIT`（源文件 9 处检查）。`Mcu_PerformReset()` 不返回。`Mcu_GetRamState`/`Mcu_InitRamSection` 由 `MCU_GET_RAM_STATE_API`/`MCU_INIT_RAM_SECTION_API` 关闭（默认 STD_OFF）。

**配置参数对应**

SWS 有 10 个配置容器（McuGeneralConfiguration/McuClockSettingConfig/McuModeSettingConf 等）。本实现：`Mcu_Cfg.h` 给 `MCU_DEV_ERROR_DETECT`、`MCU_INIT_CLOCK_API`、`MCU_NO_PLL`、`MCU_PERFORM_RESET_API`、`MCU_GET_RAM_STATE_API`、`MCU_INIT_RAM_SECTION_API`、`MCU_VERSION_INFO_API`、`MCU_MAX_CLOCK_NODES`；`Mcu_PBcfg.c` 给 `const Mcu_ConfigType Mcu_Config`（时钟节点表、模式表）。

**验证状态**

| 例程 | 上板 | 日志 | 量化数据 |
|---|---|---|---|
| `mcu_clock_init` | PASS | `logs/20261102-135140-mcu_clock_init.log` | 480 MHz 实测 479,997,400 Hz（−5 ppm） |
| `mcu_reset_mode` | PASS | `logs/20261102-140030-mcu_reset_mode.log` | 软复位约 4.2 µs，RESET_FLAG 可读 |

### 2.2 Port（模块 ID 124）

- 头文件：`mcal/Port/include/Port.h`；源文件：`mcal/Port/src/Port.c`；配置：`mcal/Port/cfg/Port_Cfg.h`、`mcal/Port/cfg/Port_PBcfg.c`
- 版本宏：AR (4u).(4u).(0u) / SW (1u).(0u).(0u)；API ID 常量 5 个；DET 错误码 8 个
- 配置：pre-compile 开关 6 个，post-build 结构字段 19 个（`Port_ConfigType`）
- SWS 要求：API 5 个、回调/调度 0 个、DET 错误码 9 个、配置容器 5 个

| SWS API | Service ID | Sync/Async | 本实现 | 实现状态 | DET | 备注 |
|---|---|---|---|---|---|---|
| `Port_Init` | 0x00 | Synchronous | `Port_Init` | 已实现 | √ |  |
| `Port_SetPinDirection` | 0x01 | Synchronous | `Port_SetPinDirection` | 已实现 | √ |  |
| `Port_RefreshPortDirection` | 0x02 | Synchronous | `Port_RefreshPortDirection` | 已实现 | √ |  |
| `Port_GetVersionInfo` | 0x03 | Synchronous | `Port_GetVersionInfo` | 已实现 | √ |  |
| `Port_SetPinMode` | 0x04 | Synchronous | `Port_SetPinMode` | 已实现 | √ |  |

**状态机/生命周期（SWS §7.1 摘要）**

UNINIT → INIT（`Port_Init()`）。运行期改引脚方向/复用受 `PortPinDirectionChangeable`/`PortPinModeChangeable` 约束，配置为不可改时报 `PORT_E_DIRECTION_UNCHANGEABLE`/`PORT_E_MODE_UNCHANGEABLE`（各 1 处检查）；未初始化报 `PORT_E_UNINIT`（4 处）。

**配置参数对应**

SWS 5 个容器。本实现：`Port_Cfg.h` 给 `PORT_SET_PIN_DIRECTION_API`、`PORT_SET_PIN_MODE_API`、`PORT_VERSION_INFO_API`、`PORT_PIN_COUNT_CFG`；`Port_PBcfg.c` 给 `Port_Config`（引脚表：端口/引脚/复用/方向/初始电平/可改性）。

**验证状态**

| 例程 | 上板 | 日志 | 量化数据 |
|---|---|---|---|
| `port_pin_config` | PASS | `logs/20261102-135603-port_pin_config.log` | 2000 组 LFSR 回环误码 0；上下拉/开漏 PASS |

### 2.3 Dio（模块 ID 120）

- 头文件：`mcal/Dio/include/Dio.h`；源文件：`mcal/Dio/src/Dio.c`；配置：`mcal/Dio/cfg/Dio_Cfg.h`、`mcal/Dio/cfg/Dio_PBcfg.c`
- 版本宏：AR (4u).(4u).(0u) / SW (1u).(0u).(0u)；API ID 常量 9 个；DET 错误码 4 个
- 配置：pre-compile 开关 8 个，post-build 结构字段 12 个（`Dio_ConfigType`）
- SWS 要求：API 9 个、回调/调度 0 个、DET 错误码 4 个、配置容器 7 个

| SWS API | Service ID | Sync/Async | 本实现 | 实现状态 | DET | 备注 |
|---|---|---|---|---|---|---|
| `Dio_ReadChannel` | 0x00 | Synchronous | `Dio_ReadChannel` | 已实现 | √ |  |
| `Dio_WriteChannel` | 0x01 | Synchronous | `Dio_WriteChannel` | 已实现 | √ |  |
| `Dio_ReadPort` | 0x02 | Synchronous | `Dio_ReadPort` | 已实现 | √ |  |
| `Dio_WritePort` | 0x03 | Synchronous | `Dio_WritePort` | 已实现 | √ |  |
| `Dio_ReadChannelGroup` | 0x04 | Synchronous | `Dio_ReadChannelGroup` | 已实现 | √ |  |
| `Dio_WriteChannelGroup` | 0x05 | Synchronous | `Dio_WriteChannelGroup` | 已实现 | √ |  |
| `Dio_GetVersionInfo` | 0x12 | Synchronous | `Dio_GetVersionInfo` | 已实现 | √ |  |
| `Dio_FlipChannel` | 0x11 | Synchronous | `Dio_FlipChannel` | 已实现 | √ |  |
| `Dio_MaskedWritePort` | 0x13 | Synchronous | `Dio_MaskedWritePort` | 已实现 | √ |  |

**状态机/生命周期（SWS §7.1 摘要）**

SWS 的 Dio 没有 Init/DeInit，也没有模块状态；任何时刻可调用，只做参数校验。非法通道/端口/组/空指针分别报 `DIO_E_PARAM_INVALID_CHANNEL_ID`、`DIO_E_PARAM_INVALID_PORT_ID`、`DIO_E_PARAM_INVALID_GROUP`、`DIO_E_PARAM_POINTER`（源码各 3 处）。

**配置参数对应**

SWS 7 个容器。本实现：`Dio_Cfg.h` 给 `DIO_FLIP_CHANNEL_API`、`DIO_MASKED_WRITE_PORT_API`、`DIO_CHANNEL_COUNT_CFG`、`DIO_CHANNEL_GROUP_COUNT_CFG`、`DIO_MAX_PORTS`；`Dio_PBcfg.c` 给 `Dio_Config`（端口/通道/通道组映射）。

**验证状态**

| 例程 | 上板 | 日志 | 量化数据 |
|---|---|---|---|
| `dio_led_button` | PASS | `logs/20261102-140549-dio_led_button.log` | 写 127 ns、读 185 ns；DET +6 |

### 2.4 Gpt（模块 ID 100）

- 头文件：`mcal/Gpt/include/Gpt.h`；源文件：`mcal/Gpt/src/Gpt.c`；配置：`mcal/Gpt/cfg/Gpt_Cfg.h`、`mcal/Gpt/cfg/Gpt_PBcfg.c`
- 版本宏：AR (4u).(4u).(0u) / SW (1u).(0u).(0u)；API ID 常量 14 个；DET 错误码 10 个
- 配置：pre-compile 开关 14 个，post-build 结构字段 15 个（`Gpt_ConfigType`）
- SWS 要求：API 14 个、回调/调度 3 个、DET 错误码 11 个、配置容器 7 个

| SWS API | Service ID | Sync/Async | 本实现 | 实现状态 | DET | 备注 |
|---|---|---|---|---|---|---|
| `Gpt_GetVersionInfo` | 0x00 | Synchronous | `Gpt_GetVersionInfo` | 已实现 | √ |  |
| `Gpt_Init` | 0x01 | Synchronous | `Gpt_Init` | 已实现 | √ |  |
| `Gpt_DeInit` | 0x02 | Synchronous | `Gpt_DeInit` | 已实现 | √ |  |
| `Gpt_GetTimeElapsed` | 0x03 | Synchronous | `Gpt_GetTimeElapsed` | 已实现 | √ |  |
| `Gpt_GetTimeRemaining` | 0x04 | Synchronous | `Gpt_GetTimeRemaining` | 已实现 | √ |  |
| `Gpt_StartTimer` | 0x05 | Synchronous | `Gpt_StartTimer` | 已实现 | √ |  |
| `Gpt_StopTimer` | 0x06 | Synchronous | `Gpt_StopTimer` | 已实现 | √ |  |
| `Gpt_EnableNotification` | 0x07 | Synchronous | `Gpt_EnableNotification` | 已实现 | √ |  |
| `Gpt_DisableNotification` | 0x08 | Synchronous | `Gpt_DisableNotification` | 已实现 | √ |  |
| `Gpt_SetMode` | 0x09 | Synchronous | `Gpt_SetMode` | 已实现 | √ |  |
| `Gpt_DisableWakeup` | 0x0a | Synchronous | – | _未实现_ | – |  |
| `Gpt_EnableWakeup` | 0x0b | Synchronous | – | _未实现_ | – |  |
| `Gpt_CheckWakeup` | 0x0c | Synchronous | – | _未实现_ | – |  |
| `Gpt_GetPredefTimerValue` | 0x0d | Synchronous | `Gpt_GetPredefTimerValue` | 已实现 | √ |  |
| `Gpt_DisableNotification` | – | 回调 | `Gpt_DisableNotification` | 已实现 | – | SWS 8.4/8.5 |
| `Gpt_EnableNotification` | – | 回调 | `Gpt_EnableNotification` | 已实现 | – | SWS 8.4/8.5 |
| `Gpt_Notification_<channel>` | – | 回调 | – | _未实现_ | – | SWS 8.4/8.5 |

**状态机/生命周期（SWS §7.1 摘要）**

UNINIT → INIT（`Gpt_Init()`）；重复初始化报 `GPT_E_ALREADY_INITIALIZED`；定时器运行中再次 Start 报 `GPT_E_BUSY`（3 处）；未初始化报 `GPT_E_UNINIT`（12 处）。唤醒类 API（`Gpt_EnableWakeup`/`Gpt_DisableWakeup`/`Gpt_CheckWakeup`）本子集未实现，头文件里标注为可选。

**配置参数对应**

SWS 7 个容器。本实现：`Gpt_Cfg.h` 给 `GPT_NUM_CHANNELS`、`GPT_DEINIT_API`、`GPT_TIME_ELAPSED_API`、`GPT_TIME_REMAINING_API`、`GPT_VERSION_INFO_API`、`GPT_SET_MODE_API`、`GPT_NOTIFICATION_SUPPORTED`；`Gpt_PBcfg.c` 给 `Gpt_Config`（通道表、定时器基地址、通知回调）。

**验证状态**

| 例程 | 上板 | 日志 | 量化数据 |
|---|---|---|---|
| `gpt_periodic` | PASS | `logs/20261102-134707-gpt_periodic.log` | StartTimer 2973 ns；1 ms tick 0 计数误差 |
| `gpt_oneshot` | PASS | `logs/20261102-141404-gpt_oneshot.log` | 1 ms 单发误差 +1066.7 ns（+1066.67 ppm） |

### 2.5 Pwm（模块 ID 121）

- 头文件：`mcal/Pwm/include/Pwm.h`；源文件：`mcal/Pwm/src/Pwm.c`；配置：`mcal/Pwm/cfg/Pwm_Cfg.h`、`mcal/Pwm/cfg/Pwm_PBcfg.c`
- 版本宏：AR (4u).(4u).(0u) / SW (1u).(0u).(0u)；API ID 常量 10 个；DET 错误码 10 个
- 配置：pre-compile 开关 9 个，post-build 结构字段 22 个（`Pwm_ConfigType`）
- SWS 要求：API 13 个、回调/调度 3 个、DET 错误码 16 个、配置容器 6 个

| SWS API | Service ID | Sync/Async | 本实现 | 实现状态 | DET | 备注 |
|---|---|---|---|---|---|---|
| `Pwm_Init` | 0x00 | Synchronous | `Pwm_Init` | 已实现 | √ |  |
| `Pwm_DeInit` | 0x01 | Synchronous | `Pwm_DeInit` | 已实现 | √ |  |
| `Pwm_SetDutyCycle` | 0x02 | Asynchronous | `Pwm_SetDutyCycle` | 已实现 | √ |  |
| `Pwm_SetPeriodAndDuty` | 0x03 | Asynchronous | `Pwm_SetPeriodAndDuty` | 已实现 | √ |  |
| `Pwm_SetOutputToIdle` | 0x04 | Asynchronous | `Pwm_SetOutputToIdle` | 已实现 | √ |  |
| `Pwm_GetOutputState` | 0x05 | Synchronous | `Pwm_GetOutputState` | 已实现 | √ |  |
| `Pwm_DisableNotification` | 0x06 | Asynchronous | `Pwm_DisableNotification` | 已实现 | √ |  |
| `Pwm_EnableNotification` | 0x07 | Asynchronous | `Pwm_EnableNotification` | 已实现 | √ |  |
| `Pwm_SetPowerState` | 0x09 | Synchronous | – | _未实现_ | – |  |
| `Pwm_GetCurrentPowerState` | 0x0a | Synchronous | – | _未实现_ | – |  |
| `Pwm_GetTargetPowerState` | 0x0b | Synchronous | – | _未实现_ | – |  |
| `Pwm_PreparePowerState` | 0x0c | Synchronous | – | _未实现_ | – |  |
| `Pwm_GetVersionInfo` | 0x08 | Synchronous | `Pwm_GetVersionInfo` | 已实现 | √ |  |
| `Pwm_DisableNotification` | – | 回调 | `Pwm_DisableNotification` | 已实现 | – | SWS 8.4/8.5 |
| `Pwm_EnableNotification` | – | 回调 | `Pwm_EnableNotification` | 已实现 | – | SWS 8.4/8.5 |
| `Pwm_Notification_<` | – | 回调 | – | _未实现_ | – | SWS 8.4/8.5 |

**状态机/生命周期（SWS §7.1 摘要）**

UNINIT → INIT（`Pwm_Init()`）；`Pwm_SetDutyCycle`/`Pwm_SetPeriodAndDuty` 在通道运行中改占空比/周期，SWS 要求对同通道的修改在下个周期生效；未初始化报 `PWM_E_UNINIT`；低功耗状态机（`Pwm_SetPowerState` 等）由 `PWM_POWER_STATE_API=STD_OFF` 关闭，本子集不实现（无 EcuM）。

**配置参数对应**

SWS 6 个容器。本实现：`Pwm_Cfg.h` 给 `PWM_NUM_CHANNELS`、`PWM_DEINIT_API`、`PWM_VERSION_INFO_API`、`PWM_NOTIFICATION_SUPPORTED`、`PWM_POWER_STATE_API`（默认 STD_OFF）、`PWM_IRQ_PRIORITY`、`PWM_MAX_PERIOD_TICKS`；`Pwm_PBcfg.c` 给 `Pwm_Config`（通道表：实例/通道/极性/周期/初始占空比/空闲电平/通知回调）。

**验证状态**

| 例程 | 上板 | 日志 | 量化数据 |
|---|---|---|---|
| `pwm_basic` | PASS | `logs/20261102-141320-pwm_basic.log` | 10 kHz 占空比 0/25/50/75/100% 扫描 |
| `pwm_notify` | PASS | `logs/20261102-141427-pwm_notify.log` | 40006 边沿/2 s，stddev 0.052 µs |

### 2.6 Icu（模块 ID 122）

- 头文件：`mcal/Icu/include/Icu.h`；源文件：`mcal/Icu/src/Icu.c`；配置：`mcal/Icu/cfg/Icu_Cfg.h`、`mcal/Icu/cfg/Icu_PBcfg.c`
- 版本宏：AR (4u).(4u).(0u) / SW (1u).(0u).(0u)；API ID 常量 20 个；DET 错误码 12 个
- 配置：pre-compile 开关 12 个，post-build 结构字段 23 个（`Icu_ConfigType`）
- SWS 要求：API 26 个、回调/调度 6 个、DET 错误码 15 个、配置容器 5 个

| SWS API | Service ID | Sync/Async | 本实现 | 实现状态 | DET | 备注 |
|---|---|---|---|---|---|---|
| `Icu_Init` | 0x00 | Synchronous | `Icu_Init` | 已实现 | √ |  |
| `Icu_DeInit` | 0x01 | Synchronous | `Icu_DeInit` | 已实现 | √ |  |
| `Icu_SetMode` | 0x02 | Synchronous | `Icu_SetMode` | 已实现 | √ |  |
| `Icu_DisableWakeup` | 0x03 | Synchronous | – | _未实现_ | – |  |
| `Icu_EnableWakeup` | 0x04 | Synchronous | – | _未实现_ | – |  |
| `Icu_CheckWakeup` | 0x15 | Synchronous | – | _未实现_ | – |  |
| `Icu_SetActivationCondition` | 0x05 | Synchronous | `Icu_SetActivationCondition` | 已实现 | √ |  |
| `Icu_DisableNotification` | 0x06 | Synchronous | `Icu_DisableNotification` | 已实现 | √ |  |
| `Icu_EnableNotification` | 0x07 | Synchronous | `Icu_EnableNotification` | 已实现 | √ |  |
| `Icu_GetInputState` | 0x08 | Synchronous | `Icu_GetInputState` | 已实现 | √ |  |
| `Icu_StartTimestamp` | 0x09 | Asynchronous | `Icu_StartTimestamp` | 已实现 | √ |  |
| `Icu_StopTimestamp` | 0x0a | Synchronous | `Icu_StopTimestamp` | 已实现 | √ |  |
| `Icu_GetTimestampIndex` | 0x0b | Synchronous | `Icu_GetTimestampIndex` | 已实现 | √ |  |
| `Icu_ResetEdgeCount` | 0x0c | Synchronous | – | _未实现_ | – |  |
| `Icu_EnableEdgeCount` | 0x0d | Synchronous | – | _未实现_ | – |  |
| `Icu_EnableEdgeDetection` | 0x16 | Synchronous | `Icu_EnableEdgeDetection` | 已实现 | √ |  |
| `Icu_DisableEdgeDetection` | 0x17 | Synchronous | `Icu_DisableEdgeDetection` | 已实现 | √ |  |
| `Icu_DisableEdgeCount` | 0x0e | Synchronous | – | _未实现_ | – |  |
| `Icu_GetEdgeNumbers` | 0x0f | Synchronous | – | _未实现_ | – |  |
| `Icu_StartSignalMeasurement` | 0x13 | Asynchronous | `Icu_StartSignalMeasurement` | 已实现 | √ |  |
| `Icu_StopSignalMeasurement` | 0x14 | Synchronous | `Icu_StopSignalMeasurement` | 已实现 | √ |  |
| `Icu_GetTimeElapsed` | 0x10 | Synchronous | `Icu_GetTimeElapsed` | 已实现 | √ |  |
| `Icu_GetDutyCycleValues` | 0x11 | Synchronous | `Icu_GetDutyCycleValues` | 已实现 | √ |  |
| `Icu_GetVersionInfo` | 0x12 | Synchronous | `Icu_GetVersionInfo` | 已实现 | √ |  |
| `Icu_DisableNotificationAsync` | 0x18 | Asynchronous | – | _未实现_ | – |  |
| `Icu_EnableNotificationAsync` | 0x19 | Asynchronous | – | _未实现_ | – |  |
| `Icu_DisableNotification` | – | 回调 | `Icu_DisableNotification` | 已实现 | – | SWS 8.4/8.5 |
| `Icu_DisableNotificationAsync` | – | 回调 | – | _未实现_ | – | SWS 8.4/8.5 |
| `Icu_EnableNotification` | – | 回调 | `Icu_EnableNotification` | 已实现 | – | SWS 8.4/8.5 |
| `Icu_EnableNotificationAsync` | – | 回调 | – | _未实现_ | – | SWS 8.4/8.5 |
| `Icu_SignalNotification_<Channel>` | – | 回调 | – | _未实现_ | – | SWS 8.4/8.5 |
| `Icu_TimestampNotification_<Channel>` | – | 回调 | – | _未实现_ | – | SWS 8.4/8.5 |

**状态机/生命周期（SWS §7.1 摘要）**

UNINIT → INIT（`Icu_Init()`）；激活条件（上升/下降/双边沿）由 `Icu_SetActivationCondition` 在运行期设置，SWS 允许在 INIT 后、启动测量前设置；未初始化报 `ICU_E_UNINIT`。时间戳/边沿计数族由 `ICU_TIMESTAMP_API`、`ICU_EDGE_DETECT_API` 控制。

**配置参数对应**

SWS 5 个容器。本实现：`Icu_Cfg.h` 给 `ICU_NUM_CHANNELS`、`ICU_DEINIT_API`、`ICU_VERSION_INFO_API`、`ICU_SIGNAL_MEASUREMENT_API`、`ICU_TIMESTAMP_API`、`ICU_EDGE_DETECT_API`、`ICU_NOTIFICATION_SUPPORTED`；`Icu_PBcfg.c` 给 `Icu_Config`（通道表：捕获源/边沿/测量模式/通知回调）。

**验证状态**

| 例程 | 上板 | 日志 | 量化数据 |
|---|---|---|---|
| `icu_capture` | PASS | `logs/20261102-141450-icu_capture.log` | 1 kHz 0 ppm、占空比 49.917%；100 kHz 占空比 41.7% |

### 2.7 Wdg（模块 ID 102）

- 头文件：`mcal/Wdg/include/Wdg.h`；源文件：`mcal/Wdg/src/Wdg.c`；配置：`mcal/Wdg/cfg/Wdg_Cfg.h`、`mcal/Wdg/cfg/Wdg_PBcfg.c`
- 版本宏：AR (4u).(4u).(0u) / SW (1u).(0u).(0u)；API ID 常量 4 个；DET 错误码 8 个
- 配置：pre-compile 开关 6 个，post-build 结构字段 11 个（`Wdg_ConfigType`）
- SWS 要求：API 4 个、回调/调度 0 个、DET 错误码 11 个、配置容器 3 个

| SWS API | Service ID | Sync/Async | 本实现 | 实现状态 | DET | 备注 |
|---|---|---|---|---|---|---|
| `Wdg_Init` | 0x00 | Synchronous | `Wdg_Init` | 已实现 | √ |  |
| `Wdg_SetMode` | 0x01 | Synchronous | `Wdg_SetMode` | 已实现 | √ |  |
| `Wdg_SetTriggerCondition` | 0x03 | Synchronous | `Wdg_SetTriggerCondition` | 已实现 | √ |  |
| `Wdg_GetVersionInfo` | 0x04 | Synchronous | `Wdg_GetVersionInfo` | 已实现 | √ |  |

**状态机/生命周期（SWS §7.1 摘要）**

UNINIT → INIT（`Wdg_Init()`）；`Wdg_SetMode()` 在 WDGIF_MODE_OFF/SLOW/FAST 间切换（`WdgDisableAllowed=STD_OFF` 时不接受 OFF）；`Wdg_SetTriggerCondition(timeout)` 设置下个触发窗口，超时则由硬件复位；未初始化报 `WDG_E_UNINIT`。

**配置参数对应**

SWS 3 个容器。本实现：`Wdg_Cfg.h` 给 `WDG_DISABLE_ALLOWED`（默认 STD_OFF，安全件不允许关狗）、`WDG_TRIGGER_CONDITION_API`、`WDG_VERSION_INFO_API`、`WDG_SRC_CLK_HZ`；`Wdg_PBcfg.c` 给 `Wdg_Config`（EWDG 实例、超时窗口、初始模式）。

**验证状态**

| 例程 | 上板 | 日志 | 量化数据 |
|---|---|---|---|
| `wdg_trigger` | PASS | `logs/20261102-141513-wdg_trigger.log` | slow 1000 ms / fast 100 ms |
| `wdg_timeout_reset` | 已知限制 | `logs/20261102-142810-wdg_timeout_reset.log` | 跨复位抓包需两次 hwrun（第 15 节） |

### 2.8 Adc（模块 ID 123）

- 头文件：`mcal/Adc/include/Adc.h`；源文件：`mcal/Adc/src/Adc.c`；配置：`mcal/Adc/cfg/Adc_Cfg.h`、`mcal/Adc/cfg/Adc_PBcfg.c`
- 版本宏：AR (4u).(4u).(0u) / SW (1u).(0u).(0u)；API ID 常量 13 个；DET 错误码 11 个
- 配置：pre-compile 开关 12 个，post-build 结构字段 23 个（`Adc_ConfigType`）
- SWS 要求：API 17 个、回调/调度 3 个、DET 错误码 20 个、配置容器 7 个

| SWS API | Service ID | Sync/Async | 本实现 | 实现状态 | DET | 备注 |
|---|---|---|---|---|---|---|
| `Adc_Init` | 0x00 | Synchronous | `Adc_Init` | 已实现 | √ |  |
| `Adc_SetupResultBuffer` | 0x0c | Asynchronous | `Adc_SetupResultBuffer` | 已实现 | √ |  |
| `Adc_DeInit` | 0x01 | Synchronous | `Adc_DeInit` | 已实现 | √ |  |
| `Adc_StartGroupConversion` | 0x02 | Asynchronous | `Adc_StartGroupConversion` | 已实现 | √ |  |
| `Adc_StopGroupConversion` | 0x03 | Synchronous | `Adc_StopGroupConversion` | 已实现 | √ |  |
| `Adc_ReadGroup` | 0x04 | Synchronous | `Adc_ReadGroup` | 已实现 | √ |  |
| `Adc_EnableHardwareTrigger` | 0x05 | Asynchronous | `Adc_EnableHardwareTrigger` | 已实现 | √ |  |
| `Adc_DisableHardwareTrigger` | 0x06 | Asynchronous | `Adc_DisableHardwareTrigger` | 已实现 | √ |  |
| `Adc_EnableGroupNotification` | 0x07 | Asynchronous | `Adc_EnableGroupNotification` | 已实现 | √ |  |
| `Adc_DisableGroupNotification` | 0x08 | Asynchronous | `Adc_DisableGroupNotification` | 已实现 | √ |  |
| `Adc_GetGroupStatus` | 0x09 | Synchronous | `Adc_GetGroupStatus` | 已实现 | √ |  |
| `Adc_GetStreamLastPointer` | 0x0b | Synchronous | `Adc_GetStreamLastPointer` | 已实现 | √ |  |
| `Adc_GetVersionInfo` | 0x0a | Synchronous | `Adc_GetVersionInfo` | 已实现 | √ |  |
| `Adc_SetPowerState` | 0x10 | Synchronous | – | _未实现_ | – |  |
| `Adc_GetCurrentPowerState` | 0x11 | Synchronous | – | _未实现_ | – |  |
| `Adc_GetTargetPowerState` | 0x12 | Synchronous | – | _未实现_ | – |  |
| `Adc_PreparePowerState` | 0x13 | Synchronous | – | _未实现_ | – |  |
| `Adc_DisableGroupNotification` | – | 回调 | `Adc_DisableGroupNotification` | 已实现 | – | SWS 8.4/8.5 |
| `Adc_EnableGroupNotification` | – | 回调 | `Adc_EnableGroupNotification` | 已实现 | – | SWS 8.4/8.5 |
| `IoHwAb_AdcNotification<` | – | 回调 | – | _未实现_ | – | SWS 8.4/8.5 |

**状态机/生命周期（SWS §7.1 摘要）**

UNINIT → INIT（`Adc_Init()`）；组转换由 `Adc_StartGroupConversion` 启动，单次/连续模式在 PBcfg 配；结果缓冲区由 `Adc_SetupResultBuffer` 绑定，`Adc_ReadGroup` 读结果，`Adc_GetGroupStatus` 查状态；未初始化报 `ADC_E_UNINIT`，缓冲区未设置/组号非法报对应 `ADC_E_*`。低功耗状态机（`Adc_SetPowerState` 等 4 个 API）未实现（无 EcuM）。

**配置参数对应**

SWS 7 个容器。本实现：`Adc_Cfg.h` 给 `ADC_DEV_ERROR_DETECT`、`ADC_DEINIT_API`、`ADC_ENABLE_START_STOP_GROUP_API`、`ADC_READ_GROUP_API`、`ADC_HW_TRIGGER_API`、`ADC_GRP_NOTIF_CAPABILITY`、`ADC_VERSION_INFO_API`、`ADC_RESULT_ALIGNMENT_RIGHT`；`Adc_PBcfg.c` 给 `Adc_Config`（组表：通道/采样时间/触发源/结果缓冲/回调）。

**验证状态**

| 例程 | 上板 | 日志 | 量化数据 |
|---|---|---|---|
| `adc_single` | PASS | `logs/20261102-143144-adc_single.log` | ENOB 7.93/8.50 bit；508.4 kHz |
| `adc_bench` | 无日志 | `-` | 本轮未跑通（第 15 节） |

### 2.9 Spi（模块 ID 83）

- 头文件：`mcal/Spi/include/Spi.h`；源文件：`mcal/Spi/src/Spi.c`；配置：`mcal/Spi/cfg/Spi_Cfg.h`、`mcal/Spi/cfg/Spi_PBcfg.c`
- 版本宏：AR ((uint8)MCAL_AR_RELEASE_MAJOR_VERSION).((uint8)MCAL_AR_RELEASE_MINOR_VERSION).((uint8)MCAL_AR_RELEASE_REVISION_VERSION) / SW ((uint8)1u).((uint8)0u).((uint8)0u)；API ID 常量 15 个；DET 错误码 10 个
- 配置：pre-compile 开关 18 个，post-build 结构字段 38 个（`Spi_ConfigType`）
- SWS 要求：API 14 个、回调/调度 2 个、DET 错误码 12 个、配置容器 10 个

| SWS API | Service ID | Sync/Async | 本实现 | 实现状态 | DET | 备注 |
|---|---|---|---|---|---|---|
| `Spi_Init` | 0x00 | Synchronous | `Spi_Init` | 已实现 | √ |  |
| `Spi_DeInit` | 0x01 | Synchronous | `Spi_DeInit` | 已实现 | √ |  |
| `Spi_WriteIB` | 0x02 | Synchronous | `Spi_WriteIB` | 已实现 | √ |  |
| `Spi_AsyncTransmit` | 0x03 | Asynchronous | `Spi_AsyncTransmit` | 已实现 | √ |  |
| `Spi_ReadIB` | 0x04 | Synchronous | `Spi_ReadIB` | 已实现 | √ |  |
| `Spi_SetupEB` | 0x05 | Synchronous | `Spi_SetupEB` | 已实现 | √ |  |
| `Spi_GetStatus` | 0x06 | Synchronous | `Spi_GetStatus` | 已实现 | × |  |
| `Spi_GetJobResult` | 0x07 | Synchronous | `Spi_GetJobResult` | 已实现 | √ |  |
| `Spi_GetSequenceResult` | 0x08 | Synchronous | `Spi_GetSequenceResult` | 已实现 | √ |  |
| `Spi_GetVersionInfo` | 0x09 | Synchronous | `Spi_GetVersionInfo` | 已实现 | √ |  |
| `Spi_SyncTransmit` | 0x0a | Synchronous | `Spi_SyncTransmit` | 已实现 | √ |  |
| `Spi_GetHWUnitStatus` | 0x0b | Synchronous | `Spi_GetHWUnitStatus` | 已实现 | √ |  |
| `Spi_Cancel` | 0x0c | Asynchronous | `Spi_Cancel` | 已实现 | √ |  |
| `Spi_SetAsyncMode` | 0x0d | Synchronous | `Spi_SetAsyncMode` | 已实现 | √ |  |
| `Spi_MainFunction_Handling` | – | async | `Spi_MainFunction_Handling` | 已实现 | – | SWS 8.4/8.5 |
| `Spi_MainFunction_Handling` | – | async | `Spi_MainFunction_Handling` | 已实现 | – | SWS 8.4/8.5 |

**状态机/生命周期（SWS §7.1 摘要）**

UNINIT → INIT（`Spi_Init()`）；异步 Sequence 处理中再提交报 `SPI_E_SEQ_IN_PROCESS` 或 `SPI_E_SEQ_PENDING`；未初始化报 `SPI_E_UNINIT`（11 处）；Job/Sequence/IB 三层模型与 SWS 一致。

**配置参数对应**

SWS 10 个容器。本实现：`Spi_Cfg.h` 给 `SPI_LEVEL_DELIVERED`、`SPI_ASYNC_MODE_SUPPORT`、`SPI_CANCEL_API`、`SPI_HW_STATUS_API`、`SPI_INTERRUPT_SUPPORT`、`SPI_DMA_SUPPORT`；`Spi_PBcfg.c` 给 `Spi_Config`/`Spi_Config_10M`（两个波特率配置集，对应 post-build 多配置）。

**验证状态**

| 例程 | 上板 | 日志 | 量化数据 |
|---|---|---|---|
| `spi_master_slave` | PASS | `logs/20261102-142330-spi_master_slave.log` | 1 MHz、1024 B 双向 249 KB/s |
| `spi_async_job` | PASS | `logs/20261102-142810-spi_async_job.log` | 4 Job 并发 PASS |

### 2.10 I2c（模块 ID 113）

- 头文件：`mcal/I2c/include/I2c.h`；源文件：`mcal/I2c/src/I2c.c`；配置：`mcal/I2c/cfg/I2c_Cfg.h`、`mcal/I2c/cfg/I2c_PBcfg.c`
- 版本宏：AR ((uint8)MCAL_AR_RELEASE_MAJOR_VERSION).((uint8)MCAL_AR_RELEASE_MINOR_VERSION).((uint8)MCAL_AR_RELEASE_REVISION_VERSION) / SW ((uint8)1u).((uint8)0u).((uint8)0u)；API ID 常量 9 个；DET 错误码 9 个
- 配置：pre-compile 开关 9 个，post-build 结构字段 0 个（`I2c_ConfigType`）
- SWS 要求：API 8 个、回调/调度 2 个、DET 错误码 13 个、配置容器 6 个

| SWS API | Service ID | Sync/Async | 本实现 | 实现状态 | DET | 备注 |
|---|---|---|---|---|---|---|
| `I2C_Init` | 0x00 | Synchronous | `I2C_Init` | 已实现 | √ |  |
| `I2C_DeInit` | 0x01 | Synchronous | `I2C_DeInit` | 已实现 | √ |  |
| `I2C_SetupEB` | 0x02 | Synchronous | `I2C_SetupEB` | 已实现 | √ |  |
| `I2C_AsyncTransmit` | 0x03 | Asynchronous | `I2C_AsyncTransmit` | 已实现 | √ |  |
| `I2C_SyncTransmit` | 0x04 | Synchronous | `I2C_SyncTransmit` | 已实现 | √ |  |
| `I2C_GetVersionInfo` | 0x07 | Synchronous | `I2C_GetVersionInfo` | 已实现 | √ |  |
| `I2C_GetSequenceResult` | 0x09 | Synchronous | `I2C_GetSequenceResult` | 已实现 | √ |  |
| `I2C_GetChannelStatus` | 0x0B | Synchronous | `I2C_GetChannelStatus` | 已实现 | √ |  |
| `I2C_MainFunction` | – | async | `I2C_MainFunction` | 已实现 | – | SWS 8.4/8.5 |
| `I2C_MainFunction` | – | async | `I2C_MainFunction` | 已实现 | – | SWS 8.4/8.5 |

**状态机/生命周期（SWS §7.1 摘要）**

UNINIT → INIT（`I2C_Init()`）；作业模型与 SWS 一致：`I2C_SetupEB` 配置外部缓冲 → `I2C_AsyncTransmit` 提交序列（异步，需周期调 `I2C_MainFunction_Handling`）或 `I2C_SyncTransmit` 同步等待；结果用 `I2C_GetSequenceResult`/`I2C_GetChannelStatus` 读；未初始化报 `I2C_E_UNINIT`，序列运行中再提交报 `I2C_E_BUSY`/`I2C_E_SEQ_PENDING`。轮询等待有 `I2C_TIMEOUT_POLLS` 上限，不会死等。

**配置参数对应**

SWS 6 个容器。本实现：`I2c_Cfg.h` 给 `I2C_DEV_ERROR_DETECT`、`I2C_VERSION_INFO_API`、`I2C_CHANNEL_COUNT`、`I2C_JOB_COUNT`、`I2C_SEQUENCE_COUNT`、`I2C_MAX_JOB_PER_SEQUENCE`、`I2C_MAX_DATA_LENGTH`、`I2C_TIMEOUT_POLLS`；`I2c_PBcfg.c` 给 `I2c_Config`（控制器/从地址/速率/回调/超时）。

**验证状态**

| 例程 | 上板 | 日志 | 量化数据 |
|---|---|---|---|
| `i2c_master_slave` | PASS（含已知偏差） | `logs/20261102-142919-i2c_master_slave.log` | 1M 读 541.1 kbps；400k 读 1 字节误差 |
| `i2c_error` | PASS | `logs/20261102-142544-i2c_error.log` | nackPass=1、busyPass=1 |

### 2.11 Can（模块 ID 80）

- 头文件：`mcal/Can/include/Can.h`；源文件：`mcal/Can/src/Can.c`；配置：`mcal/Can/cfg/Can_Cfg.h`、`mcal/Can/cfg/Can_PBcfg.c`
- 版本宏：AR ?.?.? / SW ?.?.?；API ID 常量 18 个；DET 错误码 10 个
- 配置：pre-compile 开关 24 个，post-build 结构字段 68 个（`Can_ConfigType`）
- SWS 要求：API 18 个、回调/调度 10 个、DET 错误码 12 个、配置容器 18 个

| SWS API | Service ID | Sync/Async | 本实现 | 实现状态 | DET | 备注 |
|---|---|---|---|---|---|---|
| `Can_Init` | 0x00 | Synchronous | `Can_Init` | 已实现 | √ |  |
| `Can_GetVersionInfo` | 0x07 | Synchronous | `Can_GetVersionInfo` | 已实现 | √ |  |
| `Can_DeInit` | 0x10 | Synchronous | `Can_DeInit` | 已实现 | √ |  |
| `Can_SetBaudrate` | 0x0f | Synchronous | `Can_SetBaudrate` | 已实现 | √ |  |
| `Can_SetControllerMode` | 0x03 | Asynchronous | `Can_SetControllerMode` | 已实现 | √ |  |
| `Can_DisableControllerInterrupts` | 0x04 | Synchronous | `Can_DisableControllerInterrupts` | 已实现 | √ |  |
| `Can_EnableControllerInterrupts` | 0x05 | Synchronous | `Can_EnableControllerInterrupts` | 已实现 | √ |  |
| `Can_CheckWakeup` | 0x0b | Synchronous | `Can_CheckWakeup` | 已实现 | √ |  |
| `Can_GetControllerErrorState` | 0x11 | Synchronous | `Can_GetControllerErrorState` | 已实现 | √ |  |
| `Can_GetControllerMode` | 0x12 | Synchronous | `Can_GetControllerMode` | 已实现 | √ |  |
| `Can_GetControllerRxErrorCounter` | 0x30 | Synchronous | `Can_GetControllerRxErrorCounter` | 已实现 | √ |  |
| `Can_GetControllerTxErrorCounter` | 0x31 | Synchronous | `Can_GetControllerTxErrorCounter` | 已实现 | √ |  |
| `Can_GetCurrentTime` | 0x32 | Synchronous | – | _未实现_ | – |  |
| `Can_EnableEgressTimeStamp` | 0x33 | Synchronous | – | _未实现_ | – |  |
| `Can_GetEgressTimeStamp` | 0x34 | Synchronous | – | _未实现_ | – |  |
| `Can_GetIngressTimeStamp` | 0x35 | Synchronous | – | _未实现_ | – |  |
| `Can_SetCanPnFrameDataMask` | 0x13 | Synchronous | – | _未实现_ | – |  |
| `Can_Write` | 0x06 | Synchronous | `Can_Write` | 已实现 | √ |  |
| `Can_MainFunction_BusOff` | – | async | `Can_MainFunction_BusOff` | 已实现 | – | SWS 8.4/8.5 |
| `Can_MainFunction_Mode` | – | async | `Can_MainFunction_Mode` | 已实现 | – | SWS 8.4/8.5 |
| `Can_MainFunction_Read` | – | async | `Can_MainFunction_Read` | 已实现 | – | SWS 8.4/8.5 |
| `Can_MainFunction_Wakeup` | – | async | `Can_MainFunction_Wakeup` | 已实现 | – | SWS 8.4/8.5 |
| `Can_MainFunction_Write` | – | async | `Can_MainFunction_Write` | 已实现 | – | SWS 8.4/8.5 |
| `Can_MainFunction_BusOff` | – | async | `Can_MainFunction_BusOff` | 已实现 | – | SWS 8.4/8.5 |
| `Can_MainFunction_Mode` | – | async | `Can_MainFunction_Mode` | 已实现 | – | SWS 8.4/8.5 |
| `Can_MainFunction_Read` | – | async | `Can_MainFunction_Read` | 已实现 | – | SWS 8.4/8.5 |
| `Can_MainFunction_Wakeup` | – | async | `Can_MainFunction_Wakeup` | 已实现 | – | SWS 8.4/8.5 |
| `Can_MainFunction_Write` | – | async | `Can_MainFunction_Write` | 已实现 | – | SWS 8.4/8.5 |

**状态机/生命周期（SWS §7.1 摘要）**

UNINIT → INIT（`Can_Init()`）→ STOPPED；`Can_SetControllerMode()` 在 STOPPED/STARTED/SLEEP 间迁移，非法迁移报 `CAN_E_TRANSITION`（10 处）；未初始化报 `CAN_E_UNINIT`（16 处）；BusOff 由 `Can_MainFunction_Write`/中断推进恢复。

**配置参数对应**

SWS 18 个容器。本实现：`Can_Cfg.h` 给 `CAN_CONTROLLER_COUNT`、`CAN_MAX_HTH`、`CAN_MAX_HRH`、`CAN_MAX_FILTER_ELEMS`、`CAN_MAX_BAUDRATE_CONFIGS`、`CAN_FD_SUPPORT`、`CAN_EXTENDED_ID_SUPPORT`、`CAN_TX_BUF_PER_CONTROLLER`；`Can_PBcfg.c` 给 `Can_Config`（控制器/波特率/HTH/HRH/过滤器表）。

**验证状态**

| 例程 | 上板 | 日志 | 量化数据 |
|---|---|---|---|
| `can_classic_echo` | PASS | `logs/20261102-142930-can_classic_echo.log` | 200/200 帧 0 丢，4203 frame/s |
| `can_fd_echo` | PASS（5M 降级） | `logs/20261102-143020-can_fd_echo.log` | 500k/2M 64 B pingpong 30/30 |
| `can_filter` | PASS | `logs/20261102-141905-can_filter.log` | 12 条过滤规则全 PASS |
| `can_busoff` | PASS | `logs/20261102-142116-can_busoff.log` | TEC 平台 129；恢复后 pingpong PASS |
| `can_irq_perf` | PASS | `logs/20261102-142208-can_irq_perf.log` | 4 通道 8366 frame/s，CPU 4.62% |

### 2.12 Mem（模块 ID 91）

- 头文件：`mcal/Mem/include/Mem.h`；源文件：`mcal/Mem/src/Mem.c`；配置：`mcal/Mem/cfg/Mem_Cfg.h`、`mcal/Mem/cfg/Mem_PBcfg.c`
- 版本宏：AR (4u).(4u).(0u) / SW (1u).(0u).(0u)；API ID 常量 13 个；DET 错误码 7 个
- 配置：pre-compile 开关 7 个，post-build 结构字段 15 个（`Mem_ConfigType`）
- SWS 要求：API 12 个、回调/调度 2 个、DET 错误码 6 个、配置容器 2 个

| SWS API | Service ID | Sync/Async | 本实现 | 实现状态 | DET | 备注 |
|---|---|---|---|---|---|---|
| `Mem_Init` | 0x01 | Synchronous | `Mem_Init` | 已实现 | × |  |
| `Mem_DeInit` | 0x0b | Synchronous | `Mem_DeInit` | 已实现 | × |  |
| `Mem_GetVersionInfo` | 0x02 | Synchronous | `Mem_GetVersionInfo` | 已实现 | × |  |
| `Mem_GetJobResult` | 0x04 | Synchronous | `Mem_GetJobResult` | 已实现 | × |  |
| `Mem_Suspend` | 0x0c | Synchronous | `Mem_Suspend` | 已实现 | × |  |
| `Mem_Resume` | 0x0d | Synchronous | `Mem_Resume` | 已实现 | × |  |
| `Mem_PropagateError` | 0x08 | Synchronous | `Mem_PropagateError` | 已实现 | × |  |
| `Mem_Read` | 0x05 | Asynchronous | `Mem_Read` | 已实现 | × |  |
| `Mem_Write` | 0x06 | Asynchronous | `Mem_Write` | 已实现 | × |  |
| `Mem_Erase` | 0x07 | Asynchronous | `Mem_Erase` | 已实现 | × |  |
| `Mem_BlankCheck` | 0x09 | Asynchronous | `Mem_BlankCheck` | 已实现 | × |  |
| `Mem_HwSpecificService` | 0x0a | Asynchronous | `Mem_HwSpecificService` | 已实现 | × |  |
| `Mem_MainFunction` | – | async | `Mem_MainFunction` | 已实现 | – | SWS 8.4/8.5 |
| `Mem_MainFunction` | – | async | `Mem_MainFunction` | 已实现 | – | SWS 8.4/8.5 |

**状态机/生命周期（SWS §7.1 摘要）**

UNINIT → INIT（`Mem_Init()`）；作业模型一次只允许一个作业，作业未完成再提交报 `MEM_E_JOB_PENDING`；地址/长度/实例非法分别报 `MEM_E_PARAM_ADDRESS`、`MEM_E_PARAM_LENGTH`、`MEM_E_PARAM_INSTANCE_ID`；未初始化报 `MEM_E_UNINIT`；`Mem_MainFunction()` 推进作业，结果由 `Mem_GetJobResult()` 读。

**配置参数对应**

SWS 2 个容器。本实现：`Mem_Cfg.h` 给 `MEM_MAX_INSTANCES`、`MEM_ERASED_VALUE`、`MEM_UNITS_PER_MAIN_FUNCTION`、`MEM_ALLOW_ONE_JOB`；`Mem_PBcfg.c` 给 `Mem_Config`（实例几何、擦写窗口、读块大小、ECC 标志）。

**验证状态**

| 例程 | 上板 | 日志 | 量化数据 |
|---|---|---|---|
| `mem_flash_rw` | PASS | `logs/20261102-141617-mem_flash_rw.log` | 写 261.8 KB/s、读 39.8 MB/s |
| `mem_bench` | PASS | `logs/20261102-141425-mem_bench.log` | 擦 2092.6/写 261.7/读 44859.8 KB/s |

### 2.13 Crc（模块 ID 201）

- 头文件：`mcal/Crc/include/Crc.h`；源文件：`mcal/Crc/src/Crc.c`；配置：`mcal/Crc/cfg/Crc_Cfg.h`、`mcal/Crc/cfg/Crc_PBcfg.c`
- 版本宏：AR (4u).(4u).(0u) / SW (1u).(0u).(0u)；API ID 常量 9 个；DET 错误码 0 个
- 配置：pre-compile 开关 6 个，post-build 结构字段 4 个（`Crc_ConfigType`）
- SWS 要求：API 9 个、回调/调度 0 个、DET 错误码 0 个、配置容器 2 个

| SWS API | Service ID | Sync/Async | 本实现 | 实现状态 | DET | 备注 |
|---|---|---|---|---|---|---|
| `Crc_CalculateCRC8` | 0x01 | Synchronous | `Crc_CalculateCRC8` | 已实现 | × |  |
| `Crc_CalculateCRC8H2F` | 0x05 | Synchronous | `Crc_CalculateCRC8H2F` | 已实现 | × |  |
| `Crc_CalculateCRC16` | 0x02 | Synchronous | `Crc_CalculateCRC16` | 已实现 | × |  |
| `Crc_CalculateCRC16ARC` | 0x08 | Synchronous | `Crc_CalculateCRC16ARC` | 已实现 | × |  |
| `Crc_CalculateCRC32` | 0x03 | Synchronous | `Crc_CalculateCRC32` | 已实现 | × |  |
| `Crc_CalculateCRC32P4` | 0x06 | Synchronous | `Crc_CalculateCRC32P4` | 已实现 | × |  |
| `Crc_CalculateCRC32P76` | 0x09 | Synchronous | `Crc_CalculateCRC32P76` | 已实现 | × |  |
| `Crc_CalculateCRC64` | 0x07 | Synchronous | `Crc_CalculateCRC64` | 已实现 | × |  |
| `Crc_GetVersionInfo` | 0x04 | Synchronous | `Crc_GetVersionInfo` | 已实现 | × |  |

**状态机/生命周期（SWS §7.1 摘要）**

SWS 的 CRC 库没有 Init/DeInit、没有状态机，8 个 `Crc_CalculateCRC*` 是纯函数，任何时刻可调用；`Crc_GetVersionInfo()` 受 `CRC_VERSION_INFO_API` 控制。SWS 明确无开发错误（§7.8），因此本模块不开 DET。

**配置参数对应**

SWS 的 CRC 库没有配置容器（纯函数库）。本实现为“可选硬件加速”增加了工程扩展配置：`Crc_Cfg.h` 给 `CRC_USE_HW_ACCEL`、`CRC_HW_MIN_LENGTH`、`CRC_USE_TABLE`；`Crc_PBcfg.c` 给 `Crc_Config`。这是 SWS 之外的扩展，已在偏差清单登记。

**验证状态**

| 例程 | 上板 | 日志 | 量化数据 |
|---|---|---|---|
| `crc_bench` | PASS | `logs/20261102-135730-crc_bench.log` | 8 算法标准向量 PASS；HW 60.2 MB/s |
| `crc_integrity` | PASS | `logs/20261102-140431-crc_integrity.log` | flash/blank 双路径 PASS |

### 2.14 Eth（模块 ID 88）

- 头文件：`mcal/Eth/include/Eth.h`；源文件：`mcal/Eth/src/Eth.c`；配置：`mcal/Eth/cfg/Eth_Cfg.h`、`mcal/Eth/cfg/Eth_PBcfg.c`
- 版本宏：AR (4U).(4U).(0U) / SW (1U).(0U).(0U)；API ID 常量 25 个；DET 错误码 15 个
- 配置：pre-compile 开关 20 个，post-build 结构字段 18 个（`Eth_ConfigType`）
- SWS 要求：API 42 个、回调/调度 0 个、DET 错误码 0 个、配置容器 0 个

| SWS API | Service ID | Sync/Async | 本实现 | 实现状态 | DET | 备注 |
|---|---|---|---|---|---|---|
| `Eth_Init` | 0x01 | Synchronous | `Eth_Init` | 已实现 | √ |  |
| `Eth_SetControllerMode` | 0x03 | Asynchronous | `Eth_SetControllerMode` | 已实现 | √ |  |
| `Eth_GetControllerMode` | 0x04 | Synchronous | `Eth_GetControllerMode` | 已实现 | √ |  |
| `Eth_GetPhysAddr` | 0x08 | Synchronous | `Eth_GetPhysAddr` | 已实现 | √ |  |
| `Eth_SetPhysAddr` | 0x13 | Synchronous | `Eth_SetPhysAddr` | 已实现 | √ |  |
| `Eth_UpdatePhysAddrFilter` | 0x12 | Synchronous | `Eth_UpdatePhysAddrFilter` | 已实现 | √ |  |
| `Eth_WriteMii` | 0xa0 | Synchronous | `Eth_WriteMii` | 已实现 | √ |  |
| `Eth_ReadMii` | 0xa1 | Synchronous | `Eth_ReadMii` | 已实现 | √ |  |
| `Eth_GetCounterValues` | 0x14 | Synchronous | `Eth_GetCounterValues` | 已实现 | √ |  |
| `Eth_GetRxStats` | 0x15 | Synchronous | `Eth_GetRxStats` | 已实现 | √ |  |
| `Eth_GetTxStats` | 0x1c | Synchronous | `Eth_GetTxStats` | 已实现 | √ |  |
| `Eth_GetTxErrorCounterValues` | 0x1d | Synchronous | `Eth_GetTxErrorCounterValues` | 已实现 | √ |  |
| `Eth_GetSpiStatus` | 0x1E | Synchronous | – | _未实现_ | – |  |
| `Eth_GetCurrentTimeTuple` | 0x21 | Synchronous | – | _未实现_ | – |  |
| `Eth_SetPhcTime` | 0x22 | Synchronous | – | _未实现_ | – |  |
| `Eth_SetPhcCorrection` | 0x23 | Synchronous | – | _未实现_ | – |  |
| `Eth_GetPhcTime` | 0x24 | Synchronous | – | _未实现_ | – |  |
| `Eth_SetPpsSignalMode` | 0x25 | Synchronous | – | _未实现_ | – |  |
| `Eth_EnableEgressTimeStamp` | 0x17 | Synchronous | – | _未实现_ | – |  |
| `Eth_GetEgressTimeStamp` | 0x18 | Synchronous | – | _未实现_ | – |  |
| `Eth_GetIngressTimeStamp` | 0x19 | Synchronous | – | _未实现_ | – |  |
| `Eth_ProvideTxBuffer` | 0x09 | Synchronous | `Eth_ProvideTxBuffer` | 已实现 | √ |  |
| `Eth_Transmit` | 0x0A | Synchronous | `Eth_Transmit` | 已实现 | √ |  |
| `Eth_Receive` | 0x0B | Synchronous | `Eth_Receive` | 已实现 | √ |  |
| `Eth_ImmediateTransmit` | 0x26 | Synchronous | – | _未实现_ | – |  |
| `Eth_ReleaseRxBuffer` | 0x27 | Synchronous | `Eth_ReleaseRxBuffer` | 已实现 | √ |  |
| `Eth_TxConfirmation` | 0x0C | Synchronous | `Eth_TxConfirmation` | 已实现 | × |  |
| `Eth_GetVersionInfo` | 0x0D | Synchronous | `Eth_GetVersionInfo` | 已实现 | √ |  |
| `Eth_ReadMmd` | 0xa3 | Synchronous | `Eth_ReadMmd` | 已实现 | √ |  |
| `Eth_WriteMmd` | 0xa2 | Synchronous | `Eth_WriteMmd` | 已实现 | √ |  |
| `Eth_MacSecUpdateSecY` | 0x2a | Asynchronous | – | _未实现_ | – |  |
| `Eth_MacSecInitRxSc` | 0x2b | Synchronous | – | _未实现_ | – |  |
| `Eth_MacSecResetRxSc` | 0x2c | Synchronous | – | _未实现_ | – |  |
| `Eth_MacSecAddTxSa` | 0x2d | Asynchronous | – | _未实现_ | – |  |
| `Eth_MacSecUpdateTxSa` | 0x2e | Synchronous | – | _未实现_ | – |  |
| `Eth_MacSecDeleteTxSa` | 0x2f | Synchronous | – | _未实现_ | – |  |
| `Eth_MacSecAddRxSa` | 0x30 | Asynchronous | – | _未实现_ | – |  |
| `Eth_MacSecUpdateRxSa` | 0x31 | Synchronous | – | _未实现_ | – |  |
| `Eth_MacSecDeleteRxSa` | 0x32 | Synchronous | – | _未实现_ | – |  |
| `Eth_MacSecGetTxSaNextPn` | 0x33 | Synchronous | – | _未实现_ | – |  |
| `Eth_MacSecSetControlledPortEn` | 0x34 | Synchronous | – | _未实现_ | – |  |
| `Eth_MacSecGetMacSecStatistics` | 0x35 | Synchronous | – | _未实现_ | – |  |

### 2.15 EthTrcv（模块 ID 73）

- 头文件：`mcal/EthTrcv/include/EthTrcv.h`；源文件：`mcal/EthTrcv/src/EthTrcv.c`；配置：`mcal/EthTrcv/cfg/EthTrcv_Cfg.h`、`mcal/EthTrcv/cfg/EthTrcv_PBcfg.c`
- 版本宏：AR (4U).(4U).(0U) / SW (1U).(0U).(0U)；API ID 常量 21 个；DET 错误码 5 个
- 配置：pre-compile 开关 9 个，post-build 结构字段 14 个（`EthTrcv_ConfigType`）
- SWS 要求：API 31 个、回调/调度 0 个、DET 错误码 0 个、配置容器 0 个

| SWS API | Service ID | Sync/Async | 本实现 | 实现状态 | DET | 备注 |
|---|---|---|---|---|---|---|
| `EthTrcv_Init` | 0x01 | Synchronous | `EthTrcv_Init` | 已实现 | √ |  |
| `EthTrcv_SetTransceiverMode` | 0xa8 | Asynchronous | `EthTrcv_SetTransceiverMode` | 已实现 | √ |  |
| `EthTrcv_GetTransceiverMode` | 0x04 | Synchronous | `EthTrcv_GetTransceiverMode` | 已实现 | √ |  |
| `EthTrcv_GetBusWuReason` | 0x17 | Synchronous | `EthTrcv_GetBusWuReason` | 已实现 | √ |  |
| `EthTrcv_CheckWakeup` | 0x0f | Synchronous | `EthTrcv_CheckWakeup` | 已实现 | × |  |
| `EthTrcv_StartAutoNegotiation` | 0xa9 | Synchronous | `EthTrcv_StartAutoNegotiation` | 已实现 | × |  |
| `EthTrcv_TransceiverLinkState` | 0x02 | Asynchronous | – | _未实现_ | – |  |
| `EthTrcv_GetLinkState` | 0x06 | Synchronous | `EthTrcv_GetLinkState` | 已实现 | √ |  |
| `EthTrcv_GetBaudRate` | 0x07 | Synchronous | `EthTrcv_GetBaudRate` | 已实现 | √ |  |
| `EthTrcv_GetDuplexMode` | 0x08 | Synchronous | `EthTrcv_GetDuplexMode` | 已实现 | √ |  |
| `EthTrcv_SetPhyTestMode` | 0x11 | Synchronous | `EthTrcv_SetPhyTestMode` | 已实现 | √ |  |
| `EthTrcv_SetPhyLoopbackMode` | 0x12 | Synchronous | `EthTrcv_SetPhyLoopbackMode` | 已实现 | √ |  |
| `EthTrcv_GetPhySignalQuality` | 0x10 | Synchronous | `EthTrcv_GetPhySignalQuality` | 已实现 | √ |  |
| `EthTrcv_SetPhyTxMode` | 0x13 | Synchronous | `EthTrcv_SetPhyTxMode` | 已实现 | √ |  |
| `EthTrcv_RunCableDiagnostic` | 0x16 | Asynchronous | `EthTrcv_RunCableDiagnostic` | 已实现 | √ |  |
| `EthTrcv_GetCableDiagnostics` | 0x14 | Synchronous | – | _未实现_ | – |  |
| `EthTrcv_GetPhyIdentifier` | 0x15 | Synchronous | `EthTrcv_GetPhyIdentifier` | 已实现 | √ |  |
| `EthTrcv_GetMacMethod` | 0x18 | Synchronous | `EthTrcv_GetMacMethod` | 已实现 | √ |  |
| `EthTrcv_GetVersionInfo` | 0x0b | Synchronous | `EthTrcv_GetVersionInfo` | 已实现 | √ |  |
| `EthTrcv_MacSecUpdateSecY` | 0x19 | Asynchronous | – | _未实现_ | – |  |
| `EthTrcv_MacSecInitRxSc` | 0x1a | Synchronous | – | _未实现_ | – |  |
| `EthTrcv_MacSecResetRxSc` | 0x1b | Synchronous | – | _未实现_ | – |  |
| `EthTrcv_MacSecAddTxSa` | 0x1c | Asynchronous | – | _未实现_ | – |  |
| `EthTrcv_MacSecUpdateTxSa` | 0x1d | Synchronous | – | _未实现_ | – |  |
| `EthTrcv_MacSecDeleteTxSa` | 0x1e | Synchronous | – | _未实现_ | – |  |
| `EthTrcv_MacSecAddRxSa` | 0x1f | Asynchronous | – | _未实现_ | – |  |
| `EthTrcv_MacSecUpdateRxSa` | 0x20 | Synchronous | – | _未实现_ | – |  |
| `EthTrcv_MacSecDeleteRxSa` | 0x21 | Synchronous | – | _未实现_ | – |  |
| `EthTrcv_MacSecGetTxSaNext` | 0x22 | Synchronous | – | _未实现_ | – |  |
| `EthTrcv_MacSecGetMacSec` | 0x23 | Asynchronous | – | _未实现_ | – |  |
| `EthTrcv_MacSecSetControlled` | 0x24 | Synchronous | – | _未实现_ | – |  |

## 3. 与 SWS 的已知偏差清单

> 原则：只列**能指着文件说清**的偏差，不写“基本符合”这类模糊结论。每条给影响与后续动作；“后续动作”列不做承诺，只写可行路径。

| # | 偏差项 | SWS 依据 | 本实现现状 | 影响 | 后续动作 |
|---|---|---|---|---|---|
| 1 | AUTOSAR 版本宏写 4.4.0 | 各 SWS 文件头 R25-11 | 所有模块头 `<MOD>_AR_RELEASE_{MAJOR,MINOR,REVISION}_VERSION = 4.4.0`（公共头 `Mcal_Version.h` 约定） | 版本追溯字段与 SWS 文件头不对应；集成商用版本号判断能力会误判 | 在公共版本头统一声明 R25-11 对应的 AR 版本号，或提供映射表（需 Lead 决策，成员不要单独改） |
| 2 | 无 link-time 配置变体 | SWS 支持 pre-compile/link-time/post-build 三种配置阶段 | 只实现 pre-compile 开关 + post-build 指针（见第 42 章） | 同一份二进制无法通过链接切换配置集 | 增加 link-time 变体：配置实例在链接期由不同 .o 提供 |
| 3 | 无多分区/多核支持 | SWS 支持 partition 映射与多核 | `InstanceId` 固定 0，无分区/核间保护概念 | 不能直接运行在多核 OS 的不同 partition 上 | 引入 OS 后按 partition 扩展 InstanceId 语义与核间互斥 |
| 4 | Dem 只有桩实现 | Mcu/Wdg 等模块的 production error 需上报 Dem | `mcal/common/Dem.c` 仅 20 行，`Dem_ReportErrorStatus` 只计数 | 生产错误无法进入 DTC/事件管理 | 接入完整 Dem（DTC 存储、去抖、事件优先级） |
| 5 | 无 `*_Cbk.h` 回调头文件 | Can/Icu 等模块规定回调原型与回调头 | 回调类型与通知函数指针在 `<Mod>.h`/`*_PBcfg.h` 声明 | 与 CanIf/Os 集成时需手写原型，易与实际签名不一致 | 按 SWS 补 `<Mod>_Cbk.h` 并在 PBcfg 中引用 |
| 6 | 配置参数只覆盖 SWS 容器的常用子集 | SWS §10 每模块 5~18 个配置容器 | `*_Cfg.h`/`*_PBcfg.c` 手写，覆盖本板需要的字段（逐模块见 §2 配置参数对应） | 换 ECU 配置要改 C 代码，不能由配置工具生成 | 编写配置生成器或 EB tresos 插件，从 .arxml 生成 `*_PBcfg.c` |
| 7 | 无 ASIL 安全论证与安全手册 | ISO 26262-5 对 MCAL 的安全机制与假设要求 | 无安全手册、无 FMEDA/DFA、无安全案例 | 不能作为安全相关开发项（ASIL 任何等级） | 商业认证流程或按 ISO 26262 自建安全案例（见第 9 节第 4~5 项） |
| 8 | `E_MEM_SERVICE_NOT_AVAIL` 取 0x02 | SWS_Mem 引用了该符号但未给数值 | 按 Vector/EB Mem 驱动惯例取 `0x02`（`Mem.h` 注释说明） | 与其它厂商实现的数值可能不同，跨平台代码应只用符号名 | 集成时通过该宏名而非字面值比较返回值 |
| 9 | Crc 增加 SWS 之外的扩展 API 与配置类型 | SWS_CRCLibrary 无 Init、无配置容器、无开发错误 | 扩展 `Crc_Init`、`Crc_ConfigType`、`Crc_CalculateByAlgo`、`Crc_GetHwStats` 等（用于硬件加速与 bench 对拍） | 只按 SWS 编程的应用不会调用它们，无兼容问题；但头文件与 SWS 不同构，工具链检查会报扩展 | 扩展 API 在头文件用注释段与 SWS API 分开，README/章节声明 |
| 10 | Gpt 唤醒类 API 未实现 | SWS 8.3 `Gpt_EnableWakeup`/`Gpt_DisableWakeup`/`Gpt_CheckWakeup` | 头文件标注“可选：本子集未实现”，API ID 宏保留 | 不能用 GPT 作为唤醒源 | 按需补：配置 EcuM 唤醒源 + 中断唤醒路径 |
| 11 | Can 时间戳与 CAN PN API 未实现 | SWS 8.3 `Can_GetCurrentTime`/`Can_EnableEgressTimeStamp`/`Can_GetEgressTimeStamp`/`Can_GetIngressTimeStamp`/`Can_SetCanPnFrameDataMask` | 未实现（例程 `can_irq_perf` 用软件时间戳近似） | 拿不到硬件时间戳，不能做高精度总线时间分析；不支持 CAN PN 唤醒过滤 | MCAN 的 TS 计数器与 PN 过滤寄存器可用，按 SWS 补 |
| 12 | Dev Error 覆盖不等于 SWS 全部错误路径 | 各模块 SWS §7.2.1 开发错误表 | 主要错误码都有检查（每模块错误码使用次数见溯源统计），少数边界条件未逐一验证 | 极端非法配置下的行为未全部被测试覆盖 | 按 SWS 错误表逐条设计负向测试用例 |
| 13 | 无 MISRA/CERT C 静态分析证据 | AUTOSAR 对 MCAL 的编码规范要求（MISRA C:2012） | 只有 `-Wall -Wextra` 零告警 | 不能声称编码规范合规 | 接 Polyspace/Coverity/PC-lint 跑 MISRA C:2012，逐条消警或写偏差说明 |
| 14 | 无 WCET 测量 | 安全相关模块需提供最坏执行时间 | 未测（例程只统计平均/循环耗时） | 无法做时序安全分析 | 用 Lauterbach/示波器 GPIO 翻转法测最坏路径，或静态分析工具 |
| 15 | 无单元测试与覆盖率报告 | AUTOSAR 交付物惯例（模块级单元测试） | 20 个上板例程（集成级验证） | 语句/分支覆盖率未知，回归依赖人工烧板 | 建 Unity/Ceedling 或 VectorCAST 单元测试工程 |
| 16 | 无已知问题清单/Release Notes | 商用 MCAL 随包交付物惯例 | README 的“待确认项”与本章第 15 节 | 缺陷追溯与版本对比弱 | 随版本维护 known issues 清单 |
| 17 | 配置开关命名与 SWS 配置参数名不是一一对应 | SWS §10 配置参数（如 McuInitClockApi） | C 宏大写形式（如 `MCU_INIT_CLOCK_API`），含义对应但名称不同 | 配置工具无法直接映射到 SWS 参数名 | 提供 `<Mod>_Cfg.h` 与 SWS 配置参数的映射表 |
| 18 | 调度函数需应用手动调用 | SWS 规定 MainFunction 由 BSW Scheduler 周期调用 | 例程在主循环调用（如 `Spi_MainFunction_Handling`、`Mem_MainFunction`、`Can_MainFunction_*`） | 集成到 OS 后需配置 Alarm/Task 才能保证周期 | 提供 OS 调度表模板与周期建议 |
| 19 | Pwm/Adc 低功耗状态机 API 未实现 | SWS 8.3 Pwm/Adc 的 Set/Get/PreparePowerState（各 4 个） | `PWM_POWER_STATE_API`/`ADC_POWER_STATE_API` 默认 STD_OFF；Adc 实现 13/17、Pwm 9/13 | 不能参与 EcuM 的低功耗状态迁移，不能用 Adc/Pwm 作为唤醒相关外设 | 接入 EcuM 后按 SWS 补；PM 状态与输出关闭顺序需实测 |
| 20 | Icu 唤醒/边沿计数/异步通知 API 未实现 | SWS 8.3 Icu 的 *Wakeup（3 个）、*EdgeCount/GetEdgeNumbers（4 个）、*NotificationAsync（2 个） | 实现信号测量 + 边沿检测主路径，共 17/26 | 不能统计脉冲个数、不能用 Icu 做唤醒源、不能用异步通知 | GPTMR 捕获通道可扩展计数；异步通知依赖 OS，视集成计划 |

## 4. 机器可读统计

`docs/mcal/traceability_stats.json` 由同一脚本输出，第 50 章图 50-2 直接读取该文件。

```json
{
  "totals": {
    "sws_total": 233,
    "implemented": 172,
    "partial": 0,
    "missing": 61,
    "coverage": 0.738,
    "det_checked": 147,
    "loc": 14959
  },
  "source": {
    "sws": "AUTOSAR CP SWS R25-11 抽取文本（autosar_txt/）",
    "impl": "本仓库 mcal/<Mod>/",
    "date": "2026-11-02"
  }
}
```

## 5. 复跑方法

```bash
# 1) 重新解析 SWS 抽取文本（Windows 侧，产物为 docs/mcal/sws_api_data.json）
#    python parse_sws.py
# 2) 重新生成追溯矩阵与统计
python3 docs/mcal/gen_traceability.py
# 3) 只做检查不写文件
python3 docs/mcal/gen_traceability.py --check
```

