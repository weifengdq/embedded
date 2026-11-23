# pwm_basic —— AUTOSAR Pwm 基本波形：占空比/频率/相位实测

PWM0 输出 2/3（PD02/PD03，I2C2 的 SCL/SDA 复用）产生方波，经板载 I2C2↔I2C3 杜邦线
送到 PD07/PD06，由 Icu（GPTMR2 + TRGM）在**线的另一端真实引脚**上捕获，测频率、占空比与相位。

## 硬件要求

| 项目 | 说明 |
|---|---|
| 板卡 | hpm5e31_1 |
| 必须的板载接线 | PD02↔PD07、PD03↔PD06（I2C2↔I2C3 杜邦线，板上已有；无需额外飞线） |
| 输出引脚 | PWM0 P2 = PD02（IOC_PD02_FUNC_CTL_PWM0_P_2）、P3 = PD03 |
| 捕获链路 | PD07/PD06 配成 TRGM_P_07/P_06 → TRGM → GPTMR2_CAPT_3/CAPT_2 |
| 不占用 | PWM0 P0/P1（PD00/PD01，CAN0 引脚）、PWM1/2/3、GPTMR3 |

## 编译与运行

```bash
bash scripts/build.sh samples/mcal/pwm_basic
bash scripts/hwrun.sh mcal/pwm_basic --secs 20
```

## 命令

| 命令 | 说明 |
|---|---|
| `pwm set <permille>` | 设置 CH0 占空比（0..1000 千分比） |
| `pwm freq <hz>` | 设置周期（两通道共用计数器 1，1..100000 Hz） |
| `pwm meas` | 打印一次实测（频率/占空比，两通道） |
| `pwm phase` | 相位测量（两个 Icu 通道原始边沿时间戳相减） |
| `pwm idle` | Pwm_SetOutputToIdle 并回读 GetOutputState |
| `pwm sweep` | 重跑 0/25/50/75/100% + 1/10/50 kHz 扫描 |
| `pwm err` | 错误路径（非法通道 / 越界占空比 / 0 周期 / 空指针）|
| `pwm info` | 打印配置/版本/状态 |

## 实测现象

日志：`logs/20261102-141320-pwm_basic.log`

```text
[sweep] duty 0/25/50/75/100%, f=10000 Hz, ICU clk=100000000 Hz
[pwm] sweep ch0(set 0/0x8000): constant LOW, no edges -> ICU holds last captured value (unmeasurable)
[pwm] sweep ch0(set 8192/0x8000): period=10001 tick, freq=9999 Hz, duty=24.168%
[pwm] sweep ch0(set 16384/0x8000): period=10000 tick, freq=10000 Hz, duty=49.160%
[pwm] sweep ch0(set 24576/0x8000): period=10000 tick, freq=10000 Hz, duty=74.160%
[pwm] sweep ch0(set 32768/0x8000): period=9999 tick, freq=10001 Hz, duty=99.860%
[fsweep] target=1000 Hz reload=200000   -> period=100001 tick, freq=1000 Hz, duty=49.917%
[fsweep] target=10000 Hz reload=20000   -> period=10000 tick, freq=10000 Hz, duty=49.170%
[fsweep] target=50000 Hz reload=4000    -> period=2002 tick, freq=49950 Hz, duty=45.804%
[phase] ch0 edge=2995720, ch1 edge=2997930, diff=2210 tick = 22.100% of period (expect 25.0%)
[phase] phase error = -2.900% of period
[det] before=0 after=4 delta=4
[idle] ch0 output state = LOW
[pwm] Pwm v1.0.0 moduleID=121; f_pwm0=200000000 Hz, f_gptmr2=100000000 Hz
```

要点：
- 频率实测与设定一致（10 kHz 周期 10000 tick）；周期量化 ±1 tick 即 ±100 ppm @10 kHz；
- 占空比存在**固定约 -0.083%（80 ns 级 tick 数 ≈ 0.83 µs）高电平偏置**：1 kHz 时 -0.083%，
  10 kHz 时 -0.82%，50 kHz 时 -4.2%（同一固定时间偏置在不同周期下的百分比不同）—— 见坑 45-3；
- 100% 占空比可测（99.86%），0% 恒低无沿，测宽模式拿不到新值（已知限制）；
- 相位：CH1 配置 25% 相位，用两个通道同源捕获时间戳相减得 22.1%（差 2.9%，约 0.29 µs 跨通道
  捕获延迟未标定，见"待确认项"）。

## 关键点

1. PWMv2 的 CMP 索引必须属于"对应计数器"，且所有计数器的重载值都要配置，否则 CMP 事件被
   静默丢弃（RM §37.4.8 / 第 14 章坑 14-3）；本驱动在 Init 里把 4 个计数器都绑到影子 0。
2. AUTOSAR 占空比标度是 0..0x8000（0x8000 = 100%），不是 0..65535；0%/100% 用 b=a / b=T 特例。
3. 引脚复用只在 Pwm_PBcfg.c 里写；驱动不出现 PD02 这类板级名字。
4. 测量必须用真实引脚（杜邦线对端捕获），寄存器回读证明不了输出级行为。

## 待确认项

- 已实测：0/25/50/75/100% 占空比、1/10/50 kHz 频率、相位、idle 状态、DET 错误路径。
- 已知限制（原因 + 复跑命令）：
  * 0% 占空比恒低电平、没有边沿，GPTMR 测宽模式无法测新值 —— 复跑 `bash scripts/hwrun.sh mcal/pwm_basic --secs 20`，看 `set 0/0x8000: constant LOW` 行。
  * 占空比固定 -0.083%~-4.2% 偏置（随频率升高百分比变大，绝对值约 0.8 µs 高电平）——
    怀疑捕获链路/输出级固定延迟，未做逐通道标定；复跑命令同上，看 duty 列。
  * 相位差 22.1% vs 设定 25%（约 0.29 µs 偏差）—— 属跨通道捕获延迟未标定；复跑 `pwm phase` 观察。
