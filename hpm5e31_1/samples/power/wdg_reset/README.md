# wdg_reset —— EWDG 看门狗复位 / 喂狗存活 / 复位源识别

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

- 启动时读并清除 PPOR `RESET_FLAG`，识别上次复位来源（调试复位/看门狗/上电）；
- `wdgarm ms`：武装 EWDG0（32 kHz OSC）且不喂狗 → 到点复位，串口能看到复位后重新启动；
- `wdgfeed`：武装后持续喂狗 5 倍超时时间，验证不会误复位；
- 打印请求超时与实测超时（µs 级）。

## 硬件要求 / 接线

无外部接线。注意：复位后串口会先出现 ROM 启动信息再出现本程序横幅。

## 编译与运行

```bash
bash scripts/build.sh power/wdg_reset
bash scripts/hwrun.sh power/wdg_reset --secs 14 --send "resetcause;wdgarm 500"
```

## 命令

| 命令 | 说明 |
|---|---|
| resetcause | 打印启动时 PPOR RESET_FLAG 并解释（bit4=调试复位，bit16=WDG0） |
| wdgarm [ms] | 武装看门狗不喂狗，到时复位（1~60000 ms） |
| wdgfeed [timeout_ms] [period_ms] | 武装后按周期喂狗 5×timeout，PASS 表示存活 |

## 实测现象（串口输出）

日志：`logs/20261030-112938-wdg_reset.log`

```text
=== wdg_reset: EWDG watchdog ===
shell: resetcause | wdgarm [ms] | wdgfeed [timeout_ms] [period_ms]
resetcause
PPOR RESET_FLAG at boot = 0x00000010
  no watchdog reset bit set (power-on / pin / software reset)
  current PPOR RESET_FLAG = 0x00000000

letter:/$ wdgarm 500
EWDG0 armed: request 500 ms, actual 499990 us (ticks=16384)
NOT feeding: SoC should reset in ~500 ms ...

（约 500 ms 后串口出现 ROM 启动横幅，程序重启）

=== wdg_reset: EWDG watchdog ===
*** last reset was caused by EWDG0 timeout (RESET_FLAG=0x00010000) ***
shell: resetcause | wdgarm [ms] | wdgfeed [timeout_ms] [period_ms]
```

要点：

- 请求 500 ms，实测 **499990 µs**（误差 -10 µs，0.002%）；
- 复位后 `RESET_FLAG = 0x00010000`（bit16 = WDG0）；
- 烧写后首次启动读到 `0x00000010`（bit4 = 调试复位，J-Link 造成，不是上电）。

## 关键点 / 坑

1. **bit4 是调试复位**，不是"上电复位"；判读复位源时要把 J-Link 烧写的影响排除（RM §12.6.1 p.171）。
2. 若开了 `enable_config_lock` / `enable_refresh_lock`，`ewdg_refresh()` 前必须先
   `ewdg_unlock_refresh()`（或用固定 key），否则喂狗无效仍会复位。
3. 喂狗周期必须小于超时，且喂狗代码要放在"真的活着"的路径上（不要放定时器中断）。
4. 看门狗复位后外设不会自动复位到可控状态，重要外设要在启动时重新初始化。
