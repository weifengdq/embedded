# mcu_reset_mode —— AUTOSAR Mcu 复位源读取 / 软件复位 / 模式切换

一句话：读 PPOR RESET_FLAG 判定复位原因，用 `Mcu_PerformReset()` 或 `Mcu_SetMode(MCU_MODE_RESET)`
触发软件复位，重启后串口打印 `MCU_SW_RESET` 完成自证。

## 硬件要求 / 接线

| 项目 | 要求 |
|---|---|
| 板卡 | hpm5e31_1（HPM5E31） |
| 串口 | /dev/ttyACM0 @921600（复位后 USB 串口保持连接，可看到第二次启动） |
| 外部接线 | 无 |
| 注意 | 复位用 PPOR SOFTWARE_RESET，是芯片内部软复位，不依赖 J-Link |

## 编译与运行

```bash
bash scripts/build.sh samples/mcal/mcu_reset_mode
bash scripts/hwrun.sh mcal/mcu_reset_mode --secs 25 --send 'minfo;mclear;mreset'
```

| 命令 | 作用 |
|---|---|
| `minfo` | 打印复位原因（`Mcu_GetResetReason`）+ 原始标志（`Mcu_GetResetRawValue`） |
| `mclear` | 写 1 清 PPOR RESET_FLAG（W1C），便于观察下一次复位的来源 |
| `mreset` | `Mcu_PerformReset()` 软件复位，重启后应报 `MCU_SW_RESET` |
| `mmode 0\|2` | `Mcu_SetMode(MCU_MODE_RUN)` / `MCU_MODE_RESET`（后者等效软复位） |
| `mver` / `merr` | 版本信息 / 触发 2 条 Dev Error |

## 实测现象（串口输出）

```text
=== mcu_reset_mode: AUTOSAR Mcu reset source / software reset ===
RESET: reason=MCU_RESET_UNDEFINED(3) raw=0x00000010
RESET: bit4 debug reset (J-Link)
BOOT: reason=MCU_RESET_UNDEFINED
letter:/$ mclear
PPOR RESET_FLAG cleared (W1C)
RESET: reason=MCU_RESET_UNDEFINED(3) raw=0x00000000
letter:/$ mreset
Mcu_PerformReset(): software reset in ~4 us ...
=== mcu_reset_mode: AUTOSAR Mcu reset source / software reset ===
RESET: reason=MCU_SW_RESET(2) raw=0x80000000
RESET: bit31 software reset <- Mcu_PerformReset worked
BOOT: reason=MCU_SW_RESET
```

日志：`logs/20261102-140030-mcu_reset_mode.log`（mmode 2 路径）、`logs/20261102-135254-mcu_reset_mode.log`（mreset 路径）。

## 关键点

- 上电首启 `raw=0x10` 是 J-Link 烧写后的 debug reset，AUTOSAR 只定义 4 种复位原因，
  驱动把非 brownout/wdog/software 的来源统一映射成 `MCU_RESET_UNDEFINED`（SWS 允许扩展，但这里保持标准枚举）。
- `Mcu_Init` 里对 RESET_FLAG 做一次快照，保证 `Mcu_GetResetReason` 多次调用结果一致（SWS_Mcu_00158 Note）。
- `Mcu_PerformReset` 写 `PPOR->SOFTWARE_RESET = 100`：计数按 24 MHz 递减、到 2 触发复位，约 4.2 us。
- `MCU_MODE_RESET` 与 `Mcu_PerformReset` 走同一条路径；`MCU_MODE_SLEEP` 是 WFI，本轮不演示。

## 待确认项

- 已实测：debug reset(`0x10`)、软件复位(`0x80000000`)、W1C 清零、两种触发方式。
- 已知限制：看门狗复位（bit16）未在本例程触发（属于 Wdg 模块，见第 44 章）。
