# ewdg_window —— EWDG 窗口看门狗（窗口外喂狗复位）

## 功能

在 EWDG0 上使能**窗口模式**（`enable_window_mode`），演示"喂狗时机"本身就是安全条件：

- 窗口外（早于下限）喂狗 → 立即复位；
- 窗口内按周期喂狗 → 正常运行。

上电自动跑两阶段（不依赖串口命令，方便 `hwrun --secs N` 单次采集）：

1. 第一次启动：50 ms 就喂一次（远早于窗口下限 0.5×1 s）→ 预期复位；
2. 复位后的启动：识别到 `PPOR RESET_FLAG` 的 WDG0 位（bit16），打印证据，然后从 500 ms 起每 50 ms 喂一次、持续 2.5 s → 预期存活。

## 命令

| 命令 | 说明 |
|---|---|
| `wdinfo` | 打印 BOOT_RESET / EWDG0 CTRL0 / 时钟 |
| `wdw <delay_ms> [T_ms]` | 窗口模式下在 delay_ms 喂一次（默认 T=1000 ms） |
| `wdn <delay_ms> [T_ms]` | 关窗口的对照组 |

## 实测（2026-11-02）

| 试验 | 结果 |
|---|---|
| 50 ms 喂狗（窗口外） | 复位，重启 `BOOT_RESET flag=0x00010000`（WDG0） |
| ≥500 ms 起每 50 ms 喂、持续 2500 ms | `SURVIVED feeds=41` |

日志：`logs/20261102-124007-ewdg_window-r5c.log`；图：第 22 章 图 22-3。

## 易错点

- 不先 `clock_add_to_group(clock_watchdog0, 0)` 就 `ewdg_init()`，寄存器写入会被丢弃（CTRL0 读回 0，看门狗根本没跑）；
- "喂一次然后等 2 s 看是否复位"不能用来验证窗口内喂狗——等的那段时间本身就会超时复位。
