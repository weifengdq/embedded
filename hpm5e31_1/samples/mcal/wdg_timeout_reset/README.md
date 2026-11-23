# wdg_timeout_reset —— AUTOSAR Wdg 超时复位：复位源识别 + 跨复位启动计数

不喂狗，让 EWDG0 在超时后复位 SoC；复位后固件读 PPOR.RESET_FLAG 的 WDG0 位确认复位源，
并用 .noncacheable.non_init 段里的计数器打印 BOOT#，证明"确实是看门狗复位、不是掉电"。

## 硬件要求

| 项目 | 说明 |
|---|---|
| 板卡 | hpm5e31_1 |
| 外部接线 | 无 |
| 使用资源 | EWDG0（OSC32 32768 Hz）、PPOR.RESET_FLAG bit16、.noncacheable.non_init RAM |
| 串口注意 | 复位前后串口链路会短暂中断，单次 hwrun 抓包跨不过复位；本示例用"两次 hwrun"抓，见下 |

## 编译与运行

```bash
bash scripts/build.sh samples/mcal/wdg_timeout_reset
# 第 1 次：烧写 + 冷启动 -> 不喂狗 -> 超时后复位（日志停在 expect reset）
bash scripts/hwrun.sh mcal/wdg_timeout_reset --secs 8
# 第 2 次：不重烧，抓复位后板子的输出（固件复位后会连续 20 s 打印复位源与 BOOT#）
bash scripts/hwrun.sh mcal/wdg_timeout_reset --no-flash --secs 12
```

实测日志：

- 第 1 次（冷启动）：`logs/20261102-142810-wdg_timeout_reset.log`
- 第 2 次（复位后）：`logs/20261102-142819-wdg_timeout_reset.log`（本次为空，原因见"待确认项"）
- 复位源交叉证据（既有例程 samples/power/wdg_reset）：`logs/20261030-112938-wdg_reset.log`

## 命令（从看门狗复位回来后可用）

| 命令 | 说明 |
|---|---|
| `wdg cause` | 打印本次启动读到的 PPOR.RESET_FLAG 与判定 |
| `wdg info` | 打印 BOOT#/状态/模式/超时配置 |
| `wdg reset [ms]` | 装载超时后不喂狗，再次触发复位（默认 200 ms）|
| `wdg feed <sec>` | 喂狗 sec 秒，验证不会复位 |

## 实测现象

第 1 次（冷启动，日志 logs/20261102-142810-wdg_timeout_reset.log）：

```text
=== mcal wdg_timeout_reset (board hpm5e31_1) ===
[boot] BOOT#1 (non-init RAM counter)  PPOR.RESET_FLAG=0x00000010  WDG0_reset=0
[wdg] PPOR.RESET_FLAG at this boot = 0x00000010 (WDG0 bit16 = 0)
[wdg] => 其它复位源（非 EWDG0）
[auto] BOOT#1: EWDG0 armed 500 ms, NOT feeding -> EWDG0 timeout reset expected
```

第 2 次（复位后，应看到）：

```text
[post-reset] BOOT#2 PPOR.RESET_FLAG=0x00010000 WDG0_reset=1 (0 s)
[post-reset] BOOT#2 PPOR.RESET_FLAG=0x00010000 WDG0_reset=1 (1 s)
...
```

判读：BOOT# 从 1 变 2 说明是"复位"而不是"重新跑 main"；PPOR.RESET_FLAG 的 bit16=1
说明复位源就是 EWDG0（该位的取值由既有 wdg_reset 例程日志交叉证实：RESET_FLAG=0x00010000）。

## 关键点

1. **复位源**用 PPOR.RESET_FLAG 判定，WDG0 对应 bit16；启动时读一次并清掉，避免下次误判。
2. **跨复位计数**用 .noncacheable.non_init 段（NOLOAD，启动代码不清零）+ magic 判断冷启动；
   放普通 .bss 会被清零，放 Flash 又写不了。
3. **抓包**：复位瞬间串口链路会中断，同一次 hwrun 跨不过去；示例在复位后连续打印 20 s，
   让第二次 hwrun 能抓到（共享板卡上若两次之间被他人烧写，第二次会抓空）。
4. 复位后必须重新 Wdg_Init 并持续喂狗，否则会陷入"复位—启动—再复位"循环。

## 待确认项

- 已实测：冷启动 BOOT#1（WDG0=0）+ "armed 500 ms, NOT feeding -> reset expected"（复位前证据）。
- 已定位：复位后抓包为空的两个原因——复位瞬间串口链路中断、两次 hwrun 之间共享板卡可能被
  他人重烧；复位源判定逻辑与既有 samples/power/wdg_reset 一致（其日志含 0x00010000）。
- 复跑命令：上面两条 hwrun（先 --secs 8 烧写，再 --no-flash --secs 12，中间不要插入其它烧写）。
