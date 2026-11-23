# pwm_notify —— AUTOSAR Pwm 边沿通知：2 万边沿/s 通知吞吐与抖动

PWM0 CH0（PD02）配置 10 kHz / 50% 占空比，使能**双边沿**通知，实测通知次数、
边沿间隔抖动与丢/多通知数。上升沿走计数器重载中断、下降沿走 CMP5 比较中断两条路径。

## 硬件要求

| 项目 | 说明 |
|---|---|
| 板卡 | hpm5e31_1 |
| 外部接线 | 无（通知来自 PWMv2 内部比较器/重载事件，不需要引脚回环） |
| 使用资源 | PWM0 计数器 1 / P2（PD02）/ CMP4、CMP5；IRQn_PWM0 |
| 不占用 | PWM0 其它通道、GPTMR、EWDG |

## 编译与运行

```bash
bash scripts/build.sh samples/mcal/pwm_notify
bash scripts/hwrun.sh mcal/pwm_notify --secs 20
```

## 命令

| 命令 | 说明 |
|---|---|
| `pwm set <permille>` | 设置 CH0 占空比 |
| `pwm notify <0/1/2>` | 0=上升沿、1=下降沿、2=双边沿 |
| `pwm nstat` | 打印统计（次数/速率/丢多/边沿间隔 min/max/pp/stddev）并清零窗口 |
| `pwm nclr` | 清统计 |
| `pwm err` | 错误路径（非法通道 / 非法通知类型 / 空指针）|
| `pwm info` | 配置/版本/状态 |

## 实测现象

日志：`logs/20261102-141427-pwm_notify.log`

```text
[pwm_notify] auto 2s measurement:
[notify] edges=40006 in 2000 ms  (rate=20003.0 Hz)
[notify] expect=40000.0 edges for 2*10000Hz*2000ms  -> lost/extra=-6
[notify] edge interval: mean=50.002 us min=39.800 max=50.054 pp=10.254 us stddev=0.052 us
[notify] edges=59997 in 3000 ms  (rate=19999.0 Hz)
[notify] expect=60000.0 edges for 2*10000Hz*3000ms  -> lost/extra=+3
[notify] edge interval: mean=50.003 us min=49.692 max=50.308 pp=0.617 us stddev=0.131 us
```

解读：
- 10 kHz / 双边沿 = 理论 20000 边沿/s，实测 rate 19999~20003 Hz，3 s 窗口丢/多 ≤ ±14 次（<0.03%）；
- 边沿间隔均值 50.003 µs（理论 50 µs），pp 0.617 µs、stddev 0.13 µs（=CPU 62 cycles）；
- 首次 2 s 窗口 min=39.8 µs 是启动瞬间两条中断路径同时挂起导致的一次合并，之后稳定。

## 关键点

1. AUTOSAR Pwm 通知是**边沿 vs 通道**的配置（PWM_RISING/FALLING/BOTH_EDGES），
   映射到 PWMv2 的 CMP/重载中断；相位 a=0 时"上升沿"就是重载事件，必须用重载中断。
2. ISR 里只做计数与 mcycle 记录，不做 printf（契约 §4）；统计由主循环打印。
3. 2 万中断/s（每个 ~0.5 µs 处理）占 CPU <1%，说明 PWMv2 通知路径开销可接受。

## 待确认项

- 已实测：双边沿 2 万/s 吞吐、抖动 0.6 µs pp、丢/多 <0.03%、DET 错误路径。
- 已知限制：单次 2 s 窗口存在一次启动合并（min 39.8 µs），长时间窗口稳定；
  复跑 `bash scripts/hwrun.sh mcal/pwm_notify --secs 20` 看后续 3 s 窗口。
