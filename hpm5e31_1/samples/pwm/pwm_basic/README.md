# pwm_basic —— PWMv2 频率/占空比/互补输出与无示波器实测

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

PWM0 子模块 3（计数器 3）驱动 **P6 = PD06 / P7 = PD07** 一对互补输出，
频率与占空比可用串口命令在线改；同时用 **PWM 中断 + GPTMR 自由运行时基** 量出
真实周期、占空比、抖动与误差——**不需要示波器，也不需要飞线**。

关键映射（UM §37.4.8, p.613）：输出 6/7 ↔ 计数器 3 ↔ 比较值 12/13/14/15；
高电平区间 `[a, b)`，其中 `a = CMP12 = 0`（上升沿）、`b = CMP13 = 占空比计数`（下降沿）。

## 硬件要求 / 接线

- 串口：板上 CH343，`/dev/ttyACM0` 921600 8N1。
- PWM 输出：PD06（P6）、PD07（P7）。**要看到真实波形需示波器/逻辑分析仪**；
  本例程的数值来自内部时间戳测量，不需要探针。
- 注意：本板 PWM0 的 P0~P5 落在 PD00~PD05，与 CAN0/CAN1 复用；本示例只用 P6/P7 避开。

## 编译与运行

```bash
cd hpm5e31_1
bash scripts/build.sh pwm/pwm_basic
bash scripts/hwrun.sh pwm/pwm_basic --secs 16 --send "psweep"
```

## 串口命令

| 命令 | 说明 |
|---|---|
| `pwm <freq_hz> <duty_permille>` | 设置频率与占空比（千分比），例 `pwm 10000 500` |
| `pinfo` | 打印当前配置（重载值、比较值、引脚、时基） |
| `pmeas` | 打印实测结果并清零统计 |
| `psweep` | 自动扫 5 组频率/占空比，每组测 1.2 s，一次上板拿全表 |

## 实测现象（串口输出）

```text
=== pwm_basic (board hpm5e31_1, ext 1.0.0) ===
PWM0: clk=200000000 Hz counter3 reload(T)=20000 cmp a(CMP12)=0 cmp b(CMP13)=10000
  freq = clk/T = 10000.000 Hz   duty = (b-a)/T = 50.00%
  pins: P6=PD06  P7=PD07(PAIR_MODE=1 互补)   timebase: GPTMR0 CH3 100000000 Hz
commands: pwm <hz> <permille> | pinfo | pmeas

letter:/$ psweep
--- sweep 1: target 10000 Hz / 500 permille ---
[meas] reload=12039 cmp=12040  period=100.005 us (9999.5 Hz)  high=50.038 us  duty=50.04%
       period jitter: min=99.900 max=100.090 pp=0.190 us
       high  jitter: pp=0.170 us (=17.0 tick)
       target: 10000 Hz / 500 permille -> freq err -50.0 ppm, duty err +0.035%
```

完整 5 组扫描结果见 `docs/chapters/14-pwmv2-hrpwm.md` 表 14-7，
实测日志：`logs/20261030-114021-pwm_basic.log`。

| 目标 | 实测周期 | 实测频率 | 实测占空比 | 频率误差 |
|---|---|---|---|---|
| 1 kHz / 25% | 1000.005 us | 1000.0 Hz | 25.00% | -4.9 ppm |
| 10 kHz / 50% | 100.005 us | 9999.5 Hz | 50.04% | -50.0 ppm |
| 20 kHz / 75% | 50.005 us | 19998.0 Hz | 75.04% | -100.0 ppm |
| 50 kHz / 10% | 20.005 us | 49987.5 Hz | 10.00% | -249.9 ppm |
| 100 kHz / 50% | 10.005 us | 99950.0 Hz | 50.35% | -499.7 ppm |

## 关键点 / 坑

1. **输出 6/7 必须配计数器 3 + CMP12/13**（UM §37.4.8 p.613），配 CMP0/1 没有输出。
2. **4 个计数器的重载值要写成同一个 T**：比较值 `cmp[k*4+i]` 受计数器 i 的重载值约束，
   否则该比较事件（触发输出/中断）会被**静默丢弃**。实测现象：CMP13 的事件永远不来。
3. **不要用 PWM TRGO → TRGM → GPTMR 捕获做窄脉冲测量**：TRGO 只有 1 个 PWM 时钟
   （200 MHz 下 5 ns）宽，GPTMR 100 MHz 采样会大量漏采（50 kHz 时 2 s 只捕到 3533 个）。
   本例程因此改用"PWM 中断里读 GPTMR 时基"。
4. `pwmv2_set_dead_area()` 内部会左移 8 位；直接写寄存器时最小合法值 0x200（2 个时钟）。
5. 静态链接无 libm，`sqrt` 不可用（本示例自带牛顿迭代，仅演示时用）。
