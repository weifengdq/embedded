# pwm_deadtime_fault —— PWMv2 互补死区与同步故障保护

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

PWM0 子模块 3 的 P6/P7 互补输出上配置**死区**（`PWM_DEAD[DEAD_AREA]`）与
**同步故障保护**（输出 6/7 的同步故障输入固定为通用触发输入 3，UM §37.4.11 p.616）。
本示例把"配置正确性"用寄存器回读自证，并通过 TRGM 拉高 TRIG_IN_3 来注入/撤销故障。

## 硬件要求 / 接线

- 串口同上。
- 死区与故障的**波形**要示波器看；本示例验证的是寄存器值与状态位（见第 14 章 §10 待确认 14-A）。

## 编译与运行

```bash
bash scripts/build.sh pwm/pwm_deadtime_fault
bash scripts/hwrun.sh pwm/pwm_deadtime_fault --secs 8 --send "dinfo;dead 400;fault 1;dinfo;fault 0;dinfo"
```

## 串口命令

| 命令 | 说明 |
|---|---|
| `dinfo` | 打印死区寄存器、换算成 ns、故障相关寄存器 |
| `dead <ticks>` | 设置死区（单位 PWM 时钟周期，200 MHz 下 1 tick = 5 ns，最小 2） |
| `fault <0\|1>` | 注入/撤销同步故障（经 TRGM 把 PWM0_TRIG_IN_3 拉高/拉低） |

## 实测现象（串口输出）

```text
=== pwm_deadtime_fault (board hpm5e31_1, ext 1.0.0) ===
PWM0 clk=200000000 Hz  P6=PD06 P7=PD07 互补  PAIR_MODE=1
  死区: ticks=100 -> DEAD_AREA=0x00006400 (高16位整数=100, 低8位小数=0)
        换算: 500 ns = 0.500 us @ 200000000 Hz
  故障: 同步故障输入=TRIG_IN_3, 当前注入=0, IRQ_STS_FAULT=0x00000000
```

实测日志：`logs/20261030-114436-pwm_deadtime_fault.log`（含 `dead 100/400`、`fault 1/0` 的完整回读）

## 实测数据要点

| 命令 | 寄存器回读 | 换算 |
|---|---|---|
| `dead 100` | `DEAD_AREA=0x00006400` | 500 ns |
| `dead 400` | `DEAD_AREA=0x00019000` | 2000 ns = 2 us |
| `fault 1` → `fault 0` | `IRQ_STS_FAULT` 由 0x00 → **0xC0**（bit6/bit7 = 输出 6/7） | 证明同步故障通路被触发 |

## 关键点 / 坑

1. `DEAD_AREA` 是 24 位 = 高 16 位整数 + 低 8 位小数，**直接写 ticks 会小 256 倍**；
   最小合法值 0x200。
2. 输出 6/7 的**同步**故障输入固定为触发输入 3（不是任意选），异步故障才能从 16 个 pad 里选。
3. `IRQ_STS_FAULT` 是**事件锁存**位：故障持续期间可能读到 0，撤销后才看到 bit 置位
   （实测 0xC0 = 输出 6/7）。判断"当前是否在故障中"应看输出电平，不要只信这个锁存位。
4. 故障恢复时机有 4 种（`FAULT_REC_TIME`），本示例用"故障消失立即恢复"；
   功率级一般选"重载后恢复"或"软件清除"更安全。