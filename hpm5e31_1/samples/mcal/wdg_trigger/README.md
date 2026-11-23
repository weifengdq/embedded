# wdg_trigger —— AUTOSAR Wdg 正常喂狗 + 窗口模式 + 动态改触发周期

EWDG0（OSC32 32768 Hz）以 Wdg SLOW=1000 ms 超时启动，验证三种场景存活：
正常喂狗 3 s、Wdg_SetTriggerCondition(300 ms) 后继续喂、Wdg_SetMode(FAST/100 ms) 后 50 ms 喂。
破坏性的"窗口外喂狗"放到命令 `wdg bad` 手动触发。

## 硬件要求

| 项目 | 说明 |
|---|---|
| 板卡 | hpm5e31_1 |
| 外部接线 | 无 |
| 使用资源 | EWDG0（计数器源 OSC32，超时复位），窗口 WIN_LOWER=8/16、WIN_UPPER=8/16 |
| 复位源 | PPOR.RESET_FLAG bit16 = WDG0 |

## 编译与运行

```bash
bash scripts/build.sh samples/mcal/wdg_trigger
bash scripts/hwrun.sh mcal/wdg_trigger --secs 20
```

## 命令

| 命令 | 说明 |
|---|---|
| `wdg info` | 状态/模式/超时配置 |
| `wdg feed` | 喂狗一次 |
| `wdg mode slow|fast` | 切换 WDGIF_SLOW/FAST 模式 |
| `wdg trig <ms>` | Wdg_SetTriggerCondition(<ms>) |
| `wdg err` | 错误路径（超时超上限 / 空指针）|
| `wdg bad` | 窗口外喂狗 → 预期复位（破坏性，慎用）|

## 实测现象

日志：`logs/20261102-141513-wdg_trigger.log`

```text
Wdg v1.0.0: EWDG0 armed, mode=SLOW, SLOW=1000 ms
[t1] feed 3 s every 100 ms: feeds=30 RESULT=SURVIVED
[wdg] Wdg v1.0.0 moduleID=102 state=1 mode=SLOW
[wdg] EWDG0 src=32768 Hz slow=1000 ms fast=100 ms max=60000 ms window=1000
[t2] SetTriggerCondition(300ms) + feed 2 s: feeds=20 RESULT=SURVIVED
[t3] FAST mode (100 ms) + feed 1 s: feeds=20 RESULT=SURVIVED
[det] before=0 after=2 delta=2
[wdg_trigger] auto test done. cmds: wdg info|feed|mode|trig|err|bad
```

三段测试全部 SURVIVED，说明 Init/SetMode/SetTriggerCondition 的重新装载路径正确；
`det delta=2` 对应 1 次超时超上限 + 1 次空指针。

## 关键点

1. Wdg_SetTriggerCondition(ms) 的语义是"按 ms 重新装载超时计数器并刷新一次"，
   所以每次喂狗都传 WdgMaxTimeout 以内的值；0 表示尽快复位（SWS_Wdg_00140）。
2. WdgDisableAllowed=OFF 时 Wdg_SetMode(WDGIF_OFF_MODE) 必须拒绝（WDG_E_DISABLE_REJECTED）；
   本配置就是这样，所以 `wdg mode off` 不会被真正关狗。
3. 窗口模式下"窗口外刷新"由硬件直接复位，driver 不屏蔽 —— 所以 `wdg bad` 是破坏性测试。
4. EWDG 计数器源用 OSC32，不随 PLL 配置变化，掉电前也能可靠复位。

## 待确认项

- 已实测：3 段喂狗存活、模式切换、动态改周期、DET 错误路径。
- 已知限制：窗口外喂狗的复位只能通过"复位后 PPOR 标志 + 启动计数"间接证明（见
  wdg_timeout_reset 例程）；复跑 `bash scripts/hwrun.sh mcal/wdg_trigger --secs 20`。
